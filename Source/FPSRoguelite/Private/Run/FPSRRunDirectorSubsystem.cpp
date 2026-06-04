// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/FPSRRunDirectorSubsystem.h"
#include "Run/FPSRRunScheduleDataAsset.h"
#include "Run/Mission/FPSRMissionActor.h"
#include "Run/Mission/FPSRMissionDataAsset.h"
#include "Run/Mission/FPSRMissionSpawnPoint.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRPlayerController.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Core/FPSRLogChannels.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
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

	// Build fallback rounds if needed
	if (!ActiveSchedule || ActiveSchedule->Rounds.Num() == 0)
	{
		FallbackRounds.Empty();
		FallbackRounds.Add(FFPSRRoundDef{ 120.0f, 50, nullptr, false });
		FallbackRounds.Add(FFPSRRoundDef{ 120.0f, 80, nullptr, false });
		FallbackRounds.Add(FFPSRRoundDef{ 60.0f, 120, nullptr, false });
		FallbackRounds.Add(FFPSRRoundDef{ 0.0f, 0, nullptr, true });
	}

	bRunActive = true;
	TotalElapsed = 0.0f;

	// Don't begin round 0 (which sets a positive spawn target) until a player pawn exists — otherwise the
	// spawn director would populate enemies around world origin (ComputeSpawnLocation falls back to origin
	// with no player). The director timer polls for the first pawn and begins the run then.
	if (HasAnyPlayerPawn())
	{
		BeginOpeningHold();
	}
	else
	{
		bAwaitingFirstPlayer = true;
		UE_LOG(LogFPSR, Log, TEXT("[Run] StartRun deferred — waiting for first player pawn"));
	}

	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().SetTimer(
			DirectorTimerHandle,
			this,
			&UFPSRRunDirectorSubsystem::DirectorTick,
			DirectorInterval,
			true
		);
	}
}

int32 UFPSRRunDirectorSubsystem::GetNumRounds() const
{
	if (ActiveSchedule && ActiveSchedule->Rounds.Num() > 0)
	{
		return ActiveSchedule->Rounds.Num();
	}
	return FallbackRounds.Num();
}

FFPSRRoundDef UFPSRRunDirectorSubsystem::GetRoundDef(int32 Index) const
{
	if (ActiveSchedule && ActiveSchedule->Rounds.IsValidIndex(Index))
	{
		return ActiveSchedule->Rounds[Index];
	}
	if (FallbackRounds.IsValidIndex(Index))
	{
		return FallbackRounds[Index];
	}
	// Out of range: return a boss round (safe fallback)
	return FFPSRRoundDef{ 0.0f, 0, nullptr, true };
}

void UFPSRRunDirectorSubsystem::BeginRound(int32 Index)
{
	CurrentRoundIndex = Index;

	AFPSRGameState* GS = GetGS();
	if (GS)
	{
		GS->SetCurrentRound(Index);
	}

	DestroyActiveMission();

	const FFPSRRoundDef Def = GetRoundDef(Index);

	if (Def.bBossRound)
	{
		EnterBoss();
		return;
	}

	RoundDuration = Def.Duration;
	ElapsedInRound = 0.0f;
	bMissionSpawned = false;
	bMissionClearedThisRound = false;

	MissionTriggerTime = (Def.Mission != nullptr)
		? FMath::FRandRange(Def.Duration * 0.1f, Def.Duration * 0.8f)
		: -1.0f;

	if (GS)
	{
		GS->SetRunPhase(ERunPhase::Combat);
	}

	UFPSREnemySpawnSubsystem* SpawnSub = GetSpawnSub();
	if (SpawnSub)
	{
		SpawnSub->SetTargetAliveCount(Def.TargetAliveCount);
	}

	UE_LOG(LogFPSR, Log, TEXT("[Run] Round %d begin (dur=%.0f target=%d missionTime=%.0f)"),
		Index, RoundDuration, Def.TargetAliveCount, MissionTriggerTime);
}

void UFPSRRunDirectorSubsystem::BeginOpeningHold()
{
	// No round/spawn target is set yet, so no enemies spawn while we hold. The director tick polls for
	// opening-seed completion and then begins round 0 (which sets the Combat phase + spawn target).
	bAwaitingOpeningSeed = true;
	OpeningHoldElapsed = 0.0f;
	UE_LOG(LogFPSR, Log, TEXT("[Run] Pre-combat hold — waiting for opening-seed selection before spawning"));
}

