// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Templates/SubclassOf.h"
#include "Weapon/FPSRProjectileTypes.h"
#include "FPSRProjectileSubsystem.generated.h"

class AFPSRProjectile;

/** Server-authoritative projectile pool with replicated-projectile cap (Game.MD §5).
 *  Manages acquisition, release, and lifetime of pooled projectiles. */
UCLASS()
class FPSROGUELITE_API UFPSRProjectileSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;

	/** Acquire a projectile from the pool or spawn a new one.
	 *  Enforces MaxReplicatedProjectiles cap by force-releasing the oldest active if needed.
	 *  Returns nullptr if not server authority. */
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
};
