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

	/** Release only the ENEMY-team projectiles in flight, leaving the player's alone (ADR 0010 D6 — 사용자 결정
	 *  2026-08-25). Called once at transition start (UFPSRStageDirectorSubsystem::RequestTransition): enemy fire
	 *  used to simply FREEZE for the window alongside the swarm, which left live bullets hanging in the air for the
	 *  whole ~10s transition and then resuming into a player who had been teleported to a different arena. Clearing
	 *  is the "탄막 제거" read instead — the window opens on a clean screen. Player projectiles are deliberately NOT
	 *  touched: firing is the grace window's whole reward (안 G). */
	void ReleaseEnemyProjectiles();

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

	/** Max simultaneous replicated PLAYER projectiles (Game.MD §5 cap). Enemy projectiles use a SEPARATE budget
	 *  (CVarEnemyProjectileBudget) so heavy enemy fire never FIFO-evicts a player's in-flight AOE and vice versa. */
	static constexpr int32 MaxReplicatedProjectiles = 64;

	/** Hard ceiling for the enemy projectile budget cvar (FPSR.Enemy.ProjectileBudget). Bounds total replicated
	 *  projectiles (player + enemy) — exceeds the legacy ≤64 line, documented as a provisional §5 budget pending the
	 *  deferred perf measurement; the per-player ranged concurrency token keeps the typical enemy count far below it. */
	static constexpr int32 MaxEnemyProjectileCeiling = 100;

	/** Last observed ALL-projectiles-paused state (the global card-selection freeze), so Tick only acts on its
	 *  transition edge. */
	bool bPausedAll = false;

	/** Last observed ENEMY-projectiles-ONLY-paused state (an active stage transition while the run is NOT otherwise
	 *  paused, ADR 0010 D6), so Tick only acts on its transition edge. Player projectiles keep flying through a
	 *  transition — firing is the grace window's whole reward (안 G) — only enemy-team ones freeze alongside the
	 *  frozen swarm.
	 *
	 *  ⚠️ This is now a SAFETY NET, not the primary mechanism: since 2026-08-25 the enemy bullets in flight are
	 *  CLEARED outright at transition start (ReleaseEnemyProjectiles, called from RequestTransition), so there is
	 *  normally nothing left for this to pause. It stays because "nothing left" rests on enemy FIRING also being
	 *  gated by IsStageTransitionActive — if any path ever spawns an enemy projectile mid-transition, freezing it
	 *  is far better than letting it fly into a player who cannot move. Costs one edge-triggered pass over an
	 *  empty-of-enemies list. */
	bool bPausedEnemyOnly = false;
};
