// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/FPSRRunDirectorSubsystem.h"
#include "Run/Mission/FPSRMissionActor.h"
#include "Run/Mission/FPSRMissionDataAsset.h"
#include "Run/Mission/FPSRMissionSpawnPoint.h"
#include "Run/Mission/FPSRMissionPointSet.h"
#include "Card/FPSRCardDataAsset.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRGameMode.h" // GetParticipantCount (ApplyStageDifficultyToArena's party-size lookup)
#include "Core/FPSRPlayerController.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Enemy/FPSREnemyRosterDataAsset.h"
#include "Enemy/FPSRFlowFieldSubsystem.h" // U P-F: ResetDoorTopologyToBaseline on a same-world re-run
#include "Boss/FPSRBossBase.h"
#include "Boss/FPSRBossSpawnPoint.h"
#include "Boss/FPSRBossDefinitionDataAsset.h"
#include "Arena/FPSRArenaActor.h" // ApplyStageDifficultyToArena's arena arg + FindActiveInWorld (stage 0)
#include "Arena/FPSRArenaDestructible.h" // GetOwnedDestructibles' element type
#include "Core/FPSRLogChannels.h"
#include "Containers/ArrayView.h" // TConstArrayView (the evaluator contract — UFPSRRunScheduleDataAsset statics)
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

bool UFPSRRunDirectorSubsystem::HasServerAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

void UFPSRRunDirectorSubsystem::StartRun()
{
	if (!HasServerAuthority() || bRunActive)
	{
		return;
	}

	bRunActive = true;
	RunClock = 0.0f;
	PostBossElapsed = 0.0f;
	bBossStarted = false;
	NextRunLogTime = 30.0f;

	// Publish the schedule to the GameState so every client's HUD can lay out the run-timeline bar (window markers +
	// boss endpoint) (B2), and reset the replicated mission progress (B1).
	if (AFPSRGameState* GS = GetGS())
	{
		GS->SetRunSchedule(ActiveSchedule);
		GS->SetMissionProgress(0.0f);
	}

	// Re-run safety: drop the previous run's boss — hide the HUD boss bar on every client, then tear the actor down
	// (HandleDeath deliberately leaves the defeated boss standing for the result beat, so a same-world re-run would
	// otherwise start with a dead boss still occupying the level). Order matters: clear the replicated ref FIRST —
	// once the actor is garbage that ref self-nulls, and the setter's ActiveBoss == InBoss early-out would then
	// swallow the broadcast the HUD needs. No-op on a first run (nothing spawned yet).
	if (AFPSRGameState* GS = GetGS())
	{
		GS->SetActiveBoss(nullptr);

		// 🔴 Re-run safety, stage-difficulty axis (신설 2026-08-26): SetStageIndex's ONLY other caller is
		// UFPSRStageDirectorSubsystem::PerformSwap (a committed stage-N swap) — nothing ever reset it back to 0, so
		// a same-world re-run silently started combat already reading the PREVIOUS run's final stage index (both
		// the alive-count stage anchor and the suppressor durability axis key off GetStageIndex()). A first run is
		// unaffected (StageIndex starts at its UPROPERTY default, 0).
		GS->SetStageIndex(0);
	}
	if (ActiveBoss)
	{
		ActiveBoss->Destroy();
		ActiveBoss = nullptr;
	}

	// Push schedule-driven spawn pacing to the spawn subsystem (the swarm fill rate — how fast it builds toward the
	// target alive count). Both the per-tick batch (MaxSpawnPerTick) and the tick interval (SpawnIntervalSeconds) are
	// tunable on DA_RunSchedule without further code changes; together they set the per-second spawn pace.
	if (UFPSREnemySpawnSubsystem* SpawnSub = GetSpawnSub())
	{
		SpawnSub->SetMaxSpawnPerTick(ActiveSchedule ? ActiveSchedule->MaxSpawnPerTick : FallbackMaxSpawnPerTick);
		SpawnSub->SetSpawnInterval(ActiveSchedule ? ActiveSchedule->SpawnIntervalSeconds : FallbackSpawnIntervalSeconds);
		// Data-driven enemy archetype mix (Game.MD §2-6): push the run's roster so the swarm spawns melee/ranged by
		// weighted random. Null = the spawn subsystem falls back to its single configured EnemyClass (no regression).
		SpawnSub->SetEnemyRoster(ActiveSchedule ? ActiveSchedule->EnemyRoster : nullptr);
		// Re-run safety: restart the room-spawn accumulation from only the start room(s) (a same-world re-run would
		// otherwise keep zones opened in the previous run). A fresh level load is already reset at world begin.
		SpawnSub->ResetSpawnZones();
		// U P-F: reset the director's transient run state (trickle clock, grace/tracker maps, active enemies). A first
		// run starts empty, so this is a no-op there; it's the same-world-reset safety net (Stage 2 also re-gates acks).
		SpawnSub->ResetForNewRun();
	}

	// U P-F: restore the unified flow field's door topology to its world-begin baked baseline (all seam doors closed) and
	// bump the topology generation. No-op on a first (unmutated) run so the generation stays 0; the current path (new run
	// = full map reload = fresh field) never mutates it, so this is the future same-world-reset safety net (production
	// structure). Paired with SpawnSub->ResetForNewRun() above.
	if (UWorld* World = GetWorld())
	{
		if (UFPSRFlowFieldSubsystem* Flow = World->GetSubsystem<UFPSRFlowFieldSubsystem>())
		{
			Flow->ResetDoorTopologyToBaseline();
		}
	}

	// Size the per-window fired flags and roll each window's trigger time within its [MinTime, MaxTime] range
	// (server-authoritative; the schedule varies run to run). No missions without a schedule asset.
	const int32 NumWindows = ActiveSchedule ? ActiveSchedule->MissionWindows.Num() : 0;
	MissionWindowFired.Init(false, NumWindows);
	WindowTriggerTimes.Reset();
	WindowTriggerTimes.Reserve(NumWindows);
	for (int32 i = 0; i < NumWindows; ++i)
	{
		const FFPSRMissionWindow& W = ActiveSchedule->MissionWindows[i];
		WindowTriggerTimes.Add(FMath::FRandRange(W.MinTime, FMath::Max(W.MaxTime, W.MinTime)));
	}

	// Spawning begins once a player pawn exists (avoids origin spawns) AND the opening-seed freeze has engaged
	// (so enemies can't spawn before the run-start card selection). We don't set a spawn target yet.
	if (HasAnyPlayerPawn())
	{
		bWaitingForOpeningSeed = true;
		OpeningWaitElapsed = 0.0f;
	}
	else
	{
		bAwaitingFirstPlayer = true;
		UE_LOG(LogFPSR, Log, TEXT("[Run] StartRun deferred — waiting for first player pawn"));
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			DirectorTimerHandle, this, &UFPSRRunDirectorSubsystem::DirectorTick, DirectorInterval, true);
	}
}

