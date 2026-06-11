// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMission_LimitedVision.h"
#include "Core/FPSRGameState.h"
#include "Engine/World.h"

AFPSRMission_LimitedVision::AFPSRMission_LimitedVision()
{
	PrimaryActorTick.TickInterval = 0.1f;
}

void AFPSRMission_LimitedVision::OnMissionActivated()
{
	ElapsedSeconds = 0.0f;
	SetVisionRestricted(true);
}

void AFPSRMission_LimitedVision::OnMissionTickServer(float DeltaSeconds)
{
	ElapsedSeconds += DeltaSeconds;
	SetMissionProgress(FMath::Clamp(ElapsedSeconds / FMath::Max(RequiredSeconds, 0.01f), 0.0f, 1.0f));
	if (ElapsedSeconds >= RequiredSeconds)
	{
		CompleteMission();
	}
}

void AFPSRMission_LimitedVision::OnMissionEnded(bool bSuccess)
{
	// Complete or fail — always restore vision.
	SetVisionRestricted(false);
}

void AFPSRMission_LimitedVision::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Safety: clear the restriction if the actor is destroyed without a normal mission end (run end / level travel).
	SetVisionRestricted(false);
	Super::EndPlay(EndPlayReason);
}

void AFPSRMission_LimitedVision::SetVisionRestricted(bool bRestricted)
{
	if (!HasAuthority())
	{
		return;
	}
	if (AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr)
	{
		GS->SetVisionRestricted(bRestricted);
	}
}
