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
 *  U7 multi-layer (bounded 2-layer surface graph): each XY cell holds up to NumLayers vertically-stacked
 *  walkable SURFACES (rank 0 = lowest, rank 1 = above), so an upper deck / mezzanine overlapping the ground
 *  floor at the same XY is its own routable layer and the swarm chases a player UP a connecting stair. All
 *  per-cell data becomes per-surface (Surf(Cell,Rank) = Cell*NumLayers + Rank); connectivity is surface->surface
 *  (a staircase transitively lifts rank across cells, no explicit inter-layer edge). All height/trace cost stays
 *  ONE-TIME in BuildObstacleMask; the 0.2s recompute + per-enemy sample remain pure array math (Performance §5-2).
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

	/** Returns the normalized flow direction (XY, Z=0) at WorldLocation, or ZeroVector if outside the grid /
	 *  field not ready / no reachable surface. The caller passes the enemy's ACTOR location; this picks the
	 *  walkable SURFACE (layer) the enemy stands on from its Z (WorldLocation.Z - EnemyStandOffset) with pure
	 *  arithmetic — so an enemy on an upper deck samples the deck's flow, not the ground below it (U7). Callers
	 *  should fall back to a direct-to-player direction on ZeroVector. */
	FVector SampleFlowDirection(const FVector& WorldLocation) const;

