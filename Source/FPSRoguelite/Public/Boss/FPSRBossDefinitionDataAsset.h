// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Templates/SubclassOf.h"
#include "FPSRBossDefinitionDataAsset.generated.h"

class AFPSRBossBase;

/** Content-driven boss definition (Game.MD §2-7): WHICH boss to spawn and its tuning. Referenced by the run
 *  schedule (UFPSRRunScheduleDataAsset.BossDefinition) so the director can spawn the right boss at BossTime
 *  without hardcoding any asset path. The visual mesh and weakpoint zones live on the boss BP (BossClass);
 *  this asset holds only the spawn-time numbers, keeping designer tuning in one place.
 *
 *  Extensibility (directive): a new boss = a new BossClass BP + a new definition asset — no central code edit.
 *  Real-boss tuning (movement/ability sets / phase thresholds / a StateTree reference) extends this asset in the
 *  long-term boss unit; the scaffold ships only the health-only fields. */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRBossDefinitionDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Boss actor class to spawn (a BP child of AFPSRBossBase). If null the director falls back to the C++
	 *  AFPSRBossBase (cube placeholder) so the victory loop is testable before any boss BP exists. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss")
	TSubclassOf<AFPSRBossBase> BossClass;

	/** Max health applied to the spawned boss (overrides the boss class's DefaultMaxHealth). Balance value. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss", meta = (ClampMin = "1.0"))
	float MaxHealth = 10000.0f;

	/** If true, the director spawns the boss at a designer-placed AFPSRBossSpawnPoint (falling back to a player
	 *  location when none is placed). If false, it spawns at the fallback directly. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss")
	bool bUseBossSpawnPoint = true;

	/** One-line summary for designer tooling / catalog (auto-description directive). */
	UFUNCTION(BlueprintPure, Category = "Boss")
	FText GetDescription() const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