float UFPSRRunDirectorSubsystem::GetBossTime() const
{
	return ActiveSchedule ? ActiveSchedule->BossTime : FallbackBossTime;
}

bool UFPSRRunDirectorSubsystem::CancelActiveMission()
{
	const bool bHadMission = (ActiveMission != nullptr);
	DestroyActiveMission();
	return bHadMission;
}

void UFPSRRunDirectorSubsystem::ApplyStageDifficultyToArena(AFPSRArenaActor* Arena, int32 StageIndex)
{
	if (!ActiveSchedule || !Arena)
	{
		return; // asset-less / world-less test run — every suppressor keeps its actor-authored Durability
	}

	// 🔴 참가자 수 = GetParticipantCount()(PlayerArray 중 !IsOnlyASpectator()) — GetLivingPlayerCount() 가 아니다.
	// AFPSRPlayerState::IsAlive() 는 DBNO 를 제외한다. 스테이지 커밋 시점(억제기 파괴 직후)은 전환 전 최고
	// 압박 구간이라 DBNO 가 가장 빈발하고, "인원수는 스테이지 시작에 1회 고정"과 결합하면 4인이 3명 고의 다운
	// 상태로 억제기를 마저 부수고 커밋 직후 부활 → 다음 스테이지 내내 4인 DPS로 1인 배수 억제기를 상대하는
	// 악용이 열린다(ADR 0010 §512 위험이 우회 경로로 부활). 참가자 수는 접속/이탈로만 변하고 사망/DBNO 로는
	// 변하지 않으므로 "1회 고정" 의도와 정합한다(역방향 — 우연히 다운 중 커밋되어 스테이지 전체가 과소 스케일
	// 되는 경우 — 도 있지만, 그건 참가자 정의를 접속 기준으로 고정한 대가이지 악용 경로는 아니다).
	int32 ParticipantCount = 1;
	if (UWorld* World = GetWorld())
	{
		if (AFPSRGameMode* GameMode = World->GetAuthGameMode<AFPSRGameMode>())
		{
			ParticipantCount = GameMode->GetParticipantCount();
		}
	}

	const FFPSRStageDifficultyAnchor StageAnchor = UFPSRRunScheduleDataAsset::EvalStageAt(ActiveSchedule->StageDifficulty, StageIndex);
	const float PartySizeMultiplier = UFPSRRunScheduleDataAsset::EvalPartySizeMultiplier(ActiveSchedule->InhibitorDurabilityByPartySize, ParticipantCount);
	const float EffectiveDurability = ActiveSchedule->InhibitorBaseDurability * StageAnchor.InhibitorDurabilityMultiplier * PartySizeMultiplier;

	// GetOwnedDestructibles, not a spatial/world-wide search: Arena's own ULevel scope (invariant 4 — one arena =
	// one sublevel), the same scope SetArenaActive already uses to reset every destructible in the arena it just
	// (re)activated. IsSuppressor() filters to ADR 0010 D6's exact definition (Rewards contains
	// UFPSRDestructibleReward_StageTransition) — an ordinary destructible (a crate) is left at its actor-authored
	// Durability, only suppressors scale with stage/party size.
	TArray<AFPSRArenaDestructible*> Destructibles;
	Arena->GetOwnedDestructibles(Destructibles);
	for (AFPSRArenaDestructible* Destructible : Destructibles)
	{
		if (Destructible && Destructible->IsSuppressor())
		{
			Destructible->ServerSetDurabilityOverride(EffectiveDurability);
		}
	}

	// 판정용 로그 1줄 — alive-count 축의 Bonus/Multiplier 도 함께 찍는다(EvalStageAt 이 이미 통째로 돌려주므로
	// 추가 비용 없음). ComputeTargetAliveCount 는 매 틱 호출이라 여기서 로그를 남기지 않고, 이 함수가 스테이지당
	// 정확히 1회 호출되는 지점이라 여기서 두 축을 함께 남긴다(PIE 스모크 판정 — 계획 검증 6번).
	UE_LOG(LogFPSR, Log, TEXT("[Run] Stage %d difficulty: alive (%+d) x%.2f, inhibitor %.0f x%.2f x%.2f = %.0f (participants %d)"),
		StageIndex, StageAnchor.AliveCountBonus, StageAnchor.AliveCountMultiplier,
		ActiveSchedule->InhibitorBaseDurability, StageAnchor.InhibitorDurabilityMultiplier, PartySizeMultiplier, EffectiveDurability,
		ParticipantCount);
}

