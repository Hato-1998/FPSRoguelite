// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Attributes/FPSRHealthSet.h"
#include "Net/UnrealNetwork.h"
#include "Math/UnrealMathUtility.h"

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

void UFPSRHealthSet::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		NewValue = FMath::Max(NewValue, 1.0f);
	}
}

void UFPSRHealthSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UFPSRHealthSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UFPSRHealthSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);

	if (Attribute == GetHealthAttribute())
	{
		if (!bOutOfHealthBroadcast && OldValue > 0.0f && NewValue <= 0.0f)
		{
			bOutOfHealthBroadcast = true;
			OnOutOfHealth.Broadcast();
		}
		else if (NewValue > 0.0f)
		{
			bOutOfHealthBroadcast = false;
		}
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		// A MaxHealth increase (e.g. a card) also heals current Health by the same amount, so picking a
		// +MaxHealth upgrade feels immediately impactful. SetHealth re-clamps to the new max. Decreases are
		// left to the Health clamp (current health is capped to max on its next change).
		// Authority-only: the server heals and Health replicates to clients. A replicated MaxHealth change
		// also fires PostAttributeChange on clients (for active/infinite GEs), so guarding here prevents the
		// client from double-applying the heal on top of the already-replicated Health.
		const float Delta = NewValue - OldValue;
		const UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
		if (Delta > 0.0f && ASC && ASC->IsOwnerActorAuthoritative())
		{
			SetHealth(GetHealth() + Delta);
		}
	}
}
