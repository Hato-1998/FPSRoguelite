// Copyright Epic Games, Inc. All Rights Reserved.

#include "Arena/FPSRArenaGenerator.h"
#include "Enemy/FPSRFlowFieldComputer.h"
#include "Core/FPSRLogChannels.h"
#include "Math/RandomStream.h"

namespace
{
	constexpr int32 NL = UFPSRFlowFieldComputer::NumLayers; // 2

	/** Must match UFPSRFlowFieldComputer's own Surf(Cell, Rank) = Cell * NumLayers + Rank. */
	FORCEINLINE int32 SurfIndex(int32 Cell, int32 Rank) { return Cell * NL + Rank; }

	/** The rank0<->rank0 connectivity bit inside an EdgeMask byte. A single-plane arena never uses another one:
	 *  rank1 is left absent (CellFloorZ = MAX_flt), which is what makes NumLayers=2 cost nothing here. */
	constexpr uint8 Rank0EdgeBit = static_cast<uint8>(1u << (0 * NL + 0));

	/** Integer split of [0, Total) into Parts contiguous ranges; returns the half-open [Begin, End) of index I. */
	FORCEINLINE void SplitRange(int32 Total, int32 Parts, int32 I, int32& OutBegin, int32& OutEnd)
	{
		OutBegin = (Total * I) / Parts;
		OutEnd = (Total * (I + 1)) / Parts;
	}

	/** Turn a 0..1 fill fraction into whole percent. Done ONCE from the params so every per-cluster size decision
	 *  downstream is pure integer arithmetic — floats would make determinism depend on FP settings, and ADR 0010
	 *  invariant 10 has to hold across machines, not just across runs on this one. */
	FORCEINLINE int32 ToPercent(float Fraction)
	{
		return FMath::Clamp(FMath::RoundToInt(Fraction * 100.0f), 1, 100);
	}
}

bool FFPSRArenaGenParams::Validate(FString& OutError) const
{
	if (CellSize <= 0.0f)
	{
		OutError = TEXT("CellSize must be > 0.");
		return false;
	}
	if (ClimbableStepHeight <= 0.0f)
	{
		OutError = TEXT("ClimbableStepHeight must be > 0 (the flow field requires a uniform positive step).");
		return false;
	}
	if (ArenaSizeCells.X < 1 || ArenaSizeCells.Y < 1)
	{
		OutError = TEXT("ArenaSizeCells must be at least 1x1.");
		return false;
	}
	if (MinCorridorWidthCells < 1)
	{
		OutError = TEXT("MinCorridorWidthCells must be >= 1.");
		return false;
	}
	if (BoundaryMarginCells < 0)
	{
		OutError = TEXT("BoundaryMarginCells must be >= 0.");
		return false;
	}
	if (ClusterFillMin <= 0.0f || ClusterFillMax > 1.0f || ClusterFillMin > ClusterFillMax)
	{
		OutError = TEXT("Cluster fill range must satisfy 0 < Min <= Max <= 1.");
		return false;
	}
	if (SlotGridOptions.Num() == 0)
	{
		OutError = TEXT("SlotGridOptions is empty — the lattice proposal has no shape to pick.");
		return false;
	}

	// Same compile-time caps the flow field enforces. Checking here means an over-budget arena is a content error
	// caught at author time, instead of BuildFromWorldTrace's old behaviour of silently coarsening the cell size
	// and quietly degrading routing quality (ADR 0010 axis 2 / 0011 E1).
	const int64 TotalCells = static_cast<int64>(ArenaSizeCells.X) * ArenaSizeCells.Y;
	if (ArenaSizeCells.X > UFPSRFlowFieldComputer::GetMaxGridDimPerAxis() ||
		ArenaSizeCells.Y > UFPSRFlowFieldComputer::GetMaxGridDimPerAxis() ||
		TotalCells > UFPSRFlowFieldComputer::GetMaxTotalCells())
	{
		OutError = FString::Printf(
			TEXT("Arena %dx%d (%lld cells) exceeds the flow-field budget (axis <= %d, total <= %d)."),
			ArenaSizeCells.X, ArenaSizeCells.Y, TotalCells,
			UFPSRFlowFieldComputer::GetMaxGridDimPerAxis(), UFPSRFlowFieldComputer::GetMaxTotalCells());
		return false;
	}

	// Every lattice option has to leave room for a >=1 cell cluster once both sides are inset by the corridor
	// width; otherwise that option would silently propose fewer clusters than the designer asked for.
	const int32 InteriorW = ArenaSizeCells.X - 2 * BoundaryMarginCells;
	const int32 InteriorH = ArenaSizeCells.Y - 2 * BoundaryMarginCells;
	const int32 MinSlotSpan = 2 * MinCorridorWidthCells + 1;
	if (InteriorW < MinSlotSpan || InteriorH < MinSlotSpan)
	{
		OutError = FString::Printf(
			TEXT("Interior %dx%d is too small for a single cluster (needs >= %d per axis after the boundary margin)."),
			InteriorW, InteriorH, MinSlotSpan);
		return false;
	}
	for (const FIntPoint& Lattice : SlotGridOptions)
	{
		if (Lattice.X < 1 || Lattice.Y < 1)
		{
			OutError = TEXT("SlotGridOptions contains a lattice with a zero or negative axis.");
			return false;
		}
		if (InteriorW / Lattice.X < MinSlotSpan || InteriorH / Lattice.Y < MinSlotSpan)
		{
			OutError = FString::Printf(
				TEXT("Lattice %dx%d does not fit: each slot needs >= %d cells per axis, interior is %dx%d."),
				Lattice.X, Lattice.Y, MinSlotSpan, InteriorW, InteriorH);
			return false;
		}
	}

	return true;
}

