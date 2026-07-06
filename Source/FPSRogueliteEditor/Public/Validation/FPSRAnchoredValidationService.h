// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

/**
 * Anchored-validation discovery (P0 data-validation seam). "Anchors" are the DataAssets a run actually reaches at
 * runtime (card pool / run schedule / loadout pool) — everything else (cards, weapons, missions, fragments) is only
 * meaningful if it's reachable FROM one of those, so orphaned content (abandoned drafts, renamed-off assets) doesn't
 * need to gate a build. Plain static helper (not a UObject) so it can be called from a validator, the commandlet, or
 * a future editor tool without any subsystem lifetime to manage.
 */
class FFPSRAnchoredValidationService
{
public:
	/** All assets of the anchor classes (UFPSRCardPoolDataAsset / UFPSRRunScheduleDataAsset /
	 *  UFPSRLoadoutPoolDataAsset), excluding designer scratch space. Discovery only — does not load anything. */
	static TArray<FAssetData> FindAnchorAssets();

	/** Anchors plus every leaf asset (card / weapon / mission / fragment) reachable from an anchor by following
	 *  on-disk package dependencies. This is the validate set for the commandlet / Tools menu entry: it covers
	 *  everything a run can touch without forcing every stray leftover draft in Content/ to be valid. */
	static TArray<FAssetData> GatherAssetsToValidate();

	/** Leaf-type assets (card / weapon / mission / fragment, excluding designer scratch space) that are NOT
	 *  reachable from any anchor. Warning-only signal for designers (dead content), never fails a build. */
	static TArray<FAssetData> FindOrphans();

private:
	/** True if PackagePath sits under an excluded designer/test/scratch root — /Game/Developers, /Game/Test, or any
	 *  path containing "_Scratch" (case-insensitive). Anchors and leaves alike are excluded from discovery here so a
	 *  designer's private sandbox never blocks CI. */
	static bool IsExcludedPath(FName PackagePath);

	/** All on-disk assets of the leaf content types (Card / Weapon / Mission / Fragment), excluding scratch roots.
	 *  Shared by GatherAssetsToValidate (reachable subset) and FindOrphans (unreachable subset). */
	static TArray<FAssetData> FindLeafCandidates();
};
