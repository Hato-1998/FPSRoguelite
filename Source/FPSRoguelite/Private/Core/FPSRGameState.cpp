// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRPlayerController.h"
#include "Run/FPSRRunScheduleDataAsset.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Core/FPSRLogChannels.h"

AFPSRGameState::AFPSRGameState()
{
}

void AFPSRGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, SharedXP, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, PartyLevel, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, RunPhase, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, bRunPaused, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, bVisionRestricted, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, RunClockSeconds, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, bFriendlyFireEnabled, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, LobbyCountdownEndServerTime, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, ActiveBoss, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, ActiveMissionData, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, MissionProgress, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, RunScheduleAsset, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRGameState, TopologyGeneration, Params);
}

void AFPSRGameState::SetActiveBoss(AFPSRBossBase* InBoss)
{
	if (!HasAuthority() || ActiveBoss == InBoss)
	{
		return;
	}
	ActiveBoss = InBoss;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, ActiveBoss, this);
	// The listen-server host gets no OnRep — broadcast directly so the host's HUD boss bar reacts too (same pattern
	// as the ready/run-state setters).
	OnActiveBossChanged.Broadcast(ActiveBoss);
}

void AFPSRGameState::OnRep_ActiveBoss()
{
	OnActiveBossChanged.Broadcast(ActiveBoss);
}

void AFPSRGameState::SetActiveMission(UFPSRMissionDataAsset* InMission)
{
	if (!HasAuthority() || ActiveMissionData == InMission)
	{
		return;
	}
	ActiveMissionData = InMission;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, ActiveMissionData, this);
	OnActiveMissionChanged.Broadcast(ActiveMissionData); // host gets no OnRep — broadcast directly (B10 banner)
}

void AFPSRGameState::OnRep_ActiveMission()
{
	OnActiveMissionChanged.Broadcast(ActiveMissionData);
}

void AFPSRGameState::SetTopologyGeneration(int32 NewGen)
{
	if (!HasAuthority() || TopologyGeneration == NewGen)
	{
		return;
	}
	TopologyGeneration = NewGen;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, TopologyGeneration, this);
	// No host broadcast: the listen-server host IS the server (local authority), so its own controller is instantly
	// ack-satisfied (PlayerState::HasAckedJoinTopology short-circuits) and needs no OnRep. Only remote clients ack.
}

void AFPSRGameState::OnRep_TopologyGeneration()
{
	// Client: the server's topology generation changed (or arrived on late-join initial replication). Re-ack from every
	// LOCAL controller so the server records this client's confirmation of the current topology. Iterate + IsLocalController
	// (NOT GetFirstPlayerController — that can return a non-local proxy PC in some net configs / split-screen).
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (AFPSRPlayerController* PC = Cast<AFPSRPlayerController>(It->Get()))
		{
			if (PC->IsLocalController())
			{
				PC->ServerAckTopology(TopologyGeneration);
			}
		}
	}
}

void AFPSRGameState::SetMissionProgress(float NewProgress)
{
	if (!HasAuthority())
	{
		return;
	}
	NewProgress = FMath::Clamp(NewProgress, 0.0f, 1.0f);
	if (MissionProgress == NewProgress)
	{
		return;
	}
	// Dead-band to avoid per-director-tick churn, but always honor the exact endpoints (0 on clear, 1 on complete).
	const bool bEndpoint = (NewProgress == 0.0f || NewProgress == 1.0f);
	if (!bEndpoint && FMath::IsNearlyEqual(MissionProgress, NewProgress, 0.01f))
	{
		return;
	}
	MissionProgress = NewProgress;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, MissionProgress, this);
	OnRunStateChanged.Broadcast(); // host has no OnRep — refresh host HUD directly (B1)
}

void AFPSRGameState::SetRunSchedule(UFPSRRunScheduleDataAsset* InSchedule)
{
	if (!HasAuthority() || RunScheduleAsset == InSchedule)
	{
		return;
	}
	RunScheduleAsset = InSchedule;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, RunScheduleAsset, this);
	OnRunStateChanged.Broadcast(); // host has no OnRep — refresh host HUD directly (B2)
}

float AFPSRGameState::GetRunTotalDuration() const
{
	// The boss endpoint from the authored schedule; a default when no schedule is set (test/fallback runs) matching
	// the run director's FallbackBossTime so the bar still reads sensibly.
	return RunScheduleAsset ? RunScheduleAsset->BossTime : 300.0f;
}

