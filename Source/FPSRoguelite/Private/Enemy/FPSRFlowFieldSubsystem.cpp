// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSRFlowFieldSubsystem.h"
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

	// Grid setup: square grid centered on the world origin, anchored in Z to the playable floor.
	// The obstacle probe is taken at GridOrigin.Z + ObstacleProbeZ, so GridOrigin.Z MUST sit near the floor —
	// otherwise (floor far from the world origin, e.g. a basement at Z ~ -1000) the probe samples wall/doorway
	// geometry at the wrong height and mis-marks passable floor openings as blocked, jamming enemies. Detect the
	// floor under a PlayerStart (trace down); fall back to the start's Z, then to the origin.
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

	GridDim = FMath::Max(1, FMath::CeilToInt((2.0f * HalfExtent) / CellSize));
	GridOrigin = FVector(-HalfExtent, -HalfExtent, FloorZ);
	DistField.Init(MAX_int32, GridDim * GridDim);
	FlowField.Init(FVector2D::ZeroVector, GridDim * GridDim);
	BlockedField.Init(false, GridDim * GridDim);

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
	if (!World || GridDim <= 0)
	{
		return;
	}

	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRFlowObstacle), false);
	// Full-cell half-extent (no inter-cell gap) so a thin wall on a cell boundary is still caught.
	// ⚠️ Known tradeoff (Codex 2026-06-09, deferred to the flow-field hardening unit C1 / Game.MD §5-2):
	// full-cell probing also marks BOTH cells adjacent to a boundary wall as blocked, so corridors/doorways
	// narrower than ~2 cells can become unreachable even though a capsule fits. The correct fix is
	// clearance-aware probing (agent-footprint box / passage detection), but that depends on the real map's
	// corridor widths & wall thickness and must be validated in PIE — so it is intentionally NOT flipped blind
	// here (a prior round chose full-cell to catch thin boundary walls; this is the opposing pull).
	const FCollisionShape Box = FCollisionShape::MakeBox(
		FVector(CellSize * 0.5f, CellSize * 0.5f, ObstacleProbeHalfHeight));

	int32 BlockedCount = 0;
	for (int32 CY = 0; CY < GridDim; ++CY)
	{
		for (int32 CX = 0; CX < GridDim; ++CX)
		{
			const FVector Center(
				GridOrigin.X + (CX + 0.5f) * CellSize,
				GridOrigin.Y + (CY + 0.5f) * CellSize,
				GridOrigin.Z + ObstacleProbeZ);
			if (World->OverlapAnyTestByObjectType(Center, FQuat::Identity, ObjParams, Box, QueryParams))
			{
				BlockedField[CY * GridDim + CX] = true;
				++BlockedCount;
			}
		}
	}

	UE_LOG(LogFPSR, Log, TEXT("[FlowField] Obstacle mask: %d/%d cells blocked."), BlockedCount, GridDim * GridDim);
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
	if (FromCell == INDEX_NONE || GridDim <= 0)
	{
		return INDEX_NONE;
	}
	const UWorld* World = GetWorld();
	const int32 CX = FromCell % GridDim;
	const int32 CY = FromCell / GridDim;

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
				if (NX < 0 || NX >= GridDim || NY < 0 || NY >= GridDim)
				{
					continue;
				}
				const int32 NIdx = NY * GridDim + NX;
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
					GridOrigin.X + (NX + 0.5f) * CellSize,
					GridOrigin.Y + (NY + 0.5f) * CellSize,
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
	if (GridDim <= 0)
	{
		return INDEX_NONE;
	}
	const int32 CX = FMath::FloorToInt((WorldLocation.X - GridOrigin.X) / CellSize);
	const int32 CY = FMath::FloorToInt((WorldLocation.Y - GridOrigin.Y) / CellSize);
	if (CX < 0 || CX >= GridDim || CY < 0 || CY >= GridDim)
	{
		return INDEX_NONE;
	}
	return CY * GridDim + CX;
}

void UFPSRFlowFieldSubsystem::RecomputeField()
{
	if (!HasServerAuthority() || GridDim <= 0)
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

	const int32 NumCells = GridDim * GridDim;
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
		const int32 CX = Current % GridDim;
		const int32 CY = Current / GridDim;
		for (int32 N = 0; N < 4; ++N)
		{
			const int32 NX = CX + DX4[N];
			const int32 NY = CY + DY4[N];
			if (NX < 0 || NX >= GridDim || NY < 0 || NY >= GridDim)
			{
				continue;
			}
			const int32 NIdx = NY * GridDim + NX;
			if (BlockedField[NIdx])
			{
				continue; // never propagate flow through static obstacles (walls/buildings)
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
	for (int32 CY = 0; CY < GridDim; ++CY)
	{
		for (int32 CX = 0; CX < GridDim; ++CX)
		{
			const int32 Idx = CY * GridDim + CX;

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
				if (NX < 0 || NX >= GridDim || NY < 0 || NY >= GridDim)
				{
					continue;
				}
				const int32 NIdx = NY * GridDim + NX;
				// Don't skip blocked neighbors outright: unreachable ones already have MAX distance (never
				// chosen below), while a blocked cell with a finite distance is a player SOURCE we must still be
				// able to point at (a player standing next to geometry, Codex P2). Corner-clearance is enough.
				// Corner-clearance: take a diagonal (N >= 4) only if BOTH orthogonal cells are open, so the
				// swept enemy capsule can't clip a blocked corner and jam (Codex P2).
				if (N >= 4)
				{
					const int32 OrthoA = CY * GridDim + NX; // (NX, CY)
					const int32 OrthoB = NY * GridDim + CX; // (CX, NY)
					if (BlockedField[OrthoA] || BlockedField[OrthoB])
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
