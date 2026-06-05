// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "FPSRMissionSpawnPoint.generated.h"

class UArrowComponent;

/** Designer-placed mission spawn anchor. The run director selects among enabled points (tag-matched,
 *  weighted) to decide where a round's mission spawns. Server-only selection; this actor is not replicated
 *  (the spawned mission actor it anchors is the replicated object). Lightweight: no tick, no collision. */
UCLASS()
class FPSROGUELITE_API AFPSRMissionSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AFPSRMissionSpawnPoint();

	/** Mission category this point can host. A point is eligible for a mission when the mission's
	 *  SpawnPointTag is empty (any point), or this point's tag matches (is, or is a child of) that tag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mission Spawn")
	FGameplayTag MissionTag;

	/** Relative weight in the weighted-random selection among matching points (<= 0 excludes the point). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mission Spawn", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/** If > 0, this point is only eligible when the nearest player is at least this far away (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mission Spawn", meta = (ClampMin = "0.0"))
	float MinPlayerDistance = 0.0f;

	/** Disable to exclude this point from selection without deleting it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mission Spawn")
	bool bEnabled = true;

	FGameplayTag GetMissionTag() const { return MissionTag; }
	float GetWeight() const { return Weight; }
	float GetMinPlayerDistance() const { return MinPlayerDistance; }
	bool IsEnabled() const { return bEnabled; }

#if WITH_EDITORONLY_DATA
private:
	/** Editor-only direction arrow so designers can see placement + facing (mission spawn rotation). */
	UPROPERTY()
	TObjectPtr<UArrowComponent> EditorArrow;
#endif
};