int32 UFPSRRunDirectorSubsystem::ComputeTargetAliveCount() const
{
	const int32 MaxCount = ActiveSchedule ? ActiveSchedule->MaxAliveCount : FallbackMaxAliveCount;

	// Stage-difficulty anchor (신설 2026-08-26, ADR 0010 D6 비용 축): StageIndex comes off the SAME GameState both
	// branches below already read (no new replication — GetStageIndex() is already replicated). Evaluated ONCE
	// here so both branches apply the identical anchor. An unauthored StageDifficulty (or no schedule at all)
	// evaluates to identity (Bonus 0, Multiplier 1.0) via EvalStageAt's empty-array fallback — complete no-op, so
	// an existing schedule with no StageDifficulty authored does not regress.
	int32 StageIndex = 0;
	if (const UWorld* World = GetWorld())
	{
		if (const AFPSRGameState* GameState = World->GetGameState<AFPSRGameState>())
		{
			StageIndex = GameState->GetStageIndex();
		}
	}
	const FFPSRStageDifficultyAnchor StageAnchor = UFPSRRunScheduleDataAsset::EvalStageAt(
		ActiveSchedule ? TConstArrayView<FFPSRStageDifficultyAnchor>(ActiveSchedule->StageDifficulty) : TConstArrayView<FFPSRStageDifficultyAnchor>(),
		StageIndex);

	// Level-driven scaling (preferred): density scales with party PROGRESSION, not the clock (user 2026-06-24). The
	// schedule's AliveCountByLevel anchors map party level -> target alive count (piecewise-linear). Falls back to the
	// legacy time ramp only when no anchors are authored (null/legacy schedule), so existing schedules don't regress.
	if (ActiveSchedule && ActiveSchedule->AliveCountByLevel.Num() > 0)
	{
		int32 PartyLevel = 1;
		if (const UWorld* World = GetWorld())
		{
			if (const AFPSRGameState* GameState = World->GetGameState<AFPSRGameState>())
			{
				PartyLevel = GameState->GetPartyLevel();
			}
		}
		const float LevelScaled = UFPSRRunScheduleDataAsset::EvalAliveCountByLevel(ActiveSchedule->AliveCountByLevel, PartyLevel);
		// 🔴 스테이지 계수는 라운딩 직전 float 단계에서 곱한다, 그리고 이 분기의 라운딩(RoundToInt)은 그대로
		// 보존한다 — 레거시 분기(FloorToInt, 아래)와 라운딩 방식을 통일하면 항등 앵커(빈 StageDifficulty)에서도
		// 레거시 스케줄이 미세 회귀한다(꼬리를 하나로 합치지 말 것 — 계획 3단계).
		const float Scaled = (LevelScaled + StageAnchor.AliveCountBonus) * StageAnchor.AliveCountMultiplier;
		return FMath::Clamp(FMath::RoundToInt(Scaled), 0, MaxCount);
	}

	// Legacy time ramp (only when AliveCountByLevel is empty): +PerMin/min up to BossTime (RunClock stops there), then
	// +PerMinAfterBoss/min while the boss is up (PostBossElapsed). No discontinuity at the boss.
	const int32 Base = ActiveSchedule ? ActiveSchedule->BaseAliveCount : FallbackBaseAliveCount;
	const float PerMin = ActiveSchedule ? ActiveSchedule->AliveCountPerMinute : FallbackAliveCountPerMinute;
	const float PerMinAfterBoss = ActiveSchedule ? ActiveSchedule->AliveCountPerMinuteAfterBoss : FallbackAliveCountPerMinuteAfterBoss;
	const float PreBossClock = FMath::Min(RunClock, GetBossTime());
	const float TimeScaled = Base + PerMin * (PreBossClock / 60.0f) + PerMinAfterBoss * (PostBossElapsed / 60.0f);
	// 🔴 레거시 분기는 FloorToInt — 레벨 분기(RoundToInt, 위)와 다른 라운딩을 그대로 보존한다(같은 이유).
	const float Scaled = (TimeScaled + StageAnchor.AliveCountBonus) * StageAnchor.AliveCountMultiplier;
	return FMath::Clamp(FMath::FloorToInt(Scaled), 0, MaxCount);
}

void UFPSRRunDirectorSubsystem::UpdateSpawnIntensity()
{
	if (UFPSREnemySpawnSubsystem* SpawnSub = GetSpawnSub())
	{
		SpawnSub->SetTargetAliveCount(ComputeTargetAliveCount());
	}
}

