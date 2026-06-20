// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Card/FPSRCardTypes.h"
#include "FPSRCardSubsystem.generated.h"

class UFPSRCardDataAsset;
class UFPSRCardPoolDataAsset;
class UFPSRWeaponDataAsset;
class AController;

/** Server-authoritative card draw and application logic (P3-C).
 *  Cards define per-rarity magnitude tiers; the draw rolls a rarity (weighted by rarity base weight + player
 *  Luck) and returns offers carrying the rolled rarity + magnitude. Selection applies a GE to the ASC. */
UCLASS()
class FPSROGUELITE_API UFPSRCardSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** Set the active card pool for this world. */
	void SetActivePool(UFPSRCardPoolDataAsset* InPool) { ActivePool = InPool; }

	/** Get the active card pool. */
	UFPSRCardPoolDataAsset* GetActivePool() const { return ActivePool; }

	/** Draw Count card offers for the given player, excluding any whose card is in the Exclude list
	 *  (server authority only). Each offer carries the rolled rarity and the magnitude to apply. */
	TArray<FFPSRCardDraw> DrawCards(AController* ForPlayer, int32 Count = 3, const TArray<UFPSRCardDataAsset*>& Exclude = TArray<UFPSRCardDataAsset*>());

	/** Apply a selected card offer to the given player (server authority only). Returns true if the selection
	 *  was accepted (so the offer flow can advance / unfreeze). Consume behavior depends on OfferType:
	 *   - OpeningSeed: applies, consumes nothing.
	 *   - LevelUp: requires & consumes a level-up pick (CardPicksPending).
	 *   - WeaponUnlock: requires & consumes a weapon-unlock pick (WeaponUnlockPicksPending).
	 *  Character-scope cards apply their GE now; weapon-scope (modifier) cards are accepted/consumed but their
	 *  effect application lands in P4-B (logged no-op here) so the freeze can never soft-lock. */
	bool ApplyCard(AController* ForPlayer, const FFPSRCardDraw& Draw, EFPSROfferType OfferType);

	/** Build a single card draw from one card (used for mission-reward offers), rolling a rarity tier by
	 *  the player's luck. Returns an offer with a null Card if the card has no tiers. */
	FFPSRCardDraw BuildSingleDraw(UFPSRCardDataAsset* Card, AController* ForPlayer) const;

	/** Build a WeaponUnlock offer: new-weapon candidates from the pool's WeaponUnlockCards (gated on free slot +
	 *  not-already-owned, de-duped by granted weapon). U18b. (U18b2 adds feature-unlock candidates.) */
	TArray<FFPSRCardDraw> DrawWeaponUnlockOffer(AController* ForPlayer, int32 Count = 3);

	/** Try to consume a reroll charge from the player. Returns true if successful. */
	bool TryReroll(AController* ForPlayer);

protected:
	/** Effective draw weight of a card at a specific rarity, given player luck. */
	float GetEffectiveWeight(const UFPSRCardDataAsset* Card, ECardRarity Rarity, float Luck) const;

	/** Gather candidate cards: the central pool (character / all-weapons) plus every owned weapon's WeaponCards.
	 *  OutSourceWeapons is index-aligned with OutCandidates — the weapon that contributed each card (null for the
	 *  central pool / character cards), used to set FFPSRCardDraw::TargetWeapon. */
	void GatherCandidatePool(AController* ForPlayer, TArray<UFPSRCardDataAsset*>& OutCandidates, TArray<UFPSRWeaponDataAsset*>& OutSourceWeapons) const;

private:
	UPROPERTY()
	TObjectPtr<UFPSRCardPoolDataAsset> ActivePool;
};
