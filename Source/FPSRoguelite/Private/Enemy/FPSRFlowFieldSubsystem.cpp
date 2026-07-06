// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSRFlowFieldSubsystem.h"
#include "Enemy/FPSRFlowFieldComputer.h"
#include "Enemy/FPSRFlowFieldBoundsVolume.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRLogChannels.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/HitResult.h"
#include "TimerManager.h"
#include "EngineUtils.h"

#if !UE_BUILD_SHIPPING
#include "HAL/IConsoleManager.h"
static TAutoConsoleVariable<int32> CVarFlowFieldDebug(
	TEXT("FPSR.FlowField.Debug"), 0,
	TEXT("Draw the swarm flow field near players (1 = flow arrows + blocked cells, per surface at each layer's floor height; rank>=1 arrows tinted cyan). Dev only."),
	ECVF_Cheat);
#endif

static constexpr float GFlowUpdateInterval = 0.2f; // seconds between recomputes

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

float UFPSRFlowFieldSubsystem::DetectFloorZ(UWorld& InWorld) const
{
	// Grid Z anchor: the obstacle probe is taken relative to GridOrigin.Z, so it MUST sit near the playable floor.
	// Detect the floor under a PlayerStart (trace down); fall back to the start's Z, then to the origin.
	for (TActorIterator<APlayerStart> It(&InWorld); It; ++It)
	{
		if (const APlayerStart* Start = *It)
		{
			const FVector StartLoc = Start->GetActorLocation();
			FHitResult Hit;
			return InWorld.LineTraceSingleByChannel(Hit, StartLoc, StartLoc - FVector(0.0f, 0.0f, 5000.0f), ECC_WorldStatic)
				? Hit.ImpactPoint.Z : StartLoc.Z;
		}
	}
	return 0.0f;
}

void UFPSRFlowFieldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!HasServerAuthority())
	{
		return; // clients never build or recompute the field
	}

	const float FloorZ = DetectFloorZ(InWorld);

	// Data-driven grid bounds: discover a designer-placed AFPSRFlowFieldBoundsVolume (single-map, S1a: first found).
	const AFPSRFlowFieldBoundsVolume* BoundsVolume = nullptr;
	for (TActorIterator<AFPSRFlowFieldBoundsVolume> It(&InWorld); It; ++It)
	{
		if (*It)
		{
			BoundsVolume = *It;
			break;
		}
	}

	DefaultComputer = NewObject<UFPSRFlowFieldComputer>(this);
	DefaultComputer->BuildFromWorldTrace(&InWorld, BoundsVolume, FloorZ); // once: fixed map

	InWorld.GetTimerManager().SetTimer(
		RecomputeTimerHandle, this, &UFPSRFlowFieldSubsystem::RecomputeField,
		GFlowUpdateInterval, true);
}

void UFPSRFlowFieldSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RecomputeTimerHandle);
	}
	Super::Deinitialize();
}

void UFPSRFlowFieldSubsystem::RecomputeField()
{
	if (!HasServerAuthority() || !DefaultComputer)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Skip the recompute during the global freeze (§2-2): enemy movement is gated off, so nothing samples the field.
	if (const AFPSRGameState* GS = World->GetGameState<AFPSRGameState>())
	{
		if (GS->IsRunPaused())
		{
			return;
		}
	}

	DefaultComputer->RecomputeFromWorld(World);

#if !UE_BUILD_SHIPPING
	if (CVarFlowFieldDebug.GetValueOnAnyThread() > 0)
	{
		TArray<FVector> PlayerLocs;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (const APlayerController* PC = It->Get())
			{
				if (const APawn* Pawn = PC->GetPawn())
				{
					PlayerLocs.Add(Pawn->GetActorLocation());
				}
			}
		}
		DefaultComputer->DebugDraw(World, PlayerLocs, GFlowUpdateInterval * 1.2f);
	}
#endif
}

FVector UFPSRFlowFieldSubsystem::SampleFlowDirection(const FVector& WorldLocation) const
{
	return DefaultComputer ? DefaultComputer->Sample(WorldLocation) : FVector::ZeroVector;
}
