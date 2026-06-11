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

	UPROPERTY(EditDefaultsOnly, Category = "Mission|StandStill")
	float RequiredStillSeconds = 15.0f;

	/** Horizontal speed (cm/s) below which a player counts as still. */
	UPROPERTY(EditDefaultsOnly, Category = "Mission|StandStill", meta = (ClampMin = "0.0"))
	float StillSpeedThreshold = 50.0f;

protected:
	virtual void OnMissionActivated() override;
	virtual void OnMissionTickServer(float DeltaSeconds) override;

private:
	float StillSeconds = 0.0f;
};
