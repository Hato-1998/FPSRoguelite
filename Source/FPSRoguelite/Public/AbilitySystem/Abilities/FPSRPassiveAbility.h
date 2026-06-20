// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/FPSRGameplayAbility.h"
#include "FPSRPassiveAbility.generated.h"

class UGameplayEffect;

/**
 * Base for character PASSIVE abilities (U18c §2-3-5). Granted by a UCardEffect_CharacterPassive card and tracked on
 * the PlayerState for run-reset (ClearAbility). ServerOnly — passives mutate authoritative state and any heal GE
 * replicates to the owner. A passive either auto-activates on grant (bActivateOnGrant, for always-on loops) or fires
 * from an AbilityTrigger (event-driven, e.g. lifesteal). Players are GAS-native (ASC, ≤4) so this is the right home
 * for character behaviors — unlike the stateless weapon fragments (which exist for the 500-enemy perf budget).
 */
UCLASS(Abstract)
class FPSROGUELITE_API UFPSRPassiveAbility : public UFPSRGameplayAbility
{
	GENERATED_BODY()

public:
	UFPSRPassiveAbility();

	/** True if this passive activates from GameplayEvent.Player.DealtDamage. Drives the PlayerState listener count so
	 *  FPSRCombat::ApplyDamage only sends the (hot-path) event for players who actually picked such a passive. */
	virtual bool RequiresDealtDamageEvent() const { return false; }

protected:
	/** Auto-activate when granted (always-on passives). Event-triggered passives leave this false and rely on their
	 *  AbilityTrigger instead. */
	UPROPERTY(EditDefaultsOnly, Category = "Passive")
	bool bActivateOnGrant = false;

	//~UGameplayAbility — a card grant mid-run fires OnGiveAbility (avatar already set); a startup grant before
	// possession fires OnAvatarSet later. Try to auto-activate from BOTH (guarded against double-activation) so an
	// always-on passive starts no matter which order grant/avatar happen.
	virtual void OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	virtual void OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;

	/** Activate the granted spec if bActivateOnGrant and the avatar/spec are ready, unless it is already running. */
	void TryAutoActivate(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec);
};

/**
 * Lifesteal passive (U18c): heals a fraction of the ACTUAL damage the player deals. Triggered by
 * GameplayEvent.Player.DealtDamage — the payload magnitude is the real DamageDealt (overkill / corpse re-hits already
 * excluded by FPSRCombat::ApplyDamage), so it can't be farmed by overkilling. Applies an instant heal GE whose
 * SetByCaller magnitude = DamageDealt * HealRatio.
 */
UCLASS()
class FPSROGUELITE_API UFPSRPassiveAbility_Lifesteal : public UFPSRPassiveAbility
{
	GENERATED_BODY()

public:
	UFPSRPassiveAbility_Lifesteal();

	virtual bool RequiresDealtDamageEvent() const override { return true; }

protected:
	/** Fraction of dealt damage healed back (0.05 = 5%). Authored on the GA. Rarity-scaling of this ratio is a
	 *  follow-up polish (the card magnitude system targets GE SetByCaller, not per-grant ability scalars). */
	UPROPERTY(EditDefaultsOnly, Category = "Lifesteal", meta = (ClampMin = "0.0"))
	float HealRatio = 0.05f;

	/** Instant heal GE; magnitude injected via SetByCaller (tag SetByCaller.CardMagnitude). Content-authored —
	 *  null safely no-ops (build/smoke don't require content). */
	UPROPERTY(EditDefaultsOnly, Category = "Lifesteal")
	TSubclassOf<UGameplayEffect> HealEffect;

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
};
