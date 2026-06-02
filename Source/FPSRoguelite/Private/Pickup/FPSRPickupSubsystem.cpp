// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pickup/FPSRPickupSubsystem.h"
#include "Pickup/FPSRXPPickup.h"
#include "Core/FPSRGameState.h"

#include "Engine/World.h"

bool UFPSRPickupSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

bool UFPSRPickupSubsystem::HasServerAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

void UFPSRPickupSubsystem::PruneActivePickups()
{
	ActivePickups.RemoveAll([](const TObjectPtr<AFPSRXPPickup>& Pickup)
	{
		return !IsValid(Pickup);
	});
}

void UFPSRPickupSubsystem::SpawnXPPickup(const FVector& Location, int32 XPValue)
{
	UWorld* World = GetWorld();
	if (!World || !HasServerAuthority())
	{
		return;
	}

	PruneActivePickups();

	// Over the active cap: grant XP directly to the party rather than spawning another actor.
	if (ActivePickups.Num() >= MaxActivePickups)
	{
		if (AFPSRGameState* GameState = World->GetGameState<AFPSRGameState>())
		{
			GameState->AddSharedXP(XPValue);
		}
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AFPSRXPPickup* Pickup = World->SpawnActor<AFPSRXPPickup>(AFPSRXPPickup::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
	if (Pickup == nullptr)
	{
		return;
	}

	Pickup->SetXPValue(XPValue);
	ActivePickups.Add(Pickup);
}
