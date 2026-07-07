// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "FPSRMissionDataAsset.generated.h"

class AFPSRMissionActor;
class UFPSRCardDataAsset;
class UFPSRMissionTuning;

/** Data-driven mission definition (selected per-round from the run schedule). */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRMissionDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission", meta = (MultiLine = true))
	FText Description;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	FText ObjectiveText;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	TSubclassOf<AFPSRMissionActor> MissionClass;

	/** Time limit in seconds (0 = no time limit). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	float TimeLimit = 0.0f;

	/** Which designer-placed AFPSRMissionSpawnPoint(s) this mission may spawn at (matched against the
	 *  point's MissionTag). Leave empty to allow any point / fall back to a player location when none exist.
	 *  Restricted to the Mission.Spawn.* root (DefaultGameplayTags.ini) so the picker only shows spawn-point tags. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission", meta = (Categories = "Mission.Spawn"))
	FGameplayTag SpawnPointTag;

	/** Polymorphic per-mission-type tuning (§2-8-1 soft migration). When set, its concrete subclass (matching
	 *  MissionClass's GetExpectedTuningClass()) supplies the mission's numeric parameters; the mission actor
	 *  falls back to its own legacy EditDefaultsOnly fields when this is null or the wrong subclass — so
	 *  existing content keeps working unchanged until a designer migrates it to a Tuning asset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Instanced, Category = "Mission")
	TObjectPtr<UFPSRMissionTuning> Tuning = nullptr;

#if WITH_EDITOR
	/** Editor validation: MissionClass must be set; a non-empty SpawnPointTag must be a valid (registered) tag. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
