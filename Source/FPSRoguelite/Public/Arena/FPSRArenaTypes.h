// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Enemy/FPSRFlowFieldComputer.h"
#include "FPSRArenaTypes.generated.h"

/**
 * One axis-aligned blocking cluster in arena CELL space (both corners inclusive).
 *
 * Rectangles are not a convenience — they are ADR 0010 invariant 7 ("차단 요소는 볼록하고, 서로 오목을
 * 만들 수 없다"). A procedurally placed concave pocket traps the swarm, and that failure cannot be fixed
 * after the fact: the layout is already committed by the time anyone could notice. A rectangle cannot be
 * concave, and a guaranteed minimum gap between rectangles means two of them cannot form a pocket either.
 */
USTRUCT(BlueprintType)
struct FFPSRArenaCluster
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arena")
	FIntPoint MinCell = FIntPoint::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arena")
	FIntPoint MaxCell = FIntPoint::ZeroValue;

	int32 WidthCells() const { return MaxCell.X - MinCell.X + 1; }
	int32 HeightCells() const { return MaxCell.Y - MinCell.Y + 1; }

	bool ContainsCell(int32 CX, int32 CY) const
	{
		return CX >= MinCell.X && CX <= MaxCell.X && CY >= MinCell.Y && CY <= MaxCell.Y;
	}
};

/**
 * Resolved generator inputs. Deliberately a PLAIN struct rather than the DataAsset itself so the generator
 * stays callable from a worldless automation test with no UObject in sight (ADR 0010 module boundary: the
 * generator touches no world, subsystem or singleton). UFPSRArenaParamsDataAsset copies itself into one of
 * these — the asset is the authoring surface, this is the contract.
 */
USTRUCT()
struct FFPSRArenaGenParams
{
	GENERATED_BODY()

	/** Arena size in cells. 80x80 at CellSize 100 = the 80x80 m of ADR 0010 D2. */
	FIntPoint ArenaSizeCells = FIntPoint(80, 80);

	/** Flow-field cell size (cm). ADR 0010 D3 = 100; the trade-off table and why not 50/250 is in that section. */
	float CellSize = 100.0f;

	/** Mirrors UFPSRFlowFieldComputer::DefaultClimbableStepHeight. Single-plane arenas never step, but the
	 *  surface data carries it and CommitSubregion validates uniformity, so it must be the same number. */
	float ClimbableStepHeight = 45.0f;

	/** Candidate cluster lattices (columns x rows). The seed picks one; 2x2 gives a central crossing, 2x1/1x2
	 *  give a figure-eight. ADR 0010 D1 caps clusters at 2~4, which is what these shapes produce. */
	TArray<FIntPoint> SlotGridOptions;

	/** Minimum EFFECTIVE corridor width in cells (ADR 0010 invariant 6 + D5). Every cluster is inset this far
	 *  from its lattice slot, so two adjacent clusters end up 2x this apart and no corridor can be narrower. */
	int32 MinCorridorWidthCells = 3;

	/** Cells kept clear between the arena boundary and the cluster lattice — this is what makes the outer ring
	 *  a real loop instead of a gutter. */
	int32 BoundaryMarginCells = 3;

	/** Fraction of its available slot area a cluster fills, jittered per axis by the seed. */
	float ClusterFillMin = 0.45f;
	float ClusterFillMax = 0.80f;

	FFPSRArenaGenParams()
	{
		SlotGridOptions = { FIntPoint(2, 2), FIntPoint(2, 1), FIntPoint(1, 2), FIntPoint(3, 2), FIntPoint(2, 3) };
	}

	/** Cheap self-check: are these numbers geometrically satisfiable at all? Fills OutError on failure. */
	bool Validate(FString& OutError) const;
};

/**
 * The generator's whole output. ADR 0010 data ownership: this has NO owner — every machine regenerates it
 * from the replicated seed, so it is never replicated and never saved.
 *
 * Surface is intentionally NOT Blueprint-exposed: it is the generator's internal representation and the flow
 * field's input, and letting content bind to it would freeze the layout format.
 */
USTRUCT(BlueprintType)
struct FFPSRArenaLayout
{
	GENERATED_BODY()

	/** Fed straight into UFPSRFlowFieldComputer::BuildFromSurfaceData — no world trace anywhere in between. */
	FFPSRFlowFieldSurfaceData Surface;

	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arena")
	TArray<FFPSRArenaCluster> Clusters;

	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arena")
	int32 Seed = 0;

	/** Lattice the seed picked (columns x rows), kept for debug readouts. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arena")
	FIntPoint SlotGrid = FIntPoint::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arena")
	FIntPoint GridDims = FIntPoint::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arena")
	float CellSize = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arena")
	FVector GridOrigin = FVector::ZeroVector;

	bool IsValid() const { return GridDims.X > 0 && GridDims.Y > 0 && CellSize > 0.0f; }

	int32 NumCells() const { return GridDims.X * GridDims.Y; }

	/** Cell index used by both the layout and the flow field (they must agree — same formula as
	 *  UFPSRFlowFieldComputer: Cell = CY * GridDimX + CX). */
	int32 CellIndex(int32 CX, int32 CY) const { return CY * GridDims.X + CX; }

	/** World-space centre of a cell, on the arena floor. */
	FVector CellCenterWorld(int32 CX, int32 CY) const
	{
		return FVector(
			GridOrigin.X + (static_cast<double>(CX) + 0.5) * CellSize,
			GridOrigin.Y + (static_cast<double>(CY) + 0.5) * CellSize,
			GridOrigin.Z);
	}
};
