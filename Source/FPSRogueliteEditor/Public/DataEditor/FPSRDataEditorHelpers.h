// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Card/FPSRCardTypes.h"

class UFPSRCardDataAsset;
class UFPSRCardPoolDataAsset;
class UFPSRWeaponDataAsset;
class UFPSRLoadoutPoolDataAsset;
class UFPSRRunScheduleDataAsset;
class UFPSRMissionDataAsset;
class UPackage;

/** Result of a card-route wiring preflight (FFPSRDataEditorHelpers::CheckCardRoute). */
enum class EFPSRWiringVerdict : uint8
{
	Allowed, // Route is one of the card's eligible routes (GetCardEligibleRoutes) with no caveats.
	Warn,    // Route is technically eligible but not the recommended one (currently only the WeaponBehavior
	         // H2-ambiguous LevelUpWeapon choice — MissionClearWeaponFeature is recommended instead).
	Blocked  // Route is not in the card's eligible set at all — placing the card there would be a silent no-op
	         // at draw/apply time (or worse, a semantically wrong offer).
};

/**
 * Plain static helper class (not a UObject) backing the FPSR Data Editor Slate tool (P1). Mirrors the style of
 * FFPSRAnchoredValidationService: no lifetime to manage, callable from the widget, a future commandlet, or tests.
 *
 * OCP note: GetCardEligibleRoutes below does NOT switch on effect type — it purely intersects each effect's own
 * UFPSRCardEffect::GetEditorEligibleRoutes() override. A brand-new effect subclass surfaces correctly here with
 * zero edits to this file, as long as it overrides GetEditorEligibleRoutes (the base returns empty = ineligible
 * everywhere, which is the conservative default for an effect nobody has yet declared routing for).
 */
class FPSROGUELITEEDITOR_API FFPSRDataEditorHelpers
{
public:
	// --- Routing preflight -----------------------------------------------------------------------------------

	/** Card-level eligible routes = INTERSECTION of each effect's GetEditorEligibleRoutes(). A card with no effects,
	 *  or effects that share no route, yields empty (nothing eligible). No effect-type switch here — pure iteration. */
	static TArray<EFPSRCardRoute> GetCardEligibleRoutes(const UFPSRCardDataAsset* Card);

	/** Preflight for placing Card into Route: Blocked if Route not in eligible set; Warn for the H2-ambiguous
	 *  WeaponBehavior LevelUpWeapon choice (recommended = MissionClearWeaponFeature); else Allowed. OutReason set
	 *  for Warn/Blocked (empty for Allowed). */
	static EFPSRWiringVerdict CheckCardRoute(const UFPSRCardDataAsset* Card, EFPSRCardRoute Route, FText& OutReason);

	// --- Membership add/remove (each type-safe, transaction-wrapped, reuses PostEditChangeProperty so the engine's
	//     own dirty/undo/redo machinery stays correct — no hand-rolled property editing). Returns true on change;
	//     false on null owner/element, duplicate add, or missing-on-remove (no-op). ---------------------------

	static bool AddCardToPool(UFPSRCardPoolDataAsset* Pool, UFPSRCardDataAsset* Card, bool bUnlockArray); // bUnlockArray -> WeaponUnlockCards else Cards
	static bool RemoveCardFromPool(UFPSRCardPoolDataAsset* Pool, UFPSRCardDataAsset* Card, bool bUnlockArray);

	static bool AddCardToWeapon(UFPSRWeaponDataAsset* Weapon, UFPSRCardDataAsset* Card, bool bUnlockableFeatures); // bUnlockableFeatures -> UnlockableFeatures else WeaponCards
	static bool RemoveCardFromWeapon(UFPSRWeaponDataAsset* Weapon, UFPSRCardDataAsset* Card, bool bUnlockableFeatures);

	static bool AddWeaponToLoadout(UFPSRLoadoutPoolDataAsset* Loadout, UFPSRWeaponDataAsset* Weapon);
	static bool RemoveWeaponFromLoadout(UFPSRLoadoutPoolDataAsset* Loadout, UFPSRWeaponDataAsset* Weapon);

	static bool AddMissionToScheduleWindow(UFPSRRunScheduleDataAsset* Schedule, int32 WindowIndex, UFPSRMissionDataAsset* Mission);
	static bool RemoveMissionFromScheduleWindow(UFPSRRunScheduleDataAsset* Schedule, int32 WindowIndex, UFPSRMissionDataAsset* Mission);

	// --- Misc --------------------------------------------------------------------------------------------------

	/** Map a route to its display text (drives combo-box labels in the guided-add UI). Small closed table — the
	 *  route axis is a closed C++ enum by design (see EFPSRCardRoute), so a switch here is the correct shape. */
	static FText GetRouteDisplayText(EFPSRCardRoute Route);

	/** Magnitude write: transaction-wrap + call Card->SetEffectRarityMagnitude. Returns true if a tier existed and
	 *  was written (see UFPSRCardDataAsset::SetEffectRarityMagnitude — does not create a new tier). */
	static bool SetCardEffectMagnitude(UFPSRCardDataAsset* Card, int32 EffectIndex, ECardRarity Rarity, float NewMagnitude);

	/** Save the given (already dirty-tracked) packages via UEditorLoadingAndSavingUtils-equivalent (FEditorFileUtils::
	 *  SavePackages, bOnlyDirty=true, no dialog prompt) so the AssetRegistry re-scans and FFPSRAnchoredValidationService
	 *  recomputes orphans/reachability against the saved-on-disk state. Returns the number of packages actually saved. */
	static int32 SavePackages(const TArray<UPackage*>& Packages);
};
