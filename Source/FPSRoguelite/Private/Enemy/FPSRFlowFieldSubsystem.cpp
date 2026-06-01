// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSRFlowFieldSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "Containers/Queue.h"

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

	// Grid setup: square grid centered on the world origin.
	GridDim = FMath::Max(1, FMath::CeilToInt((2.0f * HalfExtent) / CellSize));
	GridOrigin = FVector(-HalfExtent, -HalfExtent, 0.0f);
	DistField.Init(MAX_int32, GridDim * GridDim);
	FlowField.Init(FVector2D::ZeroVector, GridDim * GridDim);

	if (HasServerAuthority())
	{
		InWorld.GetTimerManager().SetTimer(
			RecomputeTimerHandle, this, &UFPSRFlowFieldSubsystem::RecomputeField,
			FlowUpdateInterval, true);
	}
}

void UFPSRFlowFieldSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RecomputeTimerHandle);
	}
	Super::Deinitialize();
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
				const int32 Cell = WorldToCellIndex(PlayerPawn->GetActorLocation());
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
			if (DistField[Idx] == MAX_int32)
			{
				FlowField[Idx] = FVector2D::ZeroVector; // unreachable
				continue;
			}

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
