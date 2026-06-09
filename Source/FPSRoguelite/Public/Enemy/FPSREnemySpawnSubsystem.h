// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "Templates/SubclassOf.h"
#include "GameplayTagContainer.h"
#include "FPSREnemySpawnSubsystem.generated.h"

class AFPSREnemyBase;
class AFPSREnemySpawnPoint;

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

	/** Acquire an enemy from the pool or spawn a new one at the given location.
	 *  bSnapToGround=true traces down to the static floor (procedural/ring spawns). Pass false for an
	 *  authoritative designer-placed point whose Z must be preserved exactly (Game.MD §1 fixed-map placement). */
	AFPSREnemyBase* AcquireEnemy(const FVector& Location, bool bSnapToGround = true);

	/** Release an enemy back to the dormant pool. */
	void ReleaseEnemy(AFPSREnemyBase* Enemy);

	/** Release every active enemy back to the dormant pool (server). Used by mission/debug flows. */
	void ReleaseAllEnemies();

	/** Get the current number of alive enemies. */
	int32 GetAliveCount() const { return ActiveEnemies.Num(); }

	/** Set the actor class to spawn for swarm enemies (designer-configured BP child of AFPSREnemyBase).
	 *  Falls back to AFPSREnemyBase if unset. Set this from trusted server config (e.g. GameMode). */
	void SetEnemyClass(TSubclassOf<AFPSREnemyBase> InClass) { EnemyClass = InClass; }

	/** Set the target alive count (director will spawn/release to maintain this). */
	void SetTargetAliveCount(int32 InTarget);

	/** Set the active spawn zone (empty = all points eligible). Only points whose ZoneTag is, or is a child of,
	 *  this tag are eligible while set — lets the director switch spawn regions by time/phase (Game.MD §2-8). */
	void SetActiveSpawnZone(FGameplayTag Zone) { ActiveSpawnZone = Zone; }

private:
	/** Director tick: spawn/release enemies to maintain TargetAliveCount. */
	void TickDirector();

	/** Cache designer-placed AFPSREnemySpawnPoint actors once at world begin (server). */
	void CacheSpawnPoints();

	/** Pick a designer spawn point that no player can currently see (FOV gate) and that satisfies its
	 *  MinPlayerDistance + active-zone filter, by Weight × distance falloff. Returns false when none qualify
	 *  (or none are placed), so the caller falls back to the ring pattern. */
	bool TrySelectSpawnPoint(FVector& OutLocation) const;

	/** Batched server movement pass with distance LOD (replaces per-actor enemy Tick). */
	void TickEnemyMovement(float DeltaTime);

	/** Sum a repulsion vector from nearby enemies (anti-clumping), using the per-pass spatial hash. */
	FVector ComputeSeparation(int32 AgentIndex, const TArray<FVector>& Locations, const TMap<FIntPoint, TArray<int32>>& SpatialHash) const;

	/** Check if this subsystem has server authority. */
	bool HasServerAuthority() const;

	/** Compute a spawn location: a designer spawn point if one qualifies, else a random ring point around the
	 *  nearest player. Sets bOutSnapToGround=false for an authoritative designer point (preserve its Z), true for
	 *  the ring fallback (needs floor-snapping). */
	FVector ComputeSpawnLocation(bool& bOutSnapToGround) const;

	/** Trace down to the static floor under Location and return a ground-snapped spawn point (feet on
	 *  the floor). Decouples spawn Z from the player's jump height. Falls back to Location if no floor hit. */
	FVector SnapToGround(const FVector& Location) const;

	/** Frame counter used to spread throttled (low-LOD) enemy updates across frames. */
	int32 MovementFrameCounter = 0;

	// Significance distance tiers (squared cm) and per-tier update stride / net update frequency (Game.MD §5/§5-1).
	static constexpr float TierS0RadiusSq = 1500.0f * 1500.0f; // S0: full update
	static constexpr float TierS1RadiusSq = 3500.0f * 3500.0f; // S1
	static constexpr float TierS2RadiusSq = 6000.0f * 6000.0f; // S2 (beyond = S3)

	// Separation (anti-clumping) tuning. Cell size for the spatial hash == SeparationRadius so a 3x3
	// neighbor scan covers the full radius.
	static constexpr float SeparationRadius = 120.0f;   // cm
	static constexpr float SeparationStrength = 1.5f;   // weight of separation vs the unit flow direction

	/** Max enemies allowed to deal contact damage to a single player per pass (attack token, Game.MD §2-6/§5). */
	static constexpr int32 AttackTokenLimit = 10;

	/** Max vertical (Z) gap for a contact attack to land — stops an airborne/rooftop or falling enemy from
	 *  damaging a player through a floor when only horizontal (XY) distance is in range (Codex review 2026-06-09). */
	static constexpr float AttackVerticalRange = 150.0f; // cm (covers capsule heights + a small step)

	/** Z below which a fallen enemy (pit / no static floor under it) is recycled to the pool so the endless-fall
	 *  path can't pin a director alive-count/hard-cap slot forever (Codex review 2026-06-09). Far below any
	 *  playable floor on the fixed map (§1). */
	static constexpr float WorldKillZ = -10000.0f; // cm

	// Spawn ground-snap trace (decouples spawn Z from the player's jump height). Half-height matches
	// AFPSREnemyBase's capsule (InitCapsuleSize(40, 90)).
	static constexpr float SpawnGroundTraceUp = 500.0f;     // cm above the candidate to start the down-trace
	static constexpr float SpawnGroundTraceDown = 10000.0f; // cm below the candidate to end the down-trace
	static constexpr float SpawnGroundHalfHeight = 90.0f;   // cm; capsule half-height -> feet on floor

	/** Designer-configured enemy class to spawn (BP child of AFPSREnemyBase). Null = spawn AFPSREnemyBase. */
	UPROPERTY()
	TSubclassOf<AFPSREnemyBase> EnemyClass;

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

	// --- Designer spawn points (Game.MD §2-8 / P4 backlog) ---

	/** Half-angle (deg) of each player's "visible" cone; a point inside any player's cone is excluded so
	 *  enemies appear out of view (behind/beside). Horizontal (XY) test. */
	static constexpr float SpawnPointVisibilityHalfAngleDeg = 70.0f;

	/** Distance falloff for point weighting: weight *= 1/(1 + nearestPlayerDist / this). Favors points near the
	 *  fight without hard-excluding far ones. */
	static constexpr float SpawnPointFalloffDistance = 2500.0f; // cm

	/** Designer-placed spawn anchors, cached once at world begin (server). May contain entries to null-check. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AFPSREnemySpawnPoint>> SpawnPoints;

	/** Active spawn-zone filter (invalid/empty = all points eligible). Set by the director (TimeGate, §2-8). */
	FGameplayTag ActiveSpawnZone;
};