bool FFPSRArenaGenerator::ProposeLatticeClusters(int32 Seed, const FFPSRArenaGenParams& Params,
	TArray<FFPSRArenaCluster>& OutClusters, FIntPoint& OutLattice)
{
	OutClusters.Reset();
	OutLattice = FIntPoint::ZeroValue;

	FString Error;
	if (!Params.Validate(Error))
	{
		UE_LOG(LogFPSR, Error, TEXT("[Arena] ProposeLatticeClusters rejected: %s"), *Error);
		return false;
	}

	const int32 Inset = Params.MinCorridorWidthCells;
	const int32 Margin = Params.BoundaryMarginCells;
	const int32 InteriorW = Params.ArenaSizeCells.X - 2 * Margin;
	const int32 InteriorH = Params.ArenaSizeCells.Y - 2 * Margin;

	FRandomStream RS(Seed);
	OutLattice = Params.SlotGridOptions[RS.RandRange(0, Params.SlotGridOptions.Num() - 1)];

	const int32 FillMinPct = ToPercent(Params.ClusterFillMin);
	const int32 FillMaxPct = ToPercent(Params.ClusterFillMax);
	OutClusters.Reserve(OutLattice.X * OutLattice.Y);

	// Fixed iteration order (rows, then columns) — the random stream is consumed in a defined sequence, which is
	// half of what makes two machines agree. The other half is that every decision below is integer.
	for (int32 SY = 0; SY < OutLattice.Y; ++SY)
	{
		int32 SlotBeginY, SlotEndY;
		SplitRange(InteriorH, OutLattice.Y, SY, SlotBeginY, SlotEndY);

		for (int32 SX = 0; SX < OutLattice.X; ++SX)
		{
			int32 SlotBeginX, SlotEndX;
			SplitRange(InteriorW, OutLattice.X, SX, SlotBeginX, SlotEndX);

			// Inset on ALL sides by the corridor width. Two clusters in adjacent slots therefore end up 2x Inset
			// apart, and a cluster is Margin + Inset from the arena wall — so both the crossing corridors and the
			// outer ring exist by construction, and neither can be narrower than the authored minimum.
			const int32 AvailMinX = Margin + SlotBeginX + Inset;
			const int32 AvailMaxX = Margin + SlotEndX - 1 - Inset;
			const int32 AvailMinY = Margin + SlotBeginY + Inset;
			const int32 AvailMaxY = Margin + SlotEndY - 1 - Inset;

			const int32 AvailW = AvailMaxX - AvailMinX + 1;
			const int32 AvailH = AvailMaxY - AvailMinY + 1;
			if (AvailW < 1 || AvailH < 1)
			{
				continue; // Validate() rules this out for authored params; kept so a hand-built params can't crash.
			}

			const int32 ClusterW = FMath::Clamp((AvailW * RS.RandRange(FillMinPct, FillMaxPct)) / 100, 1, AvailW);
			const int32 ClusterH = FMath::Clamp((AvailH * RS.RandRange(FillMinPct, FillMaxPct)) / 100, 1, AvailH);

			FFPSRArenaCluster Cluster;
			Cluster.MinCell.X = AvailMinX + RS.RandRange(0, AvailW - ClusterW);
			Cluster.MinCell.Y = AvailMinY + RS.RandRange(0, AvailH - ClusterH);
			Cluster.MaxCell.X = Cluster.MinCell.X + ClusterW - 1;
			Cluster.MaxCell.Y = Cluster.MinCell.Y + ClusterH - 1;
			OutClusters.Add(Cluster);
		}
	}

	return true;
}

FFPSRArenaAuthoredBox FFPSRArenaGenerator::ClusterToAuthoredBox(const FFPSRArenaCluster& Cluster,
	const FFPSRArenaGenParams& Params, const FVector& ArenaOrigin)
{
	const double Cell = Params.CellSize;
	const double MinX = ArenaOrigin.X + Cluster.MinCell.X * Cell;
	const double MinY = ArenaOrigin.Y + Cluster.MinCell.Y * Cell;
	const double SizeX = Cluster.WidthCells() * Cell;
	const double SizeY = Cluster.HeightCells() * Cell;

	FFPSRArenaAuthoredBox Box;
	Box.Center = FVector(MinX + SizeX * 0.5, MinY + SizeY * 0.5, ArenaOrigin.Z);
	Box.HalfExtentXY = FVector2D(SizeX * 0.5, SizeY * 0.5);
	Box.YawDegrees = 0.0f;
	return Box;
}

