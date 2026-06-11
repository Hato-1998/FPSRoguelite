// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/FPSRGameplayAbility.h"
#include "Weapon/FPSRWeaponFragment.h" // FFPSRFireContext (cached for per-hit fragment hooks on the payoff shot)
#include "FPSRGA_WeaponFire_ChargeLaser.generated.h"

class APawn;
class AController;
class UFPSRWeaponInstance;

/** ChargeLaser fire ability (Game.MD §2-10 laser = hitscan): ONE click runs an automatic charge sequence — no
 *  hold-to-charge. The server drives the whole sequence from internal timers (server-authoritative damage):
 *    - while charging: a fixed-damage warm-up beam fires every ChargeTickInterval (ChargeTickDamage),
 *    - on completion (ChargeTime elapsed): one full-power piercing beam (Damage) with crit + hit-marker.
 *  The beam re-traces from the current view point each tick, so it tracks the player's aim during the charge.
 *  Both the warm-up ticks and the final beam reuse the P5 friendly-fire helpers (FPSRCombat::*). */
UCLASS()
class FPSROGUELITE_API UFPSRGA_WeaponFire_ChargeLaser : public UFPSRGameplayAbility
{
	GENERATED_BODY()

public:
	UFPSRGA_WeaponFire_ChargeLaser();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	/** Clears the charge timers before ending — covers weapon-swap / death cancels so a timer can't fire into a
	 *  stale or destroyed avatar. */
	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

protected:
	/** Looping timer callback: fire one warm-up beam tick (fixed chip damage, no crit, no hit-marker). */
	void DoChargeTick();

	/** One-shot timer callback (charge complete): stop the warm-up ticks, fire the full-power beam, end the ability. */
	void DoFinalShot();

	/** Re-trace the piercing hitscan beam from the current view point and apply BeamDamage. Server-authoritative;
	 *  reuses the FF helpers. bIsPayoffShot distinguishes the two beam kinds:
	 *   - payoff (final) shot: scales by the player's global damage multiplier, rolls crit, runs per-hit fragment
	 *     hooks, aggregates an enemy hit-marker, and fires PostFire.
	 *   - warm-up tick (false): PURE fixed chip damage — NO global damage multiplier, NO crit, NO fragments, NO
	 *     marker. So a "+damage" card raises the payoff beam but never the warm-up ticks. */
	void FireBeam(float BeamDamage, bool bIsPayoffShot);

	// --- Per-activation cache (set in ActivateAbility on the server; read by the timer callbacks) ---
	TWeakObjectPtr<APawn> CachedAvatar;
	TWeakObjectPtr<AController> CachedController;
	TWeakObjectPtr<UWorld> CachedWorld;
	TWeakObjectPtr<UFPSRWeaponInstance> CachedInstance;
	float CachedDamage = 0.0f;     // full-power beam damage (the final shot)
	float CachedTickDamage = 0.0f; // warm-up beam damage per tick
	float CachedRange = 0.0f;
	float CachedSpread = 0.0f;
	float CachedChargeEndTime = 0.0f; // world time the payoff fires; lets a same-frame warm-up tick bow out
	FFPSRFireContext CachedFireCtx; // rebuilt per activation; carries fragment list context for the payoff shot

	FTimerHandle TickTimerHandle;  // looping warm-up beam
	FTimerHandle FinalTimerHandle; // one-shot charge-complete beam
};
