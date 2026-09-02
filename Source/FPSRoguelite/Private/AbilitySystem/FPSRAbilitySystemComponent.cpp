// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/FPSRAbilitySystemComponent.h"

#include "Core/FPSRLogChannels.h"
#include "GameplayEffect.h"

UFPSRAbilitySystemComponent::UFPSRAbilitySystemComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UFPSRAbilitySystemComponent::EnableTimeAxisGuard()
{
	if (bTimeAxisGuardRegistered)
	{
		return;
	}
	bTimeAxisGuardRegistered = true;

	GameplayEffectApplicationQueries.Add(
		FGameplayEffectApplicationQuery::CreateUObject(this, &UFPSRAbilitySystemComponent::RejectTimeBasedGameplayEffect));
}

bool UFPSRAbilitySystemComponent::RejectTimeBasedGameplayEffect(const FActiveGameplayEffectsContainer& ActiveGEContainer, const FGameplayEffectSpec& Spec) const
{
	const bool bHasDuration = Spec.Def && Spec.Def->DurationPolicy == EGameplayEffectDurationType::HasDuration;
	const bool bHasPeriod = Spec.GetPeriod() > 0.0f;
	if (!bHasDuration && !bHasPeriod)
	{
		return true; // Instant, or Infinite with NO period — allowed (see header doc for why Period must be checked too)
	}

	// Dev-time noise only (ensureMsgf never crashes, in Shipping or otherwise — that's the whole point of using it
	// instead of check/checkf here) + an always-fires log line so a live server's repeat offenders aren't silenced
	// after ensure's own one-shot-per-callsite suppression.
	//
	// The owner is named rather than hardcoded to "Elite": this guard is shared by every ACTOR-owned ASC (elite and
	// boss alike — see the header's owner list), so a message that named one of them would misdirect on the other.
	ensureMsgf(false, TEXT("[FPSR ASC] Rejected GE '%s' on %s — HasDuration/periodic GE timers run on the world ")
		TEXT("FTimerManager and are NOT paused by the §2-2 freeze gate. Use UFPSRFreezeCooldownAbility::")
		TEXT("CooldownSeconds, or the owner's freeze-paused accumulator, instead."),
		*GetNameSafe(Spec.Def), *GetNameSafe(GetOwner()));
	UE_LOG(LogFPSR, Warning,
		TEXT("[FPSR ASC] Rejected time-based GE '%s' on %s (HasDuration=%d, Period=%.2f)."),
		*GetNameSafe(Spec.Def), *GetNameSafe(GetOwner()), bHasDuration ? 1 : 0, Spec.GetPeriod());
	return false;
}