void FFPSRArenaGenerator::ComputeDestructibleCells(const FFPSRArenaAuthoredDestructible& Destructible,
	const FVector& ArenaOrigin, float CellSize, const FIntPoint& GridDims, TArray<int32>& OutCells)
{
	OutCells.Reset();
	if (CellSize <= 0.0f || GridDims.X <= 0 || GridDims.Y <= 0)
	{
		return;
	}

	const int32 AnchorCX = FMath::FloorToInt((Destructible.Location.X - ArenaOrigin.X) / CellSize);
	const int32 AnchorCY = FMath::FloorToInt((Destructible.Location.Y - ArenaOrigin.Y) / CellSize);
	const int32 FootprintX = FMath::Max(1, Destructible.FootprintCells.X);
	const int32 FootprintY = FMath::Max(1, Destructible.FootprintCells.Y);

	// Row-major (Y outer, X inner): since every surviving CX is < GridDims.X, each row's indices are entirely
	// below the next row's — so this loop order is what MAKES the ascending-order guarantee true, not an
	// incidental side effect of it.
	for (int32 DY = 0; DY < FootprintY; ++DY)
	{
		const int32 CY = AnchorCY + DY;
		if (CY < 0 || CY >= GridDims.Y) { continue; }
		for (int32 DX = 0; DX < FootprintX; ++DX)
		{
			const int32 CX = AnchorCX + DX;
			if (CX < 0 || CX >= GridDims.X) { continue; }
			OutCells.Add(CY * GridDims.X + CX);
		}
	}
}

namespace
{
	/**
	 * L1 (ADR 0010 D5 / 0011 E6): fill the slack the authored skeleton left with micro props.
	 *
	 * The hard part is not placing them, it is placing them without breaking the invariants the designer just
	 * satisfied. This does it with a LOCAL check rather than a place-then-validate-then-retry loop:
	 *
	 * Every open cell must keep run >= MinCorridorWidth on BOTH axes (that is what the validator measures).
	 * Blocking one cell only changes the X-runs of its own row and the Y-runs of its own column, so checking the
	 * four segments around the candidate is sufficient — and a segment is acceptable only if it vanishes entirely
	 * or stays at least MinCorridorWidth long. A chain of props therefore cannot leave a sub-minimum gap between
	 * them, which is also what stops props from quietly walling a corridor off.
	 *
	 * Placement order is a seeded Fisher-Yates over a fixed candidate list — deterministic on every machine
	 * (invariant 10) while still looking scattered rather than raster-scanned.
	 */
	/** Rotate a cell offset by 90-degree steps. */
	FORCEINLINE FIntPoint RotateOffset(const FIntPoint& O, int32 Quarter)
	{
		switch (Quarter & 3)
		{
			case 1:  return FIntPoint(-O.Y, O.X);
			case 2:  return FIntPoint(-O.X, -O.Y);
			case 3:  return FIntPoint(O.Y, -O.X);
			default: return O;
		}
	}

