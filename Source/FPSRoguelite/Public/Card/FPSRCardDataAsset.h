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

	/** Stable meta-save key (U10): identifies this card in the player's save independent of the asset's file name,
	 *  so renaming the asset does not orphan a player's unlock. Authored per card. IsDataValid requires it non-empty;
	 *  the owning card pool requires it unique. The actual unlocked-cards list lands in P0-③. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card|Identity")
	FName CardId;

	/** The stable meta-save key for this card. */
	UFUNCTION(BlueprintPure, Category = "Card|Identity")
	FName GetStableKey() const { return CardId; }

	// (v1 legacy fields + PostLoad migration removed in U18a-legacy-cleanup — every card asset was re-saved to v2.)

#if WITH_EDITOR
	/** Keep OfferRarities synced with the effects' rarity tiers on load / edit. */
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Editor validation: empty Effects, per-effect ValidateEffect, rarity coverage, multi-effect CardFamily, naming. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;

	/** Editor-tool: set the magnitude of an EXISTING rarity tier on the effect at EffectIndex. Does NOT create a tier
	 *  (returns false if that effect has no tier for Rarity — P1 edits in place only). Wraps Modify() + writes the tier
	 *  + PostEditChangeProperty so OfferRarities re-derives and the package is marked dirty. The caller (Slate) owns the
	 *  FScopedTransaction. Returns true if a tier was found and written. */
	bool SetEffectRarityMagnitude(int32 EffectIndex, ECardRarity Rarity, float NewMagnitude);

	/** Editor-tool: ensure every magnitude-bearing effect (GetEditorMagnitudeUnit()!=None) offers a tier for Rarity.
	 *  Adds a tier only to effects that lack it, seeded from that effect's nearest existing tier (closest lower rarity
	 *  preferred, else closest higher, else 0). Re-derives OfferRarities. Returns true if any tier was added (false if
	 *  every magnitude effect already covers Rarity, or the card has no magnitude effect). Caller owns the transaction. */
	bool CreateEffectRarityTier(ECardRarity Rarity);

	/** Editor-tool: remove Rarity's tier from every magnitude-bearing effect. REFUSES (returns false, no change) if
	 *  Rarity is the card's only offered rarity (a card must offer at least one). Re-derives OfferRarities. Returns true
	 *  if any tier was removed. Caller owns the transaction. */
	bool DeleteEffectRarityTier(ECardRarity Rarity);

private:
	/** Recompute OfferRarities from the first magnitude-bearing effect's tiers (IsDataValid enforces the rest match). */
	void RefreshOfferRarities();
#endif
};
