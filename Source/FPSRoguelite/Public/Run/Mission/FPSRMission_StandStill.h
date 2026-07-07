// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Run/Mission/FPSRMissionActor.h"
#include "FPSRMission_StandStill.generated.h"

/** Mission: all living players must stay still (near-zero horizontal velocity) for RequiredStillSeconds.
 *  Any player exceeding the speed threshold (or no players present) resets the accumulated streak. */
UCLASS()
class FPSROGUELITE_API AFPSRMission_StandStill : public AFPSRMissionActor
{
	GENERATED_BODY()

public:
	AFPSRMission_StandStill();

	/** fallback: Data->Tuning 미설정 시 사용(§2-8-1 소프트 마이그레이션, 콘텐츠 이관 후 제거 예정) */
	UPROPERTY(EditDefaultsOnly, Category = "Mission|StandStill")
	float RequiredStillSeconds = 15.0f;

	/** Horizontal speed (cm/s) below which a player counts as still.
	 *  fallback: Data->Tuning 미설정 시 사용(§2-8-1 소프트 마이그레이션, 콘텐츠 이관 후 제거 예정) */
	UPROPERTY(EditDefaultsOnly, Category = "Mission|StandStill", meta = (ClampMin = "0.0"))
	float StillSpeedThreshold = 50.0f;

	virtual TSubclassOf<UFPSRMissionTuning> GetExpectedTuningClass() const override;

protected:
	virtual void OnMissionActivated() override;
	virtual void OnMissionTickServer(float DeltaSeconds) override;

private:
	float StillSeconds = 0.0f;

	/** Effective tuning cached in OnMissionActivated (tuning-or-fallback resolved once, not per-tick). */
	float EffRequiredStillSeconds = 15.0f;
	float EffStillSpeedThreshold = 50.0f;
};