private:
	void RecomputeField();
	bool HasServerAuthority() const;

	/** Build the static-obstacle mask once at level start: per XY cell, collect up to NumLayers stacked walkable
	 *  SURFACES and mark a surface blocked if static geometry occupies it at play height, so the BFS routes flow
	 *  AROUND walls/buildings (and between layers via stairs) instead of straight through them. Fixed map → computed
	 *  once. (Game.MD §5-2 obstacle + height support.) */
	void BuildObstacleMask();

	/** Convert a world location to a grid cell index (XY only). Returns INDEX_NONE if outside the grid. */
	int32 WorldToCellIndex(const FVector& WorldLocation) const;

	/** Surface (cell, layer/rank) flat index into the per-surface arrays. Rank in [0, NumLayers). */
	FORCEINLINE int32 SurfIndex(int32 Cell, int32 Rank) const { return Cell * NumLayers + Rank; }

	/** Pick the walkable-surface RANK at Cell whose floor best matches a pawn's foot Z, or INDEX_NONE if the cell
	 *  has no valid surface. Pure array reads (no world query). Rule (matches AFPSREnemyBase::ApplyGravity snapping):
	 *  the LOWEST valid rank within GroundSnapTolerance of FootZ (deterministic on a degenerate <snap stack → no
	 *  frame-to-frame oscillation); else the HIGHEST valid rank at-or-below FootZ (the surface it stands on / is
	 *  falling toward); else the lowest valid rank. */
	int32 PickRankForFootZ(int32 Cell, float FootZ) const;

	/** True if an agent can cross from surface (CellA,RankA) to the orthogonally-adjacent surface (CellB,RankB): the
	 *  baked rank-pairing bit for that shared boundary is set (no wall, height change within the step/grade allowance).
	 *  Canonicalizes to the lower-index cell's +X/+Y EdgeMask byte; the reverse direction swaps the rank bit-index so
	 *  both directions read the same entry. Built once in BuildObstacleMask; the BFS + flow passes consult it. */
	bool IsSurfaceEdgeTraversable(int32 CellA, int32 RankA, int32 CellB, int32 RankB) const;

	/** Nearest non-blocked SURFACE (ring search up to SourceSearchRadius) to (FromCell, FromRank), preferring one with
	 *  clear line-of-sight from PlayerLocation and at a similar height (same layer), or the nearest open surface as a
	 *  fallback, or INDEX_NONE. Moves a player source out of a surface the coarse mask marked blocked (player standing
	 *  next to geometry) so the BFS expands from walkable ground. Returns a Surf index. */
	int32 FindNearestOpenSurface(int32 FromCell, int32 FromRank, const FVector& PlayerLocation) const;

	// --- Grid config ---
	// Default cell size + fallback half-extent (origin-centered grid) used when no AFPSRFlowFieldBoundsVolume is
	// placed; with a bounds volume the grid is sized to its world AABB (data-driven, Performance §5-2).
	static constexpr float DefaultCellSize = 200.0f;      // cm per cell
	static constexpr float HalfExtentFallback = 14000.0f; // cm; origin-centered fallback half-extent (no volume)
	static constexpr float FlowUpdateInterval = 0.2f;     // seconds between recomputes

	// U7 multi-layer: bounded count of vertically-stacked walkable surfaces per XY cell. Rank 0 = lowest surface.
	// Sparse in the map (a mezzanine is a small fraction), but stored dense so the hot path stays a flat-array read.
	static constexpr int32 NumLayers = 2;
	static_assert(NumLayers == 2,
		"EdgeMask packs NumLayers*NumLayers connectivity bits into a uint8 (4 bits for 2 layers). Widen EdgeMask "
		"to uint16 (>=3 layers needs 9 bits) before raising NumLayers.");

	// The enemy actor origin sits EnemyStandOffset ABOVE its walking surface (AFPSREnemyBase: capsule HalfHeight 90 +
	// GroundRestClearance 5). SampleFlowDirection subtracts it to convert the actor Z to the surface it stands on and
	// pick the layer with pure arithmetic — no world query. Documented invariant like MaxClimbableStepHeight ==
	// GroundSnapTolerance below; keep in sync with AFPSREnemyBase if those change.
	static constexpr float EnemyStandOffset = 95.0f;

	// Perf budget: the per-tick BFS + steepest-descent passes scan every cell, so cap the TOTAL cell count. If a
	// bounds volume would exceed it, the cell size is GROWN (coarser grid) to preserve full coverage rather than
	// clamping the dimensions (which would silently truncate the playable region). The per-axis cap guards a
	// degenerate long thin strip from a single oversized axis. NOTE (U7): this is a BASE-cell cap; the multi-layer
	// arrays scan up to MaxTotalCells*NumLayers SURFACE slots, but empty upper-layer slots (CellFloorZ==MAX_flt) hit a
	// one-branch early-out, so the flat-map cost is unchanged. Re-measure the 2x worst case at the U14 perf pass.
	static constexpr int32 MaxGridDimPerAxis = 256;
	static constexpr int32 MaxTotalCells = 40000; // ~ the 140x140 fallback grid (19600) with headroom

	// Obstacle probe: box overlap ABOVE each surface's own floor (so the flat ground isn't flagged) to catch walls/buildings.
	static constexpr float ObstacleProbeZ = 120.0f;         // cm above a surface's floor Z (knee/wall height)
	static constexpr float ObstacleProbeHalfHeight = 60.0f; // cm; box half-height for the overlap test
	static constexpr int32 SourceSearchRadius = 4;          // cells; snap a blocked player source to open ground

	// Clearance-aware probing (Part B): the occupancy probe and the per-edge tests use the enemy capsule's footprint
	// radius (AFPSREnemyBase InitCapsuleSize(40,90)) instead of the full cell, so a passage a capsule fits stays open
	// while a per-edge box overlap still catches thin boundary walls (no through-wall leak). Keep in sync with the
	// capsule; a future per-archetype radius could read this from the enemy CDO (out of scope here).
	static constexpr float AgentFootprintRadius = 40.0f;    // cm; = AFPSREnemyBase capsule radius

	// --- Part A: 2.5D per-surface ground-height (U7) — sampled ONCE in BuildObstacleMask so the 0.2s RecomputeField and
	// the per-enemy SampleFlowDirection stay pure O(1) array reads (NO height/trace in the swarm hot path). ---

	// An inter-cell floor-height step <= this is a walkable step (stairs/curb/gentle rise); a larger delta is a
	// cliff/wall and that edge is closed. Mirrors UCharacterMovementComponent::MaxStepHeight (UE 5.7 default 45cm).
	// INVARIANT: keep <= AFPSREnemyBase::GroundSnapTolerance (60) — the field only routes across steps the per-enemy
	// ground-snap can actually climb, so ApplyGravity needs no change. A bounds volume can override per map.
	static constexpr float DefaultClimbableStepHeight = 45.0f;

	// Hard cap on the (flat-step) climbable height, enforced over any bounds-volume override: a FLAT ledge taller than
	// the enemy's per-recheck ground snap (AFPSREnemyBase::GroundSnapTolerance = 60) can be opened by the field but not
	// actually climbed by ApplyGravity, jamming enemies. Keep == GroundSnapTolerance. (Ramps are climbed incrementally,
	// so the larger ramp allowance is not bound by this — only single vertical steps are.) Also the sample layer-pick's
	// snap window (PickRankForFootZ) — it mirrors GroundSnapTolerance so the picked layer matches what ApplyGravity snaps to.
	static constexpr float MaxClimbableStepHeight = 60.0f; // = AFPSREnemyBase::GroundSnapTolerance

	// Max drop below the enemy's foot that PickRankForFootZ's "highest surface at-or-below" fallback accepts as the
	// surface it stands on. Guards the multi-layer case: an enemy at deck height (-550) in a cell whose only surface is
	// the ground a storey below (-1000) must NOT resolve to that ground rank and follow ground flow while up on the deck
	// (it would walk to the rim / stall / fall — U7 PIE). Beyond this, the pick returns INDEX_NONE and the mover falls
	// back to direct-to-player. Kept well below a storey (~450cm) but above any real ledge/stair the enemy stands on.
	static constexpr float MaxLayerPickDrop = 200.0f; // cm

	// Min up-facing normal Z for a per-cell floor sample to count as walkable (rejects ceiling undersides / near-vertical
	// faces). Set to the ENEMY's actual traversal limit, NOT the UE player default (0.71 = 44.8deg): a swarm Pawn climbs
	// a slope incrementally (swept slide + per-recheck ground snap up to GroundSnapTolerance) so it can ascend steeper
	// grades than a walking player — up to ~58deg for MoveSpeed 250 / recheck 0.15s. 0.573 = cos 55deg gives margin and
	// accepts common steep-stair simple-collision ramps (e.g. a 50deg staircase collider) that 0.71 would reject as a wall.
	static constexpr float WalkableNormalZ = 0.573f;

	// Per-cell floor probe: a downward re-tracing probe starts at (GridOrigin.Z + ActiveProbeApexAboveOrigin) and collects
	// every stacked walkable surface below it (see MaxColumnSurfaces). The apex must sit ABOVE the highest walkable floor
	// (e.g. an upper storey / raised platform) or that floor is never sampled and reads as blocked. It may sit above a
	// solid roof safely: the Z-step re-trace passes THROUGH the roof to the floors below, and the ground-height flood seed
	// never selects the disconnected roof surface. Default 2000cm covers several storeys; a bounds volume can raise it.
	static constexpr float DefaultProbeApexAboveOrigin = 2000.0f; // cm above GridOrigin.Z (floor) to start the down-trace
	static constexpr float MaxProbeDrop = 6000.0f;                // cm total downward trace length (reaches sunken floors below a high apex)

	// A single object-type trace stops at the first blocking hit (engine: "only the single closest blocking result will
	// be generated"), which would hide a floor UNDER a bridge/ceiling. So the per-cell probe re-traces DOWN, restarting
	// just below each hit, to collect every stacked walkable surface — even when the upper surface and the floor are the
	// SAME merged static mesh. Capped at MaxColumnSurfaces iterations; each restart drops at least SurfaceProbeSkip so a
	// thick slab is stepped through and the loop always terminates. One-time on the fixed map.
	static constexpr int32 MaxColumnSurfaces = 16;   // max downward re-traces per column (stacked levels + slab step-through)
	static constexpr float SurfaceProbeSkip = 20.0f; // cm the next trace restarts below the last hit; also the cluster-merge epsilon

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

	// --- Per-SURFACE state (sized GridDimX*GridDimY*NumLayers, indexed by SurfIndex(Cell,Rank)). U7 multi-layer. ---
	/** Integration field: BFS distance (in cells) to nearest player; INT32_MAX = unreachable. */
	TArray<int32> DistField;
	/** Flow field: per-surface normalized 2D direction toward the nearest player (zero if none). */
	TArray<FVector2D> FlowField;
	/** Static-obstacle mask (true = a real surface exists here but is occupancy-blocked; built once, fixed map). */
	TArray<bool> BlockedField;
	/** Per-surface REACHABLE walking-surface Z (world), computed ONCE in BuildObstacleMask: candidate up-facing surfaces
	 *  are clustered into ranks then flood-filled from the ground floor accepting only one-climbable-step/grade changes,
	 *  so ramps/stairs climb onto platforms/decks but a disconnected wall/ceiling top is never mistaken for floor.
	 *  MAX_flt = no reachable surface at this (cell,rank) → treated as absent/blocked. BUILD-TIME writes ONLY; the 0.2s
	 *  BFS + 500-enemy sample only READ it (pure array), never re-trace. */
	TArray<float> CellFloorZ;

	/** Per-cell edge rank-pairing mask, sized GridDimX*GridDimY*2 ([cell*2+0] = +X edge, [cell*2+1] = +Y edge). Each byte
	 *  packs NumLayers*NumLayers bits: bit (ra*NumLayers + rb) set = an agent can cross from THIS (lower-index) cell's
	 *  surface rank `ra` to the neighbour's rank `rb`. Built once with BlockedField. Default 0 = all pairs blocked (safe:
	 *  a forgotten/edge-of-grid entry never leaks flow). Lets a 1-cell-wide doorway stay open while a thin boundary wall
	 *  still blocks, AND encodes stair transitions that change layer (ground rank of A → deck rank of B). */
	TArray<uint8> EdgeMask;

	bool bFieldReady = false;
	FTimerHandle RecomputeTimerHandle;
};
