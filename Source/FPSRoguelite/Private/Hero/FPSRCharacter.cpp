// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hero/FPSRCharacter.h"
#include "Core/FPSRPlayerState.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h"

AFPSRCharacter::AFPSRCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
	// Camera and Separated-Arms meshes are set up in a later P1 step.
}

void AFPSRCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Server: PlayerState is valid here.
	InitAbilitySystem();
}

void AFPSRCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// Client: PlayerState has replicated.
	InitAbilitySystem();
}

void AFPSRCharacter::InitAbilitySystem()
{
	AFPSRPlayerState* PS = GetPlayerState<AFPSRPlayerState>();
	if (!PS)
	{
		return;
	}

	AbilitySystemComponent = PS->GetFPSRAbilitySystemComponent();
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(PS, this);
	}
}

UAbilitySystemComponent* AFPSRCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}