int32 AFPSRGameState::GetRequiredXP(int32 Level) const
{
	return XPBaseRequired + FMath::Max(0, Level - 1) * XPPerLevel;
}

void AFPSRGameState::AddSharedXP(int32 Amount)
{
	if (!HasAuthority() || Amount <= 0)
	{
		return;
	}

	SharedXP += Amount;
	int32 LevelsGained = 0;
	const int32 PrevLevel = PartyLevel;
	while (SharedXP >= GetRequiredXP(PartyLevel))
	{
		SharedXP -= GetRequiredXP(PartyLevel);
		++PartyLevel;
		++LevelsGained;
	}

	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, SharedXP, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, PartyLevel, this);

	// Each level grants every connected player one pending card pick.
	if (LevelsGained > 0)
	{
		for (APlayerState* PS : PlayerArray)
		{
			AFPSRPlayerState* FPS = Cast<AFPSRPlayerState>(PS);
			// Non-alive players (DBNO or Dead) don't participate in level-up selection — grant them no card /
			// weapon-unlock picks (server-authoritative LifeState). Also keeps them out of the RefreshPauseState
			// freeze gate below (A4: a downed/dead teammate must not block the card-select freeze).
			if (!FPS || !FPS->IsAlive())
			{
				continue;
			}
			for (int32 i = 0; i < LevelsGained; ++i)
			{
				FPS->AddCardPick();
			}
			// Weapon-unlock milestone picks: one per milestone level crossed this XP gain (Game.MD §2-3-4).
			for (int32 L = PrevLevel + 1; L <= PartyLevel; ++L)
			{
				if (WeaponUnlockMilestones.Contains(L))
				{
					FPS->AddWeaponUnlockPick();
				}
			}
		}

		// Level-up freezes the run immediately and presents cards to everyone (Game.MD §2-2).
		RefreshPauseState();
	}

	OnRunStateChanged.Broadcast();
}

void AFPSRGameState::SetRunPhase(ERunPhase NewPhase)
{
	if (!HasAuthority() || RunPhase == NewPhase)
	{
		return;
	}
	RunPhase = NewPhase;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, RunPhase, this);
	OnRunStateChanged.Broadcast();
}

void AFPSRGameState::SetRunPaused(bool bPaused)
{
	if (!HasAuthority() || bRunPaused == bPaused)
	{
		return;
	}
	bRunPaused = bPaused;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, bRunPaused, this);
	OnRunStateChanged.Broadcast();

	UE_LOG(LogFPSR, Log, TEXT("[Run] %s"), bPaused ? TEXT("FREEZE (card selection)") : TEXT("RESUME"));
}

void AFPSRGameState::EndRunFreeze()
{
	if (!HasAuthority() || bRunEnded)
	{
		return;
	}
	// Latch first so the SetRunPaused below (and any in-flight RefreshPauseState) can't be undone by a card
	// selection that resolves after the run has ended — the world stays frozen behind the result screen.
	bRunEnded = true;
	SetRunPaused(true);
	UE_LOG(LogFPSR, Log, TEXT("[Run] END — freeze pinned (result screen)."));

	// Decoupled run-end signal (fires once — the bRunEnded latch guards re-entry). The GameMode subscribes to
	// drive the post-run travel back to the lobby (P7 §3-5) without touching EndRun's body.
	OnRunEnded.Broadcast();
}

void AFPSRGameState::SetVisionRestricted(bool bRestricted)
{
	if (!HasAuthority() || bVisionRestricted == bRestricted)
	{
		return;
	}
	bVisionRestricted = bRestricted;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, bVisionRestricted, this);
	OnRunStateChanged.Broadcast();

	UE_LOG(LogFPSR, Log, TEXT("[Run] Vision %s"), bRestricted ? TEXT("RESTRICTED (mission)") : TEXT("RESTORED"));
}

void AFPSRGameState::SetFriendlyFireEnabled(bool bEnabled)
{
	if (!HasAuthority() || bFriendlyFireEnabled == bEnabled)
	{
		return;
	}
	bFriendlyFireEnabled = bEnabled;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, bFriendlyFireEnabled, this);
	OnRunStateChanged.Broadcast();

	UE_LOG(LogFPSR, Log, TEXT("[Run] Friendly fire %s"), bEnabled ? TEXT("ON") : TEXT("OFF"));
}

