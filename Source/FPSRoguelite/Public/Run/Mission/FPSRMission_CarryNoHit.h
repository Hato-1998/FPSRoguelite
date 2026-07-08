// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Run/Mission/FPSRMissionActor.h"
#include "FPSRMission_CarryNoHit.generated.h"

class AFPSRMissionOrb;
class APawn;

/** Mission: pick up the orb and hold it for RequiredCarrySeconds without the carrier taking damage. Any damage
 *  to the carrier resets the timer; losing the carrier returns the orb so it can be picked up again. */
UCLASS()
class FPSROGUELITE_API AFPSRMission_CarryNoHit : public AFPSRMissionActor
{
	GENERATED_BODY()

public:
	AFPSRMission_CarryNoHit();

	virtual TSubclassOf<UFPSRMissionTuning> GetExpectedTuningClass() const override;

protected:
	virtual void OnMissionActivated() override;
	virtual void OnMissionTickServer(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleOrbCollected(AFPSRMissionOrb* InOrb, APawn* Collector);
	float GetPawnHealth(APawn* Pawn) const;

	UPROPERTY()
	TObjectPtr<AFPSRMissionOrb> Orb;

	FVector OrbHomeLocation = FVector::ZeroVector;
	TWeakObjectPtr<APawn> Carrier;
	float CarrySeconds = 0.0f;
	float LastCarrierHealth = -1.0f;

	/** Effective tuning cached in OnMissionActivated (tuning-or-fallback resolved once, not per-tick). */
	float EffRequiredCarrySeconds = 20.0f;
	float EffCarryHeight = 120.0f;
};
