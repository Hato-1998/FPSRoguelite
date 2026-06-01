// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "FPSREnemySpawnSubsystem.generated.h"

class AFPSREnemyBase;

/** Lightweight server-authoritative object pool + spawn director for swarm enemies (P2-A).
 *  Pooling reuses dormant actors; director keeps ~TargetAliveCount alive around players.
 *  P2-B1: Batched movement + LOD driven by FTickableGameObject pass (replaces per-actor enemy Tick). */
UCLASS()
class FPSROGUELITE_API UFPSREnemySpawnSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	// FTickableGameObject — drives the batched enemy movement pass (server-authoritative).
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickable() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;

	/** Acquire an enemy from the pool or spawn a new one at the given location. */
	AFPSREnemyBase* AcquireEnemy(const FVector& Location);

	/** Release an enemy back to the dormant pool. */
	void ReleaseEnemy(AFPSREnemyBase* Enemy);

	/** Get the current number of alive enemies. */
	int32 GetAliveCount() const { return ActiveEnemies.Num(); }

	/** Set the target alive count (director will spawn/release to maintain this). */
	void SetTargetAliveCount(int32 InTarget);

private:
	/** Director tick: spawn/release enemies to maintain TargetAliveCount. */
	void TickDirector();

	/** Batched server movement pass with distance LOD (replaces per-actor enemy Tick). */
	void TickEnemyMovement(float DeltaTime);

	/** Check if this subsystem has server authority. */
	bool HasServerAuthority() const;

	/** Compute a random spawn location around the nearest player. */
	FVector ComputeSpawnLocation() const;

	/** Frame counter used to spread throttled (low-LOD) enemy updates across frames. */
	int32 MovementFrameCounter = 0;

	// Significance distance tiers (squared cm) and per-tier update stride / net update frequency (Game.MD §5/§5-1).
	static constexpr float TierS0RadiusSq = 1500.0f * 1500.0f; // S0: full update
	static constexpr float TierS1RadiusSq = 3500.0f * 3500.0f; // S1
	static constexpr float TierS2RadiusSq = 6000.0f * 6000.0f; // S2 (beyond = S3)

	/** Pool of dormant (hidden, disabled) enemies ready for reuse. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AFPSREnemyBase>> DormantPool;

	/** Set of currently active (visible, enabled) enemies. */
	UPROPERTY(Transient)
	TSet<TObjectPtr<AFPSREnemyBase>> ActiveEnemies;

	/** Timer handle for the director tick. */
	FTimerHandle DirectorTimerHandle;

	/** Target number of alive enemies (director spawns/releases to maintain this). */
	int32 TargetAliveCount = 0;

	/** Total enemies spawned (hard cap at MaxActiveEnemies). */
	int32 TotalSpawned = 0;

	/** Hard cap on active enemies (Game.MD §5). */
	static constexpr int32 MaxActiveEnemies = 500;

	/** Max enemies spawned per tick. */
	static constexpr int32 MaxSpawnPerTick = 10;

	/** Director tick interval (seconds). */
	float SpawnInterval = 0.1f;

	/** Inner radius for ring spawn pattern. */
	float SpawnRadiusInner = 1200.0f;

	/** Outer radius for ring spawn pattern. */
	float SpawnRadiusOuter = 1500.0f;
};