	void PlaceMicroProps(FRandomStream& RS, const FFPSRArenaGenParams& Params, FFPSRArenaLayout& Layout)
	{
		const int32 W = Layout.GridDims.X;
		const int32 H = Layout.GridDims.Y;
		const int32 MinW = FMath::Max(1, Params.MinCorridorWidthCells);
		FFPSRFlowFieldSurfaceData& S = Layout.Surface;

		// No authored sets = no props. Deliberately not a fallback to something procedural: an empty list is how
		// the designer asks to judge the authored skeleton on its own (ADR 0011 invariant 12 baseline).
		if (Params.PropSets.Num() == 0) { return; }

		auto Open = [&Layout](int32 CX, int32 CY) { return FFPSRArenaGenerator::IsCellOpen(Layout, CX, CY); };

		// Landmarks are navigation beacons (0010 D6). Burying one is not a cosmetic problem — it removes the only
		// thing telling a first-person player which way they are facing.
		TArray<bool> Reserved;
		Reserved.Init(false, W * H);
		for (const FFPSRArenaAuthoredLandmark& LM : Layout.Landmarks)
		{
			const int32 LCX = FMath::FloorToInt((LM.Location.X - Layout.GridOrigin.X) / Layout.CellSize);
			const int32 LCY = FMath::FloorToInt((LM.Location.Y - Layout.GridOrigin.Y) / Layout.CellSize);
			const int32 R = FMath::Max(0, LM.ReserveRadiusCells);
			for (int32 DY = -R; DY <= R; ++DY)
			{
				for (int32 DX = -R; DX <= R; ++DX)
				{
					const int32 CX = LCX + DX;
					const int32 CY = LCY + DY;
					if (CX >= 0 && CY >= 0 && CX < W && CY < H) { Reserved[CY * W + CX] = true; }
				}
			}
		}

		// Survey: how much room is there, and how much of it is spare?
		int32 OpenCells = 0;
		int32 SlackCells = 0;
		for (int32 CY = 0; CY < H; ++CY)
		{
			for (int32 CX = 0; CX < W; ++CX)
			{
				if (!Open(CX, CY)) { continue; }
				++OpenCells;

				int32 RunX = 1, RunY = 1;
				for (int32 X = CX - 1; Open(X, CY); --X) { ++RunX; }
				for (int32 X = CX + 1; Open(X, CY); ++X) { ++RunX; }
				for (int32 Y = CY - 1; Open(CX, Y); --Y) { ++RunY; }
				for (int32 Y = CY + 1; Open(CX, Y); ++Y) { ++RunY; }
				if (FMath::Min(RunX, RunY) > MinW) { ++SlackCells; }
			}
		}
		if (OpenCells == 0) { return; }

		const int32 SlackBudget = FMath::Max(0, FMath::RoundToInt(SlackCells * Params.MaxSlackConsumption));
		int32 SlackUsed = 0;

		int32 TotalWeight = 0;
		for (const FFPSRArenaPropSet& Set : Params.PropSets) { TotalWeight += FMath::Max(1, Set.Weight); }
		if (TotalWeight <= 0) { return; }

		TArray<bool> Used;
		Used.Init(false, W * H);

		auto SegmentOk = [MinW](int32 Len) { return Len == 0 || Len >= MinW; };

		// Stratified placement: one attempt per lattice cell, anchored at a jittered point inside it. Uniform
		// random at this count clumps in places and leaves bald patches elsewhere; a jittered lattice covers
		// evenly while still not reading as a grid.
		const int32 Pitch = FMath::Max(4, Params.PropSetSpacingCells);
		const int32 Jitter = FMath::Clamp(Params.PropSetJitterCells, 0, Pitch / 2);

		for (int32 LY = 0; LY * Pitch < H; ++LY)
		{
			for (int32 LX = 0; LX * Pitch < W; ++LX)
			{
				// Pick the set first so the stream is consumed identically whether or not the placement lands.
				int32 Roll = RS.RandRange(0, TotalWeight - 1);
				const FFPSRArenaPropSet* Chosen = &Params.PropSets[0];
				for (const FFPSRArenaPropSet& Set : Params.PropSets)
				{
					Roll -= FMath::Max(1, Set.Weight);
					if (Roll < 0) { Chosen = &Set; break; }
				}
				const int32 Quarter = Chosen->bAllowRotation ? RS.RandRange(0, 3) : 0;

				// A few jittered attempts inside this lattice cell, then give up on it. Bounded on purpose —
				// ADR 0010 rules out unbounded regeneration loops, and a cell that cannot host a set is a
				// legitimate outcome (it is probably inside a cluster).
				bool bPlaced = false;
				for (int32 Attempt = 0; Attempt < 3 && !bPlaced; ++Attempt)
				{
					const int32 AX = LX * Pitch + Pitch / 2 + (Jitter > 0 ? RS.RandRange(-Jitter, Jitter) : 0);
					const int32 AY = LY * Pitch + Pitch / 2 + (Jitter > 0 ? RS.RandRange(-Jitter, Jitter) : 0);

					// Pass 1: can every cell of the set even go there?
					bool bFits = true;
					for (const FFPSRArenaPropSetEntry& E : Chosen->Entries)
					{
						const FIntPoint Off = RotateOffset(E.CellOffset, Quarter);
						const int32 CX = AX + Off.X;
						const int32 CY = AY + Off.Y;
						if (CX < 0 || CY < 0 || CX >= W || CY >= H || !Open(CX, CY)
							|| Reserved[CY * W + CX] || Used[CY * W + CX])
						{
							bFits = false;
							break;
						}
					}
					if (!bFits) { continue; }

					// Pass 2: apply the blocking entries one at a time, checking the corridor rule after each —
					// entries within one set narrow each other, so they have to be evaluated in sequence rather
					// than against the state the set started from. Any failure rolls the whole set back, so a
					// half-placed group can never survive.
					TArray<int32> BlockedNow;
					bool bSafe = true;
					for (const FFPSRArenaPropSetEntry& E : Chosen->Entries)
					{
						if (E.Tier != EFPSRArenaPropTier::Blocking) { continue; }
						const FIntPoint Off = RotateOffset(E.CellOffset, Quarter);
						const int32 CX = AX + Off.X;
						const int32 CY = AY + Off.Y;

						int32 Left = 0, Right = 0, Up = 0, Down = 0;
						for (int32 X = CX - 1; Open(X, CY); --X) { ++Left; }
						for (int32 X = CX + 1; Open(X, CY); ++X) { ++Right; }
						for (int32 Y = CY - 1; Open(CX, Y); --Y) { ++Up; }
						for (int32 Y = CY + 1; Open(CX, Y); ++Y) { ++Down; }
						if (!SegmentOk(Left) || !SegmentOk(Right) || !SegmentOk(Up) || !SegmentOk(Down))
						{
							bSafe = false;
							break;
						}
						const int32 Cell = CY * W + CX;
						S.BlockedField[SurfIndex(Cell, 0)] = true;
						BlockedNow.Add(Cell);
					}
					if (!bSafe || SlackUsed + BlockedNow.Num() > SlackBudget)
					{
						for (int32 Cell : BlockedNow) { S.BlockedField[SurfIndex(Cell, 0)] = false; }
						continue;
					}
					SlackUsed += BlockedNow.Num();

					for (const FFPSRArenaPropSetEntry& E : Chosen->Entries)
					{
						const FIntPoint Off = RotateOffset(E.CellOffset, Quarter);
						FFPSRArenaProp Prop;
						Prop.Cell = FIntPoint(AX + Off.X, AY + Off.Y);
						Prop.Tier = E.Tier;
						Prop.Variant = E.Variant;
						Prop.YawDegrees = E.YawDegrees + 90.0f * Quarter;
						Layout.Props.Add(Prop);
						Used[Prop.Cell.Y * W + Prop.Cell.X] = true;
					}
					bPlaced = true;
				}
			}
		}
	}
}

