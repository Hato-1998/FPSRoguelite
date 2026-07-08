// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMission_HoldZone.h"
#include "Run/Mission/FPSRMissionTuning.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "DrawDebugHelpers.h"

AFPSRMission_HoldZone::AFPSRMission_HoldZone()
{
	PrimaryActorTick.TickInterval = 0.1f;
}

TSubclassOf<UFPSRMissionTuning> AFPSRMission_HoldZone::GetExpectedTuningClass() const
{
	return UFPSRMissionTuning_HoldZone::StaticClass();
}

void AFPSRMission_HoldZone::OnMissionActivated()
{
	HeldSeconds = 0.0f;

	// §2-8-1: read from the DataAsset's Tuning (or the tuning subclass's CDO defaults when unset), resolved once
	// here rather than every tick.
	const UFPSRMissionTuning_HoldZone& T = GetTuning<UFPSRMissionTuning_HoldZone>();
	EffZoneRadius = T.ZoneRadius;
	EffRequiredHoldSeconds = T.RequiredHoldSeconds;
}

void AFPSRMission_HoldZone::OnMissionTickServer(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Check if any player pawn is within the zone
	bool bPlayerPresent = false;
	FVector MissionLoc = GetActorLocation();
	const float Radius = ResolveZoneRadius(EffZoneRadius); // live-tunable via FPSR.Mission.ZoneRadius

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (APawn* PlayerPawn = PC->GetPawn())
			{
				FVector PlayerLoc = PlayerPawn->GetActorLocation();
				float DistSq = FVector::DistSquared2D(MissionLoc, PlayerLoc);
				if (DistSq <= (Radius * Radius))
				{
					bPlayerPresent = true;
					break;
				}
			}
		}
	}

	// Accumulate held time if player present
	if (bPlayerPresent)
	{
		HeldSeconds += DeltaSeconds;
		SetMissionProgress(FMath::Clamp(HeldSeconds / FMath::Max(EffRequiredHoldSeconds, 0.01f), 0.0f, 1.0f));

		if (HeldSeconds >= EffRequiredHoldSeconds)
		{
			CompleteMission();
		}
	}

#if ENABLE_DRAW_DEBUG
	// Debug visualization: draw zone cylinder
	DrawDebugCylinder(
		World,
		MissionLoc,
		MissionLoc + FVector(0, 0, 200.0f),
		Radius,
		32,
		FColor::Green,
		false,
		0.0f
	);
#endif
}