bool UFPSRRunDirectorSubsystem::AreOpeningSeedsComplete(bool& bOutAnyStarted) const
{
	bOutAnyStarted = false;
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	bool bAnyPlayer = false;
	bool bAllComplete = true;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AFPSRPlayerController* PC = Cast<AFPSRPlayerController>(It->Get());
		if (!PC)
		{
			continue;
		}
		bAnyPlayer = true;
		if (PC->HasStartedOpeningSeed())
		{
			bOutAnyStarted = true;
		}
		if (!PC->IsOpeningSeedComplete())
		{
			bAllComplete = false;
		}
	}

	// With no players we can't be "complete" — keep holding (the hold is only entered once a pawn exists).
	return bAnyPlayer && bAllComplete;
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

	// Deferred start: hold until the first player pawn appears, then enter the opening-seed hold.
	if (bAwaitingFirstPlayer)
	{
		if (HasAnyPlayerPawn())
		{
			bAwaitingFirstPlayer = false;
			BeginOpeningHold();
		}
		return;
	}

	// Pre-combat hold: keep spawns off until every player has finished its opening-seed picks (§2-2), so the
	// run doesn't start spawning monsters while players are still choosing their opening cards.
	if (bAwaitingOpeningSeed)
	{
		OpeningHoldElapsed += DirectorInterval; // real seconds (ignore TimeScale for the pre-game hold)
		bool bAnyStarted = false;
		const bool bAllComplete = AreOpeningSeedsComplete(bAnyStarted);
		const float Timeout = bAnyStarted ? OpeningHoldMaxTimeout : OpeningHoldNoStartTimeout;
		if (bAnyStarted && bAllComplete)
		{
			bAwaitingOpeningSeed = false;
			UE_LOG(LogFPSR, Log, TEXT("[Run] Opening seed complete — starting combat"));
			BeginRound(0);
		}
		else if (OpeningHoldElapsed >= Timeout)
		{
			bAwaitingOpeningSeed = false;
			UE_LOG(LogFPSR, Warning, TEXT("[Run] Opening-seed hold timed out (%.0fs, anyStarted=%d) — starting combat anyway"),
				OpeningHoldElapsed, bAnyStarted ? 1 : 0);
			BeginRound(0);
		}
		return;
	}

	const ERunPhase Phase = GS->GetRunPhase();

	if (Phase == ERunPhase::Combat)
	{
		const float Dt = DirectorInterval * TimeScale;
		ElapsedInRound += Dt;
		TotalElapsed += Dt;
		GS->SetRunClockSeconds(TotalElapsed);
		// Replicate round-remaining for the HUD timer (0 if this round has no duration, e.g. boss).
		GS->SetRoundTimeRemaining(RoundDuration > 0.0f ? FMath::Max(0.0f, RoundDuration - ElapsedInRound) : 0.0f);

		// Check if mission should spawn
		if (!bMissionSpawned && MissionTriggerTime >= 0.0f && ElapsedInRound >= MissionTriggerTime)
		{
			SpawnRoundMission();
		}

		// Check if round is complete
		if (RoundDuration > 0.0f && ElapsedInRound >= RoundDuration)
		{
			EndRound();
		}
	}
	else if (Phase == ERunPhase::Breather)
	{
		TryResumeFromBreather();
	}
	// Boss phase: nothing (timeline halted)

	// Debug output
	if (bRunDebug && GEngine)
	{
		UFPSREnemySpawnSubsystem* SpawnSub = GetSpawnSub();
		int32 AliveCount = SpawnSub ? SpawnSub->GetAliveCount() : 0;
		const TCHAR* PhaseStr = (Phase == ERunPhase::Combat) ? TEXT("Combat")
							  : (Phase == ERunPhase::Breather) ? TEXT("Breather")
							  : TEXT("Boss");
		const TCHAR* MissionStr = (ActiveMission != nullptr) ? TEXT("active")
							   : (bMissionClearedThisRound) ? TEXT("cleared")
							   : TEXT("-");

		GEngine->AddOnScreenDebugMessage(
			(uint64)this,
			0.0f,
			FColor::Cyan,
			FString::Printf(
				TEXT("[Run] R%d %s remain=%.0fs (%.0f/%.0f) total=%.0f mission=%s alive=%d xScale=%.1f"),
				CurrentRoundIndex, PhaseStr,
				(RoundDuration > 0.0f ? FMath::Max(0.0f, RoundDuration - ElapsedInRound) : 0.0f),
				ElapsedInRound, RoundDuration, TotalElapsed,
				MissionStr, AliveCount, TimeScale
			)
		);
	}
}

