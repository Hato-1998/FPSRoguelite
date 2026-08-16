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
 * Which side of the 45/60 authoring band a procedural prop sits on (ADR 0010 D4).
 *
 * There is no third option on purpose. The band between 45 and 60 cm is a trap — the player cannot step over it
 * and the flow field's obstacle probe starts above it, so the swarm walks into it and jams. Making the tier an
 * enum with exactly two members means L1 cannot accidentally author into that gap.
 */
UENUM()
enum class EFPSRArenaPropTier : uint8
{
	/** >= 60 cm. Occupies its cell; the swarm routes around it. */
	Blocking,
	/** <= 45 cm. Occupies nothing — both the player and the swarm walk over it. Density and texture only. */
	Passable,
};

/** One procedurally placed micro prop (L1). Cell-space so it cannot drift off the grid the mask was built on. */
USTRUCT(BlueprintType)
struct FFPSRArenaProp
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arena")
	FIntPoint Cell = FIntPoint::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arena")
	EFPSRArenaPropTier Tier = EFPSRArenaPropTier::Passable;

	/** Which mesh of the set to use. Seeded, so the same seed dresses the arena identically everywhere. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arena")
	uint8 Variant = 0;

	/** Seeded yaw in 90-degree steps — circuit-board parts sit on an axis, not at arbitrary angles. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arena")
	float YawDegrees = 0.0f;
};

/**
 * One authored blocking volume, in WORLD space (centre + half-extent + yaw).
 *
 * The generator RASTERISES these into cells. It does not trace them, and that distinction is the whole reason
 * authoring the skeleton did not undo ADR 0010's β decision (see 0011 E3): rasterisation is pure geometry, so a
 * stage swap still costs zero world queries and the grid origin still cannot be polluted by the spare arena
 * parked below the live one.
 *
 * Only yaw is carried. A blocking volume that pitched or rolled would no longer be a prism in the XY plane, and
 * the arena is single-plane (0010 D2) — there is nothing for the extra axes to mean.
 */
USTRUCT()
struct FFPSRArenaAuthoredBox
{
	GENERATED_BODY()

	FVector Center = FVector::ZeroVector;
	FVector2D HalfExtentXY = FVector2D::ZeroVector;
	/** Degrees. */
	float YawDegrees = 0.0f;
};

/** One authored landmark: a fixed navigation beacon (ADR 0010 D6). Reserves cells so props cannot bury it. */
USTRUCT()
struct FFPSRArenaAuthoredLandmark
{
	GENERATED_BODY()

	FVector Location = FVector::ZeroVector;
	/** Cells around the landmark that stay clear of procedural props. 0 = just its own cell. */
	int32 ReserveRadiusCells = 2;
	/** Whether the landmark itself blocks movement (a tall pillar does; a floor decal does not). */
	bool bBlocking = true;
};

/**
 * Everything the DESIGNER placed, gathered from the level and handed to the generator. Empty is legal and means
 * "no skeleton authored yet" — the validator then reports an arena with no structure rather than the generator
 * inventing one, because ADR 0011 E5 moved lattice generation to an editor button and out of the runtime path.
 */
USTRUCT()
struct FFPSRArenaAuthoredInput
{
	GENERATED_BODY()

	TArray<FFPSRArenaAuthoredBox> Blockers;
	TArray<FFPSRArenaAuthoredLandmark> Landmarks;

	bool IsEmpty() const { return Blockers.Num() == 0 && Landmarks.Num() == 0; }
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

	/** Arena size in cells. 160x160 at CellSize 100 = the 160x160 m of ADR 0011 E1 (raised from 0010 D2's 80x80
	 *  once the 80x80 whitebox confirmed the loop/crossing topology and landmarks needed room). */
	FIntPoint ArenaSizeCells = FIntPoint(160, 160);

	/** Flow-field cell size (cm). ADR 0011 E1 pins this at 100 and it is no longer a free choice: at 160 m the
	 *  compile-time caps (axis <= 256, total <= 40000) leave only 80~100 cm, so the "drop to 50 if corridors feel
	 *  tight" escape hatch from 0010 D3 is arithmetically gone. The remaining lever is prop density. */
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

	// --- L1 micro props (ADR 0010 D5 / 0011 E6) ---------------------------------------------------------

	/** Blocking micro props per 100 open cells. These narrow corridors, so this is a gameplay dial, not dressing. */
	float BlockingPropsPer100Cells = 1.2f;

	/** Passable (<=45 cm) props per 100 open cells. Free of gameplay consequence — pure density. */
	float PassablePropsPer100Cells = 6.0f;

	/** Minimum gap in cells between two BLOCKING micro props. Below the swarm's own footprint they could cup a
	 *  pocket between them, which is the failure invariant 7 exists to make impossible. */
	int32 PropMinSpacingCells = 3;

	/** Ceiling on how much of a corridor's slack L1 may eat, as a fraction. At 1.0 a dense seed could shave every
	 *  corridor to the legal minimum — technically valid and miserable to move through. */
	float MaxSlackConsumption = 0.5f;

	/** How many mesh variants the content provides per tier (the generator only picks an index). */
	int32 PropVariantCount = 4;

	FFPSRArenaGenParams()
	{
		SlotGridOptions = { FIntPoint(2, 2), FIntPoint(2, 1), FIntPoint(1, 2), FIntPoint(3, 2), FIntPoint(2, 3) };
	}

	/** Cheap self-check: are these numbers geometrically satisfiable at all? Fills OutError on failure.
	 *  Exported because the EDITOR module calls it too — the authoring tool must reject bad params with the same
	 *  predicate the runtime fails on, not a second copy of the rules that agrees with it only for now. */
	FPSROGUELITE_API bool Validate(FString& OutError) const;
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

	/** Cell rects actually blocked by each authored volume, for debug readouts and the validator. NOT the source
	 *  of truth for rendering — the blocker actors carry their own meshes, so what the designer sees in the
	 *  viewport is the same object the rasteriser read. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arena")
	TArray<FFPSRArenaCluster> Clusters;

	/** Carried through so L1 can keep its props off them and the HUD can point at them. */
	TArray<FFPSRArenaAuthoredLandmark> Landmarks;

	/** L1 output. Blocking entries are already reflected in Surface.BlockedField; passable ones occupy nothing. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arena")
	TArray<FFPSRArenaProp> Props;

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
