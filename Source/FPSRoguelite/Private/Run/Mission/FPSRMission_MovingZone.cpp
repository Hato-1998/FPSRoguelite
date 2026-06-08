// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMission_MovingZone.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "DrawDebugHelpers.h"

AFPSRMission_MovingZone::AFPSRMission_MovingZone()
{
	PrimaryActorTick.TickInterval = 0.1f;
	// Replicate the moving zone transform so clients see it travel (1 actor, cheap).
	SetReplicateMovement(true);
}

void AFPSRMission_MovingZone::OnMissionActivated()
{
	SpawnOrigin = GetActorLocation();
	CurrentWaypoint = 0;
	HeldSeconds = 0.0f;
}

void AFPSRMission_MovingZone::OnMissionTickServer(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Advance the zone center toward the current waypoint (world = spawn origin + relative offset).
	if (RelativeWaypoints.IsValidIndex(CurrentWaypoint))
	{
		const FVector Target = SpawnOrigin + RelativeWaypoints[CurrentWaypoint];
		const FVector Cur = GetActorLocation();
		const FVector ToTarget = Target - Cur;
		const float Step = ZoneMoveSpeed * DeltaSeconds;
		if (ToTarget.SizeSquared() <= Step * Step)
		{
			SetActorLocation(Target);
			++CurrentWaypoint; // reached; advance to the next waypoint (stops once exhausted)
		}
		else
		{
			SetActorLocation(Cur + ToTarget.GetSafeNormal() * Step);
		}
	}

	// Occupancy: any player within the moving zone accumulates hold time.
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
		SetMissionProgress(FMath::Clamp(HeldSeconds / FMath::Max(RequiredHoldSeconds, 0.01f), 0.0f, 1.0f));
		if (HeldSeconds >= RequiredHoldSeconds)
		{
			CompleteMission();
		}
	}

#if ENABLE_DRAW_DEBUG
	DrawDebugCylinder(World, ZoneLoc, ZoneLoc + FVector(0.0f, 0.0f, 200.0f), ZoneRadius, 32, FColor::Cyan, false, 0.0f);
#endif
}
