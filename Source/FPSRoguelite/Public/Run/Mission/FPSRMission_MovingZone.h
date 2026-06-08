// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Run/Mission/FPSRMissionActor.h"
#include "FPSRMission_MovingZone.generated.h"

/** Mission: a capture zone that travels through a set of waypoints. Players accumulate occupancy time while
 *  any of them stands within the moving zone; reaching RequiredHoldSeconds completes the mission. */
UCLASS()
class FPSROGUELITE_API AFPSRMission_MovingZone : public AFPSRMissionActor
{
	GENERATED_BODY()

public:
	AFPSRMission_MovingZone();

	UPROPERTY(EditDefaultsOnly, Category = "Mission|MovingZone")
	float ZoneRadius = 400.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Mission|MovingZone")
	float RequiredHoldSeconds = 30.0f;

	/** Speed (cm/s) the zone center travels toward each waypoint. */
	UPROPERTY(EditDefaultsOnly, Category = "Mission|MovingZone", meta = (ClampMin = "0.0"))
	float ZoneMoveSpeed = 200.0f;

	/** Waypoints as offsets (cm) from the spawn location; the zone moves spawn -> wp[0] -> wp[1] ... and stops at the last. */
	UPROPERTY(EditDefaultsOnly, Category = "Mission|MovingZone")
	TArray<FVector> RelativeWaypoints;

protected:
	virtual void OnMissionActivated() override;
	virtual void OnMissionTickServer(float DeltaSeconds) override;

private:
	FVector SpawnOrigin = FVector::ZeroVector;
	int32 CurrentWaypoint = 0;
	float HeldSeconds = 0.0f;
};
