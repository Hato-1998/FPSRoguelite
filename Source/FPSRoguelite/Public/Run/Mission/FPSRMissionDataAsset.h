// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "FPSRMissionDataAsset.generated.h"

class AFPSRMissionActor;
class UFPSRCardDataAsset;

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
	 *  point's MissionTag). Leave empty to allow any point / fall back to a player location when none exist. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	FGameplayTag SpawnPointTag;

	/** Weapon/modifier reward card for clearing this mission (P4-A counts only, not offered yet). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	TObjectPtr<UFPSRCardDataAsset> RewardCard = nullptr;

#if WITH_EDITOR
	/** Editor validation: MissionClass must be set. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
