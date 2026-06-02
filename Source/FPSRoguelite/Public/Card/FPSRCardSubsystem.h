// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Card/FPSRCardTypes.h"
#include "FPSRCardSubsystem.generated.h"

class UFPSRCardDataAsset;
class UFPSRCardPoolDataAsset;
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

	/** Apply a drawn card offer to the given player (server authority only). Returns true if applied.
	 *  bConsumeLevelUp=true (breather level-up selection) requires & consumes a queued level-up;
	 *  false (opening seed, §2-2) applies without touching the level-up stack.
	 *  Only Character-scope cards apply — weapon-scope (ThisWeapon/AllWeapons) is rejected until P4. */
	bool ApplyCard(AController* ForPlayer, const FFPSRCardDraw& Draw, bool bConsumeLevelUp = true);

	/** Try to consume a reroll charge from the player. Returns true if successful. */
	bool TryReroll(AController* ForPlayer);

protected:
	/** Effective draw weight of a card at a specific rarity, given player luck. */
	float GetEffectiveWeight(const UFPSRCardDataAsset* Card, ECardRarity Rarity, float Luck) const;

	/** Gather all candidate cards from the pool and player's owned weapons. */
	void GatherCandidatePool(AController* ForPlayer, TArray<UFPSRCardDataAsset*>& OutCandidates) const;

private:
	UPROPERTY()
	TObjectPtr<UFPSRCardPoolDataAsset> ActivePool;
};
