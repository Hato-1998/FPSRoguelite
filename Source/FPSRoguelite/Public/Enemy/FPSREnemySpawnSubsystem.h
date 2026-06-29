// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "Templates/SubclassOf.h"
#include "GameplayTagContainer.h"
#include "FPSREnemySpawnSubsystem.generated.h"

class AFPSREnemyBase;
class AFPSREnemySpawnPoint;
class AFPSRSpawnRoom;

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
	 *  authoritative designer-placed point whose Z must be preserved exactly (Game.MD §1 fixed-map placement).
	 *  SpawnPoint (optional): if it has authored exit-path waypoints, the enemy follows them out of its spawn
	 *  structure before flow-field chase takes over (C1). */
	AFPSREnemyBase* AcquireEnemy(const FVector& Location, bool bSnapToGround = true, const AFPSREnemySpawnPoint* SpawnPoint = nullptr);

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

	/** Set the per-tick spawn cap = the swarm fill rate (schedule-driven; clamped to >=1). Lower = the swarm
	 *  builds up gradually instead of snapping to the target count. */
	void SetMaxSpawnPerTick(int32 InMax) { MaxSpawnPerTick = FMath::Max(1, InMax); }

	/** Set the director tick interval (seconds) = the swarm spawn PACE. With MaxSpawnPerTick this sets the per-second
	 *  fill rate (MaxSpawnPerTick / interval). Re-arms the running director timer so the new pace takes effect at
	 *  once. Clamped to a small minimum. Schedule-driven (DA_RunSchedule.SpawnIntervalSeconds), pushed at StartRun. */
	void SetSpawnInterval(float InSeconds);

	/** Server: mark a spawn zone (room) active so its tagged spawn points become eligible. ACCUMULATES — already
	 *  active zones stay active, so opening a new room ADDS spawn locations without ever removing earlier ones (the
	 *  swarm keeps spawning in cleared rooms). Called by AFPSRSpawnRoom on player entry. (Room spawn system.) */
	void ActivateSpawnZone(FGameplayTag Zone);

	/** Server: deactivate a spawn zone (remove its tag) so its tagged points stop being eligible — the symmetric
	 *  inverse of ActivateSpawnZone. Called by a Deactivate-mode AFPSRSpawnRoom on player entry (Enemy.md §2-6).
	 *  Already-spawned enemies are unaffected (zones gate spawn LOCATIONS, not existing actors). */
	void DeactivateSpawnZone(FGameplayTag Zone);

	/** Server: clear all active zones, then re-activate every cached Activate room flagged bActiveAtStart (the start
	 *  room). Called at world begin and at StartRun so a re-run starts from only the start room (no leaked accumulation). */
	void ResetSpawnZones();

private:
	/** Director tick: spawn/release enemies to maintain TargetAliveCount. */
	void TickDirector();

	/** Cache designer-placed AFPSREnemySpawnPoint actors once at world begin (server). */
	void CacheSpawnPoints();

	/** Cache designer-placed AFPSRSpawnRoom actors once at world begin (server). Used by ResetSpawnZones to know
	 *  which rooms are active at start. */
	void CacheSpawnRooms();

	/** Pick a designer spawn point UNIFORMLY at random among those that are enabled, satisfy their MinPlayerDistance,
	 *  and whose spawn zone is active (an untagged point is always eligible). No out-of-view (FOV) filter — enemies may
	 *  spawn in view (user 2026-06-29). Returns false when none qualify (or none placed) — the director then skips
	 *  spawning this tick (no fallback). OutPoint receives the chosen point (for its authored exit path, C1). */
	bool TrySelectSpawnPoint(FVector& OutLocation, const AFPSREnemySpawnPoint*& OutPoint) const;

	/** Batched server movement pass with distance LOD (replaces per-actor enemy Tick). */
	void TickEnemyMovement(float DeltaTime);

	/** Sum a repulsion vector from nearby enemies (anti-clumping), using the per-pass spatial hash. */
	FVector ComputeSeparation(int32 AgentIndex, const TArray<FVector>& Locations, const TMap<FIntPoint, TArray<int32>>& SpatialHash) const;

	/** Check if this subsystem has server authority. */
	bool HasServerAuthority() const;

	/** Try to compute a spawn location at a qualifying designer spawn point (out-of-view, weighted). Returns false
	 *  when none qualify this tick — the swarm spawns ONLY at designer points (no player-proximity/ring fallback,
	 *  removed 2026-06-24), so the director skips spawning until a point qualifies. Sets bOutSnapToGround=false (the
	 *  designer point's Z is authoritative — no ground re-snap). */
	bool ComputeSpawnLocation(FVector& OutLocation, bool& bOutSnapToGround, const AFPSREnemySpawnPoint*& OutPoint) const;

	/** Trace down to the static floor under Location and return a ground-snapped spawn point (feet on
	 *  the floor). Decouples spawn Z from the player's jump height. Falls back to Location if no floor hit. */
	FVector SnapToGround(const FVector& Location) const;

	/** Frame counter used to spread throttled (low-LOD) enemy updates across frames. */
	int32 MovementFrameCounter = 0;

	// Per-pass movement scratch, hoisted to members (Reset()+rebuilt at the top of each TickEnemyMovement pass) so
	// the batch over up to 500 enemies doesn't heap-realloc the agent/location arrays and spatial hash every frame
	// (W1 P2-4). Raw pointers are safe: the enemies are owned by ActiveEnemies/DormantPool (pooled, never GC'd
	// mid-run), and these are cleared+rebuilt before any use each pass and never read between passes.
	TArray<AFPSREnemyBase*> MovementAgentsScratch;
	TArray<FVector> MovementLocationsScratch;
	TMap<FIntPoint, TArray<int32>> MovementSpatialHashScratch;

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

	/** Max enemies spawned per director tick = the swarm FILL RATE (x ~1/SpawnInterval per second). Lower = enemies
	 *  trickle in and the crowd recovers gradually after a clear; higher = the swarm snaps to the target count fast.
	 *  Schedule-driven (DA_RunSchedule.MaxSpawnPerTick), pushed by the director at StartRun. */
	int32 MaxSpawnPerTick = 3;

	/** Director tick interval (seconds) = the swarm spawn PACE (per-second fill = MaxSpawnPerTick / SpawnInterval).
	 *  Schedule-driven (DA_RunSchedule.SpawnIntervalSeconds), pushed by the director at StartRun via SetSpawnInterval. */
	float SpawnInterval = 0.1f;

	// --- Designer spawn points (Game.MD §2-8 / P4 backlog) ---

	/** Designer-placed spawn anchors, cached once at world begin (server). May contain entries to null-check. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AFPSREnemySpawnPoint>> SpawnPoints;

	/** Designer-placed spawn rooms, cached once at world begin (server). Used to re-activate start zones on reset. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AFPSRSpawnRoom>> SpawnRooms;

	/** Active spawn zones (rooms). A point with no zone is always eligible; a tagged point is eligible only while
	 *  its zone is in this container. Accumulates as rooms open (ActivateSpawnZone); cleared by ResetSpawnZones. */
	FGameplayTagContainer ActiveSpawnZones;
};