void AFPSRGameState::RefreshPauseState()
{
	if (!HasAuthority())
	{
		return;
	}

	// Run ended: the freeze is pinned on (EndRunFreeze). Never recompute from pending picks — a card selection
	// completing after EndRun must not resume gameplay behind the result screen.
	if (bRunEnded)
	{
		return;
	}

	// Paused iff any connected player still has an outstanding selection. Present the next needed offer to
	// each player that has picks but no active offer (covers newly granted picks).
	bool bAnyPending = false;
	for (APlayerState* PS : PlayerArray)
	{
		AFPSRPlayerState* FPS = Cast<AFPSRPlayerState>(PS);
		// Dead players are excluded from card selection: no offer is presented and they never count toward the
		// freeze, so a dead teammate can't soft-lock the resume for everyone (mirrors the AddSharedXP grant gate).
		if (!FPS || !FPS->IsAlive())
		{
			continue;
		}
		AFPSRPlayerController* PC = Cast<AFPSRPlayerController>(FPS->GetOwningController());
		if (!PC)
		{
			continue;
		}
		PC->PresentNextOfferIfNeeded();
		if (PC->HasPendingSelection())
		{
			bAnyPending = true;
		}
	}

	SetRunPaused(bAnyPending);
}

void AFPSRGameState::SetRunClockSeconds(float Seconds)
{
	if (!HasAuthority())
	{
		return;
	}
	// Low-frequency UI mirror: only dirty on a meaningful change to avoid per-tick replication churn.
	if (FMath::IsNearlyEqual(RunClockSeconds, Seconds, 0.25f))
	{
		return;
	}
	RunClockSeconds = Seconds;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, RunClockSeconds, this);

	// Authority gets no OnRep_RunState, so notify HUD widgets locally on the host (clients refresh via
	// replication). Authority-only path + the 0.25s dead-band above keep this to a low UI cadence.
	OnRunStateChanged.Broadcast();
}

void AFPSRGameState::SetLobbyCountdownEndTime(float ServerTimeSeconds)
{
	if (!HasAuthority() || LobbyCountdownEndServerTime == ServerTimeSeconds)
	{
		return;
	}
	LobbyCountdownEndServerTime = ServerTimeSeconds;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRGameState, LobbyCountdownEndServerTime, this);
	// Authority gets no OnRep — notify host UI directly (clients refresh via replication).
	OnRunStateChanged.Broadcast();
}

float AFPSRGameState::GetLobbyReadyCountdownRemaining() const
{
	if (LobbyCountdownEndServerTime <= 0.0f)
	{
		return 0.0f;
	}
	// Synced server clock on host and clients (AGameStateBase::GetServerWorldTimeSeconds) — no GameMode access needed.
	return FMath::Max(0.0f, LobbyCountdownEndServerTime - GetServerWorldTimeSeconds());
}

void AFPSRGameState::OnRep_RunState()
{
	OnRunStateChanged.Broadcast();
}

#if !UE_BUILD_SHIPPING

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GCmd_AddXP(
		TEXT("FPSR.AddXP"),
		TEXT("Add shared XP to the run (debug). Usage: FPSR.AddXP [amount]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			AFPSRGameState* GS = World->GetGameState<AFPSRGameState>();
			if (!GS)
			{
				return;
			}
			int32 Amount = 50;
			if (Args.Num() > 0)
			{
				Amount = FMath::Max(1, FCString::Atoi(*Args[0]));
			}
			GS->AddSharedXP(Amount);
		}));

	FAutoConsoleCommandWithWorldAndArgs GCmd_Pause(
		TEXT("FPSR.Pause"),
		TEXT("Toggle the global run freeze (debug). Usage: FPSR.Pause [0|1]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			AFPSRGameState* GS = World->GetGameState<AFPSRGameState>();
			if (!GS)
			{
				return;
			}
			bool bPaused = !GS->IsRunPaused();
			if (Args.Num() > 0)
			{
				bPaused = FCString::Atoi(*Args[0]) != 0;
			}
			GS->SetRunPaused(bPaused);
		}));

	FAutoConsoleCommandWithWorldAndArgs GCmd_SetFriendlyFire(
		TEXT("FPSR.SetFriendlyFire"),
		TEXT("Toggle friendly fire for the run (debug). Usage: FPSR.SetFriendlyFire [0|1]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			AFPSRGameState* GS = World->GetGameState<AFPSRGameState>();
			if (!GS)
			{
				return;
			}
			bool bEnabled = !GS->IsFriendlyFireEnabled();
			if (Args.Num() > 0)
			{
				bEnabled = FCString::Atoi(*Args[0]) != 0;
			}
			GS->SetFriendlyFireEnabled(bEnabled);
		}));
}

#endif // !UE_BUILD_SHIPPING
