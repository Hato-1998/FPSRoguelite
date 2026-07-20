// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRLobbyPlayerController.h"
#include "Core/FPSRLobbyGameMode.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRGameFlowSettings.h"
#include "Core/FPSRLogChannels.h"
#include "UI/FPSRPrimaryGameLayout.h"
#include "UI/FPSRUITags.h"
#include "Weapon/FPSRLoadoutPoolDataAsset.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "CommonActivatableWidget.h"
#include "GameplayTagContainer.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "EngineUtils.h"
#include "TimerManager.h"

void AFPSRLobbyPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	// Lock the local view to the room-overview camera before/independent of the UI setup (a listen-server client's
	// pawn possession would otherwise leave the view inside the podium meshes — the reported joiner camera bug).
	ApplyLobbyViewTarget();

	if (!PrimaryLayoutClass || !LobbyWidgetClass)
	{
		UE_LOG(LogFPSR, Error, TEXT("[Lobby] PrimaryLayoutClass / LobbyWidgetClass not assigned on the lobby PC."));
		return;
	}

	PrimaryLayout = CreateWidget<UFPSRPrimaryGameLayout>(this, PrimaryLayoutClass);
	if (!PrimaryLayout)
	{
		UE_LOG(LogFPSR, Error, TEXT("[Lobby] Failed to create PrimaryGameLayout"));
		return;
	}
	PrimaryLayout->AddToViewport();

	// Lobby UI lives on the Menu layer (menu-like, no gameplay HUD).
	if (!PrimaryLayout->PushWidgetToLayer(FPSRUITags::TAG_UI_Layer_Menu.GetTag(), LobbyWidgetClass))
	{
		UE_LOG(LogFPSR, Error, TEXT("[Lobby] Failed to push LobbyWidget to Menu layer"));
	}
}

void AFPSRLobbyPlayerController::ApplyLobbyViewTarget()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Prefer a designer-tagged camera ("LobbyCamera"); otherwise fall back to the first CameraActor in the level.
	ACameraActor* LobbyCam = nullptr;
	for (TActorIterator<ACameraActor> It(World); It; ++It)
	{
		ACameraActor* Cam = *It;
		if (!Cam)
		{
			continue;
		}
		if (Cam->ActorHasTag(FName("LobbyCamera")))
		{
			LobbyCam = Cam;
			break;
		}
		if (!LobbyCam)
		{
			LobbyCam = Cam;
		}
	}

	if (LobbyCam)
	{
		// Stop the controller from re-pointing the view at the (spectator) pawn on possess, then lock the overview.
		bAutoManageActiveCameraTarget = false;
		SetViewTargetWithBlend(LobbyCam, 0.0f);
		return;
	}

	// Level CameraActor may not be present yet on a fresh client travel — retry a bounded number of times.
	if (LobbyViewRetries < 12)
	{
		++LobbyViewRetries;
		World->GetTimerManager().SetTimer(LobbyViewTimer, this, &AFPSRLobbyPlayerController::ApplyLobbyViewTarget, 0.25f, false);
	}
}

void AFPSRLobbyPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LobbyViewTimer);
	}

	// On lobby->run SEAMLESS travel this UI-only lobby PC is swapped out and destroyed. Tear down its viewport
	// layout explicitly: the lobby widget runs a CommonUI Menu input config, and if the layout is left on the
	// viewport (the LocalPlayer + CommonUI action router survive seamless travel) its activatable root lingers and
	// keeps the swapped-in gameplay client stuck in Menu input mode — so game input is swallowed and never reaches
	// the pawn after travel (P7 movement blocker). Removing it deactivates the lobby widget and deregisters it.
	if (PrimaryLayout)
	{
		PrimaryLayout->RemoveFromParent();
		PrimaryLayout = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void AFPSRLobbyPlayerController::ServerSelectLoadoutWeapon_Implementation(int32 PoolIndex)
{
	AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>();
	if (!PS)
	{
		return;
	}

	// Resolve the configured pool (server side) and validate the index — the client only ever sends an index, so
	// it can't inject an arbitrary weapon (mirrors the card-offer security model).
	const UFPSRGameFlowSettings* Settings = GetDefault<UFPSRGameFlowSettings>();
	UFPSRLoadoutPoolDataAsset* Pool = Settings ? Settings->LoadoutPool.LoadSynchronous() : nullptr;
	if (!Pool)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Lobby] ServerSelectLoadoutWeapon: LoadoutPool unset/unloadable."));
		return;
	}

	if (!Pool->IsValidIndex(PoolIndex))
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Lobby] ServerSelectLoadoutWeapon: invalid index %d."), PoolIndex);
		return;
	}

	PS->SetSelectedWeapon(Pool->GetWeaponAt(PoolIndex));
}

void AFPSRLobbyPlayerController::ServerSetReady_Implementation(bool bReady)
{
	AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>();
	if (!PS)
	{
		return;
	}

	PS->SetReady(bReady);

	// Re-evaluate the all-ready start gate (the server owns the travel decision; clients only send intent).
	if (AFPSRLobbyGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AFPSRLobbyGameMode>() : nullptr)
	{
		GM->NotifyReadyChanged();
	}
}
