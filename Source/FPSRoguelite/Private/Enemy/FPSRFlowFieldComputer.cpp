// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSRFlowFieldComputer.h"
#include "Enemy/FPSRFlowFieldBoundsVolume.h"
#include "Core/FPSRLogChannels.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerStart.h"
#include "CollisionShape.h"
#include "Engine/HitResult.h"
#include "Containers/Queue.h"
#include "EngineUtils.h"

#if !UE_BUILD_SHIPPING
#include "DrawDebugHelpers.h"
#endif

// ======================================================================================
//  WORLDLESS CORE (no world query — exercised by FPSRoguelite.FlowField.Unit)
// ======================================================================================

void UFPSRFlowFieldComputer::BuildFromSurfaceData(const FFPSRFlowFieldSurfaceData& Data)
{
	GridDimX = Data.GridDimX;
	GridDimY = Data.GridDimY;
	GridOrigin = Data.GridOrigin;
	ActiveCellSize = Data.CellSize;

	const int32 NumCells = GridDimX * GridDimY;
	const int32 NumSurf = NumCells * NumLayers;

	// Adopt the baked surface graph. Defensive: if the caller under-sized the arrays, pad to the grid size so the
	// hot path never indexes OOB (a malformed synthetic input in a unit test, or a future producer bug).
	CellFloorZ = Data.CellFloorZ;
	BlockedField = Data.BlockedField;
	EdgeMask = Data.EdgeMask;
	if (CellFloorZ.Num() != NumSurf) { CellFloorZ.SetNumZeroed(NumSurf); for (int32 i = Data.CellFloorZ.Num(); i < NumSurf; ++i) { CellFloorZ[i] = MAX_flt; } }
	if (BlockedField.Num() != NumSurf) { BlockedField.SetNumZeroed(NumSurf); }
	if (EdgeMask.Num() != NumCells * 2) { EdgeMask.SetNumZeroed(NumCells * 2); }

	DistField.Init(MAX_int32, NumSurf);
	FlowField.Init(FVector2D::ZeroVector, NumSurf);
	bFieldReady = false;
}

