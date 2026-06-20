// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "FPSRBossSpawnPoint.generated.h"

class UArrowComponent;

/** Designer-placed boss spawn anchor (U3). A DEDICATED type, separate from the enemy/mission spawn points, so the
 *  boss arena spot is authored independently (user request). The run director spawns the boss here at BossTime;
 *  with several placed it picks one by weighted random, and with none placed it falls back to a player location.
 *  Server-only selection; not replicated (the spawned boss actor is the replicated object). No tick, no collision. */
UCLASS()
class FPSROGUELITE_API AFPSRBossSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AFPSRBossSpawnPoint();

	/** Relative weight in the weighted-random selection among enabled points (<= 0 excludes the point). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss Spawn", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/** Disable to exclude this point from selection without deleting it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss Spawn")
	bool bEnabled = true;

	float GetWeight() const { return Weight; }
	bool IsEnabled() const { return bEnabled; }

#if WITH_EDITORONLY_DATA
private:
	/** Editor-only direction arrow so designers can see placement + facing (boss spawn rotation). */
	UPROPERTY()
	TObjectPtr<UArrowComponent> EditorArrow;
#endif
};
