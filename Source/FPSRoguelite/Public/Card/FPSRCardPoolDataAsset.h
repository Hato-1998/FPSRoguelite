// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Card/FPSRCardTypes.h"
#include "FPSRCardPoolDataAsset.generated.h"

class UFPSRCardDataAsset;

/** Weighted pool of available cards (P3-C data). Tuning for rarity distribution and luck scaling. */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRCardPoolDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card Pool")
	TArray<TObjectPtr<UFPSRCardDataAsset>> Cards;

	/** Weapon-unlock cards (U18b): each grants a brand-new weapon (UCardEffect_GrantWeapon). Drawn for the
	 *  WeaponUnlock offer on mission clear + level milestones. Separate from the level-up `Cards` pool. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card Pool")
	TArray<TObjectPtr<UFPSRCardDataAsset>> WeaponUnlockCards;

	UPROPERTY(EditDefaultsOnly, Category = "Card Pool|Rarity Weights")
	float CommonWeight = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Card Pool|Rarity Weights")
	float RareWeight = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Card Pool|Rarity Weights")
	float EpicWeight = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category = "Card Pool|Rarity Weights")
	float LegendaryWeight = 0.05f;

	/** Per Luck point, added to Common rarity's selection weight. */
	UPROPERTY(EditDefaultsOnly, Category = "Card Pool|Luck")
	float LuckPerRarity_Common = 0.0f;

	/** Per Luck point, added to Rare rarity's selection weight. */
	UPROPERTY(EditDefaultsOnly, Category = "Card Pool|Luck")
	float LuckPerRarity_Rare = 0.03f;

	/** Per Luck point, added to Epic rarity's selection weight. */
	UPROPERTY(EditDefaultsOnly, Category = "Card Pool|Luck")
	float LuckPerRarity_Epic = 0.02f;

	/** Per Luck point, added to Legendary rarity's selection weight. */
	UPROPERTY(EditDefaultsOnly, Category = "Card Pool|Luck")
	float LuckPerRarity_Legendary = 0.01f;

	/** Returns the base weight for the given rarity. */
	UFUNCTION(BlueprintPure, Category = "Card Pool")
	float GetRarityBaseWeight(ECardRarity Rarity) const;

	/** Returns the luck bonus per rarity for the given rarity. */
	UFUNCTION(BlueprintPure, Category = "Card Pool")
	float GetLuckPerRarity(ECardRarity Rarity) const;
};
