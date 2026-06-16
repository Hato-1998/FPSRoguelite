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

void AFPSRLobbyPlayerController::ServerRequestStartRun_Implementation()
{
	if (AFPSRLobbyGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AFPSRLobbyGameMode>() : nullptr)
	{
		GM->RequestStartRun(this);
	}
}
