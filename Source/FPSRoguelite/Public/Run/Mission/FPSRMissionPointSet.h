// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "FPSRMissionPointSet.generated.h"

/** Designer-placed set of world points for a mission: its child scene components are the points (attach order =
 *  order). Missions consume them however they like — AFPSRMission_MovingZone tours them in sequence,
 *  AFPSRMission_CollectOrbs spawns an orb at each. In the BP, add Scene (or Billboard for visibility) components
 *  under the root and drag them in the viewport — each child is one point. The run director selects among
 *  enabled, tag-matched sets (weighted). Server-only selection; not replicated (the spawned mission actor is the
 *  replicated object). */
UCLASS()
class FPSROGUELITE_API AFPSRMissionPointSet : public AActor
{
	GENERATED_BODY()

public:
	AFPSRMissionPointSet();

	/** Set category. Eligible when the mission's SpawnPointTag is empty (any), or this set's tag matches
	 *  (is, or is a child of) that tag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mission Point Set")
	FGameplayTag PointSetTag;

	/** Relative weight in the weighted-random selection among matching sets (<= 0 excludes the set). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mission Point Set", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/** If > 0, this set is only eligible when the nearest player is at least this far from its first point (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mission Point Set", meta = (ClampMin = "0.0"))
	float MinPlayerDistance = 0.0f;

	/** Disable to exclude this set from selection without deleting it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mission Point Set")
	bool bEnabled = true;

	/** Append the world-space points (child scene components, in attach order) to Out. */
	void GetWorldPoints(TArray<FVector>& Out) const;

	/** World transform of the first point (first child); actor transform if there are no point children. */
	FTransform GetFirstPointTransform() const;

	FGameplayTag GetPointSetTag() const { return PointSetTag; }
	float GetWeight() const { return Weight; }
	float GetMinPlayerDistance() const { return MinPlayerDistance; }
	bool IsEnabled() const { return bEnabled; }
};
