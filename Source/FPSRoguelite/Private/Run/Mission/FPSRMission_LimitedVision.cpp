// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMission_LimitedVision.h"
#include "Run/Mission/FPSRMissionTuning.h"
#include "Core/FPSRGameState.h"
#include "Engine/World.h"

AFPSRMission_LimitedVision::AFPSRMission_LimitedVision()
{
	PrimaryActorTick.TickInterval = 0.1f;
}

TSubclassOf<UFPSRMissionTuning> AFPSRMission_LimitedVision::GetExpectedTuningClass() const
{
	return UFPSRMissionTuning_LimitedVision::StaticClass();
}

void AFPSRMission_LimitedVision::OnMissionActivated()
{
	ElapsedSeconds = 0.0f;

	// §2-8-1: read from the DataAsset's Tuning (or the tuning subclass's CDO defaults when unset).
	const UFPSRMissionTuning_LimitedVision& T = GetTuning<UFPSRMissionTuning_LimitedVision>();
	EffRequiredSeconds = T.RequiredSeconds;

	SetVisionRestricted(true);
}

void AFPSRMission_LimitedVision::OnMissionTickServer(float DeltaSeconds)
{
	ElapsedSeconds += DeltaSeconds;
	SetMissionProgress(FMath::Clamp(ElapsedSeconds / FMath::Max(EffRequiredSeconds, 0.01f), 0.0f, 1.0f));
	if (ElapsedSeconds >= EffRequiredSeconds)
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
