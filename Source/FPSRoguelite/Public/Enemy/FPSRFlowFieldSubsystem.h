// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "FPSRFlowFieldSubsystem.generated.h"

/** Server-authoritative flow-field for swarm pathing (P2-B2). A fixed-map 2D grid is periodically
 *  recomputed via multi-source BFS (all alive players are sources); each cell stores a flow direction
 *  pointing along the shortest path toward the nearest player. Enemies sample it in O(1). On an
 *  obstacle-free map the field points roughly straight at the nearest player, but it amortizes the
 *  per-enemy nearest-player search into one grid pass and lays the groundwork for obstacle support.
 *
 *  Grid bounds are data-driven: a designer-placed AFPSRFlowFieldBoundsVolume sizes the grid to the playable
 *  region (Performance §5-2). With no volume the grid falls back to an origin-centered HalfExtentFallback. */
UCLASS()
class FPSROGUELITE_API UFPSRFlowFieldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Returns the normalized flow direction (XY, Z=0) at WorldLocation, or ZeroVector if outside the
	 *  grid / field not ready. Callers should fall back to a direct-to-player direction on ZeroVector. */
	FVector SampleFlowDirection(const FVector& WorldLocation) const;

private:
	void RecomputeField();
	bool HasServerAuthority() const;

	/** Build the static-obstacle mask once at level start: a cell is blocked if static geometry occupies it at
	 *  play height, so the BFS routes flow AROUND walls/buildings instead of straight through them (the swept
	 *  enemy movement would otherwise jam against them). Fixed map → computed once. (Game.MD §5-2 obstacle support.) */
	void BuildObstacleMask();

	/** Convert a world location to a grid cell index. Returns INDEX_NONE if outside the grid. */
	int32 WorldToCellIndex(const FVector& WorldLocation) const;

	/** True if an agent can cross the edge between two orthogonally-adjacent cells (no static wall on that shared
	 *  boundary). Canonicalizes to the lower cell's +X/+Y edge so the reverse direction reads the same entry. Built
	 *  once in BuildObstacleMask; the BFS + flow passes consult it so flow never crosses a thin boundary wall even
	 *  when both cells are individually open (Part B clearance probing). */
	bool IsEdgeTraversable(int32 CellA, int32 CellB) const;

	/** Nearest non-blocked cell (ring search up to SourceSearchRadius) to FromCell, preferring one with clear
	 *  line-of-sight from PlayerLocation (so the snapped source stays on the player's side of a wall), or the
	 *  nearest open cell as a fallback, or INDEX_NONE. Used to move a player source out of a cell the coarse
	 *  obstacle mask marked blocked (player standing next to geometry) so the BFS expands from walkable ground. */
	int32 FindNearestOpenCell(int32 FromCell, const FVector& PlayerLocation) const;

	// --- Grid config ---
	// Default cell size + fallback half-extent (origin-centered grid) used when no AFPSRFlowFieldBoundsVolume is
	// placed; with a bounds volume the grid is sized to its world AABB (data-driven, Performance §5-2).
	static constexpr float DefaultCellSize = 200.0f;      // cm per cell
	static constexpr float HalfExtentFallback = 14000.0f; // cm; origin-centered fallback half-extent (no volume)
	static constexpr float FlowUpdateInterval = 0.2f;     // seconds between recomputes

	// Perf budget: the per-tick BFS + steepest-descent passes scan every cell, so cap the TOTAL cell count. If a
	// bounds volume would exceed it, the cell size is GROWN (coarser grid) to preserve full coverage rather than
	// clamping the dimensions (which would silently truncate the playable region). The per-axis cap guards a
	// degenerate long thin strip from a single oversized axis.
	static constexpr int32 MaxGridDimPerAxis = 256;
	static constexpr int32 MaxTotalCells = 40000; // ~ the 140x140 fallback grid (19600) with headroom

	// Obstacle probe: box overlap ABOVE the floor (so the flat ground isn't flagged) to catch walls/buildings.
	// Assumes the playable floor sits near the grid Z (origin). Multi-level height awareness is a follow-up.
	static constexpr float ObstacleProbeZ = 120.0f;         // cm above grid origin Z (knee/wall height)
	static constexpr float ObstacleProbeHalfHeight = 60.0f; // cm; box half-height for the overlap test
	static constexpr int32 SourceSearchRadius = 4;          // cells; snap a blocked player source to open ground

	// Clearance-aware probing (Part B): the occupancy probe and the per-edge tests use the enemy capsule's footprint
	// radius (AFPSREnemyBase InitCapsuleSize(40,90)) instead of the full cell, so a passage a capsule fits stays open
	// while a per-edge box overlap still catches thin boundary walls (no through-wall leak). Keep in sync with the
	// capsule; a future per-archetype radius could read this from the enemy CDO (out of scope here).
	static constexpr float AgentFootprintRadius = 40.0f;    // cm; = AFPSREnemyBase capsule radius

	// --- Part A: 2.5D per-cell ground-height (U7) — sampled ONCE in BuildObstacleMask so the 0.2s RecomputeField and
	// the per-enemy SampleFlowDirection stay pure O(1) array reads (NO height/trace in the 500-enemy hot path). ---

	// An inter-cell floor-height step <= this is a walkable step (stairs/curb/gentle rise); a larger delta is a
	// cliff/wall and that edge is closed. Mirrors UCharacterMovementComponent::MaxStepHeight (UE 5.7 default 45cm).
	// INVARIANT: keep <= AFPSREnemyBase::GroundSnapTolerance (60) — the field only routes across steps the per-enemy
	// ground-snap can actually climb, so ApplyGravity needs no change. A bounds volume can override per map.
	static constexpr float DefaultClimbableStepHeight = 45.0f;

	// Hard cap on the (flat-step) climbable height, enforced over any bounds-volume override: a FLAT ledge taller than
	// the enemy's per-recheck ground snap (AFPSREnemyBase::GroundSnapTolerance = 60) can be opened by the field but not
	// actually climbed by ApplyGravity, jamming enemies. Keep == GroundSnapTolerance. (Ramps are climbed incrementally,
	// so the larger ramp allowance is not bound by this — only single vertical steps are.)
	static constexpr float MaxClimbableStepHeight = 60.0f; // = AFPSREnemyBase::GroundSnapTolerance

	// Min up-facing normal Z for a per-cell floor sample to count as walkable (rejects ceiling undersides / too-steep
	// faces). Mirrors UCharacterMovementComponent walkable floor Z (0.71 = cos ~44.8deg).
	static constexpr float WalkableNormalZ = 0.71f;

	// A sampled surface flatter than this (normal Z >= threshold, ~<11.5deg) is treated as FLAT: reaching it across a
	// cell boundary is a vertical STEP, allowed only up to one ClimbableStepHeight. A tilted-but-walkable surface
	// (WalkableNormalZ..FlatNormalZThreshold) is a continuous RAMP whose center-to-center rise over one cell can far
	// exceed a step, so its edges are allowed up to the max walkable grade across the cell (ramp allowance). This is
	// what lets the flood climb ramps/stairs while a true flat-to-flat cliff (both flat, big drop) stays blocked.
	static constexpr float FlatNormalZThreshold = 0.98f;

	// Per-cell floor probe: a downward multi-hit trace from (GridOrigin.Z + ActiveProbeApexAboveOrigin) picks the
	// topmost walkable static surface. The default apex sits above typical platforms but below a room ceiling so a solid
	// roof isn't mistaken for floor; a bounds volume raises it for taller / multi-storey maps.
	static constexpr float DefaultProbeApexAboveOrigin = 500.0f; // cm above GridOrigin.Z (floor) to start the down-trace
	static constexpr float MaxProbeDrop = 4000.0f;               // cm total downward trace length (reaches sunken floors)

	// A single object-type multi-trace stops at the first blocking hit (engine: "only the single closest blocking result
	// will be generated"), which would hide a floor UNDER a bridge/ceiling. So the per-cell probe re-traces from the apex,
	// ignoring each surface's mesh in turn, to collect every stacked walkable surface — capped at this many per column.
	static constexpr int32 MaxColumnSurfaces = 8;

	/** Active cell size (cm): DefaultCellSize, a volume's CellSizeOverride, or grown to fit the perf budget. */
	float ActiveCellSize = DefaultCellSize;
	/** Active climbable step height (cm): DefaultClimbableStepHeight or a bounds volume's ClimbableStepHeightOverride. */
	float ActiveClimbableStepHeight = DefaultClimbableStepHeight;
	/** Active floor-probe apex (cm above GridOrigin.Z): DefaultProbeApexAboveOrigin or a bounds volume override. */
	float ActiveProbeApexAboveOrigin = DefaultProbeApexAboveOrigin;
	/** Grid dimensions (cells per axis; non-square when a bounds volume's AABB is non-square). Cell index = CY*GridDimX + CX. */
	int32 GridDimX = 0;
	int32 GridDimY = 0;
	/** Min corner (world) of cell (0,0). */
	FVector GridOrigin = FVector::ZeroVector;

	/** Integration field: BFS distance (in cells) to nearest player; INT32_MAX = unreachable. */
	TArray<int32> DistField;
	/** Flow field: per-cell normalized 2D direction toward the nearest player (zero if none). */
	TArray<FVector2D> FlowField;
	/** Static-obstacle mask (true = blocked; built once at level start, fixed map). */
	TArray<bool> BlockedField;
	/** Per-cell edge-traversability mask, sized GridDimX*GridDimY*2 ([cell*2+0] = +X edge, [cell*2+1] = +Y edge;
	 *  true = an agent can cross). Built once with BlockedField. Default false = blocked (safe: a forgotten/edge-of-
	 *  grid entry never leaks flow). Lets a 1-cell-wide doorway stay open while a thin boundary wall still blocks. */
	TArray<bool> EdgeTraversable;

	/** Per-cell REACHABLE walking-surface Z (world), computed ONCE in BuildObstacleMask: candidate up-facing surfaces
	 *  are flood-filled from the ground floor accepting only one-climbable-step height changes, so ramps/stairs climb
	 *  onto platforms but a disconnected wall/ceiling top is never mistaken for floor. BUILD-TIME state ONLY: the
	 *  occupancy/edge probes + climbable-step edge gate read it during the build; RecomputeField and SampleFlowDirection
	 *  MUST NOT (the 0.2s BFS + 500-enemy sample stay pure array reads). MAX_flt = no reachable floor (cell blocked). */
	TArray<float> CellFloorZ;

	bool bFieldReady = false;
	FTimerHandle RecomputeTimerHandle;
};
