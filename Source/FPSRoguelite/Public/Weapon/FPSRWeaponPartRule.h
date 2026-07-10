// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "FPSRWeaponPartRule.generated.h"

class UFPSRWeaponPartCondition;

/** One modular weapon-part selection rule (W-U1). A rule competes for a Slot; when its Condition is met (null =
 *  Always), its Part becomes a candidate. The selector picks the winner per slot (Tier↓ then Priority↓ then rule
 *  index↑). Polymorphic Instanced UObject (mirrors UFPSRCardEffect) — held directly by the weapon DA as an Instanced
 *  array, NOT nested in a struct. Read-only consumer of stats/fragments (§2-A). */
UCLASS(EditInlineNew, DefaultToInstanced, CollapseCategories)
class FPSROGUELITE_API UFPSRWeaponPartRule : public UObject
{
	GENERATED_BODY()
public:
	/** Mutual-exclusion key. MUST be non-empty (IsDataValid errors otherwise). Only one part per slot is attached. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "규칙", meta = (DisplayName = "슬롯"))
	FGameplayTag Slot;

	/** Visual part attached when this rule wins its slot (shared-origin: Socket=None + identity Offset). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "규칙", meta = (DisplayName = "파츠"))
	FFPSRWeaponPartAttachment Part;

	/** Higher tier wins within a slot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "규칙", meta = (DisplayName = "티어"))
	int32 Tier = 0;

	/** Tie-break within equal tier (higher wins). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "규칙", meta = (DisplayName = "우선순위"))
	int32 Priority = 0;

	/** Selection predicate; null = Always (a Tier-0 default). */
	UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category = "규칙", meta = (DisplayName = "조건"))
	TObjectPtr<UFPSRWeaponPartCondition> Condition;
};
