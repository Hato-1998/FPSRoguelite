// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSRBossGameplayAbility.h"
#include "Boss/FPSRBossBase.h"

AFPSRBossBase* UFPSRBossGameplayAbility::GetBoss() const
{
	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	return Info ? Cast<AFPSRBossBase>(Info->AvatarActor.Get()) : nullptr;
}

float UFPSRBossGameplayAbility::GetPatternClock() const
{
	const AFPSRBossBase* Boss = GetBoss();
	return Boss ? Boss->GetPatternClockSeconds() : -1.0f;
}

float UFPSRBossGameplayAbility::GetCooldownClockSeconds(const FGameplayAbilityActorInfo* ActorInfo) const
{
	// -1 = "clock unreadable" -> the base fails open (see UFPSRFreezeCooldownAbility::GetCooldownClockSeconds).
	const AFPSRBossBase* Boss = ActorInfo ? Cast<AFPSRBossBase>(ActorInfo->AvatarActor.Get()) : nullptr;
	return Boss ? Boss->GetPatternClockSeconds() : -1.0f;
}
