// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "FPSRCardSubsystem.generated.h"

class UFPSRCardDataAsset;
class UFPSRCardPoolDataAsset;
class AController;

/** Server-authoritative card draw and application logic (P3-C).
 *  Weighted sampling from an active pool per-player luck/rarity bonus; applies GE to ASC on selection. */
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

	/** Draw Count cards for the given player, excluding any in the Exclude list (server authority only). */
	TArray<UFPSRCardDataAsset*> DrawCards(AController* ForPlayer, int32 Count = 3, const TArray<UFPSRCardDataAsset*>& Exclude = TArray<UFPSRCardDataAsset*>());

	/** Apply a card to the given player (server authority only). Returns true if applied.
	 *  When bConsumeLevelUp is true (breather level-up selection), a queued level-up is required and consumed;
	 *  when false (opening seed, §2-2), the card is applied without touching the level-up stack.
	 *  Only Character-scope cards apply for now — weapon-scope (ThisWeapon/AllWeapons) is rejected until P4. */
	bool ApplyCard(AController* ForPlayer, UFPSRCardDataAsset* Card, bool bConsumeLevelUp = true);

	/** Try to consume a reroll charge from the player. Returns true if successful. */
	bool TryReroll(AController* ForPlayer);

protected:
	/** Compute the effective weight of a card given player luck and rarity bonus. */
	float GetEffectiveWeight(const UFPSRCardDataAsset* Card, float Luck, float RarityBonus) const;

	/** Gather all candidate cards from the pool and player's owned weapons. */
	void GatherCandidatePool(AController* ForPlayer, TArray<UFPSRCardDataAsset*>& OutCandidates) const;

private:
	UPROPERTY()
	TObjectPtr<UFPSRCardPoolDataAsset> ActivePool;
};
