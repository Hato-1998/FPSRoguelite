// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "FPSRFlowFieldSubsystem.generated.h"

/** Server-authoritative flow-field for swarm pathing (P2-B2). A fixed-map 2D grid is periodically
 *  recomputed via multi-source BFS (all alive players are sources); each cell stores a flow direction
 *  pointing along the shortest path toward the nearest player. Enemies sample it in O(1). On an
 *  obstacle-free map the field points roughly straight at the nearest player, but it amortizes the
 *  per-enemy nearest-player search into one grid pass and lays the groundwork for obstacle support. */
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

	/** Nearest non-blocked cell (ring search up to SourceSearchRadius) to FromCell, preferring one with clear
	 *  line-of-sight from PlayerLocation (so the snapped source stays on the player's side of a wall), or the
	 *  nearest open cell as a fallback, or INDEX_NONE. Used to move a player source out of a cell the coarse
	 *  obstacle mask marked blocked (player standing next to geometry) so the BFS expands from walkable ground. */
	int32 FindNearestOpenCell(int32 FromCell, const FVector& PlayerLocation) const;

	// --- Grid config (debug-tier placeholder; data-driven bounds via a volume is a follow-up) ---
	static constexpr float CellSize = 200.0f;     // cm per cell
	static constexpr float HalfExtent = 10000.0f; // cm; grid covers [-HalfExtent, +HalfExtent] on X/Y around origin
	static constexpr float FlowUpdateInterval = 0.2f; // seconds between recomputes

	// Obstacle probe: box overlap ABOVE the floor (so the flat ground isn't flagged) to catch walls/buildings.
	// Assumes the playable floor sits near the grid Z (origin). Multi-level height awareness is a follow-up.
	static constexpr float ObstacleProbeZ = 120.0f;         // cm above grid origin Z (knee/wall height)
	static constexpr float ObstacleProbeHalfHeight = 60.0f; // cm; box half-height for the overlap test
	static constexpr int32 SourceSearchRadius = 4;          // cells; snap a blocked player source to open ground

	/** Grid dimension (cells per axis). */
	int32 GridDim = 0;
	/** Min corner (world) of cell (0,0). */
	FVector GridOrigin = FVector::ZeroVector;

	/** Integration field: BFS distance (in cells) to nearest player; INT32_MAX = unreachable. */
	TArray<int32> DistField;
	/** Flow field: per-cell normalized 2D direction toward the nearest player (zero if none). */
	TArray<FVector2D> FlowField;
	/** Static-obstacle mask (true = blocked; built once at level start, fixed map). */
	TArray<bool> BlockedField;

	bool bFieldReady = false;
	FTimerHandle RecomputeTimerHandle;
};
