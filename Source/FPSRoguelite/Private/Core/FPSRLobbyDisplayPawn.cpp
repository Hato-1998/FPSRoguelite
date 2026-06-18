// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRLobbyDisplayPawn.h"
#include "Core/FPSRPlayerState.h"
#include "Components/SkeletalMeshComponent.h"

AFPSRLobbyDisplayPawn::AFPSRLobbyDisplayPawn()
{
	// Display-only: no tick, but it must replicate so every client sees every podium (and its PlayerState).
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	BodyMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BodyMesh"));
	SetRootComponent(BodyMesh);
	// Lobby display only — no collision/physics needed.
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AFPSRLobbyDisplayPawn::BeginPlay()
{
	Super::BeginPlay();
	BindToPlayerState();
}

void AFPSRLobbyDisplayPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (BoundPlayerState)
	{
		BoundPlayerState->OnLoadoutChanged.RemoveDynamic(this, &AFPSRLobbyDisplayPawn::HandleLoadoutChanged);
		BoundPlayerState = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void AFPSRLobbyDisplayPawn::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	// Client: the PlayerState just (re)replicated — bind now that we can read the selected weapon.
	BindToPlayerState();
}

void AFPSRLobbyDisplayPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	// Server: PlayerState is assigned on possession.
	BindToPlayerState();
}

void AFPSRLobbyDisplayPawn::BindToPlayerState()
{
	AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>();
	if (PS == BoundPlayerState)
	{
		return;
	}

	if (BoundPlayerState)
	{
		BoundPlayerState->OnLoadoutChanged.RemoveDynamic(this, &AFPSRLobbyDisplayPawn::HandleLoadoutChanged);
	}

	BoundPlayerState = PS;

	if (BoundPlayerState)
	{
		BoundPlayerState->OnLoadoutChanged.AddDynamic(this, &AFPSRLobbyDisplayPawn::HandleLoadoutChanged);
		// Initial paint with whatever pick already exists.
		HandleLoadoutChanged();
	}
}

void AFPSRLobbyDisplayPawn::HandleLoadoutChanged()
{
	OnDisplayWeaponChanged(GetDisplayedWeapon());
}

UFPSRWeaponDataAsset* AFPSRLobbyDisplayPawn::GetDisplayedWeapon() const
{
	const AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>();
	return PS ? PS->GetSelectedWeapon() : nullptr;
}
