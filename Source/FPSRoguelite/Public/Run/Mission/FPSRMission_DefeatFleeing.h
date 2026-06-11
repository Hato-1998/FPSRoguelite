// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Run/Mission/FPSRMissionActor.h"
#include "FPSRMission_DefeatFleeing.generated.h"

class AFPSRMissionFleeTarget;

/** Mission: a high-HP target that flees from nearby players; kill it to complete. Self-contained (spawns its
 *  own target), independent of the swarm pool. */
UCLASS()
class FPSROGUELITE_API AFPSRMission_DefeatFleeing : public AFPSRMissionActor
{
	GENERATED_BODY()

public:
	AFPSRMission_DefeatFleeing();

	UPROPERTY(EditDefaultsOnly, Category = "Mission|DefeatFleeing")
	TSubclassOf<AFPSRMissionFleeTarget> TargetClass;

	/** Flee speed (cm/s) while a player is within FleeTriggerRange. */
	UPROPERTY(EditDefaultsOnly, Category = "Mission|DefeatFleeing")
	float FleeSpeed = 350.0f;

	/** The target flees only while the nearest player is within this range (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Mission|DefeatFleeing")
	float FleeTriggerRange = 900.0f;

protected:
	virtual void OnMissionActivated() override;
	virtual void OnMissionTickServer(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleTargetDeath(AActor* DeadActor, AActor* Killer);

	UPROPERTY()
	TObjectPtr<AFPSRMissionFleeTarget> Target;
};
