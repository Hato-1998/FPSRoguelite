// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "Weapon/FPSRProjectileTypes.h"
#include "FPSRProjectile.generated.h"

class USphereComponent;
class UProjectileMovementComponent;
class UStaticMeshComponent;

/** Server-authoritative projectile with pooling support. Spawns via UFPSRProjectileSubsystem. */
UCLASS()
class FPSROGUELITE_API AFPSRProjectile : public AActor
{
	GENERATED_BODY()

public:
	AFPSRProjectile();

	/** Launch projectile in a given direction with the specified parameters.
	 *  Called after Activate() to apply the initial velocity and replication setup. */
	void Launch(const FFPSRProjectileParams& InParams, const FVector& Direction);

	/** Pooling reactivate: Set location, show, enable collision, and launch toward Direction.
	 *  Gated on server authority. */
	void Activate(const FVector& Location, const FFPSRProjectileParams& InParams, const FVector& Direction);

	/** Pooling deactivate: Stop movement, hide, disable collision, dormancy prep. */
	void Deactivate();

protected:
	virtual void BeginPlay() override;

	/** Hit detection: world collision blocks the projectile. */
	UFUNCTION()
	void OnSphereHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	/** Overlap detection: pawn targets for damage. Server-authoritative gate on ServerAuthoritative mode. */
	UFUNCTION()
	void OnSphereOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	/** Lifetime expired: release to pool. */
	UFUNCTION()
	void OnLifetimeExpired();

	/** Handle projectile impact: apply damage (AOE or single-target) and release to pool. */
	void HandleImpact(const FVector& ImpactPoint);

	/** True if Target is a valid hostile for this projectile's team (enemy for Player team, player character
	 *  for Enemy team), and is not the instigator. Used to decide damage AND whether an AOE round detonates,
	 *  so a round never explodes on a friendly pass-through. */
	bool IsHostileTarget(AActor* Target) const;

	/** Try to apply damage to an actor (returns true if a damage path applied). Server-only. */
	bool TryDamageActor(AActor* Target);

	/** Release this projectile back to the pool. */
	void ReleaseToPool();

	/** Collision sphere (root component). */
	UPROPERTY()
	TObjectPtr<USphereComponent> CollisionSphere;

	/** Projectile movement component (non-ticking). */
	UPROPERTY()
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	/** Visual mesh (no collision). */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> MeshComp;

	/** Current projectile parameters. */
	UPROPERTY()
	FFPSRProjectileParams Params;

	/** Remaining pierce count (decrements on pawn hit; stops at 0). */
	int32 PierceRemaining = 0;

	/** True between Activate() and Deactivate(). Guards the hit/overlap/lifetime handlers so a re-entrant
	 *  event (e.g. a world hit and a pawn overlap in the same movement sweep) can't release this projectile
	 *  to the pool twice. */
	bool bActive = false;

	/** Lifetime timer handle. */
	FTimerHandle LifetimeTimer;
};
