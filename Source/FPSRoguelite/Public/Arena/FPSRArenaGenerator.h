// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Arena/FPSRArenaTypes.h"

/**
 * Pure arena layout generator (ADR 0010 D5 · module boundary).
 *
 * Touches NO world, subsystem or singleton — it is a plain static class precisely so that nothing can drift
 * into it. Same (Seed, Params) produces a byte-identical layout on every machine, which is load-bearing:
 * ADR 0010 gives the layout NO owner, so each client regenerates it locally from the replicated seed. If two
 * machines disagreed, the server's flow field and the client's geometry would disagree and enemies would
 * appear to walk through walls.
 *
 * That is why the only randomness here is FRandomStream. FMath::Rand shares global state, and TMap/TSet
 * iteration order is unspecified — either one silently breaks the equality above (ADR 0010 invariant 10).
 *
 * ## Why the L0 skeleton is a lattice
 *
 * Clusters are placed one per cell of a small lattice, each inset from its slot by the minimum corridor
 * width. That single construction discharges four invariants WITHOUT a validate-and-retry loop:
 *
 *   - circulation (a rectangle with disjoint rectangles punched out is always connected and always has a
 *     cycle — so "at least one loop" is structural, not something we check afterwards and hope for);
 *   - crossings (the lattice's corridor junctions ARE the decision points D1 asks for);
 *   - convexity (invariant 7 — rectangles);
 *   - no concave pockets (invariant 7 — adjacent clusters are 2x the inset apart, so no two of them can
 *     cup a pocket between them).
 *
 * The automation tests therefore CONFIRM these properties; the generator does not depend on them passing.
 */
class FPSROGUELITE_API FFPSRArenaGenerator
{
public:
	/**
	 * Build a layout. ArenaOrigin is the world-space min corner of cell (0,0); its Z is the arena floor.
	 * Returns false (and leaves OutLayout default-constructed) if Params are not geometrically satisfiable —
	 * callers must fail-fast on that rather than falling back to a world-trace bake (ADR 0010 invariant 5).
	 */
	static bool Generate(int32 Seed, const FFPSRArenaGenParams& Params, const FVector& ArenaOrigin, FFPSRArenaLayout& OutLayout);

	/** True if the cell is inside the grid and not covered by a cluster. Mirrors the flow field's own
	 *  traversability predicate (surface exists AND not blocked), so layout and field never disagree. */
	static bool IsCellOpen(const FFPSRArenaLayout& Layout, int32 CX, int32 CY);
};
