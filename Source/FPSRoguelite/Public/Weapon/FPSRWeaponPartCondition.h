// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Weapon/FPSRWeaponTypes.h"
#include "GameplayTagContainer.h"
#include "FPSRWeaponPartCondition.generated.h"

class UFPSRWeaponFragment;
struct FFPSRWeaponStatBlock;

/** Polymorphic, read-only predicate that selects a weapon part rule (W-U1, plan v3 §2-B). Evaluated against the
 *  weapon's RESOLVED stats + active behavior fragments ONLY — never reads/writes cards, save, or replicated state
 *  (§2-A isolation contract). New condition type = one subclass, zero central edits (mirrors UFPSRCardEffect). */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories)
class FPSROGUELITE_API UFPSRWeaponPartCondition : public UObject
{
	GENERATED_BODY()
public:
	virtual bool IsMet(const FFPSRWeaponStatBlock& Stats,
		const TArray<TObjectPtr<UFPSRWeaponFragment>>& Fragments) const
		PURE_VIRTUAL(UFPSRWeaponPartCondition::IsMet, return false;);
#if WITH_EDITOR
	/** One-line auto description for the designer (Details panel / tooling). Base = class display name. */
	virtual FText GetEditorDescription() const;
#endif
};

/** Always true — the default/base part for a slot (Tier 0). */
UCLASS(meta = (DisplayName = "Always"))
class FPSROGUELITE_API UFPSRPartCondition_Always : public UFPSRWeaponPartCondition
{
	GENERATED_BODY()
public:
	virtual bool IsMet(const FFPSRWeaponStatBlock&, const TArray<TObjectPtr<UFPSRWeaponFragment>>&) const override { return true; }
#if WITH_EDITOR
	virtual FText GetEditorDescription() const override;
#endif
};

/** True when a resolved stat axis compares against a threshold (e.g. FireRate >= 12 → long barrel). */
UCLASS(meta = (DisplayName = "Stat Threshold"))
class FPSROGUELITE_API UFPSRPartCondition_StatThreshold : public UFPSRWeaponPartCondition
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "조건", meta = (DisplayName = "스탯 축"))
	EFPSRWeaponStat Axis = EFPSRWeaponStat::FireRate;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "조건", meta = (DisplayName = "비교"))
	EFPSRStatCompare Cmp = EFPSRStatCompare::GreaterOrEqual;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "조건", meta = (DisplayName = "기준값"))
	float Value = 0.0f;
	virtual bool IsMet(const FFPSRWeaponStatBlock& Stats, const TArray<TObjectPtr<UFPSRWeaponFragment>>&) const override;
#if WITH_EDITOR
	virtual FText GetEditorDescription() const override;
#endif
};

/** True when the weapon holds >= MinStacks copies of a specific behavior fragment (identity = ASSET POINTER,
 *  matching UFPSRWeaponInstance::HasFragment — NOT the optional FragmentTag). */
UCLASS(meta = (DisplayName = "Has Fragment"))
class FPSROGUELITE_API UFPSRPartCondition_HasFragment : public UFPSRWeaponPartCondition
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "조건", meta = (DisplayName = "프래그먼트"))
	TObjectPtr<UFPSRWeaponFragment> Fragment = nullptr;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "조건", meta = (DisplayName = "최소 스택", ClampMin = "1"))
	int32 MinStacks = 1;
	virtual bool IsMet(const FFPSRWeaponStatBlock&, const TArray<TObjectPtr<UFPSRWeaponFragment>>& Fragments) const override;
#if WITH_EDITOR
	virtual FText GetEditorDescription() const override;
#endif
};
