// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "FPSRCritTypes.generated.h"

// Forward-declared, NOT included: GameplayEffect.h is a heavy GAS header and this file rides inside
// FPSRCombatStatics.h / FPSRProjectileTypes.h / FPSRWeaponFragment.h, i.e. most of the combat module. HasRiders()
// therefore lives in the .cpp — TSubclassOf::operator*() calls T::StaticClass() (SubclassOf.h:110), so comparing
// HealEffect against nullptr in an inline body would drag the whole header in behind it.
class UGameplayEffect;

/**
 * The complete crit rule for one activation. Replaces the (CritChance, CritMultiplier) float pair every damage
 * path used to carry separately — the whole point of this struct is that adding one more crit rule no longer
 * means re-touching all 5 path signatures (CRIT1).
 *
 * USTRUCT for exactly one reason: it rides as a UPROPERTY member of FFPSRProjectileParams (itself a USTRUCT) for
 * the projectile's whole flight, and HealEffect (a UClass*) must not be garbage-collected out from under it while
 * it does. BlueprintReadOnly, not BlueprintReadWrite: the filler is always server C++ (this is exposed only
 * because the params struct it rides in is already BlueprintType, not because BP is meant to author it).
 *
 * This value is baked ONCE at fire time. If an ASC attribute or a timed buff changes mid-activation, that
 * activation does not see it — every one of the 5 paths already worked this way, and this unit preserves it.
 */
USTRUCT(BlueprintType)
struct FPSROGUELITE_API FFPSRCritContext
{
	GENERATED_BODY()

	/** Crit roll chance [0,1]. = ASC GlobalCritChance + the weapon instance's active timed-buff sum.
	 *  0 = can never crit (an enemy-fired projectile leaves this at its default — the existing "enemy fire never
	 *  crits" contract). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crit")
	float Chance = 0.0f;

	/** Damage multiplier applied on a successful crit. = ASC GlobalCritMultiplier + the active timed-buff sum. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crit")
	float Multiplier = 1.0f;

	/** Card 4 (Weakpoint Precision) — a hit that lands on a weakpoint is a crit with no roll. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crit")
	bool bWeakpointAlwaysCrit = false;

	/** Card 1 (Critical Overkill) — this fraction of the crit's ACTUAL damage dealt re-lands on the same target
	 *  as a second damage instance. 0 = no rider. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crit", meta = (ClampMin = "0.0"))
	float BonusInstanceRatio = 0.0f;

	/** Card 2 (Critical Leech) — this fraction of the crit's actual damage dealt heals the instigator. 0 = no rider. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crit", meta = (ClampMin = "0.0"))
	float HealRatio = 0.0f;

	/** Instant heal GE used when HealRatio > 0 (SetByCaller tag `SetByCaller.CardMagnitude`). No hardcoded asset
	 *  path (§6-2) — the owning fragment DA authors and supplies this. Null silently no-ops the heal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crit")
	TSubclassOf<UGameplayEffect> HealEffect;

	/** Any rider (bonus instance / heal) actually configured? Lets the post-crit call site early-out cheaply. */
	bool HasRiders() const;
};
