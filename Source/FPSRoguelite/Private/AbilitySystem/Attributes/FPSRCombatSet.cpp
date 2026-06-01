// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Attributes/FPSRCombatSet.h"
#include "Net/UnrealNetwork.h"

UFPSRCombatSet::UFPSRCombatSet()
{
	InitGlobalCritChance(0.05f);
	InitGlobalCritMultiplier(2.0f);
	InitGlobalDamageMultiplier(1.0f);
	InitLuck(0.0f);
	InitRarityBonus(0.0f);
}

void UFPSRCombatSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRCombatSet, GlobalCritChance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRCombatSet, GlobalCritMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRCombatSet, GlobalDamageMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRCombatSet, Luck, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRCombatSet, RarityBonus, COND_None, REPNOTIFY_Always);
}

void UFPSRCombatSet::OnRep_GlobalCritChance(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRCombatSet, GlobalCritChance, OldValue);
}

void UFPSRCombatSet::OnRep_GlobalCritMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRCombatSet, GlobalCritMultiplier, OldValue);
}

void UFPSRCombatSet::OnRep_GlobalDamageMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRCombatSet, GlobalDamageMultiplier, OldValue);
}

void UFPSRCombatSet::OnRep_Luck(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRCombatSet, Luck, OldValue);
}

void UFPSRCombatSet::OnRep_RarityBonus(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRCombatSet, RarityBonus, OldValue);
}
