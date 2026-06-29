// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRGameMode.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerController.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRGameFlowSubsystem.h"
#include "Core/FPSRGameFlowSettings.h"
#include "Core/FPSRLogChannels.h"
#include "Core/FPSRFlowLog.h"
#include "Hero/FPSRCharacter.h"
#include "Card/FPSRCardSubsystem.h"
#include "Card/FPSRCardPoolDataAsset.h"
#include "Run/FPSRRunDirectorSubsystem.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"

namespace
{
	/** Seamless ServerTravel to a configured (soft) map as a listen server. Shared by the post-run lobby travel
	 *  and the debug travel commands. No-op (logged) if the map is unset/invalid. */
	void ServerTravelListenToMap(UWorld* World, const TSoftObjectPtr<UWorld>& Map)
	{
		if (!World)
		{
			return;
		}
		const UFPSRGameFlowSettings* Settings = GetDefault<UFPSRGameFlowSettings>();
		const FName PackageName = Settings ? Settings->GetLevelPackageName(Map) : NAME_None;
		if (PackageName == NAME_None)
		{
			UE_LOG(LogFPSR, Error, TEXT("[Flow] ServerTravel target map is null/invalid — travel aborted."));
			return;
		}
		World->ServerTravel(PackageName.ToString() + TEXT("?listen"));
	}
}

AFPSRGameMode::AFPSRGameMode()
{
	GameStateClass = AFPSRGameState::StaticClass();
	PlayerControllerClass = AFPSRPlayerController::StaticClass();
	PlayerStateClass = AFPSRPlayerState::StaticClass();
	DefaultPawnClass = AFPSRCharacter::StaticClass();

	// Seamless travel keeps connections/PlayerControllers/PlayerStates alive across gameplay<->lobby travel
	// (P7 §3-4); the empty TransitionMap is configured in DefaultEngine.ini.
	bUseSeamlessTravel = true;
}

void AFPSRGameMode::BeginPlay()
{
	Super::BeginPlay();
	FPSRFlowLog::Event(this, TEXT("RUN-GM"), TEXT("Run BeginPlay (gameplay map start)"));

	if (UWorld* World = GetWorld())
	{
		if (UFPSRCardSubsystem* CardSubsystem = World->GetSubsystem<UFPSRCardSubsystem>())
		{
			CardSubsystem->SetActivePool(CardPool);
		}

		// Configure the swarm enemy class (designer BP) before the run starts spawning.
		if (UFPSREnemySpawnSubsystem* SpawnSub = World->GetSubsystem<UFPSREnemySpawnSubsystem>())
		{
			SpawnSub->SetEnemyClass(EnemyClass);
		}

		// Start the run: hand the schedule to the director (server authority) and kick off round 0.
		if (UFPSRRunDirectorSubsystem* RunDirector = World->GetSubsystem<UFPSRRunDirectorSubsystem>())
		{
			RunDirector->SetActiveSchedule(RunSchedule);
			RunDirector->StartRun();
		}
	}

	// Close the loop: when the run ends (EndRunFreeze, victory or defeat) travel back to the lobby. Subscribing
	// here (not editing EndRun's body) keeps this caller independent of the victory caller U3 adds (P7 §3-5).
	if (AFPSRGameState* GS = GetGameState<AFPSRGameState>())
	{
		GS->OnRunEnded.AddDynamic(this, &AFPSRGameMode::HandlePostRunTravel);
	}
}

void AFPSRGameMode::HandlePostRunTravel()
{
	if (!HasAuthority())
	{
		return;
	}

	// Let the result screen sit for a readable beat, then seamless-travel everyone back to the lobby hub.
	if (UWorld* World = GetWorld())
	{
		if (PostRunTravelDelay > 0.0f)
		{
			World->GetTimerManager().SetTimer(PostRunTravelTimer, this, &AFPSRGameMode::TravelToLobby, PostRunTravelDelay, false);
		}
		else
		{
			TravelToLobby();
		}
	}
}

void AFPSRGameMode::TravelToLobby()
{
	if (!HasAuthority())
	{
		return;
	}
	FPSRFlowLog::Event(this, TEXT("RUN-GM"), TEXT("ServerTravel -> lobby (post-run)"));
	const UFPSRGameFlowSettings* Settings = GetDefault<UFPSRGameFlowSettings>();
	ServerTravelListenToMap(GetWorld(), Settings ? Settings->LobbyMap : nullptr);
}

void AFPSRGameMode::RequestReturnToLobby()
{
	if (!HasAuthority())
	{
		return;
	}
	// Only valid once the run has ended (the result screen's Return). Reject a mid-run call so a client can't
	// travel the whole party out of a live run (W1 P2-3). Debug travel uses the lower-level FPSR.TravelLobby.
	if (!bRunEnded)
	{
		return;
	}
	// Cancel the pending auto-travel so the manual Return and the timer don't both ServerTravel.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PostRunTravelTimer);
	}
	TravelToLobby();
}

