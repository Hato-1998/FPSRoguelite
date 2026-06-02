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
	ECardRarity Rarity = ECardRarity::Common;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	TSubclassOf<UGameplayEffect> AppliedEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	float Weight = 1.0f;

	/** Per-card SetByCaller magnitude injected into AppliedEffect. Author the GE's modifier as
	 *  "Set By Caller" with tag SetByCaller.CardMagnitude so one GE can be reused at different
	 *  values per rarity (e.g. MaxHealth +15 Common vs +100 Legendary). Ignored by fixed-magnitude
	 *  GEs that do not reference the tag. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	float Magnitude = 0.0f;

	/** Cards sharing a family are mutually exclusive within a single draw (only one is ever offered).
	 *  If unset (None), the AppliedEffect GE class is used as the family key instead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	FGameplayTag CardFamily;
};
