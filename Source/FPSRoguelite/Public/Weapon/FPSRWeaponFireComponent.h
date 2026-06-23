// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "FPSRWeaponFireComponent.generated.h"

class UFPSRWeaponInventoryComponent;
class UFPSRWeaponInstance;
class UTexture2D;
class UMaterialInterface;
class UCameraComponent;

/** Owning-client component that drives fire cadence (fire rate / fire mode), camera recoil, and spread bloom.
 *  Each shot activates the equipped weapon's fire ability (trace + server-authoritative damage). */
UCLASS(ClassGroup = (FPSR), meta = (BlueprintSpawnableComponent))
class FPSROGUELITE_API UFPSRWeaponFireComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSRWeaponFireComponent();

	/** Called on Fire input pressed/released (owning client). ChargeLaser uses the same single-press path as other
	 *  weapons — one click activates the fire ability, which runs the whole charge sequence server-side. */
	void StartFiring();
	void StopFiring();

	/** Equip boundary (called from the inventory's server EquipSlot + client OnRep_CurrentSlotIndex): imposes a
	 *  minimum post-swap fire cooldown before the next shot, so a rapid weapon swap can't bypass fire cadence and
	 *  the local recoil prediction stays in sync with the server. */
	void OnWeaponEquipped(float EquipCooldown);

	/** Extra spread (degrees) from sustained fire; read by the fire ability when tracing. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	float GetCurrentBloom() const { return CurrentBloom; }

	/** Total spread half-angle (deg) the fire trace uses = (resolved SpreadDegrees + bloom) x ADS.
	 *  Single source of truth shared with the fire ability cone; the HUD crosshair gap reads this. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	float GetCurrentSpreadDegrees() const;

	/** Equipped weapon's per-weapon crosshair material (resolved soft-ref), or null for the HUD default MI. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	UMaterialInterface* GetEquippedCrosshairMaterial() const;

	/** Equipped weapon's dynamic-crosshair toggle (true = apply spread bloom; false = static crosshair). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	bool GetEquippedCrosshairUsesDynamic() const;

	/** Shared spread formula used by BOTH the fire ability cone and the HUD crosshair:
	 *  (Stats.SpreadDegrees + Bloom) x (bAiming && Stats.bHasADS ? Stats.ADSSpreadMultiplier : 1). */
	static float ComputeSpreadDegrees(const struct FFPSRWeaponStatBlock& Stats, float Bloom, bool bAiming);

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

	/** Spin-up fire rate (shots/sec) at the given continuous-fire elapsed time. Linear ramp from
	 *  SpinupFireRateStart to FireRate over SpinupRampTime; returns FireRate when bHasSpinup is false.
	 *  Reads BASE stats so the ramp is immune to FireRate modifier cards. Client-local cadence only
	 *  (the server still enforces just the max-rate anti-abuse ceiling). */
	static float ComputeSpinupFireRate(const struct FFPSRWeaponStatBlock& Stats, float SpinupElapsed);

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
	float NextFireReadyTime = 0.0f; // world time the next ranged shot is allowed (per-weapon cadence + post-swap cooldown); gates the immediate press shot's recoil. Mirrors the server's ServerNextAllowedFireTime.
	float SpinupElapsed = 0.0f; // seconds of continuous fire this trigger hold (spin-up ramp progress; client-local feel). Reset on StopFiring / equip; advances only while auto-firing and not run-paused.

	bool bIsAiming = false;
	TObjectPtr<UCameraComponent> CachedCamera; // resolved lazily for ADS FOV
	float DefaultFOV = 0.0f;                    // captured from the camera on first resolve

	// --- Recoil state (local feel only) ---
	float RecoilDebtPitch = 0.0f;       // up-kick owed for downward recovery (raw input units)
	float PendingRisePitch = 0.0f;      // not-yet-applied up-kick, smoothed in over time
	float PendingRiseYaw = 0.0f;        // not-yet-applied horizontal recoil, smoothed in over time (no recovery)
	float PlayerPitchCompensation = 0.0f; // player's downward look input this frame, consumes debt
	int32 ShotsFiredThisSpray = 0;      // shot index within current trigger hold (horizontal pattern)

	// --- ChargeLaser charge sequence — local-feel recoil ramp + re-press gate (driven in TickComponent) ---
	// bChargeSequenceActive marks a ChargeLaser charge in progress on this client: it drives the recoil ramp (the
	// up-kick climbs over the charge duration and finishes at the fire moment, instead of an instant kick) AND blocks
	// a re-press from starting a phantom second charge. Set up in FireOneShot on click; cleared when the ramp ends.
	bool bChargeSequenceActive = false;
	float ChargeRecoilElapsed = 0.0f;   // seconds into the current charge ramp
	float ChargeRecoilDuration = 0.0f;  // total charge ramp length (= fragment-adjusted ChargeTime, matches the server)
	float ChargeRecoilTotalPitch = 0.0f; // full up-kick spread across the ramp
	float ChargeRecoilTotalYaw = 0.0f;   // full horizontal drift spread across the ramp
};
