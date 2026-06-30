// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRLobbyGameMode.h"
#include "Core/FPSRLobbyPlayerController.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRGameFlowSettings.h"
#include "Core/FPSRLogChannels.h"
#include "Core/FPSRFlowLog.h"
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
	// Assign the podium seat BEFORE Super spawns the pawn — Super::PostLogin restarts the player, which runs our
	// ChoosePlayerStart override; that override reads the seat, so it must already be set (B3b).
	AssignSeat(NewPlayer);
	Super::PostLogin(NewPlayer);
	FPSRFlowLog::Event(this, TEXT("LOBBY-GM"), FString::Printf(TEXT("PostLogin: %s"), NewPlayer ? *NewPlayer->GetName() : TEXT("null")));
	// Fresh join: default state already, but reset is idempotent and covers a non-seamless return path.
	ResetPlayerRunState(NewPlayer);
	// A new (un-ready) participant joined — re-evaluate the start gate (cancels any armed countdown).
	NotifyReadyChanged();
}

void AFPSRLobbyGameMode::Logout(AController* Exiting)
{
	Super::Logout(Exiting);
	FPSRFlowLog::Event(this, TEXT("LOBBY-GM"), FString::Printf(TEXT("Logout: %s"), Exiting ? *Exiting->GetName() : TEXT("null")));
	// A participant left — re-evaluate the start gate, but DEFER to next tick: Super::Logout does not necessarily
	// remove the exiting PlayerState from GameState->PlayerArray before this returns, so an immediate re-eval could
	// still count the leaver (countdown stuck for an empty lobby, or failing to start when the rest are ready). (merge-gate P2)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &AFPSRLobbyGameMode::NotifyReadyChanged));
	}
}

void AFPSRLobbyGameMode::HandleSeamlessTravelPlayer(AController*& C)
{
	// Seat before Super restarts/places the returning pawn (same reason as PostLogin) so post-run returners also
	// seat by index rather than falling back to the engine random placement (B3b).
	AssignSeat(C);
	Super::HandleSeamlessTravelPlayer(C);
	// Returning from a run via seamless travel: the PlayerState carried its dirty run state — reset it here so the
	// next run starts fresh (XP/PartyLevel reset naturally with the fresh GameState; this clears the PS-resident
	// bits: life state, pending picks, AllWeapons mods, loadout pick).
	ResetPlayerRunState(C);
}

void AFPSRLobbyGameMode::AssignSeat(AController* C) const
{
	if (!HasAuthority())
	{
		return;
	}
	const APlayerController* PC = Cast<APlayerController>(C);
	AFPSRPlayerState* PS = PC ? PC->GetPlayerState<AFPSRPlayerState>() : nullptr;
	const AGameStateBase* GS = GetGameState<AGameStateBase>();
	if (!PS || !GS)
	{
		return;
	}

	// Lowest free seat = smallest index in [0,NumPodiumSlots) not held by another present participant. The server-side
	// LobbySeatIndex is set synchronously below, so a second player's PostLogin in the same frame already sees this
	// player's seat (no listen-server simultaneous-join race), and a departed player's PlayerState has left PlayerArray
	// so its seat is free again. N<=4 so the nested scan is trivial.
	int32 FreeSeat = INDEX_NONE;
	for (int32 Candidate = 0; Candidate < NumPodiumSlots; ++Candidate)
	{
		bool bTaken = false;
		for (const APlayerState* Base : GS->PlayerArray)
		{
			const AFPSRPlayerState* Other = Cast<AFPSRPlayerState>(Base);
			if (Other && Other != PS && Other->GetLobbySeatIndex() == Candidate)
			{
				bTaken = true;
				break;
			}
		}
		if (!bTaken)
		{
			FreeSeat = Candidate;
			break;
		}
	}
	// More players than podiums (shouldn't happen at the 4-player cap) → leave INDEX_NONE so ChoosePlayerStart falls
	// back to the engine default instead of colliding on a wrapped seat.
	PS->SetLobbySeatIndex(FreeSeat);
}

AActor* AFPSRLobbyGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	// Deterministic per-seat placement (B3b). Route to the PlayerStart tagged "Podium{Seat}" — FindPlayerStart's
	// IncomingName branch matches APlayerStart::PlayerStartTag, the canonical engine tag-selection path. Fall back to
	// the engine default if the seat is unassigned or the tagged start is missing (a misconfigured lobby still spawns
	// players, just possibly overlapping as before — content gate is the Podium0..N-1 tags).
	if (const APlayerController* PC = Cast<APlayerController>(Player))
	{
		if (const AFPSRPlayerState* PS = PC->GetPlayerState<AFPSRPlayerState>())
		{
			const int32 Seat = PS->GetLobbySeatIndex();
			if (Seat >= 0)
			{
				if (AActor* Start = FindPlayerStart(Player, FString::Printf(TEXT("Podium%d"), Seat)))
				{
					return Start;
				}
			}
		}
	}
	return Super::ChoosePlayerStart_Implementation(Player);
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

	AFPSRGameState* GS = GetGameState<AFPSRGameState>();

	int32 Participants = 0;
	if (AreAllParticipantsReady(Participants))
	{
		if (!World->GetTimerManager().IsTimerActive(ReadyStartTimer))
		{
			UE_LOG(LogFPSR, Log, TEXT("[Lobby] All %d participant(s) ready — starting run in %.1fs."), Participants, ReadyStartCountdown);
			FPSRFlowLog::Event(this, TEXT("LOBBY-GM"), FString::Printf(TEXT("All %d ready - run countdown %.1fs"), Participants, ReadyStartCountdown));
			World->GetTimerManager().SetTimer(ReadyStartTimer, this, &AFPSRLobbyGameMode::StartRunNow, FMath::Max(0.01f, ReadyStartCountdown), false);
			// Publish the end stamp so every lobby client (not just the host) can show the countdown.
			if (GS)
			{
				GS->SetLobbyCountdownEndTime(GS->GetServerWorldTimeSeconds() + ReadyStartCountdown);
			}
		}
	}
	else if (World->GetTimerManager().IsTimerActive(ReadyStartTimer))
	{
		UE_LOG(LogFPSR, Log, TEXT("[Lobby] Ready countdown cancelled (a participant is no longer ready or left)."));
		FPSRFlowLog::Event(this, TEXT("LOBBY-GM"), TEXT("Run countdown cancelled"));
		World->GetTimerManager().ClearTimer(ReadyStartTimer);
		if (GS)
		{
			GS->SetLobbyCountdownEndTime(0.0f);
		}
	}
}

float AFPSRLobbyGameMode::GetReadyCountdownRemaining() const
{
	// Delegate to the replicated GameState value so host and clients agree (the timer itself is host-only).
	const AFPSRGameState* GS = GetGameState<AFPSRGameState>();
	return GS ? GS->GetLobbyReadyCountdownRemaining() : 0.0f;
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
		FPSRFlowLog::Event(this, TEXT("LOBBY-GM"), FString::Printf(TEXT("StartRun -> ServerTravel %s"), *RunPackage.ToString()));
		World->ServerTravel(RunPackage.ToString() + TEXT("?listen"));
	}
	else
	{
		UE_LOG(LogFPSR, Error, TEXT("[Lobby] StartRunNow: RunMap is null/invalid."));
	}
}
