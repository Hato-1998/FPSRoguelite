// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Math/Vector2D.h"
#include "Math/IntPoint.h"
#include "FPSRFlowFieldComputer.generated.h"

class AFPSRFlowFieldBoundsVolume;
class APlayerStart;

/** Result status of a path-distance / front query (U P-D). Distinguishes the "no distance" reasons so the front-chase
 *  gate acts correctly instead of conflating them into one MAX (Codex R2 #14). Plain C++ enum (server-side, not BP). */
enum class EFPSRFieldQuery : uint8
{
	OK,           // a valid finite path-distance was returned
	OffGrid,      // no grid cell / no walkable surface at this location's Z
	SourceLess,   // grid exists but the last RunBFS had no sources (distances meaningless; connectivity still valid)
	Unreachable,  // grid + sources exist but this surface is in a different connected component
	NoGrid        // subsystem-level: no unified grid at all (single-map / pre-content)
};

/**
 * Baked, world-independent surface graph for one map's flow field (U7 multi-layer, NumLayers=2).
 * Produced either by the production world-trace bake (BuildFromWorldTrace) or, for headless unit tests,
 * hand-authored and fed via BuildFromSurfaceData. Once built, the 0.2s recompute + per-enemy sample are
 * pure O(1) array math (Performance §5-2) — this struct carries exactly the arrays the worldless core reads.
 */
USTRUCT()
struct FFPSRFlowFieldSurfaceData
{
	GENERATED_BODY()

	/** Grid dimensions (cells per axis). Cell index = CY*GridDimX + CX. */
	int32 GridDimX = 0;
	int32 GridDimY = 0;
	/** Min corner (world) of cell (0,0). */
	FVector GridOrigin = FVector::ZeroVector;
	/** Cell size (cm). */
	float CellSize = 200.0f;

	/** Climbable step height (cm) this slot was baked with (default = UFPSRFlowFieldComputer::DefaultClimbableStepHeight).
	 *  In the U unified grid every committed slot must share this value (validated by CommitSubregion) so a door seam uses
	 *  the SAME step gate as the slots' baked edges — otherwise the field could disagree with the movement graph (Codex R3). */
	float ClimbableStepHeight = 45.0f;

	/** Per-surface reachable floor Z (world), sized NumCells*NumLayers. MAX_flt = no surface at this (cell,rank). */
	TArray<float> CellFloorZ;
	/** Per-surface occupancy mask (true = surface exists but is occupancy-blocked), sized NumCells*NumLayers. */
	TArray<bool> BlockedField;
	/** Per-cell edge rank-pairing mask, sized NumCells*2 ([cell*2+0] = +X edge, [cell*2+1] = +Y edge). */
	TArray<uint8> EdgeMask;
};

/**
 * One map's flow-field computer. A UObject shell around flat TArrays — created and owned by
 * UFPSRFlowFieldSubsystem (server-only), one per map (keyed by MapId in the subsystem registry).
 *
 * Split (Codex consult 2026-07-06): the WORLDLESS CORE (BuildFromSurfaceData / RunBFS / Sample / the pure
 * index helpers) touches no world and is exercised by FPSRoguelite.FlowField.Unit with synthetic surface
 * data; the PRODUCTION path (BuildFromWorldTrace / RecomputeFromWorld / ResolveSourcesProduction) does the
 * world traces + player enumeration and then funnels into the SAME downstream core. This keeps the swarm
 * hot path pure-array and gives the multimap refactor a headless regression net.
 *
 * U7 multi-layer (bounded 2-layer surface graph): each XY cell holds up to NumLayers vertically-stacked
 * walkable SURFACES (rank 0 = lowest); Surf(Cell,Rank) = Cell*NumLayers + Rank. Connectivity is
 * surface->surface via EdgeMask (a staircase transitively lifts rank across cells, no inter-layer edge).
 */
UCLASS()
class FPSROGUELITE_API UFPSRFlowFieldComputer : public UObject
{
	GENERATED_BODY()

public:
	// U7 multi-layer: bounded count of vertically-stacked walkable surfaces per XY cell. Rank 0 = lowest surface.
	static constexpr int32 NumLayers = 2;
	static_assert(NumLayers == 2,
		"EdgeMask packs NumLayers*NumLayers connectivity bits into a uint8 (4 bits for 2 layers). Widen EdgeMask "
		"to uint16 (>=3 layers needs 9 bits) before raising NumLayers.");

