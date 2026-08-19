// Copyright Epic Games, Inc. All Rights Reserved.

#include "Arena/FPSRArenaCells.h"
#include "Enemy/FPSRFlowFieldComputer.h"

// FFPSRArenaGenParams::Validate lives here rather than with the generator it used to sit beside: the struct is a
// RUNTIME type and UFPSRArenaParamsDataAsset::IsDataValid calls this, so it cannot follow FFPSRArenaGenerator into
// the editor module (ADR 0012 — generation is editor-only now, but validating the numbers a designer typed is not).
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

void FFPSRArenaCells::ComputeDestructibleCells(const FFPSRArenaAuthoredDestructible& Destructible,
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

bool FFPSRArenaCells::IsCellOpen(const FFPSRArenaLayout& Layout, int32 CX, int32 CY)
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
