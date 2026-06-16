// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRLobbyGameMode.h"
#include "Core/FPSRLobbyPlayerController.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRGameFlowSettings.h"
#include "Core/FPSRLogChannels.h"
#include "GameFramework/SpectatorPawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

AFPSRLobbyGameMode::AFPSRLobbyGameMode()
{
	// Reuse the project GameState/PlayerState so the lobby shares PlayerArray + the loadout/run-state that carries
	// across seamless travel. The lobby PlayerController is UI-only (no gameplay HUD / opening-seed card flow).
	GameStateClass = AFPSRGameState::StaticClass();
	PlayerControllerClass = AFPSRLobbyPlayerController::StaticClass();
	PlayerStateClass = AFPSRPlayerState::StaticClass();
	DefaultPawnClass = ASpectatorPawn::StaticClass();

	// Seamless travel into the gameplay map keeps connections/PCs/PlayerStates alive (P7 §3-4).
	bUseSeamlessTravel = true;
}

void AFPSRLobbyGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	// Fresh join: default state already, but reset is idempotent and covers a non-seamless return path.
	ResetPlayerRunState(NewPlayer);
}

void AFPSRLobbyGameMode::HandleSeamlessTravelPlayer(AController*& C)
{
	Super::HandleSeamlessTravelPlayer(C);
	// Returning from a run via seamless travel: the PlayerState carried its dirty run state — reset it here so the
	// next run starts fresh (XP/PartyLevel reset naturally with the fresh GameState; this clears the PS-resident
	// bits: life state, pending picks, AllWeapons mods, loadout pick).
	ResetPlayerRunState(C);
}

void AFPSRLobbyGameMode::ResetPlayerRunState(AController* C) const
{
	if (const APlayerController* PC = Cast<APlayerController>(C))
	{
		if (AFPSRPlayerState* PS = PC->GetPlayerState<AFPSRPlayerState>())
		{
			PS->ResetRunState();
		}
	}
}

void AFPSRLobbyGameMode::RequestStartRun(APlayerController* Requester)
{
	if (!HasAuthority())
	{
		return;
	}

	// Host-only gate: on a listen server only the host's controller is local to the server. This rejects a client
	// trying to start the run (their controller is a remote proxy on the server).
	if (!Requester || !Requester->IsLocalController())
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Lobby] RequestStartRun rejected — only the host may start the run."));
		return;
	}

	const UFPSRGameFlowSettings* Settings = GetDefault<UFPSRGameFlowSettings>();
	const FName RunPackage = Settings ? Settings->GetLevelPackageName(Settings->RunMap) : NAME_None;
	if (UWorld* World = GetWorld(); World && RunPackage != NAME_None)
	{
		UE_LOG(LogFPSR, Log, TEXT("[Lobby] Host starting run — traveling to %s"), *RunPackage.ToString());
		World->ServerTravel(RunPackage.ToString() + TEXT("?listen"));
	}
	else
	{
		UE_LOG(LogFPSR, Error, TEXT("[Lobby] RequestStartRun: RunMap is null/invalid."));
	}
}
