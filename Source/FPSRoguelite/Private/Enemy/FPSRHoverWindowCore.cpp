// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSRHoverWindowCore.h"

int32 FFPSRHoverWindowDims::NumCells() const
{
	return DimX * DimY * DimZ;
}

int32 FFPSRHoverWindowDims::CellIndex(int32 X, int32 Y, int32 Z) const
{
	return (Z * DimY + Y) * DimX + X;
}

bool FFPSRHoverWindowDims::IsValid() const
{
	if (DimX <= 0 || DimY <= 0 || DimZ <= 0)
	{
		return false;
	}
	// int64 product check before trusting NumCells()'s int32 multiply — a mis-sized window (bake bug, not a runtime
	// condition) must fail loudly here rather than silently wrapping into a small/negative cell count downstream.
	const int64 Total = static_cast<int64>(DimX) * static_cast<int64>(DimY) * static_cast<int64>(DimZ);
	return Total > 0 && Total <= static_cast<int64>(MAX_int32);
}

namespace FPSRHoverWindow
{
	TConstArrayView<FIntVector> GetNeighborOffsets6()
	{
		static const FIntVector Offsets6[6] =
		{
			FIntVector( 1,  0,  0), FIntVector(-1,  0,  0),
			FIntVector( 0,  1,  0), FIntVector( 0, -1,  0),
			FIntVector( 0,  0,  1), FIntVector( 0,  0, -1),
		};
		return TConstArrayView<FIntVector>(Offsets6, 6);
	}

	TConstArrayView<FIntVector> GetNeighborOffsets26()
	{
		// All of {-1,0,1}^3 except (0,0,0): the 6 face neighbours, 12 edge (diagonal-pair) neighbours, and 8 corner
		// (diagonal-triple) neighbours. Uniform cost 1 for every entry, corners included — real-distance weighting
		// for the diagonal cases is an explicit P1 decision (ADR 0009 decision item 1), not this spike's job; the
		// point here is measuring cost SPREAD across neighbour-table density, not authoring the final metric.
		static const FIntVector Offsets26[26] =
		{
			FIntVector(-1, -1, -1), FIntVector( 0, -1, -1), FIntVector( 1, -1, -1),
			FIntVector(-1,  0, -1), FIntVector( 0,  0, -1), FIntVector( 1,  0, -1),
			FIntVector(-1,  1, -1), FIntVector( 0,  1, -1), FIntVector( 1,  1, -1),
			FIntVector(-1, -1,  0), FIntVector( 0, -1,  0), FIntVector( 1, -1,  0),
			FIntVector(-1,  0,  0),                         FIntVector( 1,  0,  0),
			FIntVector(-1,  1,  0), FIntVector( 0,  1,  0), FIntVector( 1,  1,  0),
			FIntVector(-1, -1,  1), FIntVector( 0, -1,  1), FIntVector( 1, -1,  1),
			FIntVector(-1,  0,  1), FIntVector( 0,  0,  1), FIntVector( 1,  0,  1),
			FIntVector(-1,  1,  1), FIntVector( 0,  1,  1), FIntVector( 1,  1,  1),
		};
		return TConstArrayView<FIntVector>(Offsets26, 26);
	}

	int32 OccupancyWords(int32 NumCells)
	{
		return (NumCells + 63) / 64;
	}

	void SetOccupied(TArrayView<uint64> Occupancy, int32 Cell)
	{
		Occupancy[Cell >> 6] |= (1ull << (Cell & 63));
	}