void UFPSRRunDirectorSubsystem::EndRound()
{
	DestroyActiveMission();

	AFPSRGameState* GS = GetGS();
	if (GS)
	{
		GS->BeginBreather();
		GS->SetRoundTimeRemaining(0.0f); // no round timer during the breather
	}

	UFPSREnemySpawnSubsystem* SpawnSub = GetSpawnSub();
	if (SpawnSub)
	{
		SpawnSub->SetTargetAliveCount(0);
		// Clear the remaining swarm so the breather is a genuine safe zone (Game.MD §2-2) — setting the
		// target to 0 alone leaves already-spawned enemies active (the spawn director never shrinks).
		SpawnSub->ReleaseAllEnemies();
	}

	UE_LOG(LogFPSR, Log, TEXT("[Run] Round %d ended, entering breather"), CurrentRoundIndex);
}

void UFPSRRunDirectorSubsystem::TryResumeFromBreather()
{
	AFPSRGameState* GS = GetGS();
	if (!GS)
	{
		return;
	}

	// Check if any player still has pending card picks
	for (APlayerState* PS : GS->PlayerArray)
	{
		if (AFPSRPlayerState* FPSRPS = Cast<AFPSRPlayerState>(PS))
		{
			if (FPSRPS->GetCardPicksPending() > 0)
			{
				// Someone is still picking; wait
				return;
			}
		}
	}

	// All picks consumed; resume to next round
	BeginRound(CurrentRoundIndex + 1);
}

void UFPSRRunDirectorSubsystem::SpawnRoundMission()
{
	bMissionSpawned = true;

	const FFPSRRoundDef Def = GetRoundDef(CurrentRoundIndex);

	if (!Def.Mission)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Run] Round %d mission slot is null"), CurrentRoundIndex);
		return;
	}

	TSubclassOf<AFPSRMissionActor> Cls = Def.Mission->MissionClass;
	if (!Cls)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Run] Mission %s has no MissionClass"), *Def.Mission->GetName());
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FTransform SpawnXform = SelectMissionSpawnTransform(Def.Mission);
	ActiveMission = World->SpawnActor<AFPSRMissionActor>(
		Cls,
		SpawnXform.GetLocation(),
		SpawnXform.Rotator(),
		SpawnParams
	);

	if (!ActiveMission)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Run] Failed to spawn mission actor from class %s"), *Cls->GetName());
		return;
	}

	ActiveMission->OnMissionEndedNative.AddUObject(this, &UFPSRRunDirectorSubsystem::OnMissionEnded);
	ActiveMission->ServerActivate(Def.Mission);

	UE_LOG(LogFPSR, Log, TEXT("[Run] Mission spawned: %s"), *Def.Mission->GetName());
}

