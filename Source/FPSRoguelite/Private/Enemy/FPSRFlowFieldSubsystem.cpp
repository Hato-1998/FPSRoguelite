// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSRFlowFieldSubsystem.h"
#include "Enemy/FPSRFlowFieldBoundsVolume.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRLogChannels.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerStart.h"
#include "CollisionShape.h"
#include "TimerManager.h"
#include "Containers/Queue.h"
#include "EngineUtils.h"

bool UFPSRFlowFieldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

bool UFPSRFlowFieldSubsystem::HasServerAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

void UFPSRFlowFieldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Grid Z anchor: the obstacle probe is taken at GridOrigin.Z + ObstacleProbeZ, so GridOrigin.Z MUST sit near the
	// playable floor — otherwise (floor far from the world origin, e.g. a basement at Z ~ -1000) the probe samples
	// wall/doorway geometry at the wrong height and mis-marks passable floor openings as blocked, jamming enemies.
	// Detect the floor under a PlayerStart (trace down); fall back to the start's Z, then to the origin.
	float FloorZ = 0.0f;
	for (TActorIterator<APlayerStart> It(&InWorld); It; ++It)
	{
		if (const APlayerStart* Start = *It)
		{
			const FVector StartLoc = Start->GetActorLocation();
			FHitResult Hit;
			FloorZ = InWorld.LineTraceSingleByChannel(Hit, StartLoc, StartLoc - FVector(0.0f, 0.0f, 5000.0f), ECC_WorldStatic)
				? Hit.ImpactPoint.Z : StartLoc.Z;
			break;
		}
	}

	// Data-driven grid bounds (Performance §5-2): if a designer placed an AFPSRFlowFieldBoundsVolume, size the grid to
	// its world AABB; otherwise fall back to the origin-centered HalfExtentFallback grid (existing maps unchanged).
	const AFPSRFlowFieldBoundsVolume* BoundsVolume = nullptr;
	for (TActorIterator<AFPSRFlowFieldBoundsVolume> It(&InWorld); It; ++It)
	{
		if (*It)
		{
			BoundsVolume = *It;
			break;
		}
	}

	float SizeX, SizeY;
	if (BoundsVolume)
	{
		const FBox WB = BoundsVolume->GetWorldBounds();
		const float Override = BoundsVolume->GetCellSizeOverride();
		ActiveCellSize = (Override > 0.0f) ? Override : DefaultCellSize;
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
		GridOrigin = FVector(-HalfExtentFallback, -HalfExtentFallback, FloorZ);
		SizeX = 2.0f * HalfExtentFallback;
		SizeY = 2.0f * HalfExtentFallback;
	}

	GridDimX = FMath::Max(1, FMath::CeilToInt(SizeX / ActiveCellSize));
	GridDimY = FMath::Max(1, FMath::CeilToInt(SizeY / ActiveCellSize));

	// Perf guard: if the grid would exceed the per-tick scan budget, GROW the cell size (coarser grid) so the cell
	// count fits while still covering the full region — clamping the dimensions instead would silently truncate the
	// playable area (a regression). CeilToInt keeps GridDim*ActiveCellSize >= Size, so coverage is preserved.
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

	DistField.Init(MAX_int32, GridDimX * GridDimY);
	FlowField.Init(FVector2D::ZeroVector, GridDimX * GridDimY);
	BlockedField.Init(false, GridDimX * GridDimY);
	EdgeTraversable.Init(false, GridDimX * GridDimY * 2); // default blocked; BuildObstacleMask opens passable edges

	UE_LOG(LogFPSR, Log, TEXT("[FlowField] Grid %dx%d cell=%.0f origin=%s (%s)."),
		GridDimX, GridDimY, ActiveCellSize, *GridOrigin.ToString(),
		BoundsVolume ? TEXT("bounds volume") : TEXT("origin-centered fallback"));

	if (HasServerAuthority())
	{
		BuildObstacleMask(); // once: fixed map; BFS then routes around blocked cells

		InWorld.GetTimerManager().SetTimer(
			RecomputeTimerHandle, this, &UFPSRFlowFieldSubsystem::RecomputeField,
			FlowUpdateInterval, true);
	}
}