void UFPSRRunDirectorSubsystem::DirectorTick()
{
	if (!HasServerAuthority() || !bRunActive)
	{
		return;
	}

	AFPSRGameState* GS = GetGS();
	if (!GS)
	{
		return;
	}

	// Deferred start: hold until the first player pawn appears, then wait for the opening-seed freeze.
	if (bAwaitingFirstPlayer)
	{
		if (HasAnyPlayerPawn())
		{
			bAwaitingFirstPlayer = false;
			bWaitingForOpeningSeed = true;
			OpeningWaitElapsed = 0.0f;
		}
		return;
	}

	// Pre-combat hold: keep the spawn target at 0 until the opening-seed freeze engages (then the freeze gate
	// holds spawning) — or until a short timeout if no opening seed ever comes (anti-deadlock).
	if (bWaitingForOpeningSeed)
	{
		OpeningWaitElapsed += DirectorInterval;
		// Proceed once every present player's opening seed has at least been issued (covers a fast pick that
		// freezes+unfreezes between ticks), or after a short timeout if no opening seed ever comes.
		const bool bTimedOut = OpeningWaitElapsed >= OpeningSeedWaitTimeout;
		if (AllPlayersOpeningSeedIssued() || bTimedOut)
		{
			bWaitingForOpeningSeed = false;

			// 🔴 스테이지 0 억제기 내구도 적용 지점 — 반드시 여기, 무조건(아래 IsRunPaused/IsStageTransitionActive
			// 가드 **밖**). StartRun 은 너무 이르다(클라 조인 전이라 참가자 수를 아직 못 읽는다). 그리고 바로
			// 아래 UpdateSpawnIntensity() 호출과 나란히 그 가드 **안**에 두면 안 된다 — 정상 MP 경로에서는 이
			// 해제 순간이 거의 항상 카드 프리즈(오프닝 시드 선택) 중이라, 가드 안에 두면 이 1회성 Apply 가
			// 영원히 실행되지 않는다(UpdateSpawnIntensity 는 매 틱 재호출로 자가복구하지만, 이 Apply 는 그런
			// 복구 지점이 없다 — 계획 4단계). bTimedOut 경로도 이 위치면 자동으로 함께 탄다.
			if (AFPSRArenaActor* OpeningArena = AFPSRArenaActor::FindActiveInWorld(GetWorld()))
			{
				ApplyStageDifficultyToArena(OpeningArena, GS->GetStageIndex());
			}

			if (bTimedOut)
			{
				UE_LOG(LogFPSR, Warning, TEXT("[Run] Opening-seed hold timed out — starting combat"));
			}
			// If the freeze is still up (mid-selection) or a stage transition is active, the pause gate holds
			// spawning; otherwise start now.
			if (!GS->IsRunPaused() && !GS->IsStageTransitionActive())
			{
				UpdateSpawnIntensity();
			}
		}
		return;
	}

	// Global freeze (card selection) OR an active stage transition: the whole timeline halts (Game.MD §2-2 / ADR
	// 0010 D6) — a transition window is not survival time, so the run clock and spawn ramp must not advance through
	// it either (the swarm is frozen for the grace-window reward, not growing).
	if (GS->IsRunPaused() || GS->IsStageTransitionActive())
	{
		return;
	}

	// Boss phase: the survival clock + scheduled missions halt, but the swarm KEEPS spawning and ramps at the
	// post-boss rate (the §2-2 freeze gate above still halts everything; boss death ends the run via Victory).
	// PostBossElapsed drives the continued ramp without advancing the survival/HUD RunClock.
	if (GS->GetRunPhase() == ERunPhase::Boss)
	{
		PostBossElapsed += DirectorInterval * TimeScale;
		UpdateSpawnIntensity();
		return;
	}

	// --- Combat: advance the run clock, scale spawns, fire scheduled missions, trigger the boss. ---
	RunClock += DirectorInterval * TimeScale;
	GS->SetRunClockSeconds(RunClock);

	// Mirror the active mission's progress to the GameState for the HUD capture/progress bar (B1). 0 when no mission.
	GS->SetMissionProgress(ActiveMission ? ActiveMission->GetMissionProgress() : 0.0f);

	UpdateSpawnIntensity();

	// Boss supersedes missions: check it BEFORE spawning a due mission so a mission at/near BossTime (or a
	// TimeScale jump past both) can't spawn a mission that EnterBoss immediately destroys (reward lost).
	if (RunClock >= GetBossTime())
	{
		EnterBoss();
		return;
	}

	TrySpawnDueMission();

	// Periodic progress log (every 30s of run time). while-loop so a high TimeScale can't skip one.
	while (RunClock >= NextRunLogTime && NextRunLogTime < GetBossTime())
	{
		UE_LOG(LogFPSR, Log, TEXT("[Run] t=%.0fs / boss %.0fs (target alive=%d, mission=%s)"),
			RunClock, GetBossTime(), ComputeTargetAliveCount(), ActiveMission ? TEXT("active") : TEXT("-"));
		NextRunLogTime += 30.0f;
	}

	if (bRunDebug && GEngine)
	{
		UFPSREnemySpawnSubsystem* SpawnSub = GetSpawnSub();
		const int32 AliveCount = SpawnSub ? SpawnSub->GetAliveCount() : 0;
		GEngine->AddOnScreenDebugMessage((uint64)this, 0.0f, FColor::Cyan, FString::Printf(
			TEXT("[Run] Combat t=%.0f/boss%.0f alive=%d/%d mission=%s xScale=%.1f"),
			RunClock, GetBossTime(), AliveCount, ComputeTargetAliveCount(),
			ActiveMission ? TEXT("active") : TEXT("-"), TimeScale));
	}
}

void UFPSRRunDirectorSubsystem::TrySpawnDueMission()
{
	if (ActiveMission || !ActiveSchedule)
	{
		return; // one mission at a time; no schedule = no missions
	}

	for (int32 i = 0; i < ActiveSchedule->MissionWindows.Num(); ++i)
	{
		if (MissionWindowFired.IsValidIndex(i) && MissionWindowFired[i])
		{
			continue;
		}
		const float TriggerTime = WindowTriggerTimes.IsValidIndex(i) ? WindowTriggerTimes[i] : ActiveSchedule->MissionWindows[i].MinTime;
		// Skip windows rolled at/after the boss — they'd be destroyed by the boss transition anyway.
		if (TriggerTime >= GetBossTime())
		{
			if (MissionWindowFired.IsValidIndex(i)) { MissionWindowFired[i] = true; }
			continue;
		}
		if (RunClock >= TriggerTime)
		{
			if (MissionWindowFired.IsValidIndex(i))
			{
				MissionWindowFired[i] = true;
			}
			if (UFPSRMissionDataAsset* Mission = PickRandomMission(ActiveSchedule->MissionWindows[i]))
			{
				SpawnMission(Mission);
			}
			else
			{
				UE_LOG(LogFPSR, Warning, TEXT("[Run] Mission window %d has no valid mission in its pool"), i);
			}
			return; // fire one per tick
		}
	}
}

