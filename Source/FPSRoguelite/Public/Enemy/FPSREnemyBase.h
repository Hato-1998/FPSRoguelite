// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Pawn.h"
#include "FPSREnemyBase.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;
class UFPSREnemyHealthComponent;

/** Lightweight swarm enemy (P1 test version: manual chase steering, engine cube placeholder).
 *  P2 replaces movement with flow-field + pooling. NOT GAS-based. */
UCLASS()
class FPSROGUELITE_API AFPSREnemyBase : public APawn
{
	GENERATED_BODY()

public:
	AFPSREnemyBase();

	/** Server: reactivate a pooled enemy at Location (unhide, enable collision, reset health, randomize move speed). */
	void Activate(const FVector& Location);

	/** Server: deactivate and return to dormant pool state (hide, disable collision, net dormant). */
	void Deactivate();

	/** Server: move along MoveDirection (XY world dir; magnitude ignored, normalized internally) at
	 *  CurrentMoveSpeed * ScaledDeltaSeconds. No-op if MoveDirection is ~zero. Driven by the enemy
	 *  movement subsystem's batched pass (flow-field + separation). ScaledDeltaSeconds is the real delta
	 *  times this enemy's LOD stride so throttled enemies still cover the right distance. */
	void TickServerMovement(const FVector& MoveDirection, float ScaledDeltaSeconds);

	/** Distance at which the enemy stops advancing toward a player (used by the movement subsystem). */
	float GetStopDistance() const { return StopDistance; }

	float GetAttackRange() const { return AttackRange; }
	float GetAttackDamage() const { return AttackDamage; }

	/** Server: true if the attack cooldown has elapsed at time Now. */
	bool CanAttack(float Now) const { return (Now - LastAttackTime) >= AttackInterval; }

	/** Server: stamp the time of an attack (called by the movement/attack subsystem). */
	void NotifyAttacked(float Now) { LastAttackTime = Now; }

protected:
	virtual void BeginPlay() override;

	/** Server: ground-follow + gravity each movement update — a single down-trace snaps the enemy to the floor
	 *  (slopes/steps within GroundSnapTolerance) or lets it fall under gravity off a ledge / after a high spawn,
	 *  so enemies never float and rooftop-spawned enemies drop down before chasing. */
	void ApplyGravity(float ScaledDeltaSeconds);

	UFUNCTION()
	void HandleDeath(AActor* DeadActor, AActor* Killer);

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Enemy")
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Enemy")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Enemy")
	TObjectPtr<UFPSREnemyHealthComponent> HealthComponent;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy")
	float MoveSpeed = 250.0f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy")
	float StopDistance = 120.0f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Attack")
	float AttackRange = 150.0f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Attack")
	float AttackDamage = 8.0f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Attack")
	float AttackInterval = 1.0f;

	/** Server-only: world time of last attack (init far in the past so the first attack is allowed). */
	float LastAttackTime = -1000.0f;

	/** XP dropped on death (editor-tunable per enemy type / DataAsset). Balance value. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy")
	int32 XPReward = 5;

	/** Per-instance move speed (MoveSpeed * random ±10% on Activate). Game.MD §2-6. */
	UPROPERTY(Transient)
	float CurrentMoveSpeed = MoveSpeed;

	/** Gravity acceleration (cm/s^2) applied while airborne (fall off ledges / land after a high spawn). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Movement")
	float GravityAccel = 1800.0f;

	/** If the floor is within this of the feet (up or down), snap to it (slopes/steps); beyond it (a real drop),
	 *  fall under gravity. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Movement")
	float GroundSnapTolerance = 60.0f;

	/** Short down-trace length for the ground check. Falling is incremental (re-traced each airborne update),
	 *  so this only needs to reach a bit past the feet — keeps the per-enemy scene query cheap at swarm scale. */
	static constexpr float GroundProbeDistance = 700.0f; // cm

	/** Seconds a GROUNDED enemy skips the ground trace before re-checking (amortizes the cost across the swarm;
	 *  airborne enemies trace every update). Bounds ledge-walk-off detection lag. */
	static constexpr float GroundRecheckInterval = 0.15f;

	/** Server-only vertical velocity for gravity/falling (reset to 0 on landing / while grounded). */
	UPROPERTY(Transient)
	float VerticalVelocity = 0.0f;

	/** Server-only: true while resting on the floor (gates the amortized ground re-check). */
	UPROPERTY(Transient)
	bool bGrounded = false;

	/** Server-only: countdown until the next ground re-check while grounded. */
	UPROPERTY(Transient)
	float GroundRecheckTimer = 0.0f;
};
