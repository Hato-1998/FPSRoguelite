// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "FPSRFlowFieldBoundsVolume.generated.h"

class UBoxComponent;

/** Designer-placed volume that sizes the swarm flow-field grid (Enemy.md §2-6 / Performance §5-2). The flow-field
 *  subsystem discovers ONE of these at world begin and builds its grid over this box's world-space AABB instead of
 *  the origin-centered fallback extent — so an off-origin / relocated map gets correct pathing coverage without a
 *  C++ constant change. Pure editor gizmo: no collision, not replicated; the server reads it once at begin play.
 *
 *  Content: place one in the level and size BoundsBox to enclose the whole playable region. Keep it axis-aligned —
 *  the grid is an AABB, so a rotated box is read as its (larger) world AABB, covering more than the visible wire. */
UCLASS()
class FPSROGUELITE_API AFPSRFlowFieldBoundsVolume : public AActor
{
	GENERATED_BODY()

public:
	AFPSRFlowFieldBoundsVolume();

	/** World-space axis-aligned bounds of the sizing box (rotation-robust via the component's cached bounds).
	 *  Returns an empty box if the box component is missing. */
	FBox GetWorldBounds() const;

	/** Per-map cell size override (cm); 0 = use the subsystem default. >0 trades grid resolution vs recompute cost. */
	float GetCellSizeOverride() const { return CellSizeOverride; }

	/** Per-map climbable step height (cm); 0 = subsystem default (45, mirrors UE MaxStepHeight). */
	float GetClimbableStepHeightOverride() const { return ClimbableStepHeightOverride; }

	/** Per-map floor-probe apex (cm above the grid floor); 0 = subsystem default (500). */
	float GetProbeApexAboveOriginOverride() const { return ProbeApexAboveOriginOverride; }

protected:
	/** Sizing box — its world AABB defines the flow-field grid extent. No collision (pure designer gizmo). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPSR|Flow Field")
	TObjectPtr<UBoxComponent> BoundsBox;

	/** Optional per-map cell size (cm). 0 = subsystem default (200). Larger = coarser grid, cheaper recompute. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPSR|Flow Field", meta = (ClampMin = "50.0"))
	float CellSizeOverride = 0.0f;

	/** Optional per-map climbable step height (cm); 0 = subsystem default (45, mirrors UE MaxStepHeight). An inter-cell
	 *  floor step <= this is walkable (stairs/ramps); a larger step is a wall the flow routes around. Keep <= the enemy
	 *  GroundSnapTolerance (60) so routed steps are actually climbable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPSR|Flow Field", meta = (ClampMin = "0.0"))
	float ClimbableStepHeightOverride = 0.0f;

	/** Optional per-map floor-probe apex (cm above the grid floor); 0 = subsystem default (500). Raise it for maps with
	 *  platforms/upper floors taller than the default, but keep it BELOW a solid room ceiling so the roof isn't sampled
	 *  as floor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPSR|Flow Field", meta = (ClampMin = "0.0"))
	float ProbeApexAboveOriginOverride = 0.0f;

#if WITH_EDITORONLY_DATA
private:
	/** Editor-only sprite so the volume stays clickable when zoomed out (the box wireframe is the primary viz). */
	UPROPERTY()
	TObjectPtr<class UBillboardComponent> EditorBillboard;
#endif
};
