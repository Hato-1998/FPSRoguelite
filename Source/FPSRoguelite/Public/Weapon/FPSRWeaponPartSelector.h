// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Weapon/FPSRWeaponDataAsset.h"

class UFPSRWeaponDataAsset;
class UFPSRWeaponFragment;
struct FFPSRWeaponStatBlock;

namespace FPSRWeaponPartSelector
{
	/** Deterministic, pure. OutSelected = one resolved FFPSRWeaponPartAttachment per WeaponParts1P slot, in DA order
	 *  (W-U1b 재설계 — 폴리모픽 규칙 폐기, 파츠별 스택 진화). For each slot: winner = base Part (stage 0) unless the
	 *  slot's EvolutionFragment is held and a Stage's MinStacks is met, in which case the HIGHEST met MinStacks stage's
	 *  Mesh/Offset/Scope replaces it — the slot's Socket (fixed mount) never changes. No EvolutionFragment = always
	 *  the base Part (pure structural slot). */
	FPSROGUELITE_API void SelectParts(const UFPSRWeaponDataAsset& Weapon,
		const FFPSRWeaponStatBlock& Resolved,
		const TArray<TObjectPtr<UFPSRWeaponFragment>>& Fragments,
		TArray<FFPSRWeaponPartAttachment>& OutSelected);

	/** Order-stable hash of the selected set (soft-object paths, in OutSelected order) for churn-diffing. Machine-
	 *  local use only (never replicated). Collisions are cosmetic (equip does a full rebuild). */
	FPSROGUELITE_API uint32 ComputeSignature(const TArray<FFPSRWeaponPartAttachment>& Selected);
}
