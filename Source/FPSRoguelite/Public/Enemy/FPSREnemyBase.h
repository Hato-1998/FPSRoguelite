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

protected:
	virtual void BeginPlay() override;

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

	/** Per-instance move speed (MoveSpeed * random ±10% on Activate). Game.MD §2-6. */
	UPROPERTY(Transient)
	float CurrentMoveSpeed = MoveSpeed;
};