UFPSRMissionDataAsset* UFPSRRunDirectorSubsystem::PickRandomMission(const FFPSRMissionWindow& Window) const
{
	TArray<UFPSRMissionDataAsset*> Valid;
	Valid.Reserve(Window.MissionPool.Num());
	for (const TObjectPtr<UFPSRMissionDataAsset>& M : Window.MissionPool)
	{
		if (M)
		{
			Valid.Add(M);
		}
	}
	if (Valid.Num() == 0)
	{
		return nullptr;
	}
	return Valid[FMath::RandRange(0, Valid.Num() - 1)];
}

void UFPSRRunDirectorSubsystem::SpawnMission(UFPSRMissionDataAsset* MissionData)
{
	if (!MissionData)
	{
		return;
	}

	TSubclassOf<AFPSRMissionActor> Cls = MissionData->MissionClass;
	if (!Cls)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Run] Mission %s has no MissionClass"), *MissionData->GetName());
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Point-set missions (MovingZone, CollectOrbs) use a designer-placed set; others use the point / player
	// selection. Query the class default object so we can pick the set (and spawn at it) before spawning.
	const AFPSRMissionActor* CDO = Cls->GetDefaultObject<AFPSRMissionActor>();
	AFPSRMissionPointSet* PointSet = (CDO && CDO->UsesPointSet()) ? SelectMissionPointSet(MissionData) : nullptr;

	const FTransform SpawnXform = PointSet ? PointSet->GetFirstPointTransform() : SelectMissionSpawnTransform(MissionData);
	ActiveMission = World->SpawnActor<AFPSRMissionActor>(Cls, SpawnXform.GetLocation(), SpawnXform.Rotator(), SpawnParams);
	if (!ActiveMission)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Run] Failed to spawn mission actor from class %s"), *Cls->GetName());
		return;
	}

	if (PointSet)
	{
		ActiveMission->AssignPointSet(PointSet);
	}

	ActiveMission->OnMissionEndedNative.AddUObject(this, &UFPSRRunDirectorSubsystem::OnMissionEnded);
	ActiveMission->ServerActivate(MissionData);

	// Publish to the GameState so every client's HUD shows a mission-start banner with the mission name (B10).
	if (AFPSRGameState* GS = GetGS())
	{
		GS->SetActiveMission(MissionData);
	}

	UE_LOG(LogFPSR, Log, TEXT("[Run] Mission spawned: %s (t=%.0fs)"), *MissionData->GetName(), RunClock);
}

void UFPSRRunDirectorSubsystem::OnMissionEnded(AFPSRMissionActor* Mission, bool bSuccess)
{
	if (bSuccess)
	{
		// Mission cleared: grant every player a weapon-unlock pick and freeze the run for selection (§2-3-4).
		if (UWorld* World = GetWorld())
		{
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				if (AFPSRPlayerController* PC = Cast<AFPSRPlayerController>(It->Get()))
				{
					PC->GrantWeaponUnlock();
				}
			}
		}

		if (AFPSRGameState* GS = GetGS())
		{
			GS->RefreshPauseState(); // freeze + present the weapon-unlock offer
		}

		UE_LOG(LogFPSR, Log, TEXT("[Run] Mission cleared — weapon-unlock pick granted, run frozen for selection"));
	}
	else
	{
		UE_LOG(LogFPSR, Log, TEXT("[Run] Mission failed/ended"));
	}

	DestroyActiveMission();
}

void UFPSRRunDirectorSubsystem::DestroyActiveMission()
{
	if (ActiveMission)
	{
		ActiveMission->OnMissionEndedNative.RemoveAll(this);
		ActiveMission->Destroy();
		ActiveMission = nullptr;
	}
	// Clear the replicated mission so the next mission's start banner fires cleanly (B10) + reset the HUD progress (B1).
	if (AFPSRGameState* GS = GetGS())
	{
		GS->SetActiveMission(nullptr);
		GS->SetMissionProgress(0.0f);
	}
}

void UFPSRRunDirectorSubsystem::EnterBoss()
{
	if (bBossStarted)
	{
		return;
	}
	bBossStarted = true;

	DestroyActiveMission();

	if (AFPSRGameState* GS = GetGS())
	{
		GS->SetRunPhase(ERunPhase::Boss);
	}

	// The swarm PERSISTS through the boss fight (Game.MD: enemies keep spawning after the boss appears). The next
	// DirectorTick boss branch keeps the spawn target at the post-boss ramp — no SetTargetAliveCount(0)/ReleaseAllEnemies.

	// Spawn the boss: defeating it ends the run in Victory (boss OnDeath -> GameMode::NotifyBossDefeated, U3).
	SpawnBoss();
}