void AFPSRGameMode::EndRun(EFPSRRunOutcome Outcome)
{
	if (!HasAuthority() || bRunEnded)
	{
		return;
	}

	bRunEnded = true;
	FPSRFlowLog::Event(this, TEXT("RUN-GM"), FString::Printf(TEXT("EndRun outcome=%s"), Outcome == EFPSRRunOutcome::Victory ? TEXT("Victory") : (Outcome == EFPSRRunOutcome::Defeat ? TEXT("Defeat") : TEXT("None"))));

	// Notify each player with the run outcome (they'll show the result widget locally).
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AFPSRPlayerController* PC = Cast<AFPSRPlayerController>(It->Get()))
		{
			PC->ClientShowRunResult(Outcome);
		}
	}

	// Freeze the run behind the result screen — reuse the §2-2 global freeze (gameplay-state gate, NOT
	// TimeDilation): enemies, projectiles, abilities and players stop while the world stays rendered. The
	// end latch inside EndRunFreeze keeps it frozen so a late card-selection completion can't resume it.
	if (AFPSRGameState* GS = GetGameState<AFPSRGameState>())
	{
		GS->EndRunFreeze();
		// Clear the HUD mission progress so a partial capture/hold bar doesn't strand behind the result screen (B1).
		// Victory already clears it via EnterBoss -> DestroyActiveMission; this covers the Defeat path (which freezes
		// without destroying the active mission) and is a harmless no-op when no mission is active.
		GS->SetMissionProgress(0.0f);
	}
}

int32 AFPSRGameMode::GetLivingPlayerCount() const
{
	int32 Living = 0;
	if (const AGameStateBase* GS = GetGameState<AGameStateBase>())
	{
		for (APlayerState* PS : GS->PlayerArray)
		{
			const AFPSRPlayerState* FPS = Cast<AFPSRPlayerState>(PS);
			if (FPS && !FPS->IsOnlyASpectator() && FPS->IsAlive())
			{
				++Living;
			}
		}
	}
	return Living;
}

bool AFPSRGameMode::AreAllPlayersDead() const
{
	int32 Participants = 0;
	if (const AGameStateBase* GS = GetGameState<AGameStateBase>())
	{
		for (APlayerState* PS : GS->PlayerArray)
		{
			const AFPSRPlayerState* FPS = Cast<AFPSRPlayerState>(PS);
			if (FPS && !FPS->IsOnlyASpectator())
			{
				++Participants;
			}
		}
	}
	// At least one participant AND nobody alive — avoids reading a transient empty PlayerArray as a wipe.
	return Participants > 0 && GetLivingPlayerCount() == 0;
}

void AFPSRGameMode::NotifyPlayerDefeated()
{
	if (!HasAuthority())
	{
		return;
	}
	if (AreAllPlayersDead())
	{
		EndRun(EFPSRRunOutcome::Defeat);
	}
}

void AFPSRGameMode::NotifyBossDefeated()
{
	if (!HasAuthority())
	{
		return;
	}
	EndRun(EFPSRRunOutcome::Victory);
}

#if !UE_BUILD_SHIPPING

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GCmd_EndRun(
		TEXT("FPSR.EndRun"),
		TEXT("End the run with a specified outcome (debug, authority/host only). Usage: FPSR.EndRun [victory|defeat]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AFPSRGameMode* GM = World ? World->GetAuthGameMode<AFPSRGameMode>() : nullptr;
			if (!GM)
			{
				return;
			}

			EFPSRRunOutcome Outcome = EFPSRRunOutcome::Victory;
			if (Args.Num() > 0 && Args[0].Equals(TEXT("defeat"), ESearchCase::IgnoreCase))
			{
				Outcome = EFPSRRunOutcome::Defeat;
			}

			GM->EndRun(Outcome);
		}));

	FAutoConsoleCommandWithWorld GCmd_TravelLobby(
		TEXT("FPSR.TravelLobby"),
		TEXT("Seamless ServerTravel to the lobby map as listen server (debug, host only). Pre-tests the travel skeleton without a session."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (!World || (!World->IsNetMode(NM_ListenServer) && !World->IsNetMode(NM_Standalone)))
			{
				return;
			}
			const UFPSRGameFlowSettings* Settings = GetDefault<UFPSRGameFlowSettings>();
			ServerTravelListenToMap(World, Settings ? Settings->LobbyMap : nullptr);
		}));

	FAutoConsoleCommandWithWorld GCmd_TravelGame(
		TEXT("FPSR.TravelGame"),
		TEXT("Seamless ServerTravel to the gameplay (run) map as listen server (debug, host only). Pre-tests the travel skeleton without a session."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (!World || (!World->IsNetMode(NM_ListenServer) && !World->IsNetMode(NM_Standalone)))
			{
				return;
			}
			const UFPSRGameFlowSettings* Settings = GetDefault<UFPSRGameFlowSettings>();
			ServerTravelListenToMap(World, Settings ? Settings->RunMap : nullptr);
		}));

	FAutoConsoleCommandWithWorld GCmd_ReturnToMenu(
		TEXT("FPSR.ReturnToMenu"),
		TEXT("Return to the main menu (debug, host only)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (!World || (!World->IsNetMode(NM_ListenServer) && !World->IsNetMode(NM_Standalone)))
			{
				return;
			}

			if (UGameInstance* GI = World->GetGameInstance())
			{
				if (UFPSRGameFlowSubsystem* Flow = GI->GetSubsystem<UFPSRGameFlowSubsystem>())
				{
					Flow->ReturnToMenu(EFPSRRunOutcome::None);
				}
			}
		}));
}

#endif // !UE_BUILD_SHIPPING

