// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSREliteGameplayAbility.h"
#include "Enemy/FPSREnemyEliteBase.h"

UFPSREliteGameplayAbility::UFPSREliteGameplayAbility()
{
	// Elites have no owning client at all (server-authoritative swarm actor, ADR 소유권 표 — same fact the ASC's
	// Minimal replication mode rests on, AFPSREnemyEliteBase header). LocalPredicted (UFPSRGameplayAbility's default)
	// means nothing without an owning connection to predict FOR; mirrors UFPSRPassiveAbility's own override for the
	// same reason ("passives run on the server — they mutate authoritative state").
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

bool UFPSREliteGameplayAbility::CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (CooldownSeconds <= 0.0f)
	{
		return true; // no cooldown authored (header doc) — always ready
	}

	const AFPSREnemyEliteBase* Elite = ActorInfo ? Cast<AFPSREnemyEliteBase>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (!Elite || LastActivationClockSeconds < 0.0f)
	{
		// Fail-open (mirrors UFPSRGameplayAbility::IsFirePermittedByMovementState's philosophy): no elite avatar to
		// read the clock from, or this is the first-ever check this life — neither should permanently brick the
		// ability, and "never activated" must not be misread as "on cooldown" (see LastActivationClockSeconds's doc).
		return true;
	}

	return (Elite->GetEliteCooldownClockSeconds() - LastActivationClockSeconds) >= CooldownSeconds;
}

void UFPSREliteGameplayAbility::ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	// Deliberately no Super:: call and no GE applied — see GetCooldownGameplayEffect's comment. Stamp the elite's
	// freeze-paused accumulator instead, so the cooldown this stamps against is itself freeze-accurate.
	if (const AFPSREnemyEliteBase* Elite = ActorInfo ? Cast<AFPSREnemyEliteBase>(ActorInfo->AvatarActor.Get()) : nullptr)
	{
		LastActivationClockSeconds = Elite->GetEliteCooldownClockSeconds();
	}
}

UGameplayEffect* UFPSREliteGameplayAbility::GetCooldownGameplayEffect() const
{
	// Always null — see this class's header doc. A duration/periodic GE's timer runs on the world FTimerManager
	// regardless of the §2-2 freeze gate (a state flag, not TimeDilation), which is exactly the leak this class
	// exists to close; AFPSREnemyEliteBase's GameplayEffectApplicationQueries guard also rejects one outright if
	// applied anyway.
	return nullptr;
}
