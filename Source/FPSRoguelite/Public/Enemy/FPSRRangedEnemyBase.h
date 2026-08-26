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

	/** Pooling deactivate / death-dwell completion / kill-Z recycle: ALWAYS clear the warning + release the held token
	 *  (closes the Reliable 'off' on every teardown path, not just an explicit abort), then reset the cycle. */
	virtual void Deactivate() override;

	/** Death-dwell ENTRY (server-authoritative gameplay end — see AFPSREnemyBase::EnterDyingState): release the held
	 *  token/warning + reset the cycle BEFORE Super ends collision. Same "every teardown path must close the hold"
	 *  reasoning as the Deactivate() override above — a ranged corpse can now dwell for GetDeathDwellSeconds() BEFORE
	 *  Deactivate() ever runs, so waiting for Deactivate() to close the Reliable 'off' would leave the target's
	 *  warning indicator (and this enemy's concurrency token) held for the whole dwell window instead of closing the
	 *  instant the enemy actually dies. */
	virtual void EnterDyingState() override;

	/** Server: per-pass charge->fire cycle (the base itself has no attack decision — dead melee axis removed,
	 *  ADR 0013 C0). */
	virtual void ServerTickAttack(const FFPSRServerAttackContext& Ctx) override;

	/** Server: 스테이지 전환이 **시작될 때** 진행 중이던 충전을 취소한다 (사용자 결정 2026-08-26).
	 *
	 *  이게 필요한 이유 — 전환 중에는 UFPSREnemySpawnSubsystem 이 공격 패스 **전체를 early-return** 하므로
	 *  (IsStageTransitionActive), 충전 중이던 원거리 적은 ServerTickAttack 에 다시 들어오지 못한다. 그래서
	 *  ReleaseRangedHold 를 부르는 경로가 하나도 밟히지 않고, 타겟 플레이어의 **방향 경고 표시가 전환 내내
	 *  화면에 남는다**(그 함수 주석이 이미 경고하는 "안 보내면 영원히 남는다"가 실제로 일어난 것).
	 *  기존 teardown 경로(Deactivate / EnterDyingState / EndPlay)는 전부 적이 *사라질* 때만 도는데,
	 *  전환에서 적은 사라지지 않고 **이월**되므로 그 어느 것도 안 걸린다.
	 *
	 *  Deactivate/EnterDyingState 와 **같은 쌍**(ReleaseRangedHold + ResetRangedCycle)을 부른다 — 사이클까지
	 *  되감아야 스왑 뒤에 그 적이 **텔레그래프 없이 곧장 쏘는** 일이 없다(경고를 지웠는데 충전은 이어지면
	 *  예고 없는 사격이 된다). 취소된 적은 새 아레나에서 토큰을 다시 얻고 처음부터 충전한다.
	 *
	 *  **실제로 취소할 게 있었으면 true.** 호출자가 로그에 개수를 찍을 수 있게 상태를 노출하는 대신 결과를
	 *  돌려준다(bCharging/ChargeState 는 계속 protected/private 로 둔다). 유휴 상태에서 불러도 안전하다. */
	bool ServerCancelRangedForStageTransition();

protected:
	/** World teardown / level change: ensure the warning is cleared + the token released. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Non-targeted client telegraph (user decision): the targeted player already gets a directional warning via the
	 *  Reliable ClientNotifyRangedTarget RPC (SendRangedWarning), but that RPC is unicast to ONLY that player's
	 *  controller — a bystander's screen has no cue a nearby ranged enemy is charging (the client attack-tell
	 *  heuristic in AFPSREnemyBase::PostNetReceiveLocationAndRotation keys off melee AttackRange, which a ranged
	 *  archetype's engage distance sits well outside). This 1-byte flag replicates to EVERY client so OnRep_Charging
	 *  can drive the same Attack cosmetic there. Push Model (this project's replication convention — mirrors
	 *  UFPSREnemyHealthComponent::bDead). */
	UFUNCTION()
	void OnRep_Charging();

	UPROPERTY(ReplicatedUsing = OnRep_Charging)
	bool bCharging = false;

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

	/** Idempotent: clear the warning on the held target + release the concurrency token + clear bCharging. Safe on
	 *  every teardown (see this function's own top-of-body comment on the .cpp side for why bCharging in particular
	 *  is reset unconditionally, ahead of everything else here). */
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
