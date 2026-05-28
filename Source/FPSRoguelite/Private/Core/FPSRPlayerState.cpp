// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRPlayerState.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/FPSRHealthSet.h"
#include "AbilitySystem/Attributes/FPSRCombatSet.h"

AFPSRPlayerState::AFPSRPlayerState()
{
	AbilitySystemComponent = CreateDefaultSubobject<UFPSRAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	HealthSet = CreateDefaultSubobject<UFPSRHealthSet>(TEXT("HealthSet"));
	CombatSet = CreateDefaultSubobject<UFPSRCombatSet>(TEXT("CombatSet"));

	// PlayerState updates frequently so GAS state stays responsive for clients.
	SetNetUpdateFrequency(100.0f);
}

UAbilitySystemComponent* AFPSRPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}
