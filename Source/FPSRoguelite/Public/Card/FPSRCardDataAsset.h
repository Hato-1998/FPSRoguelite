// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Card/FPSRCardTypes.h"
#include "GameplayTagContainer.h"
#include "FPSRCardDataAsset.generated.h"

class UGameplayEffect;

/** Data-driven card definition (ability modifier, stat buff, or weapon enhancement). */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRCardDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card", meta = (MultiLine = true))
	FText Description;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	ECardScope Scope = ECardScope::Character;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	TSubclassOf<UGameplayEffect> AppliedEffect;

	/** Overall draw-weight multiplier for this card (applied within each rarity it offers). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	float Weight = 1.0f;

	/** Rarity variants this card can be offered at, each with its own magnitude. One entry = a single-rarity card;
	 *  several entries = the card can roll at any of those rarities (only one is offered per draw). Author the
	 *  AppliedEffect GE modifier as "Set By Caller" (tag SetByCaller.CardMagnitude) so the tier magnitude applies
	 *  (e.g. MaxHealth +15 Common .. +100 Legendary from a single GE + single card asset). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	TArray<FFPSRCardRarityTier> RarityTiers;

	/** Cards sharing a family are mutually exclusive within a single draw (only one is ever offered).
	 *  If unset (None), the AppliedEffect GE class is used as the family key instead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	FGameplayTag CardFamily;

#if WITH_EDITOR
	/** Editor validation: a card with no RarityTiers is never offered (errors); no AppliedEffect applies nothing (warns). */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
