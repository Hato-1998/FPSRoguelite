// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPath.h"   // FDirectoryPath
#include "FPSRBlockoutSettings.generated.h"

class UMaterialInterface;

/**
 * Blockout tool configuration (editor-only). Holds the roster of content folders the FPSR Blockout palette scans for
 * placeable modular pieces (Slice ① backbone; the actual scan lands in Slice ②). Config = Editor + DefaultConfig so
 * the roster lives in Config/DefaultEditor.ini — checked in and shared by all designers, and (per decision K1) new
 * palette folders are added here in Project Settings with NO C++ rebuild. Editor module only: nothing here is read at
 * runtime. Editable in Project Settings > FPSR > FPSR Blockout.
 */
UCLASS(Config = Editor, DefaultConfig, meta = (DisplayName = "FPSR Blockout"))
class UFPSRBlockoutSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UFPSRBlockoutSettings();

	/** Groups this under the "FPSR" section in Project Settings, alongside the runtime FPSR settings. */
	virtual FName GetCategoryName() const override;

	/** Content folders the palette scans for placeable modular meshes/actors. Add folders here (no rebuild) to make a
	 *  new Synty pack — CyberCity / Nature / Space — available in the blockout palette. The ContentDir meta shows a
	 *  content-folder picker; picked values are stored as /Game/... long package paths. The default seeds the imported
	 *  CyberCity meshes so the tool has something to show on first open; change/extend freely in Project Settings
	 *  (this is a config default, not a runtime path dependency). */
	UPROPERTY(EditAnywhere, Config, Category = "Palette", meta = (ContentDir))
	TArray<FDirectoryPath> PaletteFolders;

	/** Grid snap size (cm) for the viewport placement mode (city-builder ghost). The cursor floor-hit is snapped to
	 *  this grid in X/Y before the ghost/placement lands. 0 = no snap (free placement). */
	UPROPERTY(EditAnywhere, Config, Category = "Placement", meta = (ClampMin = "0.0"))
	float PlacementGridSize = 250.0f; // 250 = Synty CyberCity Base 키트 실측(Floor/Ceiling 250×250cm; Block 1500=6×250). 2026-07-20 GetBounds 확정. ini(DefaultEditor)가 최종 override.

	/** Rotation snap step (degrees) for the viewport placement mode's [ / ] quick-rotate keys. Applied to the ghost's
	 *  (and next spawn's) yaw. 0 is not meaningful (no-op) — keep the 90° default. */
	UPROPERTY(EditAnywhere, Config, Category = "Placement", meta = (ClampMin = "0.0"))
	float RotationSnapDegrees = 90.0f;

	/** Magnetic proximity snap radius (cm, R3b) — how close the cursor needs to be to an ALREADY-PLACED Blockout piece
	 *  for the ghost to snap onto that piece's face, even when the cursor ray itself is pointing at open floor nearby.
	 *  0 = use PlacementGridSize as the radius. Read once at mode Enter (same pattern as GridSize/RotationSnapDegrees). */
	UPROPERTY(EditAnywhere, Config, Category = "Placement", meta = (ClampMin = "0.0"))
	float SnapRadius = 0.0f;

	/** Translucent preview material (R3c) applied to every material slot of the viewport-placement ghost actor, so the
	 *  designer sees a see-through preview instead of an opaque stand-in. Unset = solid ghost fallback (the piece's own
	 *  materials, pre-R3c behavior). Editor-only, no runtime impact. */
	UPROPERTY(EditAnywhere, Config, Category = "Placement", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	TSoftObjectPtr<UMaterialInterface> GhostMaterial;

	/** Where "선택→프리팹" saves the result: a lightweight BP_<name> Blueprint (FKismetEditorUtilities::
	 *  HarvestBlueprintFromActors harvests the selected actors' components into one plain actor Blueprint — no sub-level,
	 *  unlike the earlier Packed Level Actor approach). This folder is also added to the palette's scanned paths
	 *  (RefreshPalette) so new prefabs show up as cards immediately — no need to add it to PaletteFolders by hand. */
	UPROPERTY(EditAnywhere, Config, Category = "Prefab", meta = (ContentDir))
	FDirectoryPath PrefabSaveFolder;
};
