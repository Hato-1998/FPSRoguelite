// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "FPSRMovingZoneRoute.generated.h"

/** Designer-placed MovingZone "set": its child scene components are the ordered capture points (attach order =
 *  capture order) that AFPSRMission_MovingZone tours as one region's circuit. In the BP, add Scene (or Billboard
 *  for visibility) components under the root and drag them in the viewport — each child is one capture point.
 *  The run director selects among enabled, tag-matched routes (weighted). Server-only selection; not replicated
 *  (the spawned mission actor is the replicated object). */
UCLASS()
class FPSROGUELITE_API AFPSRMovingZoneRoute : public AActor
{
	GENERATED_BODY()

public:
	AFPSRMovingZoneRoute();

	/** Route category. Eligible when the mission's SpawnPointTag is empty (any), or this route's tag matches
	 *  (is, or is a child of) that tag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MovingZone Route")
	FGameplayTag RouteTag;

	/** Relative weight in the weighted-random selection among matching routes (<= 0 excludes the route). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MovingZone Route", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/** If > 0, this route is only eligible when the nearest player is at least this far from its first point (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MovingZone Route", meta = (ClampMin = "0.0"))
	float MinPlayerDistance = 0.0f;

	/** Disable to exclude this route from selection without deleting it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MovingZone Route")
	bool bEnabled = true;

	/** Append the world-space capture points (child scene components, in attach order) to Out. */
	void GetWorldPoints(TArray<FVector>& Out) const;

	/** World transform of the first capture point (first child); actor transform if there are no point children. */
	FTransform GetFirstPointTransform() const;

	FGameplayTag GetRouteTag() const { return RouteTag; }
	float GetWeight() const { return Weight; }
	float GetMinPlayerDistance() const { return MinPlayerDistance; }
	bool IsEnabled() const { return bEnabled; }
};
