// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Weapon/FPSRWeaponDataAsset.h"

class UFPSRWeaponDataAsset;
class UFPSRWeaponFragment;
struct FFPSRWeaponStatBlock;

namespace FPSRWeaponPartSelector
{
	/** Deterministic, pure. OutSelected = all slotless structural WeaponParts1P (in DA order) + the winning rule Part
	 *  for each slot (slots iterated in lexical order). Winner per slot = met rules ranked Tier↓, Priority↓, ruleIndex↑
	 *  (ruleIndex = global index into Weapon.PartRules → unique → total order). Null Condition = Always. */
	FPSROGUELITE_API void SelectParts(const UFPSRWeaponDataAsset& Weapon,
		const FFPSRWeaponStatBlock& Resolved,
		const TArray<TObjectPtr<UFPSRWeaponFragment>>& Fragments,
		TArray<FFPSRWeaponPartAttachment>& OutSelected);

	/** Order-stable hash of the selected set (soft-object paths, in OutSelected order) for churn-diffing. Machine-
	 *  local use only (never replicated). Collisions are cosmetic (equip does a full rebuild). */
	FPSROGUELITE_API uint32 ComputeSignature(const TArray<FFPSRWeaponPartAttachment>& Selected);
}