void UFPSRFlowFieldSubsystem::BuildObstacleMask()
{
	UWorld* World = GetWorld();
	if (!World || GridDimX <= 0 || GridDimY <= 0)
	{
		return;
	}

	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRFlowObstacle), false);

	// Clearance-aware probing (Part B — replaces the old full-cell over-blocking that jammed narrow doorways).
	// TWO masks, both built once on the fixed map:
	//  (1) OCCUPANCY (BlockedField): an agent-footprint box (AgentFootprintRadius, NOT the full cell) at the cell
	//      CENTER. A cell is blocked only if geometry intrudes within a capsule radius of its center, so a cell a
	//      capsule can stand in is NOT blocked just because a wall clips its far edge (fixes the doorway jam).
	//  (2) EDGE (EdgeTraversable): a box straddling each shared +X / +Y boundary face — thin across the boundary
	//      (AgentFootprintRadius) and spanning the full edge width (the cell size). This catches a thin wall sitting
	//      ON a cell boundary that both neighbors' shrunk center probes would miss, so BFS/flow never cross a wall
	//      even when both cells are individually open (closes the through-wall leak the shrink would otherwise open).
	const FCollisionShape OccupancyBox = FCollisionShape::MakeBox(
		FVector(AgentFootprintRadius, AgentFootprintRadius, ObstacleProbeHalfHeight));
	const float HalfCell = ActiveCellSize * 0.5f;
	const FCollisionShape EdgeBoxX = FCollisionShape::MakeBox(FVector(AgentFootprintRadius, HalfCell, ObstacleProbeHalfHeight));
	const FCollisionShape EdgeBoxY = FCollisionShape::MakeBox(FVector(HalfCell, AgentFootprintRadius, ObstacleProbeHalfHeight));

	int32 BlockedCount = 0;
	for (int32 CY = 0; CY < GridDimY; ++CY)
	{
		for (int32 CX = 0; CX < GridDimX; ++CX)
		{
			const int32 Cell = CY * GridDimX + CX;
			const float CenterX = GridOrigin.X + (CX + 0.5f) * ActiveCellSize;
			const float CenterY = GridOrigin.Y + (CY + 0.5f) * ActiveCellSize;
			const float ProbeZ = GridOrigin.Z + ObstacleProbeZ;

			// (1) Occupancy: can a footprint-sized agent stand at this cell's center?
			if (World->OverlapAnyTestByObjectType(FVector(CenterX, CenterY, ProbeZ), FQuat::Identity, ObjParams, OccupancyBox, QueryParams))
			{
				BlockedField[Cell] = true;
				++BlockedCount;
			}

			// (2a) +X edge (shared boundary with cell CX+1): open if no static geometry straddles the face.
			if (CX + 1 < GridDimX)
			{
				const FVector EdgeCenter(GridOrigin.X + (CX + 1) * ActiveCellSize, CenterY, ProbeZ);
				if (!World->OverlapAnyTestByObjectType(EdgeCenter, FQuat::Identity, ObjParams, EdgeBoxX, QueryParams))
				{
					EdgeTraversable[Cell * 2 + 0] = true;
				}
			}

			// (2b) +Y edge (shared boundary with cell CY+1).
			if (CY + 1 < GridDimY)
			{
				const FVector EdgeCenter(CenterX, GridOrigin.Y + (CY + 1) * ActiveCellSize, ProbeZ);
				if (!World->OverlapAnyTestByObjectType(EdgeCenter, FQuat::Identity, ObjParams, EdgeBoxY, QueryParams))
				{
					EdgeTraversable[Cell * 2 + 1] = true;
				}
			}
		}
	}

	UE_LOG(LogFPSR, Log, TEXT("[FlowField] Obstacle mask: %d/%d cells blocked (clearance-aware, footprint %.0fcm)."),
		BlockedCount, GridDimX * GridDimY, AgentFootprintRadius);
}

void UFPSRFlowFieldSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RecomputeTimerHandle);
	}
	Super::Deinitialize();
}