void UFPSRFlowFieldComputer::RunBFS(const TArray<int32>& SourceSurfaces)
{
	if (GridDimX <= 0 || GridDimY <= 0)
	{
		bFieldReady = false;
		return;
	}

	const int32 NumSurf = GridDimX * GridDimY * NumLayers;
	for (int32 i = 0; i < NumSurf; ++i)
	{
		DistField[i] = MAX_int32;
	}

	// Seed BFS from the resolved source surfaces (multi-source -> field points to NEAREST source/player).
	TQueue<int32> Frontier;
	bool bAnySource = false;
	for (const int32 Surf : SourceSurfaces)
	{
		if (Surf != INDEX_NONE && DistField.IsValidIndex(Surf) && DistField[Surf] != 0)
		{
			DistField[Surf] = 0;
			Frontier.Enqueue(Surf);
			bAnySource = true;
		}
	}

	if (!bAnySource)
	{
		bFieldReady = false;
		return;
	}

	// 4-connected BFS (uniform cost) over the surface graph -> integration field.
	static const int32 DX4[4] = { 1, -1, 0, 0 };
	static const int32 DY4[4] = { 0, 0, 1, -1 };
	int32 Current;
	while (Frontier.Dequeue(Current))
	{
		const int32 CurDist = DistField[Current];
		const int32 Cell = Current / NumLayers;
		const int32 Rank = Current - Cell * NumLayers;
		const int32 CX = Cell % GridDimX;
		const int32 CY = Cell / GridDimX;
		for (int32 N = 0; N < 4; ++N)
		{
			const int32 NX = CX + DX4[N];
			const int32 NY = CY + DY4[N];
			if (NX < 0 || NX >= GridDimX || NY < 0 || NY >= GridDimY)
			{
				continue;
			}
			const int32 NCell = NY * GridDimX + NX;
			for (int32 RB = 0; RB < NumLayers; ++RB)
			{
				const int32 NSurf = SurfIndex(NCell, RB);
				if (CellFloorZ[NSurf] == MAX_flt || BlockedField[NSurf])
				{
					continue; // absent, or an occupancy-blocked wall surface — never propagate flow through it
				}
				if (!IsSurfaceEdgeTraversable(Cell, Rank, NCell, RB))
				{
					continue; // a thin wall / non-traversable height change on the shared boundary blocks this edge
				}
				if (DistField[NSurf] > CurDist + 1)
				{
					DistField[NSurf] = CurDist + 1;
					Frontier.Enqueue(NSurf);
				}
			}
		}
	}

	// Flow per surface: steepest descent toward the lowest-distance reachable neighbour surface.
	static const int32 DX8[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	static const int32 DY8[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
	for (int32 CY = 0; CY < GridDimY; ++CY)
	{
		for (int32 CX = 0; CX < GridDimX; ++CX)
		{
			const int32 Cell = CY * GridDimX + CX;
			for (int32 Rank = 0; Rank < NumLayers; ++Rank)
			{
				const int32 Surf = SurfIndex(Cell, Rank);
				if (CellFloorZ[Surf] == MAX_flt)
				{
					FlowField[Surf] = FVector2D::ZeroVector; // absent surface — never sampled
					continue;
				}

				// Steepest descent for EVERY valid surface, including occupancy-blocked ones (DistField == MAX): an enemy
				// standing in a partially-obstructed surface still gets an escape direction toward the nearest reachable
				// open neighbour instead of zero flow (which would jam it against geometry). Codex.
				int32 BestDist = DistField[Surf];
				int32 BestNX = -1;
				int32 BestNY = -1;
				for (int32 N = 0; N < 8; ++N)
				{
					const int32 NX = CX + DX8[N];
					const int32 NY = CY + DY8[N];
					if (NX < 0 || NX >= GridDimX || NY < 0 || NY >= GridDimY)
					{
						continue;
					}
					const int32 NCell = NY * GridDimX + NX;
					if (N < 4)
					{
						// Orthogonal: consider each reachable neighbour rank. Don't point across a blocked edge (a thin
						// boundary wall the BFS routed around) unless THIS surface is itself blocked (keep escape).
						for (int32 RB = 0; RB < NumLayers; ++RB)
						{
							const int32 NSurf = SurfIndex(NCell, RB);
							if (CellFloorZ[NSurf] == MAX_flt)
							{
								continue;
							}
							if (!BlockedField[Surf] && !IsSurfaceEdgeTraversable(Cell, Rank, NCell, RB))
							{
								continue;
							}
							if (DistField[NSurf] < BestDist)
							{
								BestDist = DistField[NSurf];
								BestNX = NX;
								BestNY = NY;
							}
						}
					}
					else if (Rank == 0)
					{
						// Diagonal corner-clearance — RANK 0 (ground plane) only; upper layers use 4-connected flow.
						const int32 NSurf = SurfIndex(NCell, 0);
						if (CellFloorZ[NSurf] == MAX_flt)
						{
							continue;
						}
						const int32 OrthoA = CY * GridDimX + NX; // (NX, CY)
						const int32 OrthoB = NY * GridDimX + CX; // (CX, NY)
						const int32 SA0 = SurfIndex(OrthoA, 0);
						const int32 SB0 = SurfIndex(OrthoB, 0);
						if (CellFloorZ[SA0] == MAX_flt || CellFloorZ[SB0] == MAX_flt || BlockedField[SA0] || BlockedField[SB0])
						{
							continue;
						}
						if (!BlockedField[Surf] &&
							(!IsSurfaceEdgeTraversable(Cell, 0, OrthoA, 0) || !IsSurfaceEdgeTraversable(Cell, 0, OrthoB, 0) ||
							 !IsSurfaceEdgeTraversable(OrthoA, 0, NCell, 0) || !IsSurfaceEdgeTraversable(OrthoB, 0, NCell, 0)))
						{
							continue;
						}
						if (DistField[NSurf] < BestDist)
						{
							BestDist = DistField[NSurf];
							BestNX = NX;
							BestNY = NY;
						}
					}
				}

				if (BestNX >= 0)
				{
					const FVector2D Dir(static_cast<float>(BestNX - CX), static_cast<float>(BestNY - CY));
					FlowField[Surf] = Dir.GetSafeNormal();
				}
				else
				{
					FlowField[Surf] = FVector2D::ZeroVector; // at/near a source
				}
			}
		}
	}

	bFieldReady = true;
}

FVector UFPSRFlowFieldComputer::Sample(const FVector& WorldLocation) const
{
	if (!bFieldReady)
	{
		return FVector::ZeroVector;
	}
	const int32 Cell = WorldToCellIndex(WorldLocation);
	if (Cell == INDEX_NONE)
	{
		return FVector::ZeroVector;
	}
	// Convert the enemy's actor Z to the surface it stands on and pick that layer's flow — pure arithmetic, no query.
	const float FootZ = static_cast<float>(WorldLocation.Z) - EnemyStandOffset;
	const int32 Rank = PickRankForFootZ(Cell, FootZ);
	if (Rank == INDEX_NONE)
	{
		return FVector::ZeroVector;
	}
	const FVector2D F = FlowField[SurfIndex(Cell, Rank)];
	return FVector(F.X, F.Y, 0.0f);
}

int32 UFPSRFlowFieldComputer::WorldToCellIndex(const FVector& WorldLocation) const
{
	if (GridDimX <= 0 || GridDimY <= 0)
	{
		return INDEX_NONE;
	}
	const int32 CX = FMath::FloorToInt((WorldLocation.X - GridOrigin.X) / ActiveCellSize);
	const int32 CY = FMath::FloorToInt((WorldLocation.Y - GridOrigin.Y) / ActiveCellSize);
	if (CX < 0 || CX >= GridDimX || CY < 0 || CY >= GridDimY)
	{
		return INDEX_NONE;
	}
	return CY * GridDimX + CX;
}

int32 UFPSRFlowFieldComputer::PickRankForFootZ(int32 Cell, float FootZ) const
{
	const int32 Base = Cell * NumLayers;
	// Pick the NEAREST valid rank to FootZ within MaxLayerPickDrop (above OR below) — the surface the enemy actually
	// stands on. Strict < keeps the lower rank on an exact tie (deterministic, no frame-to-frame oscillation).
	int32 NearBest = INDEX_NONE;
	float NearDist = MaxLayerPickDrop;
	for (int32 R = 0; R < NumLayers; ++R)
	{
		const float Z = CellFloorZ[Base + R];
		if (Z != MAX_flt)
		{
			const float D = FMath::Abs(Z - FootZ);
			if (D < NearDist)
			{
				NearDist = D;
				NearBest = R;
			}
		}
	}
	return NearBest;
}

bool UFPSRFlowFieldComputer::IsSurfaceEdgeTraversable(int32 CellA, int32 RankA, int32 CellB, int32 RankB) const
{
	if (GridDimX <= 0)
	{
		return false;
	}
	const int32 AX = CellA % GridDimX, AY = CellA / GridDimX;
	const int32 BX = CellB % GridDimX, BY = CellB / GridDimX;
	if (AY == BY && FMath::Abs(AX - BX) == 1)
	{
		const bool bALeft = AX < BX;
		const int32 LeftCell = bALeft ? CellA : CellB;
		const int32 LeftRank = bALeft ? RankA : RankB;
		const int32 RightRank = bALeft ? RankB : RankA;
		return (EdgeMask[LeftCell * 2 + 0] >> (LeftRank * NumLayers + RightRank)) & 1u;
	}
	if (AX == BX && FMath::Abs(AY - BY) == 1)
	{
		const bool bABottom = AY < BY;
		const int32 BottomCell = bABottom ? CellA : CellB;
		const int32 BottomRank = bABottom ? RankA : RankB;
		const int32 TopRank = bABottom ? RankB : RankA;
		return (EdgeMask[BottomCell * 2 + 1] >> (BottomRank * NumLayers + TopRank)) & 1u;
	}
	return false; // not orthogonally adjacent — the BFS/flow passes only query the 4-neighborhood
}

FBox UFPSRFlowFieldComputer::GetGridBounds() const
{
	if (GridDimX <= 0 || GridDimY <= 0)
	{
		return FBox(ForceInit);
	}
	const FVector Min = GridOrigin;
	const FVector Max = GridOrigin + FVector(GridDimX * ActiveCellSize, GridDimY * ActiveCellSize, 0.0f);
	return FBox(Min, Max);
}

// ======================================================================================
//  PRODUCTION PATH (server, world queries) — funnels into the worldless core above
// ======================================================================================

void UFPSRFlowFieldComputer::BuildFromWorldTrace(UWorld* World, const AFPSRFlowFieldBoundsVolume* BoundsVolume, float FloorZ)
{
	if (!World)
	{
		return;
	}

	// Data-driven grid bounds (Performance §5-2): if a bounds volume was supplied, size the grid to its world AABB;
	// otherwise fall back to the origin-centered HalfExtentFallback grid.
	float SizeX, SizeY;
	if (BoundsVolume)
	{
		const FBox WB = BoundsVolume->GetWorldBounds();
		const float Override = BoundsVolume->GetCellSizeOverride();
		ActiveCellSize = (Override > 0.0f) ? Override : DefaultCellSize;
		const float StepOverride = BoundsVolume->GetClimbableStepHeightOverride();
		ActiveClimbableStepHeight = (StepOverride > 0.0f) ? StepOverride : DefaultClimbableStepHeight;
		const float ApexOverride = BoundsVolume->GetProbeApexAboveOriginOverride();
		ActiveProbeApexAboveOrigin = (ApexOverride > 0.0f) ? ApexOverride : DefaultProbeApexAboveOrigin;
		GridOrigin = FVector(WB.Min.X, WB.Min.Y, FloorZ);
		SizeX = FMath::Max(ActiveCellSize, WB.GetSize().X);
		SizeY = FMath::Max(ActiveCellSize, WB.GetSize().Y);
		if (WB.GetSize().X < ActiveCellSize || WB.GetSize().Y < ActiveCellSize)
		{
			UE_LOG(LogFPSR, Warning, TEXT("[FlowField] Bounds volume %s has a near-zero extent (%s); check its box size."),
				*BoundsVolume->GetName(), *WB.GetSize().ToString());
		}
	}
	else
	{
		ActiveCellSize = DefaultCellSize;
		ActiveClimbableStepHeight = DefaultClimbableStepHeight;
		ActiveProbeApexAboveOrigin = DefaultProbeApexAboveOrigin;
		GridOrigin = FVector(-HalfExtentFallback, -HalfExtentFallback, FloorZ);
		SizeX = 2.0f * HalfExtentFallback;
		SizeY = 2.0f * HalfExtentFallback;
	}

	GridDimX = FMath::Max(1, FMath::CeilToInt(SizeX / ActiveCellSize));
	GridDimY = FMath::Max(1, FMath::CeilToInt(SizeY / ActiveCellSize));

	// Perf guard: if the grid would exceed the per-tick scan budget, GROW the cell size (coarser grid) so the cell
	// count fits while still covering the full region.
	if (static_cast<int64>(GridDimX) * GridDimY > MaxTotalCells || GridDimX > MaxGridDimPerAxis || GridDimY > MaxGridDimPerAxis)
	{
		const float CellForTotal = ActiveCellSize * FMath::Sqrt((static_cast<float>(GridDimX) * GridDimY) / static_cast<float>(MaxTotalCells));
		const float CellForAxisX = SizeX / static_cast<float>(MaxGridDimPerAxis);
		const float CellForAxisY = SizeY / static_cast<float>(MaxGridDimPerAxis);
		ActiveCellSize = FMath::Max3(CellForTotal, CellForAxisX, CellForAxisY);
		GridDimX = FMath::Max(1, FMath::CeilToInt(SizeX / ActiveCellSize));
		GridDimY = FMath::Max(1, FMath::CeilToInt(SizeY / ActiveCellSize));
		UE_LOG(LogFPSR, Warning,
			TEXT("[FlowField] Bounds exceed the cell budget (%d); grew cell size to %.0f -> %dx%d (%d cells). Coverage preserved; set CellSizeOverride or shrink the volume for finer routing."),
			MaxTotalCells, ActiveCellSize, GridDimX, GridDimY, GridDimX * GridDimY);
	}

	const int32 NumCells = GridDimX * GridDimY;
	const int32 NumSurf = NumCells * NumLayers;

	// Build the surface graph into a value struct (world traces), then adopt it via the worldless core.
	FFPSRFlowFieldSurfaceData Data;
	Data.GridDimX = GridDimX;
	Data.GridDimY = GridDimY;
	Data.GridOrigin = GridOrigin;
	Data.CellSize = ActiveCellSize;
	Data.CellFloorZ.Init(MAX_flt, NumSurf);
	Data.BlockedField.Init(false, NumSurf);
	Data.EdgeMask.Init(0, NumCells * 2);

	UE_LOG(LogFPSR, Log, TEXT("[FlowField] Grid %dx%d cell=%.0f layers=%d origin=%s (%s)."),
		GridDimX, GridDimY, ActiveCellSize, NumLayers, *GridOrigin.ToString(),
		BoundsVolume ? TEXT("bounds volume") : TEXT("origin-centered fallback"));

	// --- BuildObstacleMask (moved verbatim; writes into Data arrays, reads grid config from the members set above) ---
	{
		// Enforce the movement invariant (Codex P2): a FLAT ledge taller than the enemy per-recheck ground snap can be
		// opened by the field but NOT climbed by ApplyGravity. Cap the step height regardless of any designer override.
		if (ActiveClimbableStepHeight > MaxClimbableStepHeight)
		{
			UE_LOG(LogFPSR, Warning,
				TEXT("[FlowField] ClimbableStepHeight %.0fcm exceeds the enemy ground-snap limit %.0fcm; clamping (a taller flat step can't be climbed)."),
				ActiveClimbableStepHeight, MaxClimbableStepHeight);
			ActiveClimbableStepHeight = MaxClimbableStepHeight;
		}

		FCollisionObjectQueryParams ObjParams;
		ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRFlowObstacle), false);

		const float ApexZ = GridOrigin.Z + ActiveProbeApexAboveOrigin;

		const float MaxSlopeTan = FMath::Sqrt(FMath::Max(0.0f, 1.0f - WalkableNormalZ * WalkableNormalZ)) / WalkableNormalZ;
		const float RampAllowance = ActiveCellSize * MaxSlopeTan;

		TArray<uint8> AxisSloped;
		AxisSloped.Init(0, NumSurf * 2);

		const float SubBand = 0.5f * RampAllowance + ActiveClimbableStepHeight;
		static const float SubDX[5] = { 0.0f, 1.0f, -1.0f, 0.0f, 0.0f };
		static const float SubDY[5] = { 0.0f, 0.0f, 0.0f, 1.0f, -1.0f };
		const float SubR = ActiveCellSize * 0.35f;

		int32 DroppedSurfaceCells = 0;

		// (1)+(2) per cell: collect candidates, cluster into ranks, derive per-surface per-axis slope.
		TArray<FVector2f> Candidates;
		for (int32 CY = 0; CY < GridDimY; ++CY)
		{
			for (int32 CX = 0; CX < GridDimX; ++CX)
			{
				const int32 Cell = CY * GridDimX + CX;
				const float CenterX = GridOrigin.X + (CX + 0.5f) * ActiveCellSize;
				const float CenterY = GridOrigin.Y + (CY + 0.5f) * ActiveCellSize;
				const FVector Base(CenterX, CenterY, ApexZ - MaxProbeDrop);

				Candidates.Reset();
				FCollisionQueryParams IterQP(SCENE_QUERY_STAT(FPSRFlowFloor), false);
				float ProbeStartZ = ApexZ;
				for (int32 Surface = 0; Surface < MaxColumnSurfaces; ++Surface)
				{
					FHitResult Hit;
					if (!World->LineTraceSingleByObjectType(Hit, FVector(CenterX, CenterY, ProbeStartZ), Base, ObjParams, IterQP))
					{
						break;
					}
					if (!Hit.bStartPenetrating && Hit.ImpactNormal.Z >= WalkableNormalZ)
					{
						Candidates.Add(FVector2f(static_cast<float>(Hit.ImpactPoint.Z), static_cast<float>(Hit.ImpactNormal.Z)));
					}
					const float NextStartZ = FMath::Min(static_cast<float>(Hit.ImpactPoint.Z), ProbeStartZ) - SurfaceProbeSkip;
					if (NextStartZ <= Base.Z)
					{
						break;
					}
					ProbeStartZ = NextStartZ;
				}

				Candidates.Sort([](const FVector2f& A, const FVector2f& B) { return A.X < B.X; });
				int32 Assigned = 0;
				float LastClusterZ = -MAX_flt;
				for (const FVector2f& Cand : Candidates)
				{
					if (Assigned == 0 || (Cand.X - LastClusterZ) > SurfaceProbeSkip)
					{
						if (Assigned >= NumLayers)
						{
							++DroppedSurfaceCells;
							break;
						}
						Data.CellFloorZ[SurfIndex(Cell, Assigned)] = Cand.X;
						LastClusterZ = Cand.X;
						++Assigned;
					}
				}
				if (Assigned == 0)
				{
					continue;
				}

				bool SubHit[5];
				float SubZ[5];
				for (int32 S = 0; S < 5; ++S)
				{
					const FVector SubStart(CenterX + SubDX[S] * SubR, CenterY + SubDY[S] * SubR, ApexZ);
					FHitResult SubHitR;
					if (World->LineTraceSingleByObjectType(SubHitR, SubStart, FVector(SubStart.X, SubStart.Y, ApexZ - MaxProbeDrop), ObjParams, IterQP) &&
						!SubHitR.bStartPenetrating && SubHitR.ImpactNormal.Z >= WalkableNormalZ)
					{
						SubHit[S] = true;
						SubZ[S] = static_cast<float>(SubHitR.ImpactPoint.Z);
					}
					else
					{
						SubHit[S] = false;
						SubZ[S] = 0.0f;
					}
				}
				for (int32 R = 0; R < Assigned; ++R)
				{
					const float RZ = Data.CellFloorZ[SurfIndex(Cell, R)];
					auto Belongs = [&](int32 S) { return SubHit[S] && FMath::Abs(SubZ[S] - RZ) <= SubBand; };
					{
						float Mn = MAX_flt, Mx = -MAX_flt;
						const int32 XS[3] = { 0, 1, 2 };
						for (int32 k = 0; k < 3; ++k) { if (Belongs(XS[k])) { Mn = FMath::Min(Mn, SubZ[XS[k]]); Mx = FMath::Max(Mx, SubZ[XS[k]]); } }
						if (Mx > -MAX_flt && (Mx - Mn) > ActiveClimbableStepHeight) { AxisSloped[SurfIndex(Cell, R) * 2 + 0] = 1; }
					}
					{
						float Mn = MAX_flt, Mx = -MAX_flt;
						const int32 YS[3] = { 0, 3, 4 };
						for (int32 k = 0; k < 3; ++k) { if (Belongs(YS[k])) { Mn = FMath::Min(Mn, SubZ[YS[k]]); Mx = FMath::Max(Mx, SubZ[YS[k]]); } }
						if (Mx > -MAX_flt && (Mx - Mn) > ActiveClimbableStepHeight) { AxisSloped[SurfIndex(Cell, R) * 2 + 1] = 1; }
					}
				}
			}
		}

		auto EdgeMaxDelta = [&](int32 SurfA, int32 SurfB, int32 Axis) -> float
		{
			const bool bSloped = AxisSloped[SurfA * 2 + Axis] != 0 || AxisSloped[SurfB * 2 + Axis] != 0;
			return bSloped ? FMath::Max(ActiveClimbableStepHeight, RampAllowance) : ActiveClimbableStepHeight;
		};

		// (3) Flood reachability over SURFACES.
		TArray<bool> Reached;
		Reached.Init(false, NumSurf);
		TQueue<int32> Frontier;

		// (3a) PlayerStart seed (R3).
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			const APlayerStart* Start = *It;
			if (!Start)
			{
				continue;
			}
			const FVector StartLoc = Start->GetActorLocation();
			const int32 SCell = WorldToCellIndex(StartLoc);
			if (SCell == INDEX_NONE)
			{
				continue;
			}
			FHitResult FloorHit;
			const float StartFloorZ = World->LineTraceSingleByChannel(FloorHit, StartLoc, StartLoc - FVector(0.0f, 0.0f, 5000.0f), ECC_WorldStatic)
				? static_cast<float>(FloorHit.ImpactPoint.Z) : (static_cast<float>(StartLoc.Z) - EnemyStandOffset);
			int32 BestRank = INDEX_NONE;
			float BestD = MAX_flt;
			for (int32 R = 0; R < NumLayers; ++R)
			{
				const float Z = Data.CellFloorZ[SurfIndex(SCell, R)];
				if (Z != MAX_flt && FMath::Abs(Z - StartFloorZ) < BestD)
				{
					BestD = FMath::Abs(Z - StartFloorZ);
					BestRank = R;
				}
			}
			if (BestRank != INDEX_NONE)
			{
				const int32 SSurf = SurfIndex(SCell, BestRank);
				if (!Reached[SSurf])
				{
					Reached[SSurf] = true;
					Frontier.Enqueue(SSurf);
				}
			}
		}

		// (3b) Ground-plane seed — RANK 0 ONLY.
		for (int32 Cell = 0; Cell < NumCells; ++Cell)
		{
			const int32 S0 = SurfIndex(Cell, 0);
			if (!Reached[S0] && Data.CellFloorZ[S0] != MAX_flt && FMath::Abs(Data.CellFloorZ[S0] - GridOrigin.Z) <= ActiveClimbableStepHeight)
			{
				Reached[S0] = true;
				Frontier.Enqueue(S0);
			}
		}

		// (3c) Flood.
		static const int32 FDX[4] = { 1, -1, 0, 0 };
		static const int32 FDY[4] = { 0, 0, 1, -1 };
		int32 CurSurf;
		while (Frontier.Dequeue(CurSurf))
		{
			const int32 Cell = CurSurf / NumLayers;
			const int32 CX = Cell % GridDimX;
			const int32 CY = Cell / GridDimX;
			const float H = Data.CellFloorZ[CurSurf];
			for (int32 N = 0; N < 4; ++N)
			{
				const int32 NX = CX + FDX[N];
				const int32 NY = CY + FDY[N];
				if (NX < 0 || NX >= GridDimX || NY < 0 || NY >= GridDimY)
				{
					continue;
				}
				const int32 NCell = NY * GridDimX + NX;
				const int32 Axis = (N < 2) ? 0 : 1;
				for (int32 RB = 0; RB < NumLayers; ++RB)
				{
					const int32 NSurf = SurfIndex(NCell, RB);
					if (Data.CellFloorZ[NSurf] == MAX_flt || Reached[NSurf])
					{
						continue;
					}
					if (FMath::Abs(Data.CellFloorZ[NSurf] - H) <= EdgeMaxDelta(CurSurf, NSurf, Axis))
					{
						Reached[NSurf] = true;
						Frontier.Enqueue(NSurf);
					}
				}
			}
		}

		for (int32 Surf = 0; Surf < NumSurf; ++Surf)
		{
			if (!Reached[Surf])
			{
				Data.CellFloorZ[Surf] = MAX_flt;
			}
		}

		// (4) Bake per-surface occupancy (BlockedField) + per-edge rank-pairing (EdgeMask).
		const FCollisionShape OccupancyBox = FCollisionShape::MakeBox(
			FVector(AgentFootprintRadius, AgentFootprintRadius, ObstacleProbeHalfHeight));
		const FCollisionShape EdgeBox = FCollisionShape::MakeBox(
			FVector(AgentFootprintRadius, AgentFootprintRadius, ObstacleProbeHalfHeight));
		const float SlopedOccupancyRaise = AgentFootprintRadius * MaxSlopeTan + ObstacleProbeHalfHeight;

		for (int32 CY = 0; CY < GridDimY; ++CY)
		{
			for (int32 CX = 0; CX < GridDimX; ++CX)
			{
				const int32 Cell = CY * GridDimX + CX;
				const float CenterX = GridOrigin.X + (CX + 0.5f) * ActiveCellSize;
				const float CenterY = GridOrigin.Y + (CY + 0.5f) * ActiveCellSize;

				for (int32 R = 0; R < NumLayers; ++R)
				{
					const int32 Surf = SurfIndex(Cell, R);
					if (Data.CellFloorZ[Surf] == MAX_flt)
					{
						continue;
					}
					const bool bSurfSloped = AxisSloped[Surf * 2 + 0] != 0 || AxisSloped[Surf * 2 + 1] != 0;
					const float ProbeZ = Data.CellFloorZ[Surf] + (bSurfSloped ? SlopedOccupancyRaise : ObstacleProbeZ);
					if (World->OverlapAnyTestByObjectType(FVector(CenterX, CenterY, ProbeZ), FQuat::Identity, ObjParams, OccupancyBox, QueryParams))
					{
						Data.BlockedField[Surf] = true;
					}
				}

				if (CX + 1 < GridDimX)
				{
					const int32 NCell = Cell + 1;
					for (int32 RA = 0; RA < NumLayers; ++RA)
					{
						const int32 SA = SurfIndex(Cell, RA);
						if (Data.CellFloorZ[SA] == MAX_flt) { continue; }
						for (int32 RB = 0; RB < NumLayers; ++RB)
						{
							const int32 SB = SurfIndex(NCell, RB);
							if (Data.CellFloorZ[SB] == MAX_flt) { continue; }
							if (FMath::Abs(Data.CellFloorZ[SA] - Data.CellFloorZ[SB]) <= EdgeMaxDelta(SA, SB, 0))
							{
								const float EdgeZ = FMath::Max(Data.CellFloorZ[SA], Data.CellFloorZ[SB]) + ObstacleProbeZ;
								const FVector EdgeCenter(GridOrigin.X + (CX + 1) * ActiveCellSize, CenterY, EdgeZ);
								if (!World->OverlapAnyTestByObjectType(EdgeCenter, FQuat::Identity, ObjParams, EdgeBox, QueryParams))
								{
									Data.EdgeMask[Cell * 2 + 0] |= static_cast<uint8>(1u << (RA * NumLayers + RB));
								}
							}
						}
					}
				}

				if (CY + 1 < GridDimY)
				{
					const int32 NCell = Cell + GridDimX;
					for (int32 RA = 0; RA < NumLayers; ++RA)
					{
						const int32 SA = SurfIndex(Cell, RA);
						if (Data.CellFloorZ[SA] == MAX_flt) { continue; }
						for (int32 RB = 0; RB < NumLayers; ++RB)
						{
							const int32 SB = SurfIndex(NCell, RB);
							if (Data.CellFloorZ[SB] == MAX_flt) { continue; }
							if (FMath::Abs(Data.CellFloorZ[SA] - Data.CellFloorZ[SB]) <= EdgeMaxDelta(SA, SB, 1))
							{
								const float EdgeZ = FMath::Max(Data.CellFloorZ[SA], Data.CellFloorZ[SB]) + ObstacleProbeZ;
								const FVector EdgeCenter(CenterX, GridOrigin.Y + (CY + 1) * ActiveCellSize, EdgeZ);
								if (!World->OverlapAnyTestByObjectType(EdgeCenter, FQuat::Identity, ObjParams, EdgeBox, QueryParams))
								{
									Data.EdgeMask[Cell * 2 + 1] |= static_cast<uint8>(1u << (RA * NumLayers + RB));
								}
							}
						}
					}
				}
			}
		}

		// Diagnostics.
		int32 NoFloorCells = 0;
		int32 MultiLayerCells = 0;
		for (int32 Cell = 0; Cell < NumCells; ++Cell)
		{
			const bool bR0 = Data.CellFloorZ[SurfIndex(Cell, 0)] != MAX_flt;
			const bool bR1 = (NumLayers > 1) && Data.CellFloorZ[SurfIndex(Cell, 1)] != MAX_flt;
			if (!bR0 && !bR1) { ++NoFloorCells; }
			if (bR1) { ++MultiLayerCells; }
		}
		UE_LOG(LogFPSR, Log, TEXT("[FlowField] Obstacle mask: %d/%d cells no-floor, %d multi-layer (footprint %.0fcm, step<=%.0fcm, ramp<=%.0fcm, apex+%.0fcm, layers=%d)."),
			NoFloorCells, NumCells, MultiLayerCells, AgentFootprintRadius, ActiveClimbableStepHeight, RampAllowance, ActiveProbeApexAboveOrigin, NumLayers);
		if (DroppedSurfaceCells > 0)
		{
			UE_LOG(LogFPSR, Warning,
				TEXT("[FlowField] %d cell(s) have more than NumLayers(%d) stacked walkable surfaces; the highest were dropped (bounded design — raise NumLayers + widen EdgeMask for true 3+ layers)."),
				DroppedSurfaceCells, NumLayers);
		}
		if (NumCells > 0 && NoFloorCells * 10 >= NumCells * 9)
		{
			UE_LOG(LogFPSR, Warning,
				TEXT("[FlowField] %d%% of cells have NO reachable floor — the flood may not have found the ground floor. If this map has a solid ceiling below the probe apex (apex = grid floor + %.0fcm), set the bounds volume's ProbeApexAboveOriginOverride below the ceiling, or ensure the floor is a separate WorldStatic mesh."),
				(NoFloorCells * 100) / NumCells, ActiveProbeApexAboveOrigin);
		}
	}

	// Adopt the baked graph into the hot-path arrays (worldless core).
	BuildFromSurfaceData(Data);
}

