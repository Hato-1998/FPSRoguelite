// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

/** One hit's "what is hitting it". Replaces the trailing `FGameplayTag DamageType` slot U18a added — opened into a
 *  struct so the sibling units (lightweight enemy status effects / general status effects) can add fields without
 *  touching this signature again. Every field defaults to current behavior (empty tag = Physical, shield multiplier
 *  1.0). NOT a USTRUCT — it is never replicated or Blueprint-exposed (a server-internal pass-through value), so
 *  paying for UHT reflection here would buy nothing (VIT1 §5-1). */
struct FFPSRDamageSpec
{
	/** Empty = Physical. `DamageType.*` (DefaultGameplayTags.ini:29-33). The lookup key for the per-layer defense
	 *  coefficients (UFPSRVitalsProfileDataAsset::ResolveDefense). */
	FGameplayTag DamageType;

	/** Anti-shield multiplier (the penetration payoff, user decision 2026-09-01). 1 = ordinary · >1 = strong against
	 *  shields · 0 = cannot dent the shield at all (the whole hit overflows straight to health — "shield-ignoring"
	 *  is data-expressible too). */
	float ShieldDamageMultiplier = 1.0f;
};

/** Shared two-layer vitals rules (VIT1 — Shield/Health Two-Layer Vitals). Both damage receivers — the swarm's
 *  UFPSREnemyHealthComponent and the player's UFPSRHealthSet/AFPSRCharacter — call into this namespace so a shield
 *  behaves identically for every actor in the game without either storage knowing the other exists.
 *
 *  First principle (핵심원칙 1 — enemies cheap at 200-300 concurrent): this namespace holds NO state and NO
 *  replication of its own. A pure function costs nothing when nobody calls it, and its cost when called is O(1)
 *  arithmetic — it cannot become a per-enemy tick or a per-enemy UObject, which is exactly the axis this project
 *  cannot afford to spend on 200-300 actors (Game.md §1). */
namespace FPSRVitals
{
	/** How well one layer holds up. Smaller is tougher (a multiplier on incoming damage, not a subtracted armor
	 *  value). ⚠️ Naming note — "Defense" here means a MULTIPLIER, not flat mitigation: 0.5 = only half gets through. */
	struct FMitigation
	{
		float ShieldDefense = 1.0f; // per-DamageType value from the profile
		float HealthDefense = 1.0f;

		/** M4 directional armor damage reduction [0,1). This unit always passes 0 (the hook is opened, not filled). */
		float DirectionalArmorDR = 0.0f;

		/** 🔴 Invariant: no combination of mitigations may reduce a single hit's total by more than this. Backs the
		 *  `Enemy.md` §2-6 shield-archetype rule "no hard-block (0 damage)" — a DamageDealt of 0 silences hit-markers,
		 *  lifesteal, and kill-credit alike. */
		float MaxTotalReduction = 0.95f;
	};

	/** The current values of the two-layer pool. The CALLER owns this storage — this namespace persists nothing. */
	struct FPool
	{
		float Shield = 0.0f;
		float MaxShield = 0.0f;
		float Health = 0.0f;
		float MaxHealth = 0.0f;
	};

	struct FResult
	{
		float ShieldSpent = 0.0f;   // actually removed from the shield
		float HealthSpent = 0.0f;   // actually removed from health
		bool  bShieldBroke = false; // this hit took the shield from (>0) to 0
		bool  bLethal = false;      // health reached 0

		/** The "real damage dealt" hit-markers / lifesteal / the director / missions read. 🔴 The new definition
		 *  behind combat regression trap 1 (VIT1 §11-8 — replaces the old HealthBefore-HealthAfter reading). */
		float TotalSpent() const { return ShieldSpent + HealthSpent; }
	};

	/** Pure function. Spends InOutPool in place and returns what happened. No authority / replication / time
	 *  concept lives here. Incoming <= 0 is a no-op (empty result, pool untouched). */
	FPSROGUELITE_API FResult ApplyDamage(FPool& InOutPool, float Incoming,
		const FFPSRDamageSpec& Spec, const FMitigation& Mit);

	/** Delayed-regen formula — **idempotent** (the same inputs always produce the same output). That is what lets a
	 *  caller hold only two numbers ("the shield value right after the last hit" + "the time of that hit"): an enemy
	 *  can call this ONLY when it is hit (tickless), while a player calls it periodically for a live HUD bar — ONE
	 *  formula serves both.
	 *  @param ShieldAtLastDamage  the shield value right after the last hit (0 = that hit broke it -> the long delay applies)
	 *  @param ElapsedSinceDamage  seconds elapsed on the freeze-paused combat clock (§5-6) since that hit */
	FPSROGUELITE_API float ComputeRegeneratedShield(float ShieldAtLastDamage, float MaxShield,
		float ElapsedSinceDamage, float RegenPerSecond,
		float PartialDelaySeconds, float BrokenDelaySeconds);

	/** Requirement 4's "is it broken" — derived, never stored (a client with the replicated Shield/MaxShield reaches
	 *  the same answer as the server). */
	FORCEINLINE bool IsShieldBroken(float Shield, float MaxShield)
	{
		return MaxShield > 0.0f && Shield <= 0.0f;
	}
}
