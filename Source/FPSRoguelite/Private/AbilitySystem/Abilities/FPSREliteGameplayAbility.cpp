// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSREliteGameplayAbility.h"
#include "Enemy/FPSREnemyEliteBase.h"

float UFPSREliteGameplayAbility::GetCooldownClockSeconds(const FGameplayAbilityActorInfo* ActorInfo) const
{
	const AFPSREnemyEliteBase* Elite = ActorInfo ? Cast<AFPSREnemyEliteBase>(ActorInfo->AvatarActor.Get()) : nullptr;
	return Elite ? Elite->GetEliteCooldownClockSeconds() : -1.0f;
}
