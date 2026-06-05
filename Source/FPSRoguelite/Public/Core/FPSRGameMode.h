// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/GameModeBase.h"
#include "Templates/SubclassOf.h"
#include "FPSRGameMode.generated.h"

class UFPSRCardPoolDataAsset;
class UFPSRRunScheduleDataAsset;
class AFPSREnemyBase;

/** Default game mode wiring the project's GameState, PlayerController, PlayerState and Character. */
UCLASS()
class FPSROGUELITE_API AFPSRGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFPSRGameMode();

	virtual void BeginPlay() override;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Cards")
	TObjectPtr<UFPSRCardPoolDataAsset> CardPool;

	/** Round/mission schedule for the run (assigned in the GameMode BP). If null the director uses a
	 *  built-in test fallback schedule (values only, no asset paths). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Run")
	TObjectPtr<UFPSRRunScheduleDataAsset> RunSchedule;

	/** Swarm enemy class to spawn (assign a BP child of AFPSREnemyBase to set XPReward/mesh/stats).
	 *  If null the spawn director falls back to the C++ AFPSREnemyBase (engine cube placeholder). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Enemy")
	TSubclassOf<AFPSREnemyBase> EnemyClass;
};