namespace
{
	/**
	 * L2 (ADR 0010 D8): derive floor wiring traces from the L0 corridor skeleton.
	 *
	 * Three deterministic, seedless passes over the open-cell mask (Layout.Surface as it stands when called —
	 * see FFPSRArenaGenerator::DeriveFloorTraces for why call order matters):
	 *  (A) Chebyshev distance transform  - how far every open cell is from the nearest wall.
	 *  (B) Zhang-Suen thinning           - collapse the open region to a 1-cell-wide topological skeleton.
	 *  (C) walk the skeleton             - one trace segment per adjacent skeleton pair; junctions where it forks.
	 *
	 * Nothing here touches FRandomStream, FMath::Rand, or any other source of randomness — see the header
	 * comment on DeriveFloorTraces for why that is a stronger guarantee than this class's usual determinism.
	 */

	/** Zhang-Suen neighbour offsets, clockwise from north: P2=N, P3=NE, P4=E, P5=SE, P6=S, P7=SW, P8=W, P9=NW.
	 *  Index i corresponds to P(i+2); also reused (unchanged) for the 8-neighbour degree/adjacency walk in
	 *  stage (C), since a skeleton cell's "junction-ness" and its trace partners are both just its 8-neighbours. */
	constexpr int32 ZSOffsetX[8] = { 0,  1, 1, 1, 0, -1, -1, -1 };
	constexpr int32 ZSOffsetY[8] = { -1, -1, 0, 1, 1,  1,  0, -1 };

	/** (A) Chessboard (Chebyshev) distance-to-nearest-wall for every cell, via the standard two-pass chamfer
	 *  raster scan: forward pass pulls from the 4 8-neighbours already visited in raster order (Y asc, X asc),
	 *  backward pass pulls from the remaining 4. Unlike the same technique applied to Euclidean distance, this is
	 *  EXACT (not an approximation) for the Chebyshev metric. Blocked cells and off-grid neighbours both read as
	 *  distance 0, and blocked cells are never revisited after that, so they stay 0 through both passes. */
	void ComputeChebyshevWallDistance(const FFPSRArenaLayout& Layout, TArray<int32>& OutDist)
	{
		const int32 W = Layout.GridDims.X;
		const int32 H = Layout.GridDims.Y;
		OutDist.Init(0, W * H);

		auto DistAt = [&OutDist, W, H](int32 CX, int32 CY) -> int32
		{
			return (CX < 0 || CY < 0 || CX >= W || CY >= H) ? 0 : OutDist[CY * W + CX];
		};

		// Forward pass (Y asc, X asc): NW, N, NE, W are already processed at this point in the scan.
		for (int32 CY = 0; CY < H; ++CY)
		{
			for (int32 CX = 0; CX < W; ++CX)
			{
				if (!FFPSRArenaGenerator::IsCellOpen(Layout, CX, CY)) { continue; } // blocked cells stay 0
				int32 M = DistAt(CX - 1, CY - 1);
				M = FMath::Min(M, DistAt(CX, CY - 1));
				M = FMath::Min(M, DistAt(CX + 1, CY - 1));
				M = FMath::Min(M, DistAt(CX - 1, CY));
				OutDist[CY * W + CX] = 1 + M;
			}
		}
		// Backward pass (Y desc, X desc): SE, S, SW, E are already processed at this point in the reverse scan.
		for (int32 CY = H - 1; CY >= 0; --CY)
		{
			for (int32 CX = W - 1; CX >= 0; --CX)
			{
				if (!FFPSRArenaGenerator::IsCellOpen(Layout, CX, CY)) { continue; }
				int32 M = DistAt(CX + 1, CY + 1);
				M = FMath::Min(M, DistAt(CX, CY + 1));
				M = FMath::Min(M, DistAt(CX - 1, CY + 1));
				M = FMath::Min(M, DistAt(CX + 1, CY));
				const int32 Cell = CY * W + CX;
				OutDist[Cell] = FMath::Min(OutDist[Cell], 1 + M);
			}
		}
	}

