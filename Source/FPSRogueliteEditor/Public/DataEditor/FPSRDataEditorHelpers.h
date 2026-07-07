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

/** Result of a card-route wiring preflight (FFPSRDataEditorHelpers::CheckCardRoute). H2 = routing ambiguity is
 *  resolved by hard error, not a warning — a route is either eligible (Allowed) or it isn't (Blocked). */
enum class EFPSRWiringVerdict : uint8
{
	Allowed, // Route is one of the card's eligible routes (GetCardEligibleRoutes).
	Blocked  // Route is not in the card's eligible set at all — placing the card there would be a silent no-op
	         // at draw/apply time (or worse, a semantically wrong offer).
};

/** Bulk magnitude arithmetic op for the Data Editor's magnitude grid toolbar (P2). */
enum class EFPSRBulkMagnitudeOp : uint8 { Multiply, Add };

/** One target cell for a bulk magnitude op: a (card, effect, rarity) triple. */
struct FFPSRMagnitudeCellRef
{
	TWeakObjectPtr<UFPSRCardDataAsset> Card;
	int32 EffectIndex = INDEX_NONE;
	ECardRarity Rarity = ECardRarity::Common;
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

	/** Preflight for placing Card into Route: Blocked if Route not in the card's eligible set (GetCardEligibleRoutes) —
	 *  including the case where the eligible set itself is empty (a multi-effect card whose effects share no common
	 *  route); else Allowed. OutReason set for Blocked (empty for Allowed). */
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

	/** Set a schedule window's [MinTime,MaxTime] (transactional, drives the timeline bar's drag-to-edit). Clamps
	 *  NewMinTime >= 0 and NewMaxTime >= NewMinTime (mirrors FFPSRMissionWindow's own ClampMin=0.0 meta + the
	 *  MinTime<=MaxTime contract documented on the struct). No-op (false) on null Schedule/invalid WindowIndex, or
	 *  when both clamped values already match the window's current MinTime/MaxTime (nothing to write). */
	static bool SetScheduleWindowTime(UFPSRRunScheduleDataAsset* Schedule, int32 WindowIndex, float NewMinTime, float NewMaxTime);

	// --- Misc --------------------------------------------------------------------------------------------------

	/** Map a route to its display text (drives combo-box labels in the guided-add UI). Small closed table — the
	 *  route axis is a closed C++ enum by design (see EFPSRCardRoute), so a switch here is the correct shape. */
	static FText GetRouteDisplayText(EFPSRCardRoute Route);

	/** Magnitude write: transaction-wrap + call Card->SetEffectRarityMagnitude. Returns true if a tier existed and
	 *  was written (see UFPSRCardDataAsset::SetEffectRarityMagnitude — does not create a new tier). */
	static bool SetCardEffectMagnitude(UFPSRCardDataAsset* Card, int32 EffectIndex, ECardRarity Rarity, float NewMagnitude);

	/** Transaction-wrap + Card->CreateEffectRarityTier. Returns true if a tier was added. */
	static bool CreateCardOfferRarity(UFPSRCardDataAsset* Card, ECardRarity Rarity);

	/** Transaction-wrap + Card->DeleteEffectRarityTier. Returns true if a tier was removed (false if refused/no-op). */
	static bool DeleteCardOfferRarity(UFPSRCardDataAsset* Card, ECardRarity Rarity);

	/** Apply Op(Operand) to each cell's EXISTING tier under ONE transaction.
	 *  Multiply: raw *= Operand (unit-agnostic, any selection).
	 *  Add: unit-sensitive. All affected effects must share the same GetEditorMagnitudeUnit() (Percent OR Flat); a mixed
	 *   selection is REFUSED (returns 0, OutStatus explains, no change). Operand is in DISPLAY unit: Percent set ->
	 *   raw += Operand/100 ("+5" means +5 percentage points); Flat set -> raw += Operand.
	 *  Skips cells whose effect is null / unit==None / has no tier for that rarity. Returns number of cells changed. */
	static int32 BulkApplyMagnitude(const TArray<FFPSRMagnitudeCellRef>& Cells, EFPSRBulkMagnitudeOp Op, float Operand, FText& OutStatus);

	/** Set the card's Group (transactional). Makes a card wired into a weapon's level-up pool correct-by-construction:
	 *  GatherCandidatePool only sets the draw's TargetWeapon when Group == Weapon (FPSRCardSubsystem.cpp:603), so a
	 *  Character-group card in Weapon.WeaponCards would apply to the equipped weapon instead of its source. No-op (false)
	 *  if Card is null or already NewGroup. */
	static bool SetCardGroup(UFPSRCardDataAsset* Card, ECardGroup NewGroup);

	/** Save the given (already dirty-tracked) packages via UEditorLoadingAndSavingUtils-equivalent (FEditorFileUtils::
	 *  SavePackages, bOnlyDirty=true, no dialog prompt) so the AssetRegistry re-scans and FFPSRAnchoredValidationService
	 *  recomputes orphans/reachability against the saved-on-disk state. Returns the number of packages actually saved. */
	static int32 SavePackages(const TArray<UPackage*>& Packages);
};
