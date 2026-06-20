// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Card/FPSRCardTypes.h"
#include "GameplayTagContainer.h"
#include "FPSRCardDataAsset.generated.h"

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

	// (v1 legacy fields + PostLoad migration removed in U18a-legacy-cleanup — every card asset was re-saved to v2.)

#if WITH_EDITOR
	/** Keep OfferRarities synced with the effects' rarity tiers on load / edit. */
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Editor validation: empty Effects, per-effect ValidateEffect, rarity coverage, multi-effect CardFamily, naming. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;

private:
	/** Recompute OfferRarities from the first magnitude-bearing effect's tiers (IsDataValid enforces the rest match). */
	void RefreshOfferRarities();
#endif
};
