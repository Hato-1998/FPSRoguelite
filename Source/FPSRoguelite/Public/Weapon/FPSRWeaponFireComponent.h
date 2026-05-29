// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "FPSRWeaponFireComponent.generated.h"

class UFPSRWeaponInventoryComponent;

/** Owning-client component that drives fire cadence (fire rate / fire mode), camera recoil, and spread bloom.
 *  Each shot activates the equipped weapon's fire ability (trace + server-authoritative damage). */
UCLASS(ClassGroup = (FPSR), meta = (BlueprintSpawnableComponent))
class FPSROGUELITE_API UFPSRWeaponFireComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSRWeaponFireComponent();

	/** Called on Fire input pressed/released (owning client). */
	void StartFiring();
	void StopFiring();

	/** Extra spread (degrees) from sustained fire; read by the fire ability when tracing. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	float GetCurrentBloom() const { return CurrentBloom; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	void FireOneShot();
	UFPSRWeaponInventoryComponent* GetInventory() const;

	bool bWantsToFire = false;
	float TimeSinceLastShot = 0.0f;
	int32 BurstShotsRemaining = 0;
	float CurrentBloom = 0.0f;
	float AccumulatedRecoilPitch = 0.0f;
};
