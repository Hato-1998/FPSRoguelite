// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Card/FPSRCardTypes.h"
#include "Weapon/FPSRWeaponTypes.h"
#include "GameplayTagContainer.h"
#include "FPSRCardDataAsset.generated.h"

class UGameplayEffect;
class UFPSRWeaponFragment;

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card", meta = (EditConditionHides, EditCondition = "Scope == ECardScope::Character"))
	TSubclassOf<UGameplayEffect> AppliedEffect;

	/** Overall draw-weight multiplier for this card (applied within each rarity it offers). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	float Weight = 1.0f;

	/** Rarity variants this card can be offered at, each with its own magnitude. One entry = a single-rarity card;
	 *  several entries = the card can roll at any of those rarities (only one is offered per draw). Author the
	 *  AppliedEffect GE modifier as "Set By Caller" (tag SetByCaller.CardMagnitude) so the tier magnitude applies
	 *  (e.g. MaxHealth +15 Common .. +100 Legendary from a single GE + single card asset). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	TArray<FFPSRCardRarityTier> RarityTiers;

	/** Cards sharing a family are mutually exclusive within a single draw (only one is ever offered).
	 *  If unset (None), the AppliedEffect GE class is used as the family key instead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	FGameplayTag CardFamily;

	/** Weapon-scope only (ThisWeapon / AllWeapons): which weapon stat this card modifies. Ignored for
	 *  Character scope (those use AppliedEffect). The tier Magnitude is the modifier Value. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card|Weapon", meta = (EditConditionHides, EditCondition = "Scope == ECardScope::AllWeapons || (Scope == ECardScope::ThisWeapon && GrantedFragment == nullptr)"))
	EFPSRWeaponStat WeaponStat = EFPSRWeaponStat::FireRate;

	/** Weapon-scope only: how the modifier combines (additive flat vs percent multiply). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card|Weapon", meta = (EditConditionHides, EditCondition = "Scope == ECardScope::AllWeapons || (Scope == ECardScope::ThisWeapon && GrantedFragment == nullptr)"))
	EFPSRWeaponModOp WeaponStatOp = EFPSRWeaponModOp::PercentMultiply;

	/** ThisWeapon-scope behavior fragment (P4-B-2). When set, selecting this card grants the fragment to the
	 *  current weapon instead of applying a stat modifier. Authored as this weapon's AvailableModifiers reward. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card|Weapon", meta = (EditConditionHides, EditCondition = "Scope == ECardScope::ThisWeapon"))
	TObjectPtr<UFPSRWeaponFragment> GrantedFragment = nullptr;

#if WITH_EDITOR
	/** Editor validation: a card with no RarityTiers is never offered (errors); no AppliedEffect applies nothing (warns). */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
