// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSRPassiveAbility.h"

#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"

UFPSRPassiveAbility::UFPSRPassiveAbility()
{
	// Passives run on the server (they mutate authoritative state; any heal GE replicates to the owner). The base
	// UFPSRGameplayAbility sets LocalPredicted; passives override to ServerOnly.
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UFPSRPassiveAbility::OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnGiveAbility(ActorInfo, Spec);
	// A card-granted passive is given mid-run (avatar already set) — GiveAbility routes here, NOT OnAvatarSet — so
	// this is the path that actually auto-activates a card-picked always-on passive.
	TryAutoActivate(ActorInfo, Spec);
}

void UFPSRPassiveAbility::OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnAvatarSet(ActorInfo, Spec);
	// Covers the startup-grant case (granted before possession): activate once the avatar arrives.
	TryAutoActivate(ActorInfo, Spec);
}

void UFPSRPassiveAbility::TryAutoActivate(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	// Event-triggered passives leave bActivateOnGrant false and fire from their AbilityTrigger instead.
	if (!bActivateOnGrant || !ActorInfo || !ActorInfo->AvatarActor.IsValid())
	{
		return;
	}
	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	if (!ASC)
	{
		return;
	}
	// OnGiveAbility + OnAvatarSet can both fire — don't re-activate an instance that is already running.
	if (const FGameplayAbilitySpec* FoundSpec = ASC->FindAbilitySpecFromHandle(Spec.Handle))
	{
		if (FoundSpec->IsActive())
		{
			return;
		}
	}
	ASC->TryActivateAbility(Spec.Handle);
}

UFPSRPassiveAbility_Lifesteal::UFPSRPassiveAbility_Lifesteal()
{
	// Activate from the DealtDamage gameplay event (sent server-side by FPSRCombat::ApplyDamage). Tags load before
	// game-module ability CDOs construct, so RequestGameplayTag resolves the config tag here.
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = FGameplayTag::RequestGameplayTag(FName("GameplayEvent.Player.DealtDamage"));
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);
}

void UFPSRPassiveAbility_Lifesteal::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Payload magnitude is the ACTUAL damage dealt (overkill/corpse already excluded by ApplyDamage), so lifesteal
	// can't be farmed by overkilling.
	const float DamageDealt = TriggerEventData ? TriggerEventData->EventMagnitude : 0.0f;
	const float HealAmount = DamageDealt * HealRatio;
	if (HealAmount > 0.0f && HealEffect)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			const FGameplayEffectContextHandle EffectContext = ASC->MakeEffectContext();
			const FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(HealEffect, 1.0f, EffectContext);
			if (SpecHandle.IsValid())
			{
				static const FGameplayTag MagnitudeTag = FGameplayTag::RequestGameplayTag(FName("SetByCaller.CardMagnitude"));
				SpecHandle.Data->SetSetByCallerMagnitude(MagnitudeTag, HealAmount);
				ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data);
			}
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
