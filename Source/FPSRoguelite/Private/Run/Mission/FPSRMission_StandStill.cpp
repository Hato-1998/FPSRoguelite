// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMission_StandStill.h"
#include "Run/Mission/FPSRMissionTuning.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

AFPSRMission_StandStill::AFPSRMission_StandStill()
{
	PrimaryActorTick.TickInterval = 0.1f;
}

TSubclassOf<UFPSRMissionTuning> AFPSRMission_StandStill::GetExpectedTuningClass() const
{
	return UFPSRMissionTuning_StandStill::StaticClass();
}

void AFPSRMission_StandStill::OnMissionActivated()
{
	StillSeconds = 0.0f;

	// Tuning-or-fallback (§2-8-1), resolved once here rather than every tick.
	const UFPSRMissionTuning_StandStill* T = Cast<UFPSRMissionTuning_StandStill>(GetTuningBase());
	EffRequiredStillSeconds = T ? T->RequiredStillSeconds : RequiredStillSeconds;
	EffStillSpeedThreshold = T ? T->StillSpeedThreshold : StillSpeedThreshold;
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
				if (PlayerPawn->GetVelocity().Size2D() > EffStillSpeedThreshold)
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
		SetMissionProgress(FMath::Clamp(StillSeconds / FMath::Max(EffRequiredStillSeconds, 0.01f), 0.0f, 1.0f));
		if (StillSeconds >= EffRequiredStillSeconds)
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
