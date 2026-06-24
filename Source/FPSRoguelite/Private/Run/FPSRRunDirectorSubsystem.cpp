// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/FPSRRunDirectorSubsystem.h"
#include "Run/FPSRRunScheduleDataAsset.h"
#include "Run/Mission/FPSRMissionActor.h"
#include "Run/Mission/FPSRMissionDataAsset.h"
#include "Run/Mission/FPSRMissionSpawnPoint.h"
#include "Run/Mission/FPSRMissionPointSet.h"
#include "Card/FPSRCardDataAsset.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerController.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Boss/FPSRBossBase.h"
#include "Boss/FPSRBossSpawnPoint.h"
#include "Boss/FPSRBossDefinitionDataAsset.h"
#include "Core/FPSRLogChannels.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

namespace
{
	/** Piecewise-linear interpolation of the level→alive-count anchors at the given party level. Anchors are authored
	 *  in ascending Level; below the first anchor returns its Count, above the last stays flat at its Count. */
	float EvalAliveCountByLevel(const TArray<FFPSRAliveCountAnchor>& Anchors, int32 Level)
	{
		const int32 Num = Anchors.Num();
		if (Num == 0)
		{
			return 0.0f;
		}
		if (Level <= Anchors[0].Level)
		{
			return static_cast<float>(Anchors[0].Count);
		}
		if (Level >= Anchors[Num - 1].Level)
		{
			return static_cast<float>(Anchors[Num - 1].Count);
		}
		for (int32 i = 1; i < Num; ++i)
		{
			const FFPSRAliveCountAnchor& A = Anchors[i - 1];
			const FFPSRAliveCountAnchor& B = Anchors[i];
			if (Level <= B.Level)
			{
				const float Span = static_cast<float>(B.Level - A.Level);
				const float T = (Span > 0.0f) ? static_cast<float>(Level - A.Level) / Span : 0.0f;
				return FMath::Lerp(static_cast<float>(A.Count), static_cast<float>(B.Count), T);
			}
		}
		return static_cast<float>(Anchors[Num - 1].Count);
	}
}

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

	// Push schedule-driven spawn pacing to the spawn subsystem (the swarm fill rate — how fast it builds toward the
	// target alive count). Both the per-tick batch (MaxSpawnPerTick) and the tick interval (SpawnIntervalSeconds) are
	// tunable on DA_RunSchedule without further code changes; together they set the per-second spawn pace.
	if (UFPSREnemySpawnSubsystem* SpawnSub = GetSpawnSub())
	{
		SpawnSub->SetMaxSpawnPerTick(ActiveSchedule ? ActiveSchedule->MaxSpawnPerTick : FallbackMaxSpawnPerTick);
		SpawnSub->SetSpawnInterval(ActiveSchedule ? ActiveSchedule->SpawnIntervalSeconds : FallbackSpawnIntervalSeconds);
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

int32 UFPSRRunDirectorSubsystem::ComputeTargetAliveCount() const
{
	const int32 MaxCount = ActiveSchedule ? ActiveSchedule->MaxAliveCount : FallbackMaxAliveCount;

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
		const float Scaled = EvalAliveCountByLevel(ActiveSchedule->AliveCountByLevel, PartyLevel);
		return FMath::Clamp(FMath::RoundToInt(Scaled), 0, MaxCount);
	}

	// Legacy time ramp (only when AliveCountByLevel is empty): +PerMin/min up to BossTime (RunClock stops there), then
	// +PerMinAfterBoss/min while the boss is up (PostBossElapsed). No discontinuity at the boss.
	const int32 Base = ActiveSchedule ? ActiveSchedule->BaseAliveCount : FallbackBaseAliveCount;
	const float PerMin = ActiveSchedule ? ActiveSchedule->AliveCountPerMinute : FallbackAliveCountPerMinute;
	const float PerMinAfterBoss = ActiveSchedule ? ActiveSchedule->AliveCountPerMinuteAfterBoss : FallbackAliveCountPerMinuteAfterBoss;
	const float PreBossClock = FMath::Min(RunClock, GetBossTime());
	const float Scaled = Base + PerMin * (PreBossClock / 60.0f) + PerMinAfterBoss * (PostBossElapsed / 60.0f);
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
			if (bTimedOut)
			{
				UE_LOG(LogFPSR, Warning, TEXT("[Run] Opening-seed hold timed out — starting combat"));
			}
			// If the freeze is still up (mid-selection), the pause gate holds spawning; otherwise start now.
			if (!GS->IsRunPaused())
			{
				UpdateSpawnIntensity();
			}
		}
		return;
	}

	// Global freeze (card selection): the whole timeline halts (Game.MD §2-2).
	if (GS->IsRunPaused())
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