	int32 Propagate(
		const FFPSRHoverWindowDims& Dims,
		TConstArrayView<uint64> Occupancy,
		TConstArrayView<int32> Sources,
		TConstArrayView<FIntVector> NeighborOffsets,
		FFPSRHoverWindowField& Field)
	{
		if (!Dims.IsValid())
		{
			Field.Dist.Reset();
			Field.StepDir.Reset();
			return 0;
		}

		const int32 NumCells = Dims.NumCells();
		if (Occupancy.Num() < OccupancyWords(NumCells))
		{
			Field.Dist.Reset();
			Field.StepDir.Reset();
			return 0;
		}

		Field.Dist.SetNumUninitialized(NumCells);
		Field.StepDir.SetNumUninitialized(NumCells);
		FMemory::Memset(Field.Dist.GetData(), 0xFF, NumCells * sizeof(uint16)); // 0xFFFF per cell == UnreachedDist
		FMemory::Memset(Field.StepDir.GetData(), 0, NumCells * sizeof(uint8));

		// Reverse-offset table: RevIndex[j] is the index k such that Offsets[k] == -Offsets[j], i.e. the direction
		// that steps back from a cell reached via Offsets[j] toward the cell it was reached from. Both neighbour
		// tables (6 and 26) are symmetric under negation by construction, so every entry resolves. Computed once per
		// Propagate call (linear scan over <=26 entries) rather than per-relax — see the header comment on Propagate.
		const int32 NumOffsets = NeighborOffsets.Num();
		TArray<int32, TInlineAllocator<26>> RevIndex;
		RevIndex.SetNumUninitialized(NumOffsets);
		for (int32 j = 0; j < NumOffsets; ++j)
		{
			const FIntVector Neg(-NeighborOffsets[j].X, -NeighborOffsets[j].Y, -NeighborOffsets[j].Z);
			int32 Found = INDEX_NONE;
			for (int32 k = 0; k < NumOffsets; ++k)
			{
				if (NeighborOffsets[k] == Neg)
				{
					Found = k;
					break;
				}
			}
			checkf(Found != INDEX_NONE, TEXT("HoverWindowCore neighbour table is not symmetric under negation (index %d)"), j);
			RevIndex[j] = Found;
		}

		// Frontier is a plain flat array + read cursor, not TQueue: uniform-cost BFS never needs to reorder or
		// re-visit, so a ring buffer is unnecessary and TQueue's per-node heap allocation would show up directly in
		// the bench (ADR 0009 invariant 7 — this window's propagation cost is exactly what's being measured here).
		TArray<int32> Frontier;
		Frontier.Reserve(NumCells);
		int32 ReadCursor = 0;

		int32 SeededCount = 0;
		for (const int32 Src : Sources)
		{
			if (Src < 0 || Src >= NumCells)
			{
				continue;
			}
			if (IsOccupied(Occupancy, Src))
			{
				continue; // an occupied source cell can't seed a flood originating inside geometry
			}
			if (Field.Dist[Src] == UnreachedDist)
			{
				Field.Dist[Src] = 0;
				Frontier.Add(Src);
				++SeededCount;
			}
		}

		if (SeededCount == 0)
		{
			return 0;
		}

		const int32 Slice = Dims.DimX * Dims.DimY; // cells per Z-layer, used to decode a flat index back to X/Y/Z
		while (ReadCursor < Frontier.Num())
		{
			const int32 Current = Frontier[ReadCursor++];
			const int32 CurDist = Field.Dist[Current];
			// Decode X/Y/Z from the flat index rather than walking the offset arithmetically on the 1D index: adding
			// an offset directly to a flat index wraps across the X boundary into the next row/slice, so every
			// neighbour must be computed in X/Y/Z space and bounds-checked per axis before re-flattening.
			const int32 CZ = Current / Slice;
			const int32 Rem = Current - CZ * Slice;
			const int32 CY = Rem / Dims.DimX;
			const int32 CX = Rem - CY * Dims.DimX;

			for (int32 j = 0; j < NumOffsets; ++j)
			{
				const FIntVector& Offset = NeighborOffsets[j];
				const int32 NX = CX + Offset.X;
				const int32 NY = CY + Offset.Y;
				const int32 NZ = CZ + Offset.Z;
				if (NX < 0 || NX >= Dims.DimX || NY < 0 || NY >= Dims.DimY || NZ < 0 || NZ >= Dims.DimZ)
				{
					continue;
				}

				const int32 NCell = Dims.CellIndex(NX, NY, NZ);
				if (IsOccupied(Occupancy, NCell) || Field.Dist[NCell] != UnreachedDist)
				{
					continue; // walled off, or already reached by an earlier (equal-or-shorter) path
				}

				Field.Dist[NCell] = static_cast<uint16>(CurDist + 1);
				// StepDir[NCell] names the direction FROM NCell BACK toward Current (the neighbour one step closer
				// to a source) — RevIndex[j] is exactly that reverse offset's table index (see header comment).
				Field.StepDir[NCell] = static_cast<uint8>(RevIndex[j] + 1);
				Frontier.Add(NCell);
			}
		}

		return SeededCount;
	}
}
