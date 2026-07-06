// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Math/Vector2D.h"
#include "FPSRFlowFieldComputer.generated.h"

class AFPSRFlowFieldBoundsVolume;
class APlayerStart;

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

	/** Normalized flow direction (XY, Z=0) at WorldLocation, or ZeroVector if outside grid / not ready / no surface.
	 *  Picks the walkable surface (layer) from the enemy actor Z (WorldLocation.Z - EnemyStandOffset) with pure math. */
	FVector Sample(const FVector& WorldLocation) const;

	/** Convert a world location to a grid cell index (XY only). INDEX_NONE if outside the grid. */
	int32 WorldToCellIndex(const FVector& WorldLocation) const;

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

	// --- PRODUCTION PATH (server, world queries) ---

	/** Size the grid from a bounds volume (or origin-centered fallback) anchored at FloorZ, then trace the static-obstacle
	 *  surface graph ONCE, producing a FFPSRFlowFieldSurfaceData and adopting it via BuildFromSurfaceData. TargetLevel (if
	 *  set) scopes actor discovery when multiple sublevels are loaded. */
	void BuildFromWorldTrace(UWorld* World, const AFPSRFlowFieldBoundsVolume* BoundsVolume, float FloorZ);

	/** Resolve alive-player sources in World (optionally filtered to InMapPlayers when non-null) and run the BFS. */
	void RecomputeFromWorld(UWorld* World, const TArray<FVector>* SourcePlayerFootLocations = nullptr);

#if !UE_BUILD_SHIPPING
	/** Dev viz: per-surface flow arrows + blocked/no-floor boxes near the given world locations. */
	void DebugDraw(UWorld* World, const TArray<FVector>& NearLocations, float DrawLife) const;
#endif

private:
	/** Nearest non-blocked SURFACE (ring search) to (FromCell,FromRank), preferring LOS-clear from PlayerLocation. World query. */
	int32 FindNearestOpenSurface(UWorld* World, int32 FromCell, int32 FromRank, const FVector& PlayerLocation) const;

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

	bool bFieldReady = false;
};