	// --- WORLDLESS CORE (unit-testable; no world query) ---

	/** Adopt a baked surface graph (synthetic or from BuildFromWorldTrace): copies the grid config + CellFloorZ /
	 *  BlockedField / EdgeMask and sizes/zeroes the integration + flow arrays. Does NOT compute flow (call RunBFS). */
	void BuildFromSurfaceData(const FFPSRFlowFieldSurfaceData& Data);

	/** Multi-source BFS (uniform cost) from the given SOURCE surface indices + steepest-descent flow. Pure array math.
	 *  Sets bFieldReady=true on success, false if Sources is empty. Sources are Surf indices (Cell*NumLayers+Rank). */
	void RunBFS(const TArray<int32>& SourceSurfaces);

	// --- U subregion bake (P-A, 2026-07-07 continuous-field): pre-size ONE fixed 3x3 unified grid, then atomically
	//     transplant / clear per-slot subregions into it. Bake-in-isolation -> transplant reuses the proven world-trace
	//     path unchanged (BuildFromWorldTrace on a temp grid -> ExtractSurfaceData -> CommitSubregion). Alignment is
	//     INTEGER-owned (FIntPoint CellOffset); a slot's temp grid must share CellSize and snap to GridOrigin+Offset*CellSize.
	//     Every topology mutation invalidates the field (bFieldReady=false) so no stale flow is sampled before the next
	//     RunBFS (Codex R1: Sample reads FlowField, not edge validity). Headless net = FPSRoguelite.FlowField.Subregion.

	/** Pre-size the fixed unified grid: every cell absent (no surface), no edges, field not ready. The U 3x3 extent is
	 *  built once; slots stream into it via CommitSubregion. A grid over the P-0 budget warns (run CheckUnifiedGridBudget). */
	void BuildEmptyGrid(int32 InGridDimX, int32 InGridDimY, const FVector& InGridOrigin, float InCellSize, float InClimbableStepHeight = 45.0f);

	/** Atomically transplant an isolation-baked slot into the unified grid at CellOffset. Copies CellFloorZ/BlockedField
	 *  + the slot's INTERNAL edges only; the slot's boundary edges are left untouched (blocked walls until a door opens,
	 *  P-B). Returns false WITHOUT mutating if the slot is misaligned (CellSize/origin drift), out of bounds, malformed,
	 *  or has no reachable floor (fail-closed). On success, invalidates the field — the caller must RunBFS. */
	bool CommitSubregion(FIntPoint CellOffset, const FFPSRFlowFieldSurfaceData& SlotData);

	/** Clear a slot's subregion on unload: reset its cells to absent + clear boundary edges on BOTH sides (the slot cells'
	 *  own +X/+Y edges AND the outside neighbours' edges pointing in) so no ghost path survives. Invalidates the field.
	 *  No-op if the rect lies outside the grid. */
	void ClearSubregion(FIntPoint CellOffset, FIntPoint SlotDims);

	/** Low-level edge primitive: open/close connectivity between two orthogonally-adjacent SURFACES (canonical bit in the
	 *  lower/left cell, mirroring IsSurfaceEdgeTraversable). Invalidates the field. This is the primitive P-B door stamping
	 *  calls once it has computed the cross-slot rank-pairs from the unified neighbours' Z/blocked state (Codex R1 Q1). */
	void SetSurfaceEdge(int32 CellA, int32 RankA, int32 CellB, int32 RankB, bool bOpen);

	/** Copy this computer's baked surface graph out (grid config + CellFloorZ/BlockedField/EdgeMask; NOT the flow/integration
	 *  arrays) so an isolation-baked slot can be transplanted into a unified grid via CommitSubregion. */
	void ExtractSurfaceData(FFPSRFlowFieldSurfaceData& OutData) const;

	// --- U door stamping (P-B, 2026-07-07): a breakable door in a slot wall is NOT caught by the WorldStatic bake (its
	//     leaf/blocker are ECC_FPSRPlayerPawn / WorldDynamic), so the flow must be told about it explicitly. While intact,
	//     the door's gap cells are stamped BLOCKED; on break they are unblocked and the cross-slot edges are opened from
	//     the neighbours' baked Z (Codex R1 Q1). These are the grid PRIMITIVES; the door-object -> cell-span mapping is the
	//     server / FPSRDoor wiring, proven in-world (P-B PIE). Both invalidate the field (the caller then RunBFS). ---

