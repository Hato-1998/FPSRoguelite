// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "Templates/SubclassOf.h"
#include "Weapon/FPSRProjectileTypes.h"
#include "FPSRProjectileSubsystem.generated.h"

class AFPSRProjectile;

/** Server-authoritative projectile pool with the replicated-projectile cap (Game.MD §5).
 *  Manages acquisition, release, and the global-freeze suspension of pooled projectiles.
 *
 *  SCOPE: this pool owns SERVER-AUTHORITATIVE replicated projectiles only (the ≤64 cap is a replication
 *  concern, Game.MD §5). Client-side prediction (Game.MD §2-10) is the firing ability's job (A3): the
 *  projectile base uses deterministic movement, so A3 can spawn a client-local cosmetic copy that follows the
 *  same trajectory while damage stays server-authoritative here. A1 does not ship that predicted spawn path. */
UCLASS()
class FPSROGUELITE_API UFPSRProjectileSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;

	// FTickableGameObject — suspends/resumes active projectiles on the global run-freeze transition (§2-2).
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickable() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;

	/** Acquire a SERVER-AUTHORITATIVE projectile from the pool or spawn a new one.
	 *  Enforces MaxReplicatedProjectiles cap by force-releasing the oldest active if needed.
	 *  Returns nullptr if not server authority (cosmetic prediction is the firing ability's job, see SCOPE). */
	AFPSRProjectile* AcquireProjectile(
		TSubclassOf<AFPSRProjectile> ProjectileClass,
		const FVector& Location,
		const FVector& Direction,
		const FFPSRProjectileParams& InParams
	);

	/** Release a projectile back to the dormant pool. */
	void ReleaseProjectile(AFPSRProjectile* Projectile);

	/** Release all active projectiles back to the pool. */
	void ReleaseAllProjectiles();

	/** Get the current number of active projectiles. */
	int32 GetActiveCount() const { return ActiveProjectiles.Num(); }

private:
	/** Check if this subsystem has server authority. */
	bool HasServerAuthority() const;

	/** Pool of dormant (hidden, disabled) projectiles ready for reuse. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AFPSRProjectile>> DormantPool;

	/** Set of currently active projectiles (FIFO for cap enforcement). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AFPSRProjectile>> ActiveProjectiles;

	/** Max simultaneous replicated projectiles (Game.MD §5 cap). */
	static constexpr int32 MaxReplicatedProjectiles = 64;

	/** Last observed global-freeze state, so Tick only acts on the pause/resume transition. */
	bool bProjectilesPaused = false;
};
