// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Run/Mission/FPSRMissionActor.h"
#include "FPSRMission_StandStill.generated.h"

/** Mission: all living players must stay still (near-zero horizontal velocity) for RequiredStillSeconds.
 *  Any player exceeding the speed threshold (or no players present) resets the accumulated streak. */
UCLASS()
class FPSROGUELITE_API AFPSRMission_StandStill : public AFPSRMissionActor
{
	GENERATED_BODY()

public:
	AFPSRMission_StandStill();

	virtual TSubclassOf<UFPSRMissionTuning> GetExpectedTuningClass() const override;

protected:
	virtual void OnMissionActivated() override;
	virtual void OnMissionTickServer(float DeltaSeconds) override;

private:
	float StillSeconds = 0.0f;

	/** Effective tuning cached in OnMissionActivated (tuning-or-fallback resolved once, not per-tick). */
	float EffRequiredStillSeconds = 15.0f;
	float EffStillSpeedThreshold = 50.0f;
};
