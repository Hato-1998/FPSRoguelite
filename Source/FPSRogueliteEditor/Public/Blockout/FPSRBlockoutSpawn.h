// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

class AActor;
class UWorld;

/**
 * Shared spawn recipe for the blockout tool (P1a de-dup) — the ONE place that knows how to turn a palette FAssetData
 * (UStaticMesh or actor Blueprint) into a placed AActor. Previously copy-pasted 3x across
 * UFPSRBlockoutPlacementMode::RebuildGhost / SpawnAtCurrent and SFPSRBlockoutTab::PlaceAsset. Handles both asset kinds
 * (BP = as-is spawn from GeneratedClass, no collision auto-mod; mesh = AStaticMeshActor + WorldStatic "BlockAll" mesh
 * collision) and both spawn kinds (transient editor-preview ghost vs a real placed piece). Does NOT open an
 * FScopedTransaction itself — the caller owns undo scoping, because the ghost path must stay untransacted while a
 * real placement must not.
 */
class FFPSRBlockoutSpawn
{
public:
	/** Spawns Asset (mesh or actor BP) at Transform in World.
	 *  bTransientGhost=true  → RF_Transient + bTemporaryEditorActor (excluded outliner/save), bIsEditorPreviewActor=true,
	 *                          collision disabled, no label/folder — mirrors the old ghost.
	 *  bTransientGhost=false → RF_Transactional, mesh gets WorldStatic "BlockAll" collision (K4=B / K14 guardrail),
	 *                          actor gets Modify() + asset-name label + "Blockout" outliner folder — mirrors the old
	 *                          real-placement spawn.
	 *  Returns nullptr if World/Asset is invalid or the asset fails to load/cast (BP not an AActor subclass, etc). */
	static AActor* SpawnPiece(UWorld* World, const FAssetData& Asset, const FTransform& Transform, bool bTransientGhost);
};
