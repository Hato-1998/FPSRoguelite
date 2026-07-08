// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Run/Mission/FPSRMissionActor.h"
#include "FPSRMission_HoldZone.generated.h"

/** Reference mission: hold a circular zone for N seconds to complete. */
UCLASS()
class FPSROGUELITE_API AFPSRMission_HoldZone : public AFPSRMissionActor
{
	GENERATED_BODY()

public:
	AFPSRMission_HoldZone();

	virtual TSubclassOf<UFPSRMissionTuning> GetExpectedTuningClass() const override;

protected:
	virtual void OnMissionActivated() override;
	virtual void OnMissionTickServer(float DeltaSeconds) override;

private:
	float HeldSeconds = 0.0f;

	/** Effective tuning cached in OnMissionActivated (tuning-or-fallback resolved once, not per-tick). */
	float EffZoneRadius = 700.0f;
	float EffRequiredHoldSeconds = 30.0f;
};
