// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Arena/FPSRArenaTypes.h"

/**
 * Turns what the designer placed into what the flow field reads (ADR 0010 D5 as amended by 0011 E2/E3/E5).
 *
 * Touches NO world, subsystem or singleton — a plain static class precisely so nothing can drift into it. Same
 * (Seed, Params, Authored) produces a byte-identical layout on every machine, which is load-bearing: the layout
 * has no owner, so each client rebuilds it locally from the replicated seed. If two machines disagreed, the
 * server's flow field and the client's geometry would disagree and enemies would appear to walk through walls.
 * That is why the only randomness here is FRandomStream — FMath::Rand shares global state and TMap/TSet
 * iteration order is unspecified, and either one silently breaks that equality (ADR 0010 invariant 10).
 *
 * ## Rasterised, not traced
 *
 * Authored blocking volumes are converted to cells by pure geometry. They are never discovered with world
 * traces. That distinction is what let the skeleton become authored (0011 E2) without giving back the reason
 * 0010 chose generator-owned masks in the first place: a stage swap still costs zero world queries, the cell
 * size can never be silently coarsened, and the grid origin cannot be dragged down onto the spare arena parked
 * below the live one.
 *
 * ## What the lattice is now
 *
 * ProposeLatticeClusters is the old L0 generator, demoted to an editor "starting layout" button (0011 E5). It
 * still guarantees circulation, crossings, convexity and minimum width BY CONSTRUCTION — but only for what it
 * proposes. The moment a designer edits the result, that guarantee is gone and FFPSRArenaValidator is what
 * stands in its place.
 */
class FPSROGUELITE_API FFPSRArenaGenerator
{
public:
	/**
	 * Build a layout from authored input. ArenaOrigin is the world-space min corner of cell (0,0); its Z is the
	 * arena floor. Returns false (leaving OutLayout default-constructed) only if Params are not geometrically
	 * satisfiable — callers must fail-fast on that rather than substituting a world-trace bake (0010 invariant 5).
	 *
	 * Empty Authored is NOT an error: it produces a valid, entirely open arena. Whether that is acceptable is the
	 * validator's call, not the generator's — the generator does not invent a skeleton nobody placed.
	 */
	static bool Generate(int32 Seed, const FFPSRArenaGenParams& Params, const FVector& ArenaOrigin,
		const FFPSRArenaAuthoredInput& Authored, FFPSRArenaLayout& OutLayout);

	/**
	 * Propose a starting cluster lattice (ADR 0011 E5 — the editor tool's button). Deterministic in Seed.
	 * Guarantees, by construction rather than by checking: at least one circulation loop, corridor junctions,
	 * convex blockers only, and no two clusters closer than the minimum corridor width.
	 */
	static bool ProposeLatticeClusters(int32 Seed, const FFPSRArenaGenParams& Params,
		TArray<FFPSRArenaCluster>& OutClusters, FIntPoint& OutLattice);

	/** Cell-rect -> world box, so a proposal can be round-tripped through the same rasteriser the runtime uses
	 *  instead of a second code path that agrees with it only until someone edits one of them. */
	static FFPSRArenaAuthoredBox ClusterToAuthoredBox(const FFPSRArenaCluster& Cluster,
		const FFPSRArenaGenParams& Params, const FVector& ArenaOrigin);

	/** Cell indices an authored destructible occupies (its footprint, anchored at Location, growing +X/+Y).
	 *  Off-grid cells are dropped; the surviving indices come out in ascending order. Both the generator (blocking
	 *  these cells at Generate()) and the destructible actor (opening them at HandleBrokenAuthority) call this SAME
	 *  function, so the two sides can never disagree about which cells one prop owns. Pure geometry — no world
	 *  access, matching this class's module boundary. */
	static void ComputeDestructibleCells(const FFPSRArenaAuthoredDestructible& Destructible,
		const FVector& ArenaOrigin, float CellSize, const FIntPoint& GridDims, TArray<int32>& OutCells);

	/** True if the cell is inside the grid and not blocked. Mirrors the flow field's own traversability predicate
	 *  (surface exists AND not blocked), so layout and field never disagree about what is walkable. */
	static bool IsCellOpen(const FFPSRArenaLayout& Layout, int32 CX, int32 CY);
};
