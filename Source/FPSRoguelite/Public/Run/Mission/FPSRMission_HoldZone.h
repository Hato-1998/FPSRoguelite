// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Run/Mission/FPSRMissionActor.h"
#include "FPSRMission_HoldZone.generated.h"

/** Reference mission: hold a circular zone for N seconds to complete. */
UCLASS()
class FPSROGUELITE_API AFPSRMission_HoldZone : public AFPSRMissionActor
{
	GENERATED_BODY()

public:
	AFPSRMission_HoldZone();

	/** fallback: Data->Tuning 미설정 시 사용(§2-8-1 소프트 마이그레이션, 콘텐츠 이관 후 제거 예정) */
	UPROPERTY(EditDefaultsOnly, Category = "Mission|HoldZone")
	float ZoneRadius = 700.0f;

	/** fallback: Data->Tuning 미설정 시 사용(§2-8-1 소프트 마이그레이션, 콘텐츠 이관 후 제거 예정) */
	UPROPERTY(EditDefaultsOnly, Category = "Mission|HoldZone")
	float RequiredHoldSeconds = 30.0f;

	virtual TSubclassOf<UFPSRMissionTuning> GetExpectedTuningClass() const override;

protected:
	virtual void OnMissionActivated() override;
	virtual void OnMissionTickServer(float DeltaSeconds) override;

private:
	float HeldSeconds = 0.0f;

	/** Effective tuning cached in OnMissionActivated (tuning-or-fallback resolved once, not per-tick). */
	float EffZoneRadius = 700.0f;
	float EffRequiredHoldSeconds = 30.0f;
};
