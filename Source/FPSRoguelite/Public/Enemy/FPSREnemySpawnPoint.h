// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "FPSREnemySpawnPoint.generated.h"

class UArrowComponent;

/** Designer-placed enemy spawn anchor. The enemy spawn subsystem selects among enabled points (not visible
 *  to any player + min distance) by weighted random, falling back to ring-random when none qualify.
 *  Server-only selection; this actor is not replicated (the spawned enemy actor it anchors is the replicated
 *  object). Lightweight: no tick, no collision. */
UCLASS()
class FPSROGUELITE_API AFPSREnemySpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AFPSREnemySpawnPoint();

	/** Optional spawn-zone tag. When the director sets an active zone, only points whose ZoneTag matches (is, or
	 *  is a child of) the active zone are eligible; an empty active zone allows all points. Enables time-of-day /
	 *  phase spawn-region switching (Game.MD §2-8 TimeGate). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn")
	FGameplayTag ZoneTag;

	/** Relative weight in the weighted-random selection among matching points (<= 0 excludes the point). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/** If > 0, this point is only eligible when the nearest player is at least this far away (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn", meta = (ClampMin = "0.0"))
	float MinPlayerDistance = 0.0f;

	/** Disable to exclude this point from selection without deleting it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn")
	bool bEnabled = true;

	FGameplayTag GetZoneTag() const { return ZoneTag; }
	float GetWeight() const { return Weight; }
	float GetMinPlayerDistance() const { return MinPlayerDistance; }
	bool IsEnabled() const { return bEnabled; }

#if WITH_EDITORONLY_DATA
private:
	/** Editor-only direction arrow so designers can see placement + facing (enemy spawn rotation). */
	UPROPERTY()
	TObjectPtr<UArrowComponent> EditorArrow;
#endif
};