	/** Toggle a single surface's occupancy-blocked flag (a door's gap cell: blocked while the door is intact, unblocked
	 *  on break). No-op on an out-of-range surface. Invalidates the field. */
	void StampCellBlocked(int32 Cell, int32 Rank, bool bBlocked);

	/** Open a door between two orthogonally-adjacent cells: for every rank-pair whose baked floor Z is within the climbable
	 *  step, open the surface edge (SetSurfaceEdge). Rank-pairs are computed from the unified neighbours' Z so a door never
	 *  connects surfaces an enemy could not actually step between (Codex R1 Q1). Returns the edges opened; invalidates. */
	int32 StampDoorEdgesOpen(int32 CellA, int32 CellB);

	/** Map a breakable seam door (its world-space AABB) to the cross-seam cell-pairs it should open on break. The door's
	 *  THINNER horizontal axis is the seam normal; for every grid column the door spans along the seam, the pair of cells
	 *  straddling the (cell-boundary-snapped) seam. Pure grid geometry (WorldToCellIndex + adjacency, no floor read); the
	 *  FPSRDoor/subsystem wiring calls this on break, then StampDoorEdgesOpen(pair) for each + one recompute. Empty out =
	 *  door not on a real seam / off-grid / degenerate bounds. Worldless (FPSRoguelite.FlowField.DoorMap). */
	void MapDoorSeamCellPairs(const FBox& DoorWorldAABB, TArray<TPair<int32, int32>>& OutPairs) const;

	// --- U origin-aware connectivity (P-C, 2026-07-07): open-grid connected components, so combat can gate "can damage
	//     cross from the instigator's origin cell to the target's cell?" in O(1) — a closed door / wall = a DIFFERENT
	//     component = no damage through it (replaces the MapId combat gate). Rebuilt by RunBFS, so it's fresh whenever the
	//     field is. The CanAffectTarget contract change (+ explosion Center origin, call sites) is server-side, proven
	//     in-world; this is the reusable query core (FPSRoguelite.FlowField.Connectivity). ---

	/** True if two SURFACES are in the same open-grid connected component (both valid). O(1) after RunBFS. */
	bool AreSurfacesConnected(int32 SurfA, int32 SurfB) const;

	/** True if two world locations' surfaces are open-grid connected (a wall / closed door separates components -> false).
	 *  Picks each location's surface by foot Z. Outside the grid / no surface -> false (fail-closed: no damage across). */
	bool AreWorldLocationsConnected(const FVector& A, const FVector& B) const;

	/** Normalized flow direction (XY, Z=0) at WorldLocation, or ZeroVector if outside grid / not ready / no surface.
	 *  Picks the walkable surface (layer) from the enemy actor Z (WorldLocation.Z - EnemyStandOffset) with pure math. */
	FVector Sample(const FVector& WorldLocation) const;

	/** Convert a world location to a grid cell index (XY only). INDEX_NONE if outside the grid. */
	int32 WorldToCellIndex(const FVector& WorldLocation) const;

	/** U P-D: path-distance (cells, uniform-cost BFS steps) from the nearest flow source (player) to WorldLocation's surface
	 *  (foot-Z rank pick), for the front-chase range gate. OutStatus distinguishes OK / OffGrid / SourceLess / Unreachable
	 *  (returns MAX_int32 for every non-OK). O(1) array read after RunBFS. */
	int32 GetPathDistanceCells(const FVector& WorldLocation, EFPSRFieldQuery& OutStatus) const;

	/** Surface (cell, rank) flat index. Rank in [0, NumLayers). */
	FORCEINLINE int32 SurfIndex(int32 Cell, int32 Rank) const { return Cell * NumLayers + Rank; }

	/** Pick the walkable-surface RANK at Cell nearest a pawn's foot Z within MaxLayerPickDrop, or INDEX_NONE. Pure. */
	int32 PickRankForFootZ(int32 Cell, float FootZ) const;

	/** True if an agent can cross from surface (CellA,RankA) to orthogonally-adjacent (CellB,RankB) — reads EdgeMask. */
	bool IsSurfaceEdgeTraversable(int32 CellA, int32 RankA, int32 CellB, int32 RankB) const;

