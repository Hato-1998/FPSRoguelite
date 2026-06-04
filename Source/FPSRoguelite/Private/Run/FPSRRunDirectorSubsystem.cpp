// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/FPSRRunDirectorSubsystem.h"
#include "Run/FPSRRunScheduleDataAsset.h"
#include "Run/Mission/FPSRMissionActor.h"
#include "Run/Mission/FPSRMissionDataAsset.h"
#include "Run/Mission/FPSRMissionSpawnPoint.h"
#include "Card/FPSRCardDataAsset.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerController.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Core/FPSRLogChannels.h"
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
	bBossStarted = false;
	NextRunLogTime = 30.0f;

	// Size the per-event fired flags to the active schedule (no missions without a schedule asset).
	const int32 NumEvents = ActiveSchedule ? ActiveSchedule->MissionEvents.Num() : 0;
	MissionEventFired.Init(false, NumEvents);

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
	const int32 Base = ActiveSchedule ? ActiveSchedule->BaseAliveCount : FallbackBaseAliveCount;
	const float PerMin = ActiveSchedule ? ActiveSchedule->AliveCountPerMinute : FallbackAliveCountPerMinute;
	const int32 MaxCount = ActiveSchedule ? ActiveSchedule->MaxAliveCount : FallbackMaxAliveCount;
	const int32 Scaled = Base + FMath::FloorToInt(PerMin * (RunClock / 60.0f));
	return FMath::Clamp(Scaled, 0, MaxCount);
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

	// Boss phase: timeline halted (no clock, no missions, no scaling).
	if (GS->GetRunPhase() == ERunPhase::Boss)
	{
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

	for (int32 i = 0; i < ActiveSchedule->MissionEvents.Num(); ++i)
	{
		if (MissionEventFired.IsValidIndex(i) && MissionEventFired[i])
		{
			continue;
		}
		const FFPSRMissionEvent& Event = ActiveSchedule->MissionEvents[i];
		// Skip events scheduled at/after the boss — they'd be destroyed by the boss transition anyway.
		if (Event.TriggerTime >= GetBossTime())
		{
			if (MissionEventFired.IsValidIndex(i)) { MissionEventFired[i] = true; }
			continue;
		}
		if (RunClock >= Event.TriggerTime)
		{
			if (MissionEventFired.IsValidIndex(i))
			{
				MissionEventFired[i] = true;
			}
			if (Event.Mission)
			{
				SpawnMission(Event.Mission);
			}
			return; // fire one per tick
		}
	}
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

	const FTransform SpawnXform = SelectMissionSpawnTransform(MissionData);
	ActiveMission = World->SpawnActor<AFPSRMissionActor>(Cls, SpawnXform.GetLocation(), SpawnXform.Rotator(), SpawnParams);
	if (!ActiveMission)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Run] Failed to spawn mission actor from class %s"), *Cls->GetName());
		return;
	}

	ActiveMission->OnMissionEndedNative.AddUObject(this, &UFPSRRunDirectorSubsystem::OnMissionEnded);
	ActiveMission->ServerActivate(MissionData);

	UE_LOG(LogFPSR, Log, TEXT("[Run] Mission spawned: %s (t=%.0fs)"), *MissionData->GetName(), RunClock);
}

void UFPSRRunDirectorSubsystem::OnMissionEnded(AFPSRMissionActor* Mission, bool bSuccess)
{
	if (bSuccess)
	{
		// Mission cleared: grant every player a mission-reward pick and freeze the run for selection (§2-8).
		UFPSRCardDataAsset* RewardCard = nullptr;
		if (Mission)
		{
			if (const UFPSRMissionDataAsset* Data = Mission->GetMissionData())
			{
				RewardCard = Data->RewardCard;
			}
		}

		if (UWorld* World = GetWorld())
		{
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				if (AFPSRPlayerController* PC = Cast<AFPSRPlayerController>(It->Get()))
				{
					PC->GrantMissionReward(RewardCard);
				}
			}
		}

		if (AFPSRGameState* GS = GetGS())
		{
			GS->RefreshPauseState(); // freeze + present the reward offers (no-op if RewardCard was null)
		}

		UE_LOG(LogFPSR, Log, TEXT("[Run] Mission cleared — reward granted, run frozen for selection"));
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

	if (UFPSREnemySpawnSubsystem* SpawnSub = GetSpawnSub())
	{
		SpawnSub->SetTargetAliveCount(0);
		SpawnSub->ReleaseAllEnemies(); // clear the swarm for the boss arena (boss-specific spawns are P6)
	}

	UE_LOG(LogFPSR, Log, TEXT("[Run] Boss gate reached at t=%.0fs (boss actor is a P6 stub) — timeline halted"), RunClock);
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

void UFPSRRunDirectorSubsystem::DebugTriggerMission()
{
	if (!HasServerAuthority() || ActiveMission || !ActiveSchedule)
	{
		return;
	}

	// Spawn the next not-yet-fired mission immediately (ignoring its trigger time).
	for (int32 i = 0; i < ActiveSchedule->MissionEvents.Num(); ++i)
	{
		if (MissionEventFired.IsValidIndex(i) && MissionEventFired[i])
		{
			continue;
		}
		if (MissionEventFired.IsValidIndex(i))
		{
			MissionEventFired[i] = true;
		}
		if (ActiveSchedule->MissionEvents[i].Mission)
		{
			SpawnMission(ActiveSchedule->MissionEvents[i].Mission);
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
	TEXT("Spawn the next scheduled mission immediately (debug)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UFPSRRunDirectorSubsystem* Dir = World ? World->GetSubsystem<UFPSRRunDirectorSubsystem>() : nullptr)
		{
			Dir->DebugTriggerMission();
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