int32 UFPSRFlowFieldSubsystem::FindNearestOpenCell(int32 FromCell, const FVector& PlayerLocation) const
{
	if (FromCell == INDEX_NONE || GridDimX <= 0 || GridDimY <= 0)
	{
		return INDEX_NONE;
	}
	const UWorld* World = GetWorld();
	const int32 CX = FromCell % GridDimX;
	const int32 CY = FromCell / GridDimX;

	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRFlowSourceLOS), false);

	int32 Fallback = INDEX_NONE; // nearest open cell regardless of line-of-sight
	for (int32 R = 1; R <= SourceSearchRadius; ++R)
	{
		for (int32 dy = -R; dy <= R; ++dy)
		{
			for (int32 dx = -R; dx <= R; ++dx)
			{
				if (FMath::Max(FMath::Abs(dx), FMath::Abs(dy)) != R)
				{
					continue; // only the outer ring at radius R (nearest-first)
				}
				const int32 NX = CX + dx;
				const int32 NY = CY + dy;
				if (NX < 0 || NX >= GridDimX || NY < 0 || NY >= GridDimY)
				{
					continue;
				}
				const int32 NIdx = NY * GridDimX + NX;
				if (BlockedField[NIdx])
				{
					continue;
				}
				if (Fallback == INDEX_NONE)
				{
					Fallback = NIdx;
				}
				// Prefer a cell the player can actually see (no static wall between) — keeps the snapped source
				// on the player's side of the obstacle. First LOS-clear cell (nearest-first) wins.
				const FVector CellCenter(
					GridOrigin.X + (NX + 0.5f) * ActiveCellSize,
					GridOrigin.Y + (NY + 0.5f) * ActiveCellSize,
					PlayerLocation.Z);
				if (World == nullptr ||
					!World->LineTraceTestByObjectType(PlayerLocation, CellCenter, ObjParams, QueryParams))
				{
					return NIdx;
				}
			}
		}
	}
	return Fallback;
}

int32 UFPSRFlowFieldSubsystem::WorldToCellIndex(const FVector& WorldLocation) const
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

bool UFPSRFlowFieldSubsystem::IsEdgeTraversable(int32 CellA, int32 CellB) const
{
	if (GridDimX <= 0)
	{
		return false;
	}
	const int32 AX = CellA % GridDimX, AY = CellA / GridDimX;
	const int32 BX = CellB % GridDimX, BY = CellB / GridDimX;
	// Canonicalize to the lower cell's +X / +Y edge so both directions read the same stored entry.
	if (AY == BY && FMath::Abs(AX - BX) == 1)
	{
		const int32 LeftCell = (AX < BX) ? CellA : CellB; // owns the +X edge to its right neighbor
		return EdgeTraversable[LeftCell * 2 + 0];
	}
	if (AX == BX && FMath::Abs(AY - BY) == 1)
	{
		const int32 BottomCell = (AY < BY) ? CellA : CellB; // owns the +Y edge to its top neighbor
		return EdgeTraversable[BottomCell * 2 + 1];
	}
	return false; // not orthogonally adjacent — the BFS/flow passes only query the 4-neighborhood
}

