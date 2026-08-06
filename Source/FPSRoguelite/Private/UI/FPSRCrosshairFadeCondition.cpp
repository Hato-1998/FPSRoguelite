// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRCrosshairFadeCondition.h"
#include "Hero/FPSRCharacter.h"
#include "AbilitySystemComponent.h"

bool UCrosshairFade_Reloading::IsActive_Implementation(const AFPSRCharacter* Character) const
{
	return Character && Character->IsReloading();
}

bool UCrosshairFade_GameplayTag::IsActive_Implementation(const AFPSRCharacter* Character) const
{
	if (!Character || !Tag.IsValid())
	{
		return false;
	}
	// Const-correct route to the ASC: GetAbilitySystemComponent() is non-const on the interface, so ask through a
	// non-const view of the character we were handed rather than casting the component away.
	const UAbilitySystemComponent* ASC = const_cast<AFPSRCharacter*>(Character)->GetAbilitySystemComponent();
	return ASC && ASC->HasMatchingGameplayTag(Tag);
}