void UFPSRRunDirectorSubsystem::SpawnBoss()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UFPSRBossDefinitionDataAsset* Def = ActiveSchedule ? ActiveSchedule->BossDefinition : nullptr;

	// Designer-assigned boss class, or the C++ AFPSRBossBase placeholder so the victory loop is testable before
	// any boss BP/definition exists (mirrors the enemy-class fallback in the spawn subsystem).
	UClass* BossClassToSpawn = (Def && Def->BossClass) ? Def->BossClass.Get() : AFPSRBossBase::StaticClass();

	// Honor the definition's spawn-mode (default true for the C++ fallback boss / no definition).
	const bool bUseSpawnPoint = Def ? Def->bUseBossSpawnPoint : true;
	const FTransform SpawnXform = SelectBossSpawnTransform(bUseSpawnPoint);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ActiveBoss = World->SpawnActor<AFPSRBossBase>(BossClassToSpawn, SpawnXform.GetLocation(), SpawnXform.Rotator(), SpawnParams);
	if (!ActiveBoss)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Run] Boss gate reached but boss spawn failed (class %s)"), *BossClassToSpawn->GetName());
		return;
	}

	// Apply the definition's tuning (health override) to the spawned instance.
	if (Def)
	{
		ActiveBoss->InitializeFromDefinition(Def);
	}

	// Publish the boss to the GameState so every client's HUD boss bar can locate + bind it (B11). After
	// InitializeFromDefinition so the replicated MaxHealth (B12) is set before clients first read the bar.
	if (AFPSRGameState* GS = GetGS())
	{
		GS->SetActiveBoss(ActiveBoss);
	}

	UE_LOG(LogFPSR, Log, TEXT("[Run] Boss spawned: %s at %s (t=%.0fs)"), *ActiveBoss->GetName(),
		*SpawnXform.GetLocation().ToCompactString(), RunClock);
}

FTransform UFPSRRunDirectorSubsystem::SelectBossSpawnTransform(bool bUseSpawnPoint) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return FTransform::Identity;
	}

	// Spawn points only when the definition opts in. Weighted-random among enabled, designer-placed boss spawn
	// points (a boss usually has one; several allow variety). bUseBossSpawnPoint=false skips straight to the fallback.
	if (bUseSpawnPoint)
	{
		struct FBossCandidate { AFPSRBossSpawnPoint* Point; float Weight; };
		TArray<FBossCandidate> Candidates;
		float TotalWeight = 0.0f;
		for (TActorIterator<AFPSRBossSpawnPoint> It(World); It; ++It)
		{
			AFPSRBossSpawnPoint* Point = *It;
			if (Point && Point->IsEnabled() && Point->GetWeight() > 0.0f)
			{
				Candidates.Add({ Point, Point->GetWeight() });
				TotalWeight += Point->GetWeight();
			}
		}

		if (Candidates.Num() > 0 && TotalWeight > 0.0f)
		{
			const float Pick = FMath::FRandRange(0.0f, TotalWeight);
			float Cumulative = 0.0f;
			for (const FBossCandidate& C : Candidates)
			{
				Cumulative += C.Weight;
				if (Pick <= Cumulative)
				{
					return C.Point->GetActorTransform();
				}
			}
			return Candidates.Last().Point->GetActorTransform();
		}

		// Opted into spawn points but none placed — warn so the designer adds an AFPSRBossSpawnPoint, then fall back.
		UE_LOG(LogFPSR, Warning, TEXT("[Run] No AFPSRBossSpawnPoint placed — spawning boss at a player fallback. Place a boss spawn point for content."));
	}

	// Fallback: in front of the first player so the boss is visible (definition opted out, or no point placed).
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APlayerController* PC = It->Get())
		{
			if (const APawn* PlayerPawn = PC->GetPawn())
			{
				// 800cm ahead, lifted ~one boss half-height so the placeholder isn't half-buried (gravity is off on
				// the scaffold boss). Rough on purpose — this is the no-spawn-point fallback, not authored placement.
				const FVector Loc = PlayerPawn->GetActorLocation()
					+ PlayerPawn->GetActorForwardVector() * 800.0f
					+ FVector(0.0f, 0.0f, 200.0f);
				return FTransform(FRotator::ZeroRotator, Loc);
			}
		}
	}

	return FTransform::Identity;
}