	/** Whether RunBFS has produced a usable field. */
	FORCEINLINE bool IsFieldReady() const { return bFieldReady; }
	FORCEINLINE int32 GetGridDimX() const { return GridDimX; }
	FORCEINLINE int32 GetGridDimY() const { return GridDimY; }
	FORCEINLINE const FVector& GetGridOrigin() const { return GridOrigin; }
	FORCEINLINE float GetCellSize() const { return ActiveCellSize; }
	/** World-space AABB (XY) this computer's grid covers, at GridOrigin.Z. Used for occupancy / mid-transition retry. */
	FBox GetGridBounds() const;
	/** Read a surface's baked floor Z (MAX_flt = absent). For diagnostics / unit asserts. */
	float GetCellFloorZ(int32 Surf) const { return CellFloorZ.IsValidIndex(Surf) ? CellFloorZ[Surf] : MAX_flt; }
	/** Read a surface's BFS integration distance (MAX_int32 = unreachable). For unit asserts. */
	int32 GetDist(int32 Surf) const { return DistField.IsValidIndex(Surf) ? DistField[Surf] : MAX_int32; }
	/** Read a surface's flow direction. For unit asserts. */
	FVector2D GetFlow(int32 Surf) const { return FlowField.IsValidIndex(Surf) ? FlowField[Surf] : FVector2D::ZeroVector; }

	// --- U (unified 3x3) grid budget gate (P-0, 2026-07-07 continuous-field pivot) ---
	// Arithmetic content contract that locks U's viability: a fixed SlotsPerAxis x SlotsPerAxis grid of
	// author-sized slots must fit the compile-time cell caps at the intended 200cm routing quality. Pure /
	// worldless. Same caps the production bake (BuildFromWorldTrace) already enforces by SILENTLY COARSENING
	// CellSize (quality loss); this gate fails fast instead, so an oversized combat slot is caught at author
	// time rather than hidden behind an auto-coarsen. See Docs/SSOT/Performance.md §5 (U recompute budget).

	/** Fixed slot count per axis for the U multimap layout (3x3). */
	static constexpr int32 UnifiedGridSlotsPerAxis = 3;

	/** Result of CheckUnifiedGridBudget: derived full-grid dims + which cap (if any) is violated. C++-only (no USTRUCT). */
	struct FUnifiedGridBudget
	{
		int32 GridDimX = 0;
		int32 GridDimY = 0;
		int64 TotalCells = 0;
		bool bAxisWithinCap = false;
		bool bTotalWithinCap = false;
		bool bWithinBudget = false;  // bAxisWithinCap && bTotalWithinCap
		FString Reason;              // human-readable pass/fail detail (logged by the gate)
	};

	/** Cells one slot axis of SlotSizeCm occupies at CellSizeCm (ceil, clamped >= 1). */
	static int32 SlotCellsForSize(float SlotSizeCm, float CellSizeCm);

	/** P-0 arithmetic gate: derive the fixed SlotsPerAxis grid from per-slot cell dims (+ an optional inter-slot
	 *  wall band, in cells, applied to each of the SlotsPerAxis-1 internal seams) and test it against the cell
	 *  caps. Pure, no side effects. bWithinBudget == false => U is invalid at this slot size (shrink the slot,
	 *  raise CellSize [routing quality down], or raise the caps). */
	static FUnifiedGridBudget CheckUnifiedGridBudget(int32 SlotCellsX, int32 SlotCellsY,
		int32 WallCellsPerSeam = 0, int32 SlotsPerAxis = UnifiedGridSlotsPerAxis);

	/** Compile-time cell caps, exposed for the P-0 gate + diagnostics (the D1 ~132m boundary is derived from these). */
	static constexpr int32 GetMaxGridDimPerAxis() { return MaxGridDimPerAxis; }
	static constexpr int32 GetMaxTotalCells() { return MaxTotalCells; }

	// --- PRODUCTION PATH (server, world queries) ---

	/** Size the grid from a bounds volume (or origin-centered fallback) anchored at FloorZ, then trace the static-obstacle
	 *  surface graph ONCE, producing a FFPSRFlowFieldSurfaceData and adopting it via BuildFromSurfaceData. TargetLevel (if
	 *  set) scopes actor discovery when multiple sublevels are loaded. */
	void BuildFromWorldTrace(UWorld* World, const AFPSRFlowFieldBoundsVolume* BoundsVolume, float FloorZ);