void UFPSRRunDirectorSubsystem::OnMissionEnded(AFPSRMissionActor* Mission, bool bSuccess)
{
	if (bSuccess)
	{
		bMissionClearedThisRound = true;

		AFPSRGameState* GS = GetGS();
		if (GS)
		{
			GS->AddBankedMissionReward(1);
		}

		UE_LOG(LogFPSR, Log, TEXT("[Run] Mission cleared — reward banked"));
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
	AFPSRGameState* GS = GetGS();
	if (GS)
	{
		GS->SetRunPhase(ERunPhase::Boss);
		GS->SetRoundTimeRemaining(0.0f); // boss phase has no round timer
	}

	UFPSREnemySpawnSubsystem* SpawnSub = GetSpawnSub();
	if (SpawnSub)
	{
		SpawnSub->SetTargetAliveCount(0);
		SpawnSub->ReleaseAllEnemies();
	}

	UE_LOG(LogFPSR, Log, TEXT("[Run] Boss gate reached (boss actor is a P6 stub) — run timeline halted"));
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

	// Tag-matched points exist but all are within MinPlayerDistance — honor the distance intent as best we
	// can by choosing the farthest matching point, rather than spawning the objective on top of a player.
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

	// No tag-matched points at all (unmapped level / wrong tag) — fall back to the first player so the run still works.
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

void UFPSRRunDirectorSubsystem::DebugForceEndRound()
{
	if (!HasServerAuthority())
	{
		return;
	}

	AFPSRGameState* GS = GetGS();
	if (GS && GS->GetRunPhase() == ERunPhase::Combat)
	{
		EndRound();
	}
}

void UFPSRRunDirectorSubsystem::DebugTriggerMission()
{
	if (!HasServerAuthority())
	{
		return;
	}

	AFPSRGameState* GS = GetGS();
	if (GS && GS->GetRunPhase() == ERunPhase::Combat && !bMissionSpawned)
	{
		SpawnRoundMission();
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
	if (!HasServerAuthority())
	{
		return;
	}

	int32 NumRounds = GetNumRounds();
	for (int32 i = 0; i < NumRounds; ++i)
	{
		if (GetRoundDef(i).bBossRound)
		{
			BeginRound(i);
			return;
		}
	}

	// No boss round found; go directly to boss
	EnterBoss();
}

// ---- Console Commands (debug; excluded from shipping) ----

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorldAndArgs GFPSRNextRoundCmd(
	TEXT("FPSR.NextRound"),
	TEXT("Force end the current round and transition to breather."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}

		UFPSRRunDirectorSubsystem* Dir = World->GetSubsystem<UFPSRRunDirectorSubsystem>();
		if (!Dir)
		{
			return;
		}

		Dir->DebugForceEndRound();
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRMissionTriggerCmd(
	TEXT("FPSR.MissionTrigger"),
	TEXT("Spawn the mission for the current round if one is defined."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}

		UFPSRRunDirectorSubsystem* Dir = World->GetSubsystem<UFPSRRunDirectorSubsystem>();
		if (!Dir)
		{
			return;
		}

		Dir->DebugTriggerMission();
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRMissionClearCmd(
	TEXT("FPSR.MissionClear"),
	TEXT("Mark the active mission as completed."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}

		UFPSRRunDirectorSubsystem* Dir = World->GetSubsystem<UFPSRRunDirectorSubsystem>();
		if (!Dir)
		{
			return;
		}

		Dir->DebugClearMission();
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRSkipToBossCmd(
	TEXT("FPSR.SkipToBoss"),
	TEXT("Skip to the boss round."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}

		UFPSRRunDirectorSubsystem* Dir = World->GetSubsystem<UFPSRRunDirectorSubsystem>();
		if (!Dir)
		{
			return;
		}

		Dir->DebugSkipToBoss();
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRRunTimeScaleCmd(
	TEXT("FPSR.RunTimeScale"),
	TEXT("Set run time scale (1.0 = normal, 2.0 = 2x speed). Usage: FPSR.RunTimeScale [scale]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}

		UFPSRRunDirectorSubsystem* Dir = World->GetSubsystem<UFPSRRunDirectorSubsystem>();
		if (!Dir)
		{
			return;
		}

		float Scale = 1.0f;
		if (Args.Num() > 0)
		{
			Scale = FCString::Atof(*Args[0]);
		}

		Dir->SetTimeScale(Scale);
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRKillAllEnemiesCmd(
	TEXT("FPSR.KillAllEnemies"),
	TEXT("Release all active enemies back to the pool."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}

		UFPSREnemySpawnSubsystem* Sub = World->GetSubsystem<UFPSREnemySpawnSubsystem>();
		if (!Sub)
		{
			return;
		}

		Sub->ReleaseAllEnemies();
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRRunDebugCmd(
	TEXT("FPSR.RunDebug"),
	TEXT("Toggle run debug output. Usage: FPSR.RunDebug [0|1]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}

		UFPSRRunDirectorSubsystem* Dir = World->GetSubsystem<UFPSRRunDirectorSubsystem>();
		if (!Dir)
		{
			return;
		}

		bool bEnable = true;
		if (Args.Num() > 0)
		{
			bEnable = FCString::Atoi(*Args[0]) != 0;
		}

		Dir->SetRunDebug(bEnable);
	}));
#endif // !UE_BUILD_SHIPPING
