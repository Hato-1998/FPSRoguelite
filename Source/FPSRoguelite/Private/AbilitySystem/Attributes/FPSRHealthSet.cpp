// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Attributes/FPSRHealthSet.h"
#include "Net/UnrealNetwork.h"

UFPSRHealthSet::UFPSRHealthSet()
{
	InitHealth(100.0f);
	InitMaxHealth(100.0f);
}

void UFPSRHealthSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRHealthSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRHealthSet, MaxHealth, COND_None, REPNOTIFY_Always);
}

void UFPSRHealthSet::OnRep_Health(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRHealthSet, Health, OldValue);
}

void UFPSRHealthSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRHealthSet, MaxHealth, OldValue);
}
