// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FPSRCardTypes.generated.h"

class UFPSRCardDataAsset;

UENUM(BlueprintType)
enum class ECardScope : uint8
{
	Character   UMETA(DisplayName = "Character"),
	ThisWeapon  UMETA(DisplayName = "This Weapon"),
	AllWeapons  UMETA(DisplayName = "All Weapons")
};

UENUM(BlueprintType)
enum class ECardRarity : uint8
{
	Common    UMETA(DisplayName = "Common"),
	Rare      UMETA(DisplayName = "Rare"),
	Epic      UMETA(DisplayName = "Epic"),
	Legendary UMETA(DisplayName = "Legendary")
};

/** One rarity variant of a card: the rarity it can roll at and the SetByCaller magnitude applied at that rarity.
 *  A card may define several tiers so it can be offered at any of them (e.g. MaxHealth +15 Common .. +100 Legendary). */
USTRUCT(BlueprintType)
struct FFPSRCardRarityTier
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	ECardRarity Rarity = ECardRarity::Common;

	/** Magnitude injected into the card's AppliedEffect via SetByCaller (tag SetByCaller.CardMagnitude). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	float Magnitude = 0.0f;
};

/** A single drawn card offer: the card, the rarity it rolled at, and the magnitude to apply on selection. */
USTRUCT(BlueprintType)
struct FFPSRCardDraw
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Card")
	TObjectPtr<UFPSRCardDataAsset> Card = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Card")
	ECardRarity Rarity = ECardRarity::Common;

	UPROPERTY(BlueprintReadOnly, Category = "Card")
	float Magnitude = 0.0f;
};