void UFPSRFlowFieldComputer::RecomputeFromWorld(UWorld* World, const TArray<FVector>* SourcePlayerFootLocations)
{
	if (!World || GridDimX <= 0 || GridDimY <= 0)
	{
		return;
	}

	// Resolve source surfaces from alive players (production: enumerate + snap-if-blocked), then run the worldless BFS.
	TArray<int32> Sources;

	auto ResolvePawn = [&](const FVector& PawnLoc, float FootOffset)
	{
		const int32 Cell = WorldToCellIndex(PawnLoc);
		if (Cell == INDEX_NONE)
		{
			return;
		}
		const float FootZ = static_cast<float>(PawnLoc.Z) - FootOffset;
		const int32 Rank = PickRankForFootZ(Cell, FootZ);
		int32 Surf = (Rank != INDEX_NONE) ? SurfIndex(Cell, Rank) : INDEX_NONE;
		// Snap a blocked/absent source to the nearest open surface so the BFS still expands from walkable ground.
		if (Surf == INDEX_NONE || CellFloorZ[Surf] == MAX_flt || BlockedField[Surf])
		{
			Surf = FindNearestOpenSurface(World, Cell, (Rank == INDEX_NONE) ? 0 : Rank, PawnLoc);
		}
		if (Surf != INDEX_NONE)
		{
			Sources.Add(Surf);
		}
	};

	if (SourcePlayerFootLocations)
	{
		// Map-filtered path (S2): the caller supplies pawn ACTOR locations already filtered to this map's occupants.
		for (const FVector& Loc : *SourcePlayerFootLocations)
		{
			ResolvePawn(Loc, EnemyStandOffset);
		}
	}
	else
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (const APlayerController* PC = It->Get())
			{
				if (const APawn* PlayerPawn = PC->GetPawn())
				{
					float FootOffset = EnemyStandOffset;
					if (const ACharacter* Ch = Cast<ACharacter>(PlayerPawn))
					{
						if (const UCapsuleComponent* Cap = Ch->GetCapsuleComponent())
						{
							FootOffset = Cap->GetScaledCapsuleHalfHeight();
						}
					}
					ResolvePawn(PlayerPawn->GetActorLocation(), FootOffset);
				}
			}
		}
	}

	RunBFS(Sources);
}

