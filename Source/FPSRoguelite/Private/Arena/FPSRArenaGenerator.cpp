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
	void PlaceMicroProps(FRandomStream& RS, const FFPSRArenaGenParams& Params, FFPSRArenaLayout& Layout)
	{
		const int32 W = Layout.GridDims.X;
		const int32 H = Layout.GridDims.Y;
		const int32 MinW = FMath::Max(1, Params.MinCorridorWidthCells);
		const int32 Spacing = FMath::Max(1, Params.PropMinSpacingCells);
		const int32 Variants = FMath::Max(1, Params.PropVariantCount);
		FFPSRFlowFieldSurfaceData& S = Layout.Surface;

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
		TArray<int32> Candidates;
		Candidates.Reserve(W * H / 4);
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

				if (!Reserved[CY * W + CX]) { Candidates.Add(CY * W + CX); }
			}
		}
		if (OpenCells == 0 || Candidates.Num() == 0) { return; }

		// Two ceilings, and the tighter one wins: the authored density, and the share of slack L1 is allowed to
		// eat. Without the second, a dense seed could shave every corridor to the legal minimum — valid by the
		// letter of the invariants and miserable to move through.
		const int32 DensityBudget = FMath::RoundToInt(OpenCells * Params.BlockingPropsPer100Cells / 100.0f);
		const int32 SlackBudget = FMath::RoundToInt(SlackCells * Params.MaxSlackConsumption);
		const int32 BlockingBudget = FMath::Max(0, FMath::Min(DensityBudget, SlackBudget));
		const int32 PassableBudget = FMath::Max(0, FMath::RoundToInt(OpenCells * Params.PassablePropsPer100Cells / 100.0f));

		// Seeded shuffle of a fixed list: same result everywhere, without the raster-scan look of taking cells in order.
		for (int32 i = Candidates.Num() - 1; i > 0; --i)
		{
			Candidates.Swap(i, RS.RandRange(0, i));
		}

		TArray<bool> SpacingBlocked;
		SpacingBlocked.Init(false, W * H);
		TArray<bool> Used;
		Used.Init(false, W * H);

		// --- blocking tier ---------------------------------------------------------------------------------
		int32 Placed = 0;
		for (int32 Index = 0; Index < Candidates.Num() && Placed < BlockingBudget; ++Index)
		{
			const int32 Cell = Candidates[Index];
			const int32 CX = Cell % W;
			const int32 CY = Cell / W;
			if (SpacingBlocked[Cell] || !Open(CX, CY)) { continue; }

			// Each of the four segments this would carve must either disappear or stay usable. "Or disappear" is
			// what lets a prop hug a wall; "or stay >= MinW" is what stops it from pinching a corridor.
			int32 Left = 0, Right = 0, Up = 0, Down = 0;
			for (int32 X = CX - 1; Open(X, CY); --X) { ++Left; }
			for (int32 X = CX + 1; Open(X, CY); ++X) { ++Right; }
			for (int32 Y = CY - 1; Open(CX, Y); --Y) { ++Up; }
			for (int32 Y = CY + 1; Open(CX, Y); ++Y) { ++Down; }
			auto SegmentOk = [MinW](int32 Len) { return Len == 0 || Len >= MinW; };
			if (!SegmentOk(Left) || !SegmentOk(Right) || !SegmentOk(Up) || !SegmentOk(Down)) { continue; }

			S.BlockedField[SurfIndex(Cell, 0)] = true;

			FFPSRArenaProp Prop;
			Prop.Cell = FIntPoint(CX, CY);
			Prop.Tier = EFPSRArenaPropTier::Blocking;
			Prop.Variant = static_cast<uint8>(RS.RandRange(0, Variants - 1));
			Prop.YawDegrees = 90.0f * RS.RandRange(0, 3);
			Layout.Props.Add(Prop);
			Used[Cell] = true;
			++Placed;

			for (int32 DY = -Spacing; DY <= Spacing; ++DY)
			{
				for (int32 DX = -Spacing; DX <= Spacing; ++DX)
				{
					const int32 NX = CX + DX;
					const int32 NY = CY + DY;
					if (NX >= 0 && NY >= 0 && NX < W && NY < H) { SpacingBlocked[NY * W + NX] = true; }
				}
			}
		}

		// --- passable tier ---------------------------------------------------------------------------------
		// No cell occupancy at all, so no safety check is possible or needed: at <= 45 cm both the player and the
		// swarm step straight over these. They are texture, not terrain.
		int32 PlacedPassable = 0;
		for (int32 Index = 0; Index < Candidates.Num() && PlacedPassable < PassableBudget; ++Index)
		{
			const int32 Cell = Candidates[Index];
			const int32 CX = Cell % W;
			const int32 CY = Cell / W;
			if (Used[Cell] || !Open(CX, CY)) { continue; }

			FFPSRArenaProp Prop;
			Prop.Cell = FIntPoint(CX, CY);
			Prop.Tier = EFPSRArenaPropTier::Passable;
			Prop.Variant = static_cast<uint8>(RS.RandRange(0, Variants - 1));
			Prop.YawDegrees = 90.0f * RS.RandRange(0, 3);
			Layout.Props.Add(Prop);
			Used[Cell] = true;
			++PlacedPassable;
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

	// --- landmarks: only the blocking ones occupy cells ------------------------------------------------------
	for (const FFPSRArenaAuthoredLandmark& Landmark : Authored.Landmarks)
	{
		if (!Landmark.bBlocking) { continue; }
		const int32 LCX = FMath::FloorToInt((Landmark.Location.X - ArenaOrigin.X) / Cell);
		const int32 LCY = FMath::FloorToInt((Landmark.Location.Y - ArenaOrigin.Y) / Cell);
		BlockCell(LCX, LCY);
	}

	// --- L1: procedural micro props in whatever slack the authored skeleton left -----------------------------
	{
		FRandomStream RS(Seed);
		PlaceMicroProps(RS, Params, OutLayout);
	}

	UE_LOG(LogFPSR, Log,
		TEXT("[Arena] Generated seed=%d grid=%dx%d cell=%.0f authored(blockers=%d landmarks=%d) props=%d origin=%s"),
		Seed, W, H, Params.CellSize, Authored.Blockers.Num(), Authored.Landmarks.Num(),
		OutLayout.Props.Num(), *ArenaOrigin.ToString());

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
