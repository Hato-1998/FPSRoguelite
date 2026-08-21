// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Math/IntVector.h"

/**
 * P0 performance spike for ADR 0009 (Docs/Architecture/0009-hover-swarm-local-3d-flow-window.md) — the player-centred
 * 3D local uniform grid window that carries hover-swarm navigation, additive to (never replacing) the existing 2D
 * surface flow field. Pure, world-independent math: no UObject, no scene query, no allocation inside Propagate's
 * inner loop. Exercised headless by FPSRoguelite.FlowField.HoverWindowCore (correctness) and
 * FPSRoguelite.FlowField.HoverWindowBench (timing/memory, the decision's still-unmeasured §"성능 미측정").
 *
 * This is scaffolding for the window's wavefront propagation ONLY — it says nothing yet about window placement,
 * per-player ownership, staggered updates, or the QueryFlow(WorldPos) seam (decision §5, §"결정" items 4-5). Those
 * are later slices; this file's only job is "given a window's occupancy and sources, produce Dist/StepDir cheaply".
 */

/** Window cell-grid dimensions and flat-array indexing. Deliberately POD (no UHT/.generated.h — this core never
 *  needs to be a UObject/UPROPERTY; ADR 0009 invariant 6 keeps agents behind the QueryFlow seam, not touching this
 *  struct directly). */
struct FPSROGUELITE_API FFPSRHoverWindowDims
{
	int32 DimX = 0;
	int32 DimY = 0;
	int32 DimZ = 0;

	/** Total cell count (DimX*DimY*DimZ). */
	int32 NumCells() const;

	/** Flat index for cell (X,Y,Z). Caller-verified in-range (no bounds check — hot path). */
	int32 CellIndex(int32 X, int32 Y, int32 Z) const;

	/** All three axes positive AND NumCells() fits in an int32 without overflow (a mis-sized window is a bake bug,
	 *  not a runtime condition to silently clamp). */
	bool IsValid() const;
};

/**
 * Wavefront propagation output. Caller-owned and reused across Propagate calls (the window moves with its player
 * every frame/stagger-tick under ADR 0009 — reusing the arrays across ticks avoids a per-tick heap churn that would
 * itself become the "moving window" cost the decision's invariant 7 warns against).
 */
struct FFPSRHoverWindowField
{
	/** BFS integration distance in cells, uniform cost (decision §"결정" item 1 — real-distance weighting for the
	 *  26-neighbour diagonal case is an explicit P1 decision, not decided here). FPSRHoverWindow::UnreachedDist for
	 *  cells never reached (occupied cells, or cells outside the flooded component). */
	TArray<uint16> Dist;

	/** 1-based index into the neighbour-offset table the field was propagated with (0 = source cell / unreached).
	 *  StepDir[N] names the offset that steps from N TOWARD its parent (the neighbour one cell closer to a source) —
	 *  i.e. it is already the direction an agent standing in N should move, not the direction propagation arrived
	 *  from. See FPSRHoverWindow::Propagate's header comment for the reverse-offset derivation. */
	TArray<uint8> StepDir;
};

namespace FPSRHoverWindow
{
	/** Dist sentinel for a cell BFS never reached (occupied source/cell, or a different connected component — the
	 *  window has no global connectivity label pass like the 2D field's RebuildConnectivity; unreached is unreached). */
	inline constexpr uint16 UnreachedDist = 0xFFFF;

	/** Face-adjacent neighbour offsets (+-X, +-Y, +-Z). Backing storage is a function-local static array — no heap,
	 *  safe to call every tick from every window. */
	FPSROGUELITE_API TConstArrayView<FIntVector> GetNeighborOffsets6();

	/** Face + edge + corner neighbour offsets: all of {-1,0,1}^3 except (0,0,0), 26 entries. This is the table that
	 *  makes the window's 3D-ness pay for itself over a 2D-plus-layers approach — a 26-neighbour flood can route
	 *  diagonally around a corner obstruction in one hop where 6-neighbour needs two. */
	FPSROGUELITE_API TConstArrayView<FIntVector> GetNeighborOffsets26();

	/** Number of uint64 words needed to bit-pack NumCells occupancy flags: ceil(NumCells/64). */
	FPSROGUELITE_API int32 OccupancyWords(int32 NumCells);

	/** Sets the occupancy bit for Cell (Occupancy must be sized >= OccupancyWords(NumCells)). */
	FPSROGUELITE_API void SetOccupied(TArrayView<uint64> Occupancy, int32 Cell);

	/** Reads the occupancy bit for Cell. Inline: this is the innermost hot-path predicate (tested once per
	 *  neighbour, per cell popped, so a function-call indirection here would show up in the bench). */
	inline bool IsOccupied(TConstArrayView<uint64> Occupancy, int32 Cell)
	{
		return (Occupancy[Cell >> 6] >> (Cell & 63)) & 1ull;
	}

	/**
	 * Multi-source, uniform-cost breadth-first wavefront over Dims using NeighborOffsets (pass GetNeighborOffsets6()
	 * or GetNeighborOffsets26() — the offset table IS the connectivity model, nothing else about Propagate changes).
	 * Occupied cells are never visited (walls block flood) and an occupied source is skipped (can't seed from inside
	 * geometry). Field.Dist/StepDir are resized (SetNumUninitialized) to Dims.NumCells() and fully overwritten —
	 * the caller does not need to clear them between calls.
	 *
	 * StepDir derivation: when relaxing neighbour N from currently-popped cell C via offset Offsets[j] (i.e.
	 * N = C + Offsets[j]), the direction an agent AT N should move to advance toward the source is the reverse of
	 * that offset, -Offsets[j] — which is by construction the offset that steps from N back to C. Propagate
	 * precomputes each offset's reverse-table index once per call (linear scan over <=26 entries, so this is O(1)
	 * against the flood's O(NumCells) cost) rather than re-deriving it per relax.
	 *
	 * Returns the number of sources actually seeded (0 = Dims invalid, Occupancy too small, or every source cell was
	 * occupied — Field is left empty/zeroed in all zero-seed cases, never partially written).
	 *
	 * Zero scene queries and zero heap allocation happen inside the flood loop — Occupancy is baked once (offline,
	 * decision §"결정" item 3) and the frontier array is Reserve()'d up front, matching invariant 7 ("scene queries
	 * do not scale with agent count") and the "no per-window slab-follow query" prohibition it carries.
	 */
	FPSROGUELITE_API int32 Propagate(
		const FFPSRHoverWindowDims& Dims,
		TConstArrayView<uint64> Occupancy,
		TConstArrayView<int32> Sources,
		TConstArrayView<FIntVector> NeighborOffsets,
		FFPSRHoverWindowField& Field);
}
