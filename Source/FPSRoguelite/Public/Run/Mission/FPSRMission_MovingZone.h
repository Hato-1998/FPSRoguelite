// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Run/Mission/FPSRMissionActor.h"
#include "FPSRMission_MovingZone.generated.h"

class AFPSRMovingZoneRoute;

/** Mission: tour a designer-placed route (AFPSRMovingZoneRoute set), capturing each point in order. Players
 *  accumulate hold time while any of them stands within ZoneRadius of the current point; reaching
 *  RequiredHoldSeconds captures it and the zone instantly switches to the next point. Capturing every point
 *  completes the mission. Falls back to a single capture point at the spawn location when no route is assigned. */
UCLASS()
class FPSROGUELITE_API AFPSRMission_MovingZone : public AFPSRMissionActor
{
	GENERATED_BODY()

public:
	AFPSRMission_MovingZone();

	/** Server: assign the route to tour (called by the director before ServerActivate). */
	void SetRoute(AFPSRMovingZoneRoute* InRoute) { Route = InRoute; }

	UPROPERTY(EditDefaultsOnly, Category = "Mission|MovingZone")
	float ZoneRadius = 400.0f;

	/** Hold time (seconds) required to capture EACH point in the route. */
	UPROPERTY(EditDefaultsOnly, Category = "Mission|MovingZone")
	float RequiredHoldSeconds = 30.0f;

protected:
	virtual void OnMissionActivated() override;
	virtual void OnMissionTickServer(float DeltaSeconds) override;

private:
	/** Server-only: the assigned route (not replicated; the zone transform is what clients see). */
	UPROPERTY()
	TObjectPtr<AFPSRMovingZoneRoute> Route = nullptr;

	/** World-space capture points (gathered from the route on activation; fallback = spawn location). */
	TArray<FVector> Points;
	int32 CurrentPoint = 0;
	float HeldSeconds = 0.0f;
};
