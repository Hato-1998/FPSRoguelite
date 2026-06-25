// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "FPSREnemySpawnPoint.generated.h"

class UArrowComponent;

/** Designer-placed enemy spawn anchor. The enemy spawn subsystem selects UNIFORMLY at random among eligible
 *  points (enabled + not visible to any player + min distance + its spawn zone active). Server-only selection;
 *  this actor is not replicated (the spawned enemy actor it anchors is the replicated object). Lightweight: no
 *  tick, no collision. A point's ZoneTag is normally auto-applied by the enclosing AFPSRSpawnRoom at BeginPlay. */
UCLASS()
class FPSROGUELITE_API AFPSREnemySpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AFPSREnemySpawnPoint();

	/** Spawn-zone (room) tag. A point with NO tag is always eligible; a tagged point is eligible only while its
	 *  zone is active in the spawn subsystem (a room opens -> its zone activates -> its points go live, and stay
	 *  live as more rooms open — accumulating spawn locations). Usually auto-applied by the enclosing
	 *  AFPSRSpawnRoom; a manually set tag is respected (override). (Room spawn system, Enemy.md §2-6.) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn")
	FGameplayTag ZoneTag;

	/** If > 0, this point is only eligible when the nearest player is at least this far away (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn", meta = (ClampMin = "0.0"))
	float MinPlayerDistance = 0.0f;

	/** Disable to exclude this point from selection without deleting it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn")
	bool bEnabled = true;

	FGameplayTag GetZoneTag() const { return ZoneTag; }
	float GetMinPlayerDistance() const { return MinPlayerDistance; }
	bool IsEnabled() const { return bEnabled; }

	/** Server/setup: assign this point's spawn zone (used by AFPSRSpawnRoom to auto-tag its interior points). */
	void SetZoneTag(const FGameplayTag& InZoneTag) { ZoneTag = InZoneTag; }

#if WITH_EDITORONLY_DATA
private:
	/** Editor-only direction arrow so designers can see placement + facing (enemy spawn rotation). */
	UPROPERTY()
	TObjectPtr<UArrowComponent> EditorArrow;
#endif
};
