// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRLobbyGameMode.h"
#include "Core/FPSRLobbyPlayerController.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRGameFlowSettings.h"
#include "Core/FPSRLogChannels.h"
#include "GameFramework/SpectatorPawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "TimerManager.h"

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
	// A new (un-ready) participant joined — re-evaluate the start gate (cancels any armed countdown).
	NotifyReadyChanged();
}

void AFPSRLobbyGameMode::Logout(AController* Exiting)
{
	Super::Logout(Exiting);
	// A participant left — re-evaluate so the remaining party can still start if they were all ready (the leaver's
	// PlayerState is removed from the array by the time NotifyReadyChanged iterates on the next tick / here).
	NotifyReadyChanged();
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

bool AFPSRLobbyGameMode::AreAllParticipantsReady(int32& OutParticipants) const
{
	OutParticipants = 0;
	const AGameStateBase* GS = GetGameState<AGameStateBase>();
	if (!GS)
	{
		return false;
	}

	bool bAllReady = true;
	for (const APlayerState* Base : GS->PlayerArray)
	{
		const AFPSRPlayerState* PS = Cast<AFPSRPlayerState>(Base);
		// Only count live participants (skip spectators / null). No bots in the lobby.
		if (!PS || PS->IsOnlyASpectator())
		{
			continue;
		}
		++OutParticipants;
		if (!PS->IsReady())
		{
			bAllReady = false;
		}
	}

	return OutParticipants > 0 && bAllReady;
}

void AFPSRLobbyGameMode::NotifyReadyChanged()
{
	if (!HasAuthority())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	int32 Participants = 0;
	if (AreAllParticipantsReady(Participants))
	{
		if (!World->GetTimerManager().IsTimerActive(ReadyStartTimer))
		{
			UE_LOG(LogFPSR, Log, TEXT("[Lobby] All %d participant(s) ready — starting run in %.1fs."), Participants, ReadyStartCountdown);
			World->GetTimerManager().SetTimer(ReadyStartTimer, this, &AFPSRLobbyGameMode::StartRunNow, FMath::Max(0.01f, ReadyStartCountdown), false);
		}
	}
	else if (World->GetTimerManager().IsTimerActive(ReadyStartTimer))
	{
		UE_LOG(LogFPSR, Log, TEXT("[Lobby] Ready countdown cancelled (a participant is no longer ready or left)."));
		World->GetTimerManager().ClearTimer(ReadyStartTimer);
	}
}

float AFPSRLobbyGameMode::GetReadyCountdownRemaining() const
{
	const UWorld* World = GetWorld();
	if (!World || !World->GetTimerManager().IsTimerActive(ReadyStartTimer))
	{
		return 0.0f;
	}
	return FMath::Max(0.0f, World->GetTimerManager().GetTimerRemaining(ReadyStartTimer));
}

void AFPSRLobbyGameMode::StartRunNow()
{
	if (!HasAuthority())
	{
		return;
	}

	const UFPSRGameFlowSettings* Settings = GetDefault<UFPSRGameFlowSettings>();
	const FName RunPackage = Settings ? Settings->GetLevelPackageName(Settings->RunMap) : NAME_None;
	if (UWorld* World = GetWorld(); World && RunPackage != NAME_None)
	{
		UE_LOG(LogFPSR, Log, TEXT("[Lobby] Starting run — traveling to %s"), *RunPackage.ToString());
		World->ServerTravel(RunPackage.ToString() + TEXT("?listen"));
	}
	else
	{
		UE_LOG(LogFPSR, Error, TEXT("[Lobby] StartRunNow: RunMap is null/invalid."));
	}
}