FTransform UFPSRRunDirectorSubsystem::SelectMissionSpawnTransform(const UFPSRMissionDataAsset* Mission) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return FTransform::Identity;
	}

	const FGameplayTag RequiredTag = Mission ? Mission->SpawnPointTag : FGameplayTag();

	// Gather current player locations once (used for the optional MinPlayerDistance filter and fallback).
	TArray<FVector> PlayerLocations;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (APawn* PlayerPawn = PC->GetPawn())
			{
				PlayerLocations.Add(PlayerPawn->GetActorLocation());
			}
		}
	}

	// Collect tag-matched, enabled, positively-weighted points (before the distance filter), recording each
	// point's distance to the nearest player so we can apply MinPlayerDistance and still fall back sensibly.
	struct FCandidate
	{
		AFPSRMissionSpawnPoint* Point;
		float Weight;
		float NearestPlayerDistSq;
	};
	TArray<FCandidate> TagMatched;
	for (TActorIterator<AFPSRMissionSpawnPoint> It(World); It; ++It)
	{
		AFPSRMissionSpawnPoint* Point = *It;
		if (!Point || !Point->IsEnabled() || Point->GetWeight() <= 0.0f)
		{
			continue;
		}
		// Empty mission tag accepts any point; otherwise the point's tag must match (be / be a child of) it.
		if (RequiredTag.IsValid() && !Point->GetMissionTag().MatchesTag(RequiredTag))
		{
			continue;
		}
		float NearestSq = TNumericLimits<float>::Max();
		for (const FVector& PL : PlayerLocations)
		{
			NearestSq = FMath::Min(NearestSq, FVector::DistSquared(Point->GetActorLocation(), PL));
		}
		TagMatched.Add({ Point, Point->GetWeight(), NearestSq });
	}

	// Prefer points that satisfy MinPlayerDistance (weighted-random among them).
	TArray<const FCandidate*> FarEnough;
	FarEnough.Reserve(TagMatched.Num());
	float TotalWeight = 0.0f;
	for (const FCandidate& C : TagMatched)
	{
		const float MinDist = C.Point->GetMinPlayerDistance();
		const bool bFarEnough = (MinDist <= 0.0f) || PlayerLocations.Num() == 0 || C.NearestPlayerDistSq >= FMath::Square(MinDist);
		if (bFarEnough)
		{
			FarEnough.Add(&C);
			TotalWeight += C.Weight;
		}
	}

	if (FarEnough.Num() > 0 && TotalWeight > 0.0f)
	{
		const float Pick = FMath::FRandRange(0.0f, TotalWeight);
		float Cumulative = 0.0f;
		for (const FCandidate* C : FarEnough)
		{
			Cumulative += C->Weight;
			if (Pick <= Cumulative)
			{
				return C->Point->GetActorTransform();
			}
		}
		return FarEnough.Last()->Point->GetActorTransform();
	}

	// Tag-matched points exist but all are within MinPlayerDistance — choose the farthest matching point
	// rather than spawning the objective on top of a player.
	if (TagMatched.Num() > 0)
	{
		const FCandidate* Farthest = &TagMatched[0];
		for (const FCandidate& C : TagMatched)
		{
			if (C.NearestPlayerDistSq > Farthest->NearestPlayerDistSq)
			{
				Farthest = &C;
			}
		}
		UE_LOG(LogFPSR, Warning, TEXT("[Run] All mission spawn points within MinPlayerDistance — using farthest matching point"));
		return Farthest->Point->GetActorTransform();
	}

	// No tag-matched points at all (unmapped level / wrong tag) — fall back to the first player.
	UE_LOG(LogFPSR, Warning, TEXT("[Run] No matching mission spawn point (tag=%s) — using player location fallback"),
		*RequiredTag.ToString());
	if (PlayerLocations.Num() > 0)
	{
		return FTransform(FRotator::ZeroRotator, PlayerLocations[0]);
	}

	return FTransform::Identity;
}

AFPSRMissionPointSet* UFPSRRunDirectorSubsystem::SelectMissionPointSet(const UFPSRMissionDataAsset* Mission) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	const FGameplayTag RequiredTag = Mission ? Mission->SpawnPointTag : FGameplayTag();

	TArray<FVector> PlayerLocations;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (APawn* PlayerPawn = PC->GetPawn())
			{
				PlayerLocations.Add(PlayerPawn->GetActorLocation());
			}
		}
	}

	struct FPointSetCandidate
	{
		AFPSRMissionPointSet* PointSet;
		float Weight;
		float NearestPlayerDistSq;
	};
	TArray<FPointSetCandidate> TagMatched;
	for (TActorIterator<AFPSRMissionPointSet> It(World); It; ++It)
	{
		AFPSRMissionPointSet* Set = *It;
		if (!Set || !Set->IsEnabled() || Set->GetWeight() <= 0.0f)
		{
			continue;
		}
		if (RequiredTag.IsValid() && !Set->GetPointSetTag().MatchesTag(RequiredTag))
		{
			continue;
		}
		const FVector FirstPoint = Set->GetFirstPointTransform().GetLocation();
		float NearestSq = TNumericLimits<float>::Max();
		for (const FVector& PL : PlayerLocations)
		{
			NearestSq = FMath::Min(NearestSq, FVector::DistSquared(FirstPoint, PL));
		}
		TagMatched.Add({ Set, Set->GetWeight(), NearestSq });
	}

	// Prefer point sets whose first point satisfies MinPlayerDistance (weighted-random among them).
	TArray<const FPointSetCandidate*> FarEnough;
	FarEnough.Reserve(TagMatched.Num());
	float TotalWeight = 0.0f;
	for (const FPointSetCandidate& C : TagMatched)
	{
		const float MinDist = C.PointSet->GetMinPlayerDistance();
		const bool bFarEnough = (MinDist <= 0.0f) || PlayerLocations.Num() == 0 || C.NearestPlayerDistSq >= FMath::Square(MinDist);
		if (bFarEnough)
		{
			FarEnough.Add(&C);
			TotalWeight += C.Weight;
		}
	}

	if (FarEnough.Num() > 0 && TotalWeight > 0.0f)
	{
		const float Pick = FMath::FRandRange(0.0f, TotalWeight);
		float Cumulative = 0.0f;
		for (const FPointSetCandidate* C : FarEnough)
		{
			Cumulative += C->Weight;
			if (Pick <= Cumulative)
			{
				return C->PointSet;
			}
		}
		return FarEnough.Last()->PointSet;
	}

	// All within MinPlayerDistance — choose the farthest matching point set rather than none.
	if (TagMatched.Num() > 0)
	{
		const FPointSetCandidate* Farthest = &TagMatched[0];
		for (const FPointSetCandidate& C : TagMatched)
		{
			if (C.NearestPlayerDistSq > Farthest->NearestPlayerDistSq)
			{
				Farthest = &C;
			}
		}
		return Farthest->PointSet;
	}

	return nullptr;
}

bool UFPSRRunDirectorSubsystem::HasAnyPlayerPawn() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APlayerController* PC = It->Get())
		{
			if (PC->GetPawn() != nullptr)
			{
				return true;
			}
		}
	}
	return false;
}

