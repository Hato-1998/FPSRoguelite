// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "FPSRRunScheduleDataAsset.generated.h"

class UFPSRMissionDataAsset;

/** Definition of a single round in a run (combat duration, enemy targets, optional mission). */
USTRUCT(BlueprintType)
struct FFPSRRoundDef
{
	GENERATED_BODY()

	/** Combat phase duration in seconds (ignored if bBossRound). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Round")
	float Duration = 120.0f;

	/** Target alive enemy count for the spawn director during this round. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Round")
	int32 TargetAliveCount = 50;

	/** Optional mission spawned during this round (null = no mission). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Round")
	TObjectPtr<UFPSRMissionDataAsset> Mission = nullptr;

	/** If true, this round triggers the boss gate (end of run; ignores Duration and TargetAliveCount). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Round")
	bool bBossRound = false;
};

/** Data-driven run schedule: defines the sequence of rounds, enemies, and missions for a full run. */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRRunScheduleDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Ordered list of rounds. If empty, the director uses hardcoded fallback rounds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	TArray<FFPSRRoundDef> Rounds;
};
