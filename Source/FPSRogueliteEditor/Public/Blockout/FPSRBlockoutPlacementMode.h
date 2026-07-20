// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/LegacyEdModeWidgetHelpers.h"   // UBaseLegacyWidgetEdMode (implements the widget-interface pure-virtuals)
#include "Tools/Modes.h"                        // FEditorModeInfo / FEditorModeID
#include "AssetRegistry/AssetData.h"
#include "FPSRBlockoutPlacementMode.generated.h"

class AActor;
class FEditorViewportClient;
class FViewport;
class FSceneView;
class FPrimitiveDrawInterface;
struct FViewportClick;
class HHitProxy;

/**
 * City-builder style viewport placement mode for the blockout tool (the K8 "cursor raycast + grid + ghost" follow-up).
 * Activated from the blockout tab's "뷰포트 배치" button, which also calls SetAssetToPlace with the selected card's asset.
 * While active: a manual line trace along the cursor ray finds the pointed SURFACE (hit actor + normal), the ghost is
 * placed flush against that surface and grid-snapped TANGENTIALLY so same-size pieces tile edge-to-edge Minecraft-style
 * (Synty pieces have corner/edge pivots, not center pivots — pivot-snapping alone leaves gaps/overlaps). Render draws
 * the snap box + grid, LEFT-CLICK spawns a real actor at the snapped spot (mesh = WorldStatic BlockAll, actor BP =
 * as-is), and ESC exits. Placement is repeatable (the ghost stays for the next click). A modern UEdMode (auto-registered
 * by the asset-editor subsystem — the ctor just sets Info); UBaseLegacyWidgetEdMode supplies the widget-interface
 * plumbing so this only overrides the input + render handlers it needs.
 */
UCLASS()
class UFPSRBlockoutPlacementMode : public UBaseLegacyWidgetEdMode
{
	GENERATED_BODY()

public:
	/** Mode id — used by the tab to ActivateMode / GetActiveScriptableMode. */
	static const FEditorModeID EM_BlockoutPlacement;

	UFPSRBlockoutPlacementMode();

	/** Sets which asset (mesh or actor BP) the ghost previews and left-click spawns; rebuilds the ghost immediately. */
	void SetAssetToPlace(const FAssetData& InAsset);

	/** Live grid-snap size (cm), pushed by the tab's grid input while the mode is active; 0 = free (no snap). */
	void SetGridSize(float InGridSize) { GridSize = FMath::Max(0.0f, InGridSize); }

	// --- UEdMode ----------------------------------------------------------------------------------------------------
	virtual void Enter() override;
	virtual void Exit() override;
	virtual bool UsesToolkits() const override { return false; }

	// --- Viewport input (ILegacyEdModeViewportInterface) ------------------------------------------------------------
	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) override;
	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;

	// --- Render (ILegacyEdModeWidgetInterface) ----------------------------------------------------------------------
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;

private:
	/** (Re)spawns the transient ghost actor for AssetToPlace (destroyed + rebuilt on asset change / mode enter). */
	void RebuildGhost();
	void DestroyGhost();
	/** Spawns the real (non-transient) actor at CurrentLocation inside an undo transaction. */
	void SpawnAtCurrent();

	/** Editor world used for the ghost + placement (GEditor's editor world context). */
	UWorld* GetEditorWorld() const;

	FAssetData AssetToPlace;
	TWeakObjectPtr<AActor> GhostActor;
	FVector CurrentLocation = FVector::ZeroVector;
	bool bHasHit = false;
	float GridSize = 100.0f;
	/** Live yaw applied to the ghost + next spawn; reset on Enter, stepped by [ / ] in RotationSnapDegrees increments. */
	FRotator CurrentRotation = FRotator::ZeroRotator;
	/** [ / ] rotate step (degrees), pushed from UFPSRBlockoutSettings on Enter (same pattern as GridSize). */
	float RotationSnapDegrees = 90.0f;
};