	/** (B) Zhang-Suen thinning: reduce the open-cell mask to a 1-cell-wide skeleton, in place. Off-grid reads as
	 *  background (false / wall), per the standard algorithm. Bounded at 256 iterations — a corridor mask
	 *  converges in far fewer in practice, but nothing here proves that, so the cap is what stands between a
	 *  pathological mask and a hang; hitting it still returns a usable (if imperfectly thinned) skeleton. */
	void ThinToSkeleton(int32 W, int32 H, TArray<bool>& Skel)
	{
		auto At = [&Skel, W, H](int32 CX, int32 CY) -> bool
		{
			return (CX < 0 || CY < 0 || CX >= W || CY >= H) ? false : Skel[CY * W + CX];
		};

		constexpr int32 MaxIterations = 256;
		TArray<int32> ToDelete;

		for (int32 Iter = 0; Iter < MaxIterations; ++Iter)
		{
			bool bAnyDeleted = false;

			for (int32 SubIter = 0; SubIter < 2; ++SubIter)
			{
				ToDelete.Reset();

				for (int32 CY = 0; CY < H; ++CY)
				{
					for (int32 CX = 0; CX < W; ++CX)
					{
						if (!Skel[CY * W + CX]) { continue; }

						bool N[8];
						for (int32 i = 0; i < 8; ++i) { N[i] = At(CX + ZSOffsetX[i], CY + ZSOffsetY[i]); }

						// B(P1): how many of the 8 neighbours are foreground.
						const int32 B = N[0] + N[1] + N[2] + N[3] + N[4] + N[5] + N[6] + N[7];
						if (B < 2 || B > 6) { continue; }

						// A(P1): 0->1 transitions walking P2..P9..P2 — exactly 1 means removing this pixel cannot
						// disconnect its neighbourhood (the classic Zhang-Suen connectivity guard).
						int32 A = 0;
						for (int32 i = 0; i < 8; ++i) { if (!N[i] && N[(i + 1) & 7]) { ++A; } }
						if (A != 1) { continue; }

						// N[0]=P2(N), N[2]=P4(E), N[4]=P6(S), N[6]=P8(W).
						const bool bC3 = (SubIter == 0) ? !(N[0] && N[2] && N[4]) : !(N[0] && N[2] && N[6]);
						const bool bC4 = (SubIter == 0) ? !(N[2] && N[4] && N[6]) : !(N[0] && N[4] && N[6]);
						if (bC3 && bC4) { ToDelete.Add(CY * W + CX); }
					}
				}

				// Every candidate this sub-iteration found is collected above and applied HERE, all at once — not
				// as each one is found. Deleting in place mid-scan would let an early deletion change a
				// not-yet-visited neighbour's B/A count within the SAME sub-iteration, making the result depend on
				// raster scan order. That is exactly the non-determinism this whole L2 pass exists to avoid
				// (ADR 0010 invariant 10 applies to this derived data too, even though it takes no seed).
				if (ToDelete.Num() > 0)
				{
					bAnyDeleted = true;
					for (const int32 Idx : ToDelete) { Skel[Idx] = false; }
				}
			}

			if (!bAnyDeleted)
			{
				return; // converged
			}

			if (Iter == MaxIterations - 1)
			{
				UE_LOG(LogFPSR, Warning,
					TEXT("[Arena] DeriveFloorTraces: thinning hit the %d-iteration cap without converging; using the partial skeleton."),
					MaxIterations);
			}
		}
	}
}

void FFPSRArenaGenerator::DeriveFloorTraces(FFPSRArenaLayout& Layout)
{
	Layout.Traces.Reset();
	Layout.TraceJunctions.Reset();

	const int32 W = Layout.GridDims.X;
	const int32 H = Layout.GridDims.Y;
	if (W <= 0 || H <= 0) { return; }

	// (A) Distance to the nearest wall, per open cell. Becomes each trace's WidthClass below.
	TArray<int32> Dist;
	ComputeChebyshevWallDistance(Layout, Dist);

	// (B) Thin the CURRENT open-cell mask (IsCellOpen, same predicate the flow field uses) to a 1-cell skeleton.
	// Becomes the trace centrelines.
	TArray<bool> Skel;
	Skel.Init(false, W * H);
	for (int32 CY = 0; CY < H; ++CY)
	{
		for (int32 CX = 0; CX < W; ++CX)
		{
			if (FFPSRArenaGenerator::IsCellOpen(Layout, CX, CY)) { Skel[CY * W + CX] = true; }
		}
	}
	ThinToSkeleton(W, H, Skel);

	auto SkelAt = [&Skel, W, H](int32 CX, int32 CY) -> bool
	{
		return (CX < 0 || CY < 0 || CX >= W || CY >= H) ? false : Skel[CY * W + CX];
	};

	// (C1) Degree (8-neighbour skeleton count) per skeleton cell -> junctions. Collected in raster order so
	// TraceJunctions comes out cell-index ascending with no explicit sort. Done as its own pass (rather than
	// inline with C2 below) because a segment needs to know BOTH endpoints' junction status, including an
	// endpoint with a higher cell index that the raster scan has not reached yet when a segment is emitted.
	TArray<bool> IsJunction;
	IsJunction.Init(false, W * H);
	for (int32 CY = 0; CY < H; ++CY)
	{
		for (int32 CX = 0; CX < W; ++CX)
		{
			if (!Skel[CY * W + CX]) { continue; }
			int32 Degree = 0;
			for (int32 i = 0; i < 8; ++i) { if (SkelAt(CX + ZSOffsetX[i], CY + ZSOffsetY[i])) { ++Degree; } }
			if (Degree >= 3)
			{
				IsJunction[CY * W + CX] = true;
				Layout.TraceJunctions.Add(FIntPoint(CX, CY));
			}
		}
	}

	// (C2) One segment per adjacent skeleton pair. Emitting only when the neighbour's cell index is greater than
	// this cell's own is what makes each pair appear exactly once with no separate dedup set; the outer loop
	// being the same raster scan is what makes Traces come out cell-index ascending, same as TraceJunctions above.
	for (int32 CY = 0; CY < H; ++CY)
	{
		for (int32 CX = 0; CX < W; ++CX)
		{
			if (!Skel[CY * W + CX]) { continue; }
			const int32 SelfIdx = CY * W + CX;

			for (int32 i = 0; i < 8; ++i)
			{
				const int32 NX = CX + ZSOffsetX[i];
				const int32 NY = CY + ZSOffsetY[i];
				if (NX < 0 || NY < 0 || NX >= W || NY >= H || !Skel[NY * W + NX]) { continue; }
				const int32 NeighborIdx = NY * W + NX;
				if (NeighborIdx <= SelfIdx) { continue; }

				FFPSRArenaTraceSegment Seg;
				Seg.FromCell = FIntPoint(CX, CY);
				Seg.ToCell = FIntPoint(NX, NY);
				Seg.WidthClass = static_cast<uint8>(FMath::Clamp(FMath::Min(Dist[SelfIdx], Dist[NeighborIdx]), 0, 255));
				Seg.bJunction = IsJunction[SelfIdx] || IsJunction[NeighborIdx];
				Layout.Traces.Add(Seg);
			}
		}
	}
}

