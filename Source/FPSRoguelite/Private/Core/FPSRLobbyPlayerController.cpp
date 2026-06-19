// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRLobbyPlayerController.h"
#include "Core/FPSRLobbyGameMode.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRGameFlowSettings.h"
#include "Core/FPSRLogChannels.h"
#include "UI/FPSRPrimaryGameLayout.h"
#include "Weapon/FPSRLoadoutPoolDataAsset.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "CommonActivatableWidget.h"
#include "GameplayTagContainer.h"
#include "Engine/World.h"

void AFPSRLobbyPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

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
	if (!PrimaryLayout->PushWidgetToLayer(FGameplayTag::RequestGameplayTag(FName("UI.Layer.Menu")), LobbyWidgetClass))
	{
		UE_LOG(LogFPSR, Error, TEXT("[Lobby] Failed to push LobbyWidget to Menu layer"));
	}
}

void AFPSRLobbyPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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
