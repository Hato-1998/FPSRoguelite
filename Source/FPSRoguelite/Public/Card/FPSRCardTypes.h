// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FPSRCardTypes.generated.h"

class UFPSRCardDataAsset;
class UFPSRWeaponDataAsset;

/** v2 card group (§2-3-2): the draw pool / trigger / UI filter a card belongs to. Orthogonal to per-effect
 *  application scope (UCardEffect_WeaponStat::bThisWeaponOnly). Character = character + all-weapons effects
 *  (no target weapon). Weapon = this-weapon stat / behavior cards (TargetWeapon set). WeaponUnlock = declared
 *  but currently unused — U18b's weapon-unlock routing ships via UFPSRCardPoolDataAsset::WeaponUnlockCards +
 *  EFPSROfferType::WeaponUnlock, not this ECardGroup value. Kept to preserve enum ordinals/serialized values;
 *  do not author cards with this group. */
UENUM(BlueprintType)
enum class ECardGroup : uint8
{
	Character    UMETA(DisplayName = "Character"),
	Weapon       UMETA(DisplayName = "Weapon"),
	WeaponUnlock UMETA(DisplayName = "Weapon Unlock")
};

/** What a presented card offer represents — drives the draw pool and the consume/gate behavior. */
UENUM(BlueprintType)
enum class EFPSROfferType : uint8
{
	OpeningSeed  UMETA(DisplayName = "Opening Seed"),  // run-start seed; applies without consuming a pick
	LevelUp      UMETA(DisplayName = "Level Up"),      // consumes a level-up pick (CardPicksPending)
	WeaponUnlock UMETA(DisplayName = "Weapon Unlock") // WeaponUnlock: requires & consumes a weapon-unlock pick; new-weapon unlock (mission clear + level milestones). Not rerollable.
};

/** Closed set of card DRAW ROUTES = which membership array a card lives in (each has different draw semantics).
 *  Closed by schema (the arrays are fixed C++ members); the OPEN axis is card effects, which declare which of
 *  these routes they permit via UFPSRCardEffect::GetEditorEligibleRoutes(). Editor-tool preflight uses this. */
UENUM()
enum class EFPSRCardRoute : uint8
{
	LevelUpGlobal             UMETA(DisplayName = "Level-Up: Global Pool (Pool.Cards)"),
	MissionClearNewWeapon     UMETA(DisplayName = "Mission-Clear: New Weapon (Pool.WeaponUnlockCards)"),
	LevelUpWeapon             UMETA(DisplayName = "Level-Up: Weapon Card (Weapon.WeaponCards)"),
	MissionClearWeaponFeature UMETA(DisplayName = "Mission-Clear: Weapon Feature (Weapon.UnlockableFeatures)")
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

/** A single drawn card offer: the card and the rarity it rolled at. Per-effect magnitude is no longer carried on
 *  the draw (v2) — each effect resolves its own magnitude from its RarityTiers at this rolled Rarity, both on the
 *  server (UFPSRCardEffect::Apply) and on the client (UI reads Card->Effects locally). */
USTRUCT(BlueprintType)
struct FFPSRCardDraw
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Card")
	TObjectPtr<UFPSRCardDataAsset> Card = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Card")
	ECardRarity Rarity = ECardRarity::Common;

	/** Weapon this offer applies to (the weapon whose pool contributed the card). null = character / all-weapons
	 *  target. Set server-side at draw time so weapon-scope cards apply to their SOURCE weapon — owned but not
	 *  necessarily equipped — instead of whatever is currently held. */
	UPROPERTY(BlueprintReadOnly, Category = "Card")
	TObjectPtr<UFPSRWeaponDataAsset> TargetWeapon = nullptr;
};