bool FFPSRArenaGenerator::Generate(int32 Seed, const FFPSRArenaGenParams& Params, const FVector& ArenaOrigin,
	const FFPSRArenaAuthoredInput& Authored, FFPSRArenaLayout& OutLayout)
{
	OutLayout = FFPSRArenaLayout();

	FString Error;
	if (!Params.Validate(Error))
	{
		UE_LOG(LogFPSR, Error, TEXT("[Arena] Generate rejected: %s"), *Error);
		return false;
	}

	const int32 W = Params.ArenaSizeCells.X;
	const int32 H = Params.ArenaSizeCells.Y;
	const double Cell = Params.CellSize;

	OutLayout.Seed = Seed;
	OutLayout.GridDims = Params.ArenaSizeCells;
	OutLayout.CellSize = Params.CellSize;
	OutLayout.GridOrigin = ArenaOrigin;
	OutLayout.Landmarks = Authored.Landmarks;

	// --- base surface: one floor plane, every internal edge open --------------------------------------------
	const int32 NumCells = W * H;
	FFPSRFlowFieldSurfaceData& S = OutLayout.Surface;
	S.GridDimX = W;
	S.GridDimY = H;
	S.GridOrigin = ArenaOrigin;
	S.CellSize = Params.CellSize;
	S.ClimbableStepHeight = Params.ClimbableStepHeight;
	S.CellFloorZ.Init(MAX_flt, NumCells * NL);
	S.BlockedField.Init(false, NumCells * NL);
	S.EdgeMask.Init(0, NumCells * 2);

	for (int32 CY = 0; CY < H; ++CY)
	{
		for (int32 CX = 0; CX < W; ++CX)
		{
			const int32 CellIdx = CY * W + CX;

			// rank0 is the single floor plane; rank1 stays MAX_flt (absent) because the arena is single-plane, so
			// the 2-layer surface graph costs one unused slot and nothing else.
			S.CellFloorZ[SurfIndex(CellIdx, 0)] = static_cast<float>(ArenaOrigin.Z);

			// Edges are opened everywhere inside the grid, INCLUDING into blocked cells. Traversability is decided
			// by BlockedField, not by removing edges — that is how the flow field itself reads a surface
			// (CellFloorZ != MAX_flt && !BlockedField), and how door stamping expects to unblock a cell later.
			// The arena boundary needs no wall of blocked cells: the grid simply has no edge pointing outward.
			if (CX + 1 < W) { S.EdgeMask[CellIdx * 2 + 0] |= Rank0EdgeBit; }
			if (CY + 1 < H) { S.EdgeMask[CellIdx * 2 + 1] |= Rank0EdgeBit; }
		}
	}

	// --- L0: rasterise the authored blocking volumes ---------------------------------------------------------
	auto BlockCell = [&S, W, H](int32 CX, int32 CY) -> bool
	{
		if (CX < 0 || CY < 0 || CX >= W || CY >= H) { return false; }
		S.BlockedField[SurfIndex(CY * W + CX, 0)] = true;
		return true;
	};

	OutLayout.Clusters.Reserve(Authored.Blockers.Num());
	for (const FFPSRArenaAuthoredBox& Box : Authored.Blockers)
	{
		const double Yaw = FMath::DegreesToRadians(static_cast<double>(Box.YawDegrees));
		const double CosY = FMath::Cos(Yaw);
		const double SinY = FMath::Sin(Yaw);

		// A cell counts as blocked when its CENTRE falls inside the box grown by half a cell on each local axis.
		// Growing rather than testing exact overlap is deliberate: it errs toward blocking a partially covered
		// cell. The opposite error would let the swarm path through a cell that has geometry in it, and an enemy
		// walking into a wall is far worse than one taking a slightly wider berth around it.
		const double GrowX = Box.HalfExtentXY.X + Cell * 0.5;
		const double GrowY = Box.HalfExtentXY.Y + Cell * 0.5;

		// World AABB of the rotated box (plus the grow margin) -> the cell range worth testing at all.
		const double AabbX = FMath::Abs(CosY) * GrowX + FMath::Abs(SinY) * GrowY;
		const double AabbY = FMath::Abs(SinY) * GrowX + FMath::Abs(CosY) * GrowY;
		const int32 MinCX = FMath::Max(0, FMath::FloorToInt((Box.Center.X - AabbX - ArenaOrigin.X) / Cell));
		const int32 MaxCX = FMath::Min(W - 1, FMath::CeilToInt((Box.Center.X + AabbX - ArenaOrigin.X) / Cell));
		const int32 MinCY = FMath::Max(0, FMath::FloorToInt((Box.Center.Y - AabbY - ArenaOrigin.Y) / Cell));
		const int32 MaxCY = FMath::Min(H - 1, FMath::CeilToInt((Box.Center.Y + AabbY - ArenaOrigin.Y) / Cell));

		FFPSRArenaCluster Rect;
		bool bAny = false;
		for (int32 CY = MinCY; CY <= MaxCY; ++CY)
		{
			for (int32 CX = MinCX; CX <= MaxCX; ++CX)
			{
				const FVector Centre = OutLayout.CellCenterWorld(CX, CY);
				const double DX = Centre.X - Box.Center.X;
				const double DY = Centre.Y - Box.Center.Y;
				// Into the box's local frame (inverse yaw).
				const double LocalX = DX * CosY + DY * SinY;
				const double LocalY = -DX * SinY + DY * CosY;
				if (FMath::Abs(LocalX) > GrowX || FMath::Abs(LocalY) > GrowY) { continue; }

				if (!BlockCell(CX, CY)) { continue; }
				if (!bAny) { Rect.MinCell = Rect.MaxCell = FIntPoint(CX, CY); bAny = true; }
				else
				{
					Rect.MinCell.X = FMath::Min(Rect.MinCell.X, CX);
					Rect.MinCell.Y = FMath::Min(Rect.MinCell.Y, CY);
					Rect.MaxCell.X = FMath::Max(Rect.MaxCell.X, CX);
					Rect.MaxCell.Y = FMath::Max(Rect.MaxCell.Y, CY);
				}
			}
		}
		if (bAny) { OutLayout.Clusters.Add(Rect); }
	}

	// --- landmarks do NOT occupy cells (F6, 0011 E2 role split) -------------------------------------------------
	// A landmark is a navigation beacon, not a blocker — see AFPSRArenaLandmark's class comment. They still keep
	// PROCEDURAL PROPS off their reserve radius (PlaceMicroProps' Reserved bitmap, built from OutLayout.Landmarks —
	// set above), just never touch S.BlockedField. An author who wants a landmark to also physically block places
	// an AFPSRArenaBlocker on top of it (rasterised by the Authored.Blockers loop above) instead.

	// --- destructibles: authored footprints blocked while intact (ADR 0010 D7) --------------------------------
	// Same stage as landmarks (both are point-authored, non-cluster blocking) and BEFORE L1 on purpose: L1's
	// corridor-width check must see these cells as wall, or a micro prop could legally narrow a corridor right up
	// against a destructible that, once broken, would carve it below the minimum width. ComputeDestructibleCells is
	// the SAME function AFPSRArenaDestructible calls on break (HandleBrokenAuthority) — the generator (closing) and
	// the actor (opening) can never compute a different footprint for one prop.
	OutLayout.DestructibleCells.Reserve(Authored.Destructibles.Num());
	for (const FFPSRArenaAuthoredDestructible& Destructible : Authored.Destructibles)
	{
		TArray<int32> DestructibleCellsOut;
		ComputeDestructibleCells(Destructible, ArenaOrigin, static_cast<float>(Cell), OutLayout.GridDims, DestructibleCellsOut);
		for (const int32 CellIdx : DestructibleCellsOut)
		{
			S.BlockedField[SurfIndex(CellIdx, 0)] = true;
		}
		OutLayout.DestructibleCells.Add(MoveTemp(DestructibleCellsOut));
	}

	// --- L2: derive floor wiring traces from the L0 mask (ADR 0010 D8) ---------------------------------------
	// MUST run here — after L0 (blockers, landmarks, destructibles are all rasterised above) but BEFORE L1
	// (PlaceMicroProps, below). L2 is derived ONLY from L0. If this ran after L1 instead, L1's micro props would
	// have already carved the corridor mask further, so the traces would no longer line up with where the
	// swarm/player can actually walk — which is exactly the external proposal's self-contradiction ADR 0010 D8
	// called out: wiring laid wherever cells happen to be spare, in a place that has nothing to do with the
	// actual paths through the arena.
	DeriveFloorTraces(OutLayout);

	// --- L1: procedural micro props in whatever slack the authored skeleton left -----------------------------
	{
		FRandomStream RS(Seed);
		PlaceMicroProps(RS, Params, OutLayout);
	}

	UE_LOG(LogFPSR, Log,
		TEXT("[Arena] Generated seed=%d grid=%dx%d cell=%.0f authored(blockers=%d landmarks=%d destructibles=%d) props=%d traces=%d junctions=%d origin=%s"),
		Seed, W, H, Params.CellSize, Authored.Blockers.Num(), Authored.Landmarks.Num(), Authored.Destructibles.Num(),
		OutLayout.Props.Num(), OutLayout.Traces.Num(), OutLayout.TraceJunctions.Num(), *ArenaOrigin.ToString());

	return true;
}

bool FFPSRArenaGenerator::IsCellOpen(const FFPSRArenaLayout& Layout, int32 CX, int32 CY)
{
	if (CX < 0 || CY < 0 || CX >= Layout.GridDims.X || CY >= Layout.GridDims.Y)
	{
		return false;
	}
	const int32 Surf = SurfIndex(Layout.CellIndex(CX, CY), 0);
	return Layout.Surface.CellFloorZ.IsValidIndex(Surf)
		&& Layout.Surface.CellFloorZ[Surf] != MAX_flt
		&& Layout.Surface.BlockedField.IsValidIndex(Surf)
		&& !Layout.Surface.BlockedField[Surf];
}
