// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Card/FPSRCardTypes.h"
#include "Weapon/FPSRWeaponTypes.h"
#include "GameplayTagContainer.h"
#include "FPSRCardDataAsset.generated.h"

class UGameplayEffect;
class UFPSRWeaponFragment;
class UFPSRCardEffect;

/** Data-driven card definition (U18a v2): a card owns one or more polymorphic Instanced effects. The draw rolls a
 *  single rarity (OfferRarities); each effect resolves its own magnitude (RarityTiers) at that rarity. */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRCardDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card", meta = (MultiLine = true))
	FText Description;

	/** Draw pool / trigger / UI filter this card belongs to (§2-3-2). Character = central pool (character +
	 *  all-weapons effects, no target weapon); Weapon = a weapon's pool (TargetWeapon set at draw time). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	ECardGroup Group = ECardGroup::Character;

	/** The card's effects (U18a). Inline Instanced subobjects — authored per card, never replicated (they ride the
	 *  always-loaded card asset). ApplyCard loops these effect-type-agnostically. A new effect type = one subclass. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Instanced, Category = "Card")
	TArray<TObjectPtr<UFPSRCardEffect>> Effects;

	/** Overall draw-weight multiplier for this card (applied within each rarity it offers). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	float Weight = 1.0f;

	/** Rarities this card can be offered at — auto-derived from the effects' RarityTiers (the draw rolls one of
	 *  these, weighted by rarity base weight x Luck). Maintained in PostLoad / PostEditChangeProperty; read-only. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Card")
	TArray<ECardRarity> OfferRarities;

	/** Cards sharing a family are mutually exclusive within a single draw (only one is ever offered). Required for
	 *  multi-effect cards (the v1 AppliedEffect-GE-class fallback was removed — IsDataValid enforces it). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	FGameplayTag CardFamily;

	// --- Legacy v1 fields (DEPRECATED by U18a, §2-3-1). ORIGINAL names kept verbatim so pre-v2 serialized data loads
	//     by name match (no CoreRedirect needed); PostLoad migrates them into Effects and clears them. Field
	//     declarations are removed in a follow-up commit once every card asset has been re-saved (so cooked data never
	//     loses the source values). Do not author against these — they are editor-hidden. ---

	UPROPERTY(meta = (DeprecatedProperty))
	ECardScope Scope = ECardScope::Character;

	UPROPERTY(meta = (DeprecatedProperty))
	TSubclassOf<UGameplayEffect> AppliedEffect;

	UPROPERTY(meta = (DeprecatedProperty))
	TArray<FFPSRCardRarityTier> RarityTiers;

	UPROPERTY(meta = (DeprecatedProperty))
	EFPSRWeaponStat WeaponStat = EFPSRWeaponStat::FireRate;

	UPROPERTY(meta = (DeprecatedProperty))
	EFPSRWeaponModOp WeaponStatOp = EFPSRWeaponModOp::PercentMultiply;

	UPROPERTY(meta = (DeprecatedProperty))
	TObjectPtr<UFPSRWeaponFragment> GrantedFragment = nullptr;

#if WITH_EDITOR
	/** Migrate v1 single-effect fields into the polymorphic Effects array (idempotent) + refresh OfferRarities. */
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Editor validation: empty Effects, per-effect ValidateEffect, rarity coverage, multi-effect CardFamily, naming. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;

private:
	/** Build the single v2 effect from the legacy fields (only when Effects is empty — idempotent). */
	void MigrateFromLegacy();

	/** Recompute OfferRarities from the first magnitude-bearing effect's tiers (IsDataValid enforces the rest match). */
	void RefreshOfferRarities();
#endif
};
