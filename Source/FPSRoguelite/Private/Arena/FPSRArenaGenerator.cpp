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
		OutError = TEXT("SlotGridOptions is empty — the seed has no lattice to pick.");
		return false;
	}

	// Same compile-time caps the flow field enforces. Checking here means an over-budget arena is a content
	// error caught at author time, instead of BuildFromWorldTrace's old behaviour of silently coarsening the
	// cell size and quietly degrading routing quality (ADR 0010 D-axis-2).
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
	// width; otherwise that option would silently produce fewer clusters than the designer asked for.
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

bool FFPSRArenaGenerator::Generate(int32 Seed, const FFPSRArenaGenParams& Params, const FVector& ArenaOrigin, FFPSRArenaLayout& OutLayout)
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
	const int32 Inset = Params.MinCorridorWidthCells;
	const int32 Margin = Params.BoundaryMarginCells;

	OutLayout.Seed = Seed;
	OutLayout.GridDims = Params.ArenaSizeCells;
	OutLayout.CellSize = Params.CellSize;
	OutLayout.GridOrigin = ArenaOrigin;

	FRandomStream RS(Seed);

	// --- L0: cluster lattice -------------------------------------------------------------------------------
	const FIntPoint Lattice = Params.SlotGridOptions[RS.RandRange(0, Params.SlotGridOptions.Num() - 1)];
	OutLayout.SlotGrid = Lattice;

	const int32 InteriorW = W - 2 * Margin;
	const int32 InteriorH = H - 2 * Margin;
	const int32 FillMinPct = ToPercent(Params.ClusterFillMin);
	const int32 FillMaxPct = ToPercent(Params.ClusterFillMax);

	OutLayout.Clusters.Reserve(Lattice.X * Lattice.Y);

	// Fixed iteration order (rows, then columns) — the random stream is consumed in a defined sequence, which is
	// half of what makes two machines agree. The other half is that every decision below is integer.
	for (int32 SY = 0; SY < Lattice.Y; ++SY)
	{
		int32 SlotBeginY, SlotEndY;
		SplitRange(InteriorH, Lattice.Y, SY, SlotBeginY, SlotEndY);

		for (int32 SX = 0; SX < Lattice.X; ++SX)
		{
			int32 SlotBeginX, SlotEndX;
			SplitRange(InteriorW, Lattice.X, SX, SlotBeginX, SlotEndX);

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
			OutLayout.Clusters.Add(Cluster);
		}
	}

	// --- Surface data: what the flow field will adopt verbatim ---------------------------------------------
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
			const int32 Cell = CY * W + CX;

			// rank0 is the single floor plane; rank1 stays MAX_flt (absent) because ADR 0010 made the arena
			// single-plane, so the 2-layer surface graph costs one unused slot and nothing else.
			S.CellFloorZ[SurfIndex(Cell, 0)] = static_cast<float>(ArenaOrigin.Z);

			// Edges are opened everywhere inside the grid, INCLUDING into cluster cells. Traversability is decided
			// by BlockedField, not by removing edges — that is how the flow field itself reads a surface
			// (CellFloorZ != MAX_flt && !BlockedField), and how door stamping expects to unblock a cell later.
			// The arena boundary needs no wall of blocked cells: the grid simply has no edge pointing outward.
			if (CX + 1 < W) { S.EdgeMask[Cell * 2 + 0] |= Rank0EdgeBit; }
			if (CY + 1 < H) { S.EdgeMask[Cell * 2 + 1] |= Rank0EdgeBit; }
		}
	}

	for (const FFPSRArenaCluster& Cluster : OutLayout.Clusters)
	{
		for (int32 CY = Cluster.MinCell.Y; CY <= Cluster.MaxCell.Y; ++CY)
		{
			for (int32 CX = Cluster.MinCell.X; CX <= Cluster.MaxCell.X; ++CX)
			{
				S.BlockedField[SurfIndex(CY * W + CX, 0)] = true;
			}
		}
	}

	UE_LOG(LogFPSR, Log, TEXT("[Arena] Generated seed=%d grid=%dx%d cell=%.0f lattice=%dx%d clusters=%d origin=%s"),
		Seed, W, H, Params.CellSize, Lattice.X, Lattice.Y, OutLayout.Clusters.Num(), *ArenaOrigin.ToString());

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
		&& !Layout.Surface.BlockedField[Surf];
}