bool UFPSRRunDirectorSubsystem::AllPlayersOpeningSeedIssued() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	bool bAnyPlayer = false;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (const AFPSRPlayerController* PC = Cast<AFPSRPlayerController>(It->Get()))
		{
			bAnyPlayer = true;
			if (!PC->HasStartedOpeningSeed())
			{
				return false;
			}
		}
	}
	return bAnyPlayer;
}

AFPSRGameState* UFPSRRunDirectorSubsystem::GetGS() const
{
	UWorld* World = GetWorld();
	return World ? World->GetGameState<AFPSRGameState>() : nullptr;
}

UFPSREnemySpawnSubsystem* UFPSRRunDirectorSubsystem::GetSpawnSub() const
{
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UFPSREnemySpawnSubsystem>() : nullptr;
}

void UFPSRRunDirectorSubsystem::DebugTriggerMission(int32 WindowIndex, int32 PoolIndex)
{
	if (!HasServerAuthority() || ActiveMission || !ActiveSchedule)
	{
		return;
	}

	// Explicit window: spawn from that window's pool (PoolIndex >= 0 = that specific mission for targeted
	// testing, otherwise a random one). Ignores the rolled trigger time.
	if (WindowIndex >= 0)
	{
		if (!ActiveSchedule->MissionWindows.IsValidIndex(WindowIndex))
		{
			return;
		}
		const FFPSRMissionWindow& Window = ActiveSchedule->MissionWindows[WindowIndex];
		UFPSRMissionDataAsset* Mission = (PoolIndex >= 0)
			? (Window.MissionPool.IsValidIndex(PoolIndex) ? Window.MissionPool[PoolIndex].Get() : nullptr)
			: PickRandomMission(Window);
		if (Mission)
		{
			if (MissionWindowFired.IsValidIndex(WindowIndex))
			{
				MissionWindowFired[WindowIndex] = true;
			}
			SpawnMission(Mission);
		}
		return;
	}

	// Spawn the next not-yet-fired window immediately (random mission from its pool).
	for (int32 i = 0; i < ActiveSchedule->MissionWindows.Num(); ++i)
	{
		if (MissionWindowFired.IsValidIndex(i) && MissionWindowFired[i])
		{
			continue;
		}
		if (MissionWindowFired.IsValidIndex(i))
		{
			MissionWindowFired[i] = true;
		}
		if (UFPSRMissionDataAsset* Mission = PickRandomMission(ActiveSchedule->MissionWindows[i]))
		{
			SpawnMission(Mission);
		}
		return;
	}
}

void UFPSRRunDirectorSubsystem::DebugClearMission()
{
	if (ActiveMission)
	{
		ActiveMission->CompleteMission();
	}
}

void UFPSRRunDirectorSubsystem::DebugSkipToBoss()
{
	if (HasServerAuthority())
	{
		EnterBoss();
	}
}

// ---- Console Commands (debug; excluded from shipping) ----

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorldAndArgs GFPSRMissionTriggerCmd(
	TEXT("FPSR.MissionTrigger"),
	TEXT("Spawn a scheduled mission immediately (debug). Usage: FPSR.MissionTrigger [windowIndex] [poolIndex]  (no args = next unfired window, random from pool)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UFPSRRunDirectorSubsystem* Dir = World ? World->GetSubsystem<UFPSRRunDirectorSubsystem>() : nullptr)
		{
			const int32 WindowIndex = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : -1;
			const int32 PoolIndex = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : -1;
			Dir->DebugTriggerMission(WindowIndex, PoolIndex);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRMissionClearCmd(
	TEXT("FPSR.MissionClear"),
	TEXT("Mark the active mission as completed (debug)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UFPSRRunDirectorSubsystem* Dir = World ? World->GetSubsystem<UFPSRRunDirectorSubsystem>() : nullptr)
		{
			Dir->DebugClearMission();
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRSkipToBossCmd(
	TEXT("FPSR.SkipToBoss"),
	TEXT("Skip directly to the boss (debug)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UFPSRRunDirectorSubsystem* Dir = World ? World->GetSubsystem<UFPSRRunDirectorSubsystem>() : nullptr)
		{
			Dir->DebugSkipToBoss();
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRRunTimeScaleCmd(
	TEXT("FPSR.RunTimeScale"),
	TEXT("Set the run-clock time scale (1=normal, 10=10x). Usage: FPSR.RunTimeScale [scale]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UFPSRRunDirectorSubsystem* Dir = World ? World->GetSubsystem<UFPSRRunDirectorSubsystem>() : nullptr)
		{
			Dir->SetTimeScale(Args.Num() > 0 ? FCString::Atof(*Args[0]) : 1.0f);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRRunDebugCmd(
	TEXT("FPSR.RunDebug"),
	TEXT("Toggle the on-screen run debug overlay. Usage: FPSR.RunDebug [0|1]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UFPSRRunDirectorSubsystem* Dir = World ? World->GetSubsystem<UFPSRRunDirectorSubsystem>() : nullptr)
		{
			Dir->SetRunDebug(Args.Num() > 0 ? (FCString::Atoi(*Args[0]) != 0) : true);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRKillAllEnemiesCmd(
	TEXT("FPSR.KillAllEnemies"),
	TEXT("Release all active enemies back to the pool (debug)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UFPSREnemySpawnSubsystem* Sub = World ? World->GetSubsystem<UFPSREnemySpawnSubsystem>() : nullptr)
		{
			Sub->ReleaseAllEnemies();
		}
	}));
#endif // !UE_BUILD_SHIPPING
