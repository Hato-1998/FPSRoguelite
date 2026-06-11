// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Run/Mission/FPSRMissionActor.h"
#include "FPSRMission_LimitedVision.generated.h"

/** Mission: endure a globally restricted field of view for RequiredSeconds. On activation the mission sets the
 *  GameState vision-restriction flag (each client applies a camera post-process); progress accrues over time and
 *  completes at RequiredSeconds. The restriction is always cleared when the mission ends or the actor is destroyed. */
UCLASS()
class FPSROGUELITE_API AFPSRMission_LimitedVision : public AFPSRMissionActor
{
	GENERATED_BODY()

public:
	AFPSRMission_LimitedVision();

	UPROPERTY(EditDefaultsOnly, Category = "Mission|LimitedVision", meta = (ClampMin = "0.0"))
	float RequiredSeconds = 20.0f;

protected:
	virtual void OnMissionActivated() override;
	virtual void OnMissionTickServer(float DeltaSeconds) override;
	virtual void OnMissionEnded(bool bSuccess) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Server: set or clear the GameState vision restriction. Safe no-op off-authority / no GameState. */
	void SetVisionRestricted(bool bRestricted);

	float ElapsedSeconds = 0.0f;
};
