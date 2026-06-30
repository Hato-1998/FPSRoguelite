// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Enemy/FPSREnemyBase.h"
#include "Templates/SubclassOf.h"
#include "FPSRRangedEnemyBase.generated.h"

class AFPSRProjectile;
class AFPSRPlayerController;

/** Ranged-attack cycle state (server-only). */
enum class EFPSRRangedChargeState : uint8
{
	Idle,      // not engaging — chasing / waiting for a target in range
	Charging,  // telegraphing (warning sent to target); accumulating toward a shot
	Cooldown,  // recovering after a shot before it can re-engage
};

/** Ranged swarm archetype (Game.MD §2-6): stops in range, telegraphs a CHARGE (sends a ranged-target warning to the
 *  targeted player so they can dodge), then fires a VISIBLE projectile (no hitscan — §2-6 mandates a dodgeable shot).
 *  Lightweight (NOT GAS). Reuses the existing projectile + damage + warning + freeze infrastructure (no new damage or
 *  RPC code).
 *
 *  First-principles (≈500 cheap enemies): the base owns movement/gravity/pooling + the per-pass batch contract; this
 *  subclass owns only the attack DECISION via the ServerTickAttack override, so the 500-enemy hot path isn't branched.
 *  Charge & cooldown are freeze-paused accumulators (the spawn subsystem skips the attack pass while the run is
 *  globally frozen, so DeltaSeconds never accrues then). Concurrency is bounded by the subsystem's ranged attack
 *  token (held per-player for the whole charge) — this also keeps in-flight enemy projectiles within the pool budget. */
UCLASS()
class FPSROGUELITE_API AFPSRRangedEnemyBase : public AFPSREnemyBase
{
	GENERATED_BODY()

public:
	AFPSRRangedEnemyBase();

	/** Pooling reactivate: reset the ranged cycle for the reused actor. */
	virtual void Activate(const FVector& Location) override;

	/** Pooling deactivate / death / kill-Z recycle: ALWAYS clear the warning + release the held token (closes the
	 *  Reliable 'off' on every teardown path, not just an explicit abort), then reset the cycle. */
	virtual void Deactivate() override;

	/** Server: per-pass charge->fire cycle (replaces the base melee contact decision). */
	virtual EFPSRServerAttackResult ServerTickAttack(const FFPSRServerAttackContext& Ctx) override;

protected:
	/** World teardown / level change: ensure the warning is cleared + the token released. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// --- Engagement (server-authority; editor/BP tunable per archetype) ---

	/** 3D distance within which the enemy starts charging at a target. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Ranged", meta = (ClampMin = "0.0"))
	float RangedEngageRange = 1400.0f;

	/** Telegraph duration (seconds) the warning shows before the shot fires. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Ranged", meta = (ClampMin = "0.0"))
	float RangedChargeTime = 1.5f;

	/** Recovery (seconds) after firing before the enemy can re-engage. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Ranged", meta = (ClampMin = "0.0"))
	float RangedFireCooldown = 2.5f;

	/** Require an unobstructed line to the target (static geometry) before charging — avoids telegraphing / wasting
	 *  shots through walls (cheap: only the concurrency-capped chargers trace). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Ranged")
	bool bRequireLineOfSight = true;

	// --- Projectile (content assigns ProjectileClass; logic/base only here) ---

	/** Projectile BP to fire (Team=Enemy). Null = no shot (logged once). Designer content (Game.MD §6-2). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Ranged")
	TSubclassOf<AFPSRProjectile> ProjectileClass;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Ranged", meta = (ClampMin = "0.0"))
	float ProjectileDamage = 20.0f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Ranged", meta = (ClampMin = "0.0"))
	float ProjectileSpeed = 1800.0f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Ranged", meta = (ClampMin = "0.0"))
	float ProjectileLifetime = 4.0f;

	/** 0 = straight shot; >0 = arcing (lobbed). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Ranged", meta = (ClampMin = "0.0"))
	float ProjectileGravityScale = 0.0f;

	/** Local-space muzzle offset from the actor origin (also the warning / LOS source point). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Ranged")
	FVector MuzzleOffset = FVector(40.0f, 0.0f, 40.0f);

private:
	/** Fire one enemy-team projectile toward the target (reuses UFPSRProjectileSubsystem — no new damage code). */
	void FireProjectile(const FFPSRServerAttackContext& Ctx);

	/** True if a trace from the muzzle to the target is clear of static geometry AND closed door leaves (or LOS isn't
	 *  required). Ignores self + the target so neither counts as an occluder. */
	bool HasLineOfSight(const AActor* TargetActor, const FVector& TargetLocation) const;

	/** Send the ranged-target warning (Client RPC) to the held target's controller. bActive=false clears it. */
	void SendRangedWarning(bool bActive);

	/** Idempotent: clear the warning on the held target + release the concurrency token. Safe on every teardown. */
	void ReleaseRangedHold();

	/** Reset the cycle to Idle (charge/cooldown accumulators zeroed). */
	void ResetRangedCycle();

	/** World-space muzzle (origin + MuzzleOffset). */
	FVector GetMuzzleLocation() const;

	EFPSRRangedChargeState ChargeState = EFPSRRangedChargeState::Idle;

	/** Seconds accumulated this charge (freeze-paused: only accrues on non-frozen passes). */
	float ChargeElapsed = 0.0f;

	/** Seconds accumulated since the last shot (freeze-paused). */
	float CooldownElapsed = 0.0f;

	/** True while this enemy holds a ranged concurrency token (mirrors an active warning). */
	bool bHoldingToken = false;

	/** The controller currently being charged/warned (token + warning are keyed to it). */
	TWeakObjectPtr<AFPSRPlayerController> HeldTargetPC;

	/** Last location sent in a warning — re-sent once the enemy drifts this far so the indicator tracks the source. */
	FVector LastWarnLocation = FVector::ZeroVector;

	/** Re-send the warning location after drifting this far (cm^2) during a charge (separation nudges the enemy). */
	static constexpr float WarnResendDistSq = 75.0f * 75.0f;
};
