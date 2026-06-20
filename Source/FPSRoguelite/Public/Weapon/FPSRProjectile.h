// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "Weapon/FPSRProjectileTypes.h"
#include "FPSRProjectile.generated.h"

class USphereComponent;
class UProjectileMovementComponent;
class UStaticMeshComponent;

/** Server-authoritative projectile with pooling support. Spawns via UFPSRProjectileSubsystem.
 *  Movement is deterministic (UProjectileMovementComponent), so the design is client-prediction-ready
 *  (Game.MD §2-10): A3's firing ability can spawn a client-local cosmetic copy following the same trajectory.
 *  Damage is always applied on the server. */
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

	/** Server: suspend/resume the in-flight simulation for the global run freeze (card selection, Game.MD §2-2).
	 *  Paused = stop the movement component (velocity preserved) + pause the lifetime timer, so a flying
	 *  projectile holds in place and doesn't expire during the freeze; resume restores both. Driven by
	 *  UFPSRProjectileSubsystem on the pause transition. Clients follow via replicated movement (their PMC
	 *  never simulates), so this is server-side only. */
	void SetSimulationPaused(bool bPaused);

protected:
	virtual void BeginPlay() override;

	/** A gravity round can fall below KillZ; recycle it to the pool instead of letting the engine destroy it, so
	 *  the pool's active/cap accounting stays consistent and the actor is reused (mirrors the enemy KillZ recycle,
	 *  Game.MD §5-2). */
	virtual void FellOutOfWorld(const class UDamageType& DmgType) override;

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

	/** Try to apply (crit-rolled) damage to an actor. Server-only. Returns true if a damage path APPLIED (a receiver
	 *  consumed the hit — drives pierce/release), and outputs whether the hit critted, killed, hit an ENEMY (a
	 *  friendly-fire hit on another player must NOT raise the firing player's hit-marker), and whether REAL damage
	 *  landed (bOutDamaged — a corpse re-hit applies but deals 0, so it gets no marker). */
	bool TryDamageActor(AActor* Target, float WeakpointMultiplier, bool& bOutCrit, bool& bOutKill, bool& bOutWasEnemy, bool& bOutDamaged);

	/** Server: notify the instigating player's controller of a hit-marker (Player-team projectiles only — enemy
	 *  projectiles have no HUD owner). Strongest outcome wins: Kill > Weak > Crit > Hit (Game.MD §2-14). */
	void NotifyInstigatorHitMarker(bool bCrit, bool bWeak, bool bKill) const;

	/** Release this projectile back to the pool. */
	void ReleaseToPool();

	/** True while the run is globally frozen for card selection (Game.MD §2-2). Impacts are gated on this so a
	 *  projectile never lands damage during the freeze. */
	bool IsRunFrozen() const;

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

	/** True while the projectile's simulation is suspended for the run freeze (avoids redundant pause/resume). */
	bool bSimulationPaused = false;

	/** Lifetime timer handle. */
	FTimerHandle LifetimeTimer;

	/** Actors already damaged by THIS projectile (cleared on Launch/Deactivate). Prevents a body + weakpoint
	 *  overlap on the same enemy from double-damaging or double-spending pierce (U3a). */
	TSet<TWeakObjectPtr<AActor>> HitActors;
};
