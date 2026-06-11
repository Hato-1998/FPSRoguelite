// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMission_StandStill.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

AFPSRMission_StandStill::AFPSRMission_StandStill()
{
	PrimaryActorTick.TickInterval = 0.1f;
}

void AFPSRMission_StandStill::OnMissionActivated()
{
	StillSeconds = 0.0f;
}

void AFPSRMission_StandStill::OnMissionTickServer(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Every living player pawn must be below the still threshold. No players present → no progress.
	bool bAnyPlayer = false;
	bool bAllStill = true;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (APawn* PlayerPawn = PC->GetPawn())
			{
				bAnyPlayer = true;
				if (PlayerPawn->GetVelocity().Size2D() > StillSpeedThreshold)
				{
					bAllStill = false;
					break;
				}
			}
		}
	}

	if (bAnyPlayer && bAllStill)
	{
		StillSeconds += DeltaSeconds;
		SetMissionProgress(FMath::Clamp(StillSeconds / FMath::Max(RequiredStillSeconds, 0.01f), 0.0f, 1.0f));
		if (StillSeconds >= RequiredStillSeconds)
		{
			CompleteMission();
		}
	}
	else
	{
		// Any movement (or empty world) resets the streak.
		StillSeconds = 0.0f;
		SetMissionProgress(0.0f);
	}
}