void UFPSRFlowFieldSubsystem::RecomputeField()
{
	if (!HasServerAuthority() || GridDimX <= 0 || GridDimY <= 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Skip the recompute during the global freeze (§2-2): enemy movement is gated off, so nothing samples the
	// field while paused — recomputing toward stationary players is wasted work (W1 P3-2).
	if (const AFPSRGameState* GS = World->GetGameState<AFPSRGameState>())
	{
		if (GS->IsRunPaused())
		{
			return;
		}
	}

	const int32 NumCells = GridDimX * GridDimY;
	for (int32 i = 0; i < NumCells; ++i)
	{
		DistField[i] = MAX_int32;
	}

	// Seed BFS from every alive player's cell (multi-source -> field points to NEAREST player).
	TQueue<int32> Frontier;
	bool bAnySource = false;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APlayerController* PC = It->Get())
		{
			if (const APawn* PlayerPawn = PC->GetPawn())
			{
				int32 Cell = WorldToCellIndex(PlayerPawn->GetActorLocation());
					// If the player's coarse cell is blocked (standing next to geometry), seed from the nearest
					// open cell instead — otherwise the BFS expands from a wall cell across the obstacle.
					if (Cell != INDEX_NONE && BlockedField[Cell])
					{
						Cell = FindNearestOpenCell(Cell, PlayerPawn->GetActorLocation());
					}
				if (Cell != INDEX_NONE && DistField[Cell] != 0)
				{
					DistField[Cell] = 0;
					Frontier.Enqueue(Cell);
					bAnySource = true;
				}
			}
		}
	}

	if (!bAnySource)
	{
		bFieldReady = false;
		return;
	}

	// 4-connected BFS (uniform cost) -> integration field.
	static const int32 DX4[4] = { 1, -1, 0, 0 };
	static const int32 DY4[4] = { 0, 0, 1, -1 };
	int32 Current;
	while (Frontier.Dequeue(Current))
	{
		const int32 CurDist = DistField[Current];
		const int32 CX = Current % GridDimX;
		const int32 CY = Current / GridDimX;
		for (int32 N = 0; N < 4; ++N)
		{
			const int32 NX = CX + DX4[N];
			const int32 NY = CY + DY4[N];
			if (NX < 0 || NX >= GridDimX || NY < 0 || NY >= GridDimY)
			{
				continue;
			}
			const int32 NIdx = NY * GridDimX + NX;
			if (BlockedField[NIdx])
			{
				continue; // never propagate flow through static obstacles (walls/buildings)
			}
			if (!IsEdgeTraversable(Current, NIdx))
			{
				continue; // Part B: a thin wall on the shared boundary blocks this edge even if both cells are open
			}
			if (DistField[NIdx] > CurDist + 1)
			{
				DistField[NIdx] = CurDist + 1;
				Frontier.Enqueue(NIdx);
			}
		}
	}

	// Flow per cell: steepest descent toward the lowest-distance of the 8 neighbors.
	static const int32 DX8[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	static const int32 DY8[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
	for (int32 CY = 0; CY < GridDimY; ++CY)
	{
		for (int32 CX = 0; CX < GridDimX; ++CX)
		{
			const int32 Idx = CY * GridDimX + CX;

			// Compute steepest descent for EVERY cell, including blocked / unreachable ones (DistField == MAX):
			// an enemy standing in a partially-obstructed coarse cell still gets an escape direction toward the
			// nearest reachable open neighbor instead of zero flow (which would jam it against geometry). Codex.
			int32 BestDist = DistField[Idx];
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
				const int32 NIdx = NY * GridDimX + NX;
				// Don't skip blocked neighbors outright: unreachable ones already have MAX distance (never
				// chosen below), while a blocked cell with a finite distance is a player SOURCE we must still be
				// able to point at (a player standing next to geometry, Codex P2).
				if (N < 4)
				{
					// Orthogonal: must NOT point the flow across a blocked edge — a thin boundary wall blocks an
					// edge even when both cells are open (Part B). The BFS routes around it, so without this gate
					// this cell could point straight at a lower-distance neighbor through the wall and jam enemies
					// against it (the failure mode the BFS-only gate misses). EXCEPTION: if THIS cell is itself
					// blocked, keep the escape behavior (steer toward any open neighbor rather than jam). An open
					// cell always has its BFS-parent edge open, so it still gets a valid descent direction.
					if (!BlockedField[Idx] && !IsEdgeTraversable(Idx, NIdx))
					{
						continue;
					}
				}
				else
				{
					// Diagonal corner-clearance: take a diagonal only if BOTH orthogonal CELLS are open (the swept
					// capsule can't clip a blocked corner), AND — for a non-escaping open cell — BOTH orthogonal
					// EDGES are open, so the diagonal can't cut across a thin wall the edge test rejects (Part B).
					const int32 OrthoA = CY * GridDimX + NX; // (NX, CY)
					const int32 OrthoB = NY * GridDimX + CX; // (CX, NY)
					if (BlockedField[OrthoA] || BlockedField[OrthoB])
					{
						continue;
					}
					if (!BlockedField[Idx] && (!IsEdgeTraversable(Idx, OrthoA) || !IsEdgeTraversable(Idx, OrthoB)))
					{
						continue;
					}
				}
				if (DistField[NIdx] < BestDist)
				{
					BestDist = DistField[NIdx];
					BestNX = NX;
					BestNY = NY;
				}
			}

			if (BestNX >= 0)
			{
				const FVector2D Dir(static_cast<float>(BestNX - CX), static_cast<float>(BestNY - CY));
				FlowField[Idx] = Dir.GetSafeNormal();
			}
			else
			{
				FlowField[Idx] = FVector2D::ZeroVector; // at/near a source
			}
		}
	}

	bFieldReady = true;
}

FVector UFPSRFlowFieldSubsystem::SampleFlowDirection(const FVector& WorldLocation) const
{
	if (!bFieldReady)
	{
		return FVector::ZeroVector;
	}
	const int32 Idx = WorldToCellIndex(WorldLocation);
	if (Idx == INDEX_NONE)
	{
		return FVector::ZeroVector;
	}
	const FVector2D F = FlowField[Idx];
	return FVector(F.X, F.Y, 0.0f);
}
