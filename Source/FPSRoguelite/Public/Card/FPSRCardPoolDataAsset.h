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

	UPROPERTY(EditDefaultsOnly, Category = "Card Pool|Rarity Weights")
	float CommonWeight = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Card Pool|Rarity Weights")
	float RareWeight = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Card Pool|Rarity Weights")
	float EpicWeight = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category = "Card Pool|Rarity Weights")
	float LegendaryWeight = 0.05f;

	/** How strongly the player's Luck attribute biases draws toward higher rarities. */
	UPROPERTY(EditDefaultsOnly, Category = "Card Pool|Luck")
	float LuckScale = 0.1f;

	/** Returns the base weight for the given rarity. */
	UFUNCTION(BlueprintPure, Category = "Card Pool")
	float GetRarityBaseWeight(ECardRarity Rarity) const;
};
