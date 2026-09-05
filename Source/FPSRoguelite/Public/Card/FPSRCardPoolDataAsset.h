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

	/** Build synergy (CRIT2, §2-3-9): mission/unlock-pool draw weight multiplier per card already held that shares a
	 *  BuildTag with the candidate, added per stack up to SynergyMaxStacks. **Mission/unlock pool only** — the
	 *  level-up (stat) pool stays uniform by user decision, so this never reaches `GetEffectiveWeight`.
	 *  0.5/4 is a tuning STARTING POINT (4 stacks -> 3x), not a final value — PIE playtest sets the real number.
	 *  ClampMin=0 (P2-1 머지 게이트) guards the editor slider only — a serialized asset or script write can still
	 *  land a negative value under it, so IsDataValid hard-errors on that case too (a negative value here collapses
	 *  GetUnlockDrawWeight toward <=0, turning the mission-pool draw into a deterministic pick instead of a weighted
	 *  one). */
	UPROPERTY(EditDefaultsOnly, Category = "Card Pool|Synergy", meta = (ClampMin = "0.0"))
	float SynergyBonusPerCard = 0.5f;

	/** Stack cap for the build-synergy bonus above (CRIT2). ClampMin=0 for the same reason as SynergyBonusPerCard
	 *  above (P2-1) — IsDataValid is the actual hard gate; this only stops the slider. */
	UPROPERTY(EditDefaultsOnly, Category = "Card Pool|Synergy", meta = (ClampMin = "0"))
	int32 SynergyMaxStacks = 4;

	/** Closed vocabulary of BuildTags a card may declare (CRIT2 §11-3, 안 A — typo guard: FPSRCardPoolValidator
	 *  errors on any card's BuildTag that isn't in this list). Adding a new build = append a tag here + author it
	 *  onto the relevant cards' BuildTags in the sheet; no code change. */
	UPROPERTY(EditDefaultsOnly, Category = "Card Pool|Synergy")
	TArray<FName> BuildTagVocabulary;

	/** Returns the base weight for the given rarity. */
	UFUNCTION(BlueprintPure, Category = "Card Pool")
	float GetRarityBaseWeight(ECardRarity Rarity) const;

	/** Returns the luck bonus per rarity for the given rarity. */
	UFUNCTION(BlueprintPure, Category = "Card Pool")
	float GetLuckPerRarity(ECardRarity Rarity) const;

#if WITH_EDITOR
	/** Editor validation: no two cards in this pool share a CardId (U10 — duplicate keys alias unlocks in the save). */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
