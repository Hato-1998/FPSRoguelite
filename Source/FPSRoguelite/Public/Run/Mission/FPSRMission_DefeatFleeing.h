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

	virtual TSubclassOf<UFPSRMissionTuning> GetExpectedTuningClass() const override;

protected:
	virtual void OnMissionActivated() override;
	virtual void OnMissionTickServer(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleTargetDeath(AActor* DeadActor, AActor* Killer);

	UPROPERTY()
	TObjectPtr<AFPSRMissionFleeTarget> Target;

	/** Effective tuning cached in OnMissionActivated (tuning-or-fallback resolved once, not per-tick). */
	float EffFleeSpeed = 350.0f;
	float EffFleeTriggerRange = 900.0f;
};
