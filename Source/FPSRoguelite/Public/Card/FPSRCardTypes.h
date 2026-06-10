// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FPSRCardTypes.generated.h"

class UFPSRCardDataAsset;
class UFPSRWeaponDataAsset;

UENUM(BlueprintType)
enum class ECardScope : uint8
{
	Character   UMETA(DisplayName = "Character"),
	ThisWeapon  UMETA(DisplayName = "This Weapon"),
	AllWeapons  UMETA(DisplayName = "All Weapons")
};

/** What a presented card offer represents — drives the draw pool and the consume/gate behavior. */
UENUM(BlueprintType)
enum class EFPSROfferType : uint8
{
	OpeningSeed  UMETA(DisplayName = "Opening Seed"),  // run-start seed; applies without consuming a pick
	LevelUp      UMETA(DisplayName = "Level Up"),      // consumes a level-up pick (CardPicksPending)
	MissionReward UMETA(DisplayName = "Mission Reward") // consumes a mission-reward pick; weapon-modifier card
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

	/** Weapon this offer applies to (the weapon whose pool contributed the card). null = character / all-weapons
	 *  target. Set server-side at draw time so weapon-scope cards apply to their SOURCE weapon — owned but not
	 *  necessarily equipped — instead of whatever is currently held. */
	UPROPERTY(BlueprintReadOnly, Category = "Card")
	TObjectPtr<UFPSRWeaponDataAsset> TargetWeapon = nullptr;
};
