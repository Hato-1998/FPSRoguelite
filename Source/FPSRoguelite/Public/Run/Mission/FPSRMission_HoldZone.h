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

	UPROPERTY(EditDefaultsOnly, Category = "Mission|HoldZone")
	float ZoneRadius = 700.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Mission|HoldZone")
	float RequiredHoldSeconds = 30.0f;

protected:
	virtual void OnMissionActivated() override;
	virtual void OnMissionTickServer(float DeltaSeconds) override;

private:
	float HeldSeconds = 0.0f;
};
