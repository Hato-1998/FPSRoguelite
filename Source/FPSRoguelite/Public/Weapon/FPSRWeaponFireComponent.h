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

	// --- ChargeLaser (hold-to-charge, release-to-fire; server-authoritative charge measurement) ---
	/** True while a ChargeLaser is charging on this machine (set by StartFiring on the local client). */
	bool IsChargingLaser() const { return bChargingLaser; }

	/** World time the current charge began on THIS machine (-1 = not charging). The ChargeLaser fire ability
	 *  reads this to compute the charge alpha against its own clock (client = local feel, server = authoritative). */
	float GetChargeStartWorldTime() const { return ChargeStartWorldTime; }

	/** Server: stamp the charge start time (called from the owning client's ServerStartChargeLaser RPC). Only
	 *  stamps when the equipped weapon is actually a ChargeLaser, so a spoofed RPC for another weapon is ignored. */
	void ServerBeginCharge();

	/** Server: activate the charged beam authoritatively (called from the owning client's ServerReleaseChargeLaser
	 *  RPC, ordered after ServerStartChargeLaser). Reads the server-stamped charge and consumes it. */
	void ServerReleaseCharge();

	/** Clear the charge state after the fire ability consumes it (prevents a single charge firing twice). */
	void ResetCharge();

	/** Equip boundary (called from the inventory's server EquipSlot + client OnRep_CurrentSlotIndex): clears any
	 *  in-progress charge and imposes a minimum post-swap fire cooldown before the next shot, so a rapid weapon
	 *  swap can't bypass fire cadence and the local recoil prediction stays in sync with the server. */
	void OnWeaponEquipped(float EquipCooldown);

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

	// ChargeLaser charge state. ChargeStartWorldTime is stamped locally (StartFiring) and on the server
	// (ServerBeginCharge); the fire ability computes alpha against it and calls ResetCharge to consume it.
	bool bChargingLaser = false;
	float ChargeStartWorldTime = -1.0f;

	bool bReloadRequestPending = false; // guards against spamming the reload RPC each tick
	float LastMeleeTime = -1000.0f; // world time of last melee attack (melee attack-rate cooldown)
	float NextFireReadyTime = 0.0f; // world time the next ranged shot is allowed (per-weapon cadence + post-swap cooldown); gates the immediate press shot's recoil. Mirrors the server's ServerNextAllowedFireTime.

	bool bIsAiming = false;
	TObjectPtr<UCameraComponent> CachedCamera; // resolved lazily for ADS FOV
	float DefaultFOV = 0.0f;                    // captured from the camera on first resolve

	// --- Recoil state (local feel only) ---
	float RecoilDebtPitch = 0.0f;       // up-kick owed for downward recovery (raw input units)
	float PendingRisePitch = 0.0f;      // not-yet-applied up-kick, smoothed in over time
	float PendingRiseYaw = 0.0f;        // not-yet-applied horizontal recoil, smoothed in over time (no recovery)
	float PlayerPitchCompensation = 0.0f; // player's downward look input this frame, consumes debt
	int32 ShotsFiredThisSpray = 0;      // shot index within current trigger hold (horizontal pattern)
};
