// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMission_MovingZone.h"
#include "Run/Mission/FPSRMissionPointSet.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "DrawDebugHelpers.h"

AFPSRMission_MovingZone::AFPSRMission_MovingZone()
{
	PrimaryActorTick.TickInterval = 0.1f;
	// Replicate the zone transform so clients see it switch between captured points (1 actor, cheap).
	SetReplicateMovement(true);
}

void AFPSRMission_MovingZone::OnMissionActivated()
{
	Points.Reset();
	if (PointSet)
	{
		PointSet->GetWorldPoints(Points);
	}
	// Fallback: no route (or empty spline) — capture a single point at the spawn location so the mission still
	// works on a map without routes placed (debug trigger).
	if (Points.Num() == 0)
	{
		Points.Add(GetActorLocation());
	}

	CurrentPoint = 0;
	HeldSeconds = 0.0f;
	SetActorLocation(Points[0]); // teleport the zone to the first point (replicated)
	SetMissionProgress(0.0f);
}

void AFPSRMission_MovingZone::OnMissionTickServer(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || Points.Num() == 0 || !Points.IsValidIndex(CurrentPoint))
	{
		return;
	}

	// Occupancy: any player within ZoneRadius (2D) of the current point accumulates hold time.
	const FVector ZoneLoc = GetActorLocation();
	bool bPlayerPresent = false;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (APawn* PlayerPawn = PC->GetPawn())
			{
				if (FVector::DistSquared2D(ZoneLoc, PlayerPawn->GetActorLocation()) <= ZoneRadius * ZoneRadius)
				{
					bPlayerPresent = true;
					break;
				}
			}
		}
	}

	if (bPlayerPresent)
	{
		HeldSeconds += DeltaSeconds;
		if (HeldSeconds >= RequiredHoldSeconds)
		{
			// Point captured — instantly switch to the next point, or complete when the circuit is done.
			++CurrentPoint;
			HeldSeconds = 0.0f;
			if (Points.IsValidIndex(CurrentPoint))
			{
				SetActorLocation(Points[CurrentPoint]);
			}
			else
			{
				SetMissionProgress(1.0f);
				CompleteMission();
				return;
			}
		}
	}

	// Overall progress across the whole circuit (captured points + current point's hold fraction).
	const float PerPoint = FMath::Clamp(HeldSeconds / FMath::Max(RequiredHoldSeconds, 0.01f), 0.0f, 1.0f);
	SetMissionProgress(FMath::Clamp((CurrentPoint + PerPoint) / static_cast<float>(Points.Num()), 0.0f, 1.0f));

#if ENABLE_DRAW_DEBUG
	DrawDebugCylinder(World, ZoneLoc, ZoneLoc + FVector(0.0f, 0.0f, 200.0f), ZoneRadius, 32, FColor::Cyan, false, 0.0f);
#endif
}
