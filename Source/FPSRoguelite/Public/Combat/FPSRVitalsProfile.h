// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "FPSRVitalsProfile.generated.h"

/** Per-DamageType layer defense multipliers for one entity kind. An empty DamageType is the profile's DEFAULT entry
 *  (every damage type not otherwise listed). */
USTRUCT(BlueprintType)
struct FPSROGUELITE_API FFPSRVitalsDefenseEntry
{
	GENERATED_BODY()

	/** Empty = the default entry. Only `DamageType.*` is valid (checked by IsDataValid). */
	UPROPERTY(EditAnywhere, meta = (Categories = "DamageType"))
	FGameplayTag DamageType;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", UIMax = "3.0"))
	float ShieldDefense = 1.0f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", UIMax = "3.0"))
	float HealthDefense = 1.0f;
};

/** One entity kind's survival spec (VIT1 — Shield/Health Two-Layer Vitals). Shared by every actor of the same
 *  archetype (requirement 2 — "no shield" is just MaxShield 0, no subclass/branch needed).
 *  ⚠️ Enemy `MaxHealth` moves here from the BP editor default (user decision 2026-09-01) — see VIT1 §11-1 for the
 *  migration (content work; both sources are honored until it's complete, so shipping this is a zero-regression
 *  change on its own). */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRVitalsProfileDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Vitals", meta = (ClampMin = "1.0"))
	float MaxHealth = 50.0f;

	/** 0 = this entity has no shield (requirement 1). The regen fields below are meaningless when this is 0, so
	 *  they're hidden rather than merely unused ([[dataasset-conditional-field-visibility]]). */
	UPROPERTY(EditAnywhere, Category = "Vitals", meta = (ClampMin = "0.0"))
	float MaxShield = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Vitals|Shield Regen",
		meta = (ClampMin = "0.0", EditCondition = "MaxShield > 0", EditConditionHides))
	float ShieldRegenPerSecond = 0.0f;

	/** Stillness required (on the freeze-paused combat clock) after a PARTIAL hit before the shield starts refilling. */
	UPROPERTY(EditAnywhere, Category = "Vitals|Shield Regen",
		meta = (ClampMin = "0.0", EditCondition = "MaxShield > 0", EditConditionHides))
	float ShieldRegenDelaySeconds = 3.0f;

	/** The longer stillness required after the shield is fully BROKEN (0) — Halo-style dual delay (user decision
	 *  2026-09-01). */
	UPROPERTY(EditAnywhere, Category = "Vitals|Shield Regen",
		meta = (ClampMin = "0.0", EditCondition = "MaxShield > 0", EditConditionHides))
	float ShieldBrokenRegenDelaySeconds = 6.0f;

	/** Per-DamageType layer multipliers. Empty = every type is 1.0. An empty-tag entry is the default (applies to
	 *  any DamageType not otherwise listed). */
	UPROPERTY(EditAnywhere, Category = "Vitals|Defense")
	TArray<FFPSRVitalsDefenseEntry> DefenseByDamageType;

	/** 🔴 The safety rail behind invariant V1 (G1 P2-3). The maximum total mitigation a single hit against this
	 *  entity may ever reach — keeps M4 directional armor DR stacked with a layer coefficient from multiplying down
	 *  to 0 damage, which would silence hit-markers / lifesteal / kill-credit alike (Enemy.md §2-6's shield-archetype
	 *  "no hard-block" rule, enforced here in data). **1.0 cannot be authored** (ClampMax). */
	UPROPERTY(EditAnywhere, Category = "Vitals|Defense", meta = (ClampMin = "0.0", ClampMax = "0.99"))
	float MaxTotalReduction = 0.95f;

	/** Server/either side: resolve the layer coefficients for a DamageType (exact match -> default entry -> 1.0). */
	void ResolveDefense(const FGameplayTag& DamageType, float& OutShieldDefense, float& OutHealthDefense) const;

#if WITH_EDITOR
	/** (1) duplicate DamageType entries (error) (2) a tag outside `DamageType.*` (error) (3) MaxShield > 0 with 0
	 *  regen — permanently broken, almost certainly a mistake (warning) (4) more than 8 entries — a per-hit linear
	 *  scan, so warn about the cost (5) MaxTotalReduction >= 1.0 — **error** (breaks invariant V1; ClampMax only
	 *  stops editor typing, not an imported/scripted value). */
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

/** ADR 0014 per-difficulty-tier "deck" layer multipliers (user decision 2026-09-01 — "the same deck shares one
 *  set"). 🔴 A deck scales AMOUNT only, never coefficients — putting coefficients on the deck too would make the
 *  same monster resist different elements depending on the deck, which breaks the player's ability to learn it (the
 *  whole point of the survivors-like retention loop). No conflict with ADR 0014 invariant 1 ("the sole owner of
 *  enemy COUNT is the director") — this scales individual toughness, not headcount. */
USTRUCT(BlueprintType)
struct FPSROGUELITE_API FFPSRVitalsDeckModifier
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.01", UIMax = "5.0"))
	float MaxHealthScale = 1.0f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", UIMax = "5.0"))
	float MaxShieldScale = 1.0f;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", UIMax = "5.0"))
	float ShieldRegenScale = 1.0f;
};

/** Profile x deck folded once at spawn — the component then bakes this in directly (no runtime re-lookup). */
struct FPSROGUELITE_API FFPSRResolvedVitals
{
	float MaxHealth = 50.0f;
	float MaxShield = 0.0f;
	float ShieldRegenPerSecond = 0.0f;
	float ShieldRegenDelaySeconds = 3.0f;
	float ShieldBrokenRegenDelaySeconds = 6.0f;

	/** For coefficient lookups. The deck never touches coefficients, so this just carries the profile pointer
	 *  through (8 bytes per actor). */
	TObjectPtr<const UFPSRVitalsProfileDataAsset> Profile = nullptr;

	/** Profile null = the current no-shield behavior (FallbackMaxHealth from the caller). Zero-regression path. */
	static FFPSRResolvedVitals Resolve(const UFPSRVitalsProfileDataAsset* Profile,
		const FFPSRVitalsDeckModifier& Deck, float FallbackMaxHealth);
};
