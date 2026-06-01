// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "FPSRWeaponFireComponent.generated.h"

class UFPSRWeaponInventoryComponent;
class UCameraComponent;

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

	/** Owner-client + server (via RPC): set aim-down-sights state (FOV/recoil local, spread read by fire GA). */
	void SetAiming(bool bNewAiming) { bIsAiming = bNewAiming; }

	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	bool IsAiming() const { return bIsAiming; }

	/** Called from the owner's look input: forwards the player's downward pitch input (raw units, >=0)
	 *  so manual recoil compensation cancels pending auto-recovery instead of stacking with it. */
	void NotifyPlayerPitchCompensation(float DownAmount);

	/** Shared deterministic per-shot recoil delta (degrees) used by BOTH runtime firing and the
	 *  FPSR.RecoilPreview debug tool, so the preview always matches real behavior.
	 *  Returns {Yaw, Pitch} in degrees: Pitch = up-kick magnitude, Yaw = horizontal pattern (no random variance). */
	static FVector2D ComputeShotRecoilDelta(const struct FFPSRWeaponStatBlock& Stats, int32 ShotIndex);

	/** Returns the equipped weapon's inventory component (needed by debug tools). */
	UFPSRWeaponInventoryComponent* GetInventory() const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	void FireOneShot();

	/** True when the equipped weapon has ammo and is not reloading. */
	bool CanFire() const;

	/** Owner-client: if the mag is empty while the player wants to fire, request a reload (once). */
	void MaybeAutoReload();

	bool bWantsToFire = false;
	float TimeSinceLastShot = 0.0f;
	int32 BurstShotsRemaining = 0;
	float CurrentBloom = 0.0f;

	bool bReloadRequestPending = false; // guards against spamming the reload RPC each tick
	float LastMeleeTime = -1000.0f; // world time of last melee attack (melee attack-rate cooldown)

	bool bIsAiming = false;
	TObjectPtr<UCameraComponent> CachedCamera; // resolved lazily for ADS FOV
	float DefaultFOV = 0.0f;                    // captured from the camera on first resolve

	// --- Recoil state (local feel only) ---
	float RecoilDebtPitch = 0.0f;       // up-kick owed for downward recovery (raw input units)
	float PendingRisePitch = 0.0f;      // not-yet-applied up-kick, smoothed in over time
	float PlayerPitchCompensation = 0.0f; // player's downward look input this frame, consumes debt
	int32 ShotsFiredThisSpray = 0;      // shot index within current trigger hold (horizontal pattern)
};
