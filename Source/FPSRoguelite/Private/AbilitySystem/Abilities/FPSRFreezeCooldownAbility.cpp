// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSRFreezeCooldownAbility.h"

UFPSRFreezeCooldownAbility::UFPSRFreezeCooldownAbility()
{
	// An ACTOR-owned ASC has no owning client at all (server-authoritative actor — the same fact the ASC's Minimal
	// replication mode rests on). LocalPredicted (UFPSRGameplayAbility's default) means nothing without an owning
	// connection to predict FOR; mirrors UFPSRPassiveAbility's own override for the same reason ("passives run on
	// the server — they mutate authoritative state").
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

bool UFPSRFreezeCooldownAbility::CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (CooldownSeconds <= 0.0f)
	{
		return true; // no cooldown authored (header doc) — always ready
	}

	const float Clock = GetCooldownClockSeconds(ActorInfo);
	if (Clock < 0.0f || LastActivationClockSeconds < 0.0f)
	{
		// Fail-open (mirrors UFPSRGameplayAbility::IsFirePermittedByMovementState's philosophy): no readable clock,
		// or this is the first-ever check on this instance — neither should permanently brick the ability, and
		// "never activated" must not be misread as "on cooldown" (see LastActivationClockSeconds's doc).
		return true;
	}

	return (Clock - LastActivationClockSeconds) >= CooldownSeconds;
}

void UFPSRFreezeCooldownAbility::ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	// Deliberately no Super:: call and no GE applied — see GetCooldownGameplayEffect's comment. Stamp the owner's
	// freeze-paused clock instead, so the cooldown this stamps against is itself freeze-accurate.
	const float Clock = GetCooldownClockSeconds(ActorInfo);
	if (Clock >= 0.0f)
	{
		LastActivationClockSeconds = Clock;
	}
}

UGameplayEffect* UFPSRFreezeCooldownAbility::GetCooldownGameplayEffect() const
{
	// Always null — see this class's header doc. A duration/periodic GE's timer runs on the world FTimerManager
	// regardless of the §2-2 freeze gate (a state flag, not TimeDilation), which is exactly the leak this class
	// exists to close; UFPSRAbilitySystemComponent::EnableTimeAxisGuard also rejects one outright if applied anyway.
	return nullptr;
}