int32 UFPSRFlowFieldComputer::FindNearestOpenSurface(UWorld* World, int32 FromCell, int32 FromRank, const FVector& PlayerLocation) const
{
	if (FromCell == INDEX_NONE || GridDimX <= 0 || GridDimY <= 0)
	{
		return INDEX_NONE;
	}
	const int32 CX = FromCell % GridDimX;
	const int32 CY = FromCell / GridDimX;

	const int32 FromSurf = SurfIndex(FromCell, FMath::Clamp(FromRank, 0, NumLayers - 1));
	const float AnchorZ = (CellFloorZ[FromSurf] != MAX_flt) ? CellFloorZ[FromSurf] : (static_cast<float>(PlayerLocation.Z) - EnemyStandOffset);

	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRFlowSourceLOS), false);

	int32 Fallback = INDEX_NONE;
	for (int32 R = 1; R <= SourceSearchRadius; ++R)
	{
		for (int32 dy = -R; dy <= R; ++dy)
		{
			for (int32 dx = -R; dx <= R; ++dx)
			{
				if (FMath::Max(FMath::Abs(dx), FMath::Abs(dy)) != R)
				{
					continue;
				}
				const int32 NX = CX + dx;
				const int32 NY = CY + dy;
				if (NX < 0 || NX >= GridDimX || NY < 0 || NY >= GridDimY)
				{
					continue;
				}
				const int32 NCell = NY * GridDimX + NX;
				int32 BestRank = INDEX_NONE;
				float BestD = MAX_flt;
				for (int32 rb = 0; rb < NumLayers; ++rb)
				{
					const int32 S = SurfIndex(NCell, rb);
					if (CellFloorZ[S] == MAX_flt || BlockedField[S])
					{
						continue;
					}
					const float D = FMath::Abs(CellFloorZ[S] - AnchorZ);
					if (D < BestD)
					{
						BestD = D;
						BestRank = rb;
					}
				}
				if (BestRank == INDEX_NONE)
				{
					continue;
				}
				const int32 OpenSurf = SurfIndex(NCell, BestRank);
				if (Fallback == INDEX_NONE)
				{
					Fallback = OpenSurf;
				}
				const FVector CellCenter(
					GridOrigin.X + (NX + 0.5f) * ActiveCellSize,
					GridOrigin.Y + (NY + 0.5f) * ActiveCellSize,
					PlayerLocation.Z);
				if (World == nullptr ||
					!World->LineTraceTestByObjectType(PlayerLocation, CellCenter, ObjParams, QueryParams))
				{
					return OpenSurf;
				}
			}
		}
	}
	return Fallback;
}