	/** Resolve alive-player sources in World (optionally filtered to InMapPlayers when non-null) and run the BFS. */
	void RecomputeFromWorld(UWorld* World, const TArray<FVector>* SourcePlayerFootLocations = nullptr);

	/** U (P-A): bake SlotVolume in ISOLATION via the existing world-trace path (a throwaway temp grid) then transplant it
	 *  into THIS unified grid at CellOffset (CommitSubregion). Fail-closed: returns false without mutating the grid if the
	 *  slot trace yields no reachable floor or is misaligned to the unified grid. Server-only. In-world crossing is proven
	 *  at P-B (PIE); the transplant/clear primitives are headless-verified by FPSRoguelite.FlowField.Subregion. */
	bool BakeSlotIntoUnifiedGrid(UWorld* World, const AFPSRFlowFieldBoundsVolume* SlotVolume, float FloorZ, FIntPoint CellOffset);

#if !UE_BUILD_SHIPPING
	/** Dev viz: per-surface flow arrows + blocked/no-floor boxes near the given world locations. */
	void DebugDraw(UWorld* World, const TArray<FVector>& NearLocations, float DrawLife) const;
#endif

private:
	/** Nearest non-blocked SURFACE (ring search) to (FromCell,FromRank), preferring LOS-clear from PlayerLocation. World query. */
	int32 FindNearestOpenSurface(UWorld* World, int32 FromCell, int32 FromRank, const FVector& PlayerLocation) const;

	/** Flood-fill connected-component labels over valid surfaces via traversable edges (ignores occupancy-blocked, which is
	 *  crowding, not a wall). Source-independent; recomputed each RunBFS so combat connectivity queries are O(1). */
	void RebuildConnectivity();

	// --- Grid config constants (identical to the pre-refactor subsystem) ---
	static constexpr float DefaultCellSize = 200.0f;
	static constexpr float HalfExtentFallback = 14000.0f;
	static constexpr float EnemyStandOffset = 95.0f;
	static constexpr int32 MaxGridDimPerAxis = 256;
	static constexpr int32 MaxTotalCells = 40000;
	static constexpr float ObstacleProbeZ = 120.0f;
	static constexpr float ObstacleProbeHalfHeight = 60.0f;
	static constexpr int32 SourceSearchRadius = 4;
	static constexpr float AgentFootprintRadius = 40.0f;
	static constexpr float DefaultClimbableStepHeight = 45.0f;
	static constexpr float MaxClimbableStepHeight = 60.0f;
	static constexpr float MaxLayerPickDrop = 200.0f;
	static constexpr float WalkableNormalZ = 0.573f;
	static constexpr float DefaultProbeApexAboveOrigin = 2000.0f;
	static constexpr float MaxProbeDrop = 6000.0f;
	static constexpr int32 MaxColumnSurfaces = 16;
	static constexpr float SurfaceProbeSkip = 20.0f;

	// --- Active (per-bake) params ---
	float ActiveCellSize = DefaultCellSize;
	float ActiveClimbableStepHeight = DefaultClimbableStepHeight;
	float ActiveProbeApexAboveOrigin = DefaultProbeApexAboveOrigin;
	int32 GridDimX = 0;
	int32 GridDimY = 0;
	FVector GridOrigin = FVector::ZeroVector;

	// --- Per-SURFACE state (sized GridDimX*GridDimY*NumLayers). ---
	TArray<int32> DistField;
	TArray<FVector2D> FlowField;
	TArray<bool> BlockedField;
	TArray<float> CellFloorZ;
	TArray<uint8> EdgeMask;

	/** Per-surface open-grid connected-component id (INDEX_NONE = absent). Rebuilt by RunBFS; O(1) connectivity queries. */
	TArray<int32> ComponentLabels;

	bool bFieldReady = false;
	// Connectivity labels are rebuilt by RebuildConnectivity() at the top of EVERY RunBFS, independent of whether flow
	// sources resolve — so a source-less field (players airborne/unsnapped) still has valid connectivity for the combat
	// gate. Distinct from bFieldReady (flow): a mutation invalidates both; a source-less RunBFS refreshes connectivity but
	// leaves the flow not-ready. AreSurfacesConnected gates on THIS, not bFieldReady (Codex R15).
	bool bConnectivityReady = false;
};
