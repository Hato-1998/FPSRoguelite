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

	/** Convert a world location to a grid cell index. Returns INDEX_NONE if outside the grid. */
	int32 WorldToCellIndex(const FVector& WorldLocation) const;

	// --- Grid config (debug-tier placeholder; data-driven bounds via a volume is a follow-up) ---
	static constexpr float CellSize = 200.0f;     // cm per cell
	static constexpr float HalfExtent = 10000.0f; // cm; grid covers [-HalfExtent, +HalfExtent] on X/Y around origin
	static constexpr float FlowUpdateInterval = 0.2f; // seconds between recomputes

	/** Grid dimension (cells per axis). */
	int32 GridDim = 0;
	/** Min corner (world) of cell (0,0). */
	FVector GridOrigin = FVector::ZeroVector;

	/** Integration field: BFS distance (in cells) to nearest player; INT32_MAX = unreachable. */
	TArray<int32> DistField;
	/** Flow field: per-cell normalized 2D direction toward the nearest player (zero if none). */
	TArray<FVector2D> FlowField;

	bool bFieldReady = false;
	FTimerHandle RecomputeTimerHandle;
};