#if !UE_BUILD_SHIPPING
void UFPSRFlowFieldComputer::DebugDraw(UWorld* World, const TArray<FVector>& NearLocations, float DrawLife) const
{
	if (!World || !bFieldReady || GridDimX <= 0)
	{
		return;
	}
	const float CellHalf = ActiveCellSize * 0.5f;
	const int32 DrawRadius = 25;
	for (const FVector& Near : NearLocations)
	{
		const int32 PCell = WorldToCellIndex(Near);
		if (PCell == INDEX_NONE)
		{
			continue;
		}
		const int32 PCX = PCell % GridDimX, PCY = PCell / GridDimX;
		for (int32 dy = -DrawRadius; dy <= DrawRadius; ++dy)
		{
			for (int32 dx = -DrawRadius; dx <= DrawRadius; ++dx)
			{
				const int32 CX = PCX + dx, CY = PCY + dy;
				if (CX < 0 || CX >= GridDimX || CY < 0 || CY >= GridDimY)
				{
					continue;
				}
				const int32 Cell = CY * GridDimX + CX;
				const float WX = GridOrigin.X + (CX + 0.5f) * ActiveCellSize;
				const float WY = GridOrigin.Y + (CY + 0.5f) * ActiveCellSize;
				for (int32 Rank = 0; Rank < NumLayers; ++Rank)
				{
					const int32 Surf = SurfIndex(Cell, Rank);
					const bool bAbsent = (CellFloorZ[Surf] == MAX_flt);
					if (bAbsent && Rank > 0)
					{
						continue;
					}
					const float WZ = (bAbsent ? GridOrigin.Z : CellFloorZ[Surf]) + 10.0f;
					if (bAbsent || BlockedField[Surf])
					{
						const FColor BlockColor = bAbsent ? FColor::Red : FColor::Orange;
						DrawDebugBox(World, FVector(WX, WY, WZ), FVector(CellHalf * 0.8f, CellHalf * 0.8f, 4.0f), BlockColor, false, DrawLife);
					}
					else
					{
						const FVector2D F = FlowField[Surf];
						const FVector Start(WX, WY, WZ);
						const FColor ArrowColor = (Rank == 0) ? FColor::Green : FColor::Cyan;
						DrawDebugDirectionalArrow(World, Start, Start + FVector(F.X, F.Y, 0.0f) * (CellHalf * 0.9f), 20.0f, ArrowColor, false, DrawLife, 0, 2.0f);
					}
				}
			}
		}
	}
}
#endif
