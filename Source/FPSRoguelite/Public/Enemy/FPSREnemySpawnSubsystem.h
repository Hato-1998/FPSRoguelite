// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "Templates/SubclassOf.h"
#include "GameplayTagContainer.h"
#include "UObject/ObjectKey.h"
#include "Containers/ArrayView.h" // TConstArrayView (PassesCommonSpawnGates)
#include "Enemy/FPSREnemyDormantPool.h" // full type: DormantPool below is a by-value UPROPERTY member (ADR 0013 불변식 7)
#include "Enemy/FPSREnemyTuning.h" // FPSREnemyTuning::SignificanceS0RadiusSq (GetTierS0RadiusSq's SSOT — LOD1)
#include "FPSREnemySpawnSubsystem.generated.h"

class AFPSREnemyBase;
class AFPSREnemySpawnPoint;
class AFPSRSpawnRoom;
class AFPSRPlayerController;
class APlayerController;
class AFPSRCharacter;
class UFPSREnemyRosterDataAsset;
class UFPSRRunScheduleDataAsset; // ActiveSchedule (C3) — elite-cap curve source, see the member's own comment
enum class EFPSRFieldQuery : uint8; // U P-E: front path-distance query status (IsRearStatus)

/** Lightweight server-authoritative object pool + spawn director for swarm enemies (P2-A).
 *  Pooling reuses dormant actors; director keeps ~TargetAliveCount alive around players.
 *  P2-B1: Batched movement + LOD driven by FTickableGameObject pass (replaces per-actor enemy Tick). */
/** One dwelling corpse: an enemy past EnterDyingState (gameplay over, still rendering + replicating) that has not
 *  yet returned to the dormant pool. Enemy is a real UPROPERTY — during the dwell this list is the ONLY container
 *  holding it (BeginDying already pulled it out of ActiveEnemies, and it does not reach DormantPool until
 *  FinishDyingEnemy), so unlike this subsystem's per-frame scratch arrays it must not rely on some other reference
 *  outliving it. Matches how DormantPool/ActiveEnemies declare their own pooled-actor references. */
USTRUCT()
struct FFPSRDyingEnemy
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<AFPSREnemyBase> Enemy = nullptr;

	/** World seconds at which this corpse's dwell ends and FinishDyingEnemy returns it to the pool. */
	float DeadlineWorldSeconds = 0.0f;
};

UCLASS()
class FPSROGUELITE_API UFPSREnemySpawnSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	/** Hard cap on active enemies (Game.MD §5) — the pool ceiling / endless-fall backstop. Public read-only constant
	 *  so debug commands (FPSR.SpawnEnemies) can clamp their input against the real cap instead of duplicating it. */
	static constexpr int32 MaxActiveEnemies = 500;

	/** Hard ceiling on concurrent elite-tier enemies (ADR 0013 불변식 6 "엘리트 동시 마릿수는 하드캡을 갖는다" —
	 *  곡선과 무관하게 이 값을 넘지 않는다), always intersected with UFPSRRunScheduleDataAsset's per-stage
	 *  MaxEliteAlive curve — AcquireEnemy's elite gate uses min(curve value, this). Public for the SAME reason
	 *  MaxActiveEnemies above is: UFPSRRunScheduleValidator lives in a DIFFERENT module (FPSRogueliteEditor) and
	 *  must warn on an authored MaxEliteAlive that exceeds this WITHOUT duplicating the number (a copy would drift
	 *  the moment either side is tuned). Conservative (예: 8) — an elite ASC's replication cost (attribute set +
	 *  ability specs + GE handles) dwarfs a plain tier's single UActorComponent (invariant 6's own rationale in
	 *  the ADR); raise only after profiling actual elite-count replication cost. */
	static constexpr int32 EliteHardCap = 8;

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
	AFPSREnemyBase* AcquireEnemy(const FVector& Location, bool bSnapToGround = true, const AFPSREnemySpawnPoint* SpawnPoint = nullptr, bool bFrontSpawned = false);

	/** Release an enemy back to the dormant pool. */
	void ReleaseEnemy(AFPSREnemyBase* Enemy);

	/** Server (death-dwell teardown split): the SOLE entry point for the DEATH path — called once, from
	 *  AFPSREnemyBase::HandleDeath. Immediately pulls Enemy OUT of ActiveEnemies (so the very next movement/attack
	 *  pass already skips it — it can't move, can't attack, and its collision-off corpse can never front-line-shield
	 *  the swarm behind it — LineTraceMulti stops at the first blocking hit) and calls Enemy->EnterDyingState()
	 *  (gameplay ends now), then schedules the LATER presentation teardown (Deactivate + return to DormantPool) at
	 *  Now + Enemy->GetDeathDwellSeconds() via the DyingEnemies dwell list, swept every TickEnemyMovement pass
	 *  (SweepDyingEnemies). This is NOT a replacement for ReleaseEnemy — every OTHER teardown (pool release /
	 *  rear-drain / kill-Z recycle / stage-carry overflow) is not a "death" in the health-component sense and stays
	 *  an immediate ReleaseEnemy. Null-safe. */
	void BeginDying(AFPSREnemyBase* Enemy);

	/** Release every active enemy back to the dormant pool (server). Used by mission/debug flows. Also flushes any
	 *  corpse still dwelling (BeginDying already pulled it out of ActiveEnemies, so this doesn't happen for free —
	 *  see the .cpp) so a "clear the board now" call can't leave a corpse to leak into the next run/stage. */
	void ReleaseAllEnemies();

	/** Server (Phase A stage-transition redesign): carry the leftover swarm over to the new arena instead of
	 *  releasing it, called by UFPSRStageDirectorSubsystem::PerformSwap in place of the old ReleaseAllEnemies. Each
	 *  surviving enemy is offset by the DELTA of its nearest OLD player location -> the SAME player's NEW location
	 *  (OldPlayerLocs[i]/NewPlayerLocs[i] must be index-paired, one player per slot — see the call site), then
	 *  snapped onto the new arena's nearest open flow-field cell (UFPSRFlowFieldSubsystem::FindNearestOpenLocation —
	 *  array-only, no world query, D7). CarryMaxFraction (0..1, UFPSRRunScheduleDataAsset::StageCarryOverMaxFraction)
	 *  caps how many carry over; the excess is released, farthest-from-a-new-player-location first. Falls back to
	 *  ReleaseAllEnemies (+ a Warning log) if OldPlayerLocs/NewPlayerLocs is empty — no delta to carry by. No-op
	 *  off authority. The whole swarm is frozen for the transition (TickEnemyMovement's IsStageTransitionActive
	 *  gate), so there is no movement-pass race with this. */
	void CarryEnemiesToNewStage(const TArray<FVector>& OldPlayerLocs, const TArray<FVector>& NewPlayerLocs, float CarryMaxFraction);

	/** Server: 전환이 **시작될 때** 충전 중이던 원거리 적의 사이클을 전부 취소한다 (사용자 결정 2026-08-26).
	 *  전환 중에는 이 서브시스템이 공격 패스 전체를 early-return 하므로, 충전 중이던 적은 스스로 정리할 기회를
	 *  얻지 못하고 **타겟의 방향 경고 UI 가 전환 내내 화면에 남는다**. 적은 이월되지 파괴되지 않으므로 기존
	 *  teardown 경로도 안 걸린다. 상세 = AFPSREnemyBase::ServerCancelRangedForStageTransition
	 *  (ADR 0013 C1 로 원거리 FSM 이 베이스에 승격되며 AFPSRRangedEnemyBase 에서 이관됨).
	 *  RequestTransition 에서 미션 취소·탄막 제거와 같은 자리(모든 거부 가드 뒤)에서 부른다.
	 *
	 *  ADR 0013 후속 행 3 실행 1: 같은 이유로 같은 루프에서 엘리트의 ASC 도 정리한다(진행 중 어빌리티만 취소,
	 *  Infinite GE 는 보존 — 상세 = AFPSREnemyEliteBase::ServerResetEliteForStageCarry). 엘리트도 이월되지
	 *  파괴되지 않으므로 Activate/Deactivate 어느 쪽도 밟지 않는 건 원거리 FSM 과 동일한 사정이다. */
	void CancelRangedChargesForTransition();

	/** Server (U P-F): reset the director's transient run state for a same-world re-run (StartRun) — drain the trickle
	 *  token bucket, clear the director clock, empty the per-map grace + transition-tracker maps, and release every active
	 *  enemy back to the pool. Stage 2 also resets each PlayerState's topology ack. A first run starts empty, so this is a
	 *  no-op there (no regression); it's the same-world-reset safety net paired with FlowField::ResetDoorTopologyToBaseline. */
	void ResetForNewRun();

	/** Get the current number of alive enemies. */
	int32 GetAliveCount() const { return ActiveEnemies.Num(); }

#if !UE_BUILD_SHIPPING
	/** Debug (FPSR.EliteDump): log every ACTIVE elite's ASC state plus the pool/cap accounting around it.
	 *
	 *  Exists because ADR 0013's own failure scenario ("a pooled elite comes back carrying the previous life's ASC
	 *  state") has no other observer: elites have no attribute set to inspect, nothing activates their abilities yet,
	 *  and AbilitySystem.DebugAbilityTags draws tags that nothing currently grants. What IS observable without any
	 *  ability content is ACCUMULATION — if Activate() ever stops clearing the previous life's specs, the granted
	 *  count grows by GrantedAbilities.Num() on every pool reuse. Kill and respawn the same elite a few times and
	 *  watch that number: it must stay flat.
	 *
	 *  Also cross-checks ActiveEliteCount (incremented in one place, decremented in two) against the elites actually
	 *  in ActiveEnemies. A leaked decrement otherwise only ever surfaces as the vague "elites gradually stop
	 *  spawning", long after the cause. Read-only: this never mutates pool or accounting state. */
	void DumpEliteState() const;
#endif

	/** S4 readability metrics: read-only accessor for TierS0RadiusSq (15m S0 significance radius, squared cm) so
	 *  UFPSREnemyMetricsSubsystem's "Near15m" gate reuses the SAME 15m definition as the movement LOD tier pass
	 *  (Game.MD §5/§5-1) instead of duplicating the magic constant. Mirrors UFPSRFlowFieldComputer::GetMaxTotalCells's
	 *  accessor pattern. Does not expose write access — the tier pass (TickEnemyMovement) stays the sole owner. */
	static constexpr float GetTierS0RadiusSq() { return FPSREnemyTuning::SignificanceS0RadiusSq; }

	/** Set the actor class to spawn for swarm enemies (designer-configured BP child of AFPSREnemyBase).
	 *  Falls back to AFPSREnemyBase if unset. Set this from trusted server config (e.g. GameMode). Used as the
	 *  fallback when no EnemyRoster is set (or the roster yields nothing). */
	void SetEnemyClass(TSubclassOf<AFPSREnemyBase> InClass) { EnemyClass = InClass; }

	/** Set the data-driven enemy roster (archetype mix). Null/empty = spawn the single EnemyClass (no regression).
	 *  Pushed by the run director at StartRun from DA_RunSchedule.EnemyRoster (Game.MD §2-6). */
	void SetEnemyRoster(UFPSREnemyRosterDataAsset* InRoster) { EnemyRoster = InRoster; }

	/** Set the active run schedule (C3 — elite-cap curve source only; nothing else here reads it). Pushed by
	 *  AFPSRGameMode::BeginPlay from its own RunSchedule, alongside SetEnemyClass — the SAME asset it also hands to
	 *  UFPSRRunDirectorSubsystem::SetActiveSchedule (an independent copy: that subsystem's ActiveSchedule is
	 *  private with no getter, so this subsystem cannot reach it and needs its own reference). Null = the elite
	 *  gate falls back to a 0 curve cap (no elites), matching MaxEliteAlive's own "unauthored = no elites"
	 *  no-regression default. */
	void SetActiveSchedule(UFPSRRunScheduleDataAsset* InSchedule) { ActiveSchedule = InSchedule; }

	/** Server: read-only — is there a ranged-attack slot free against TargetPC? Lets a ranged enemy skip the (more
	 *  expensive) line-of-sight trace when it's already capped out, so capped idle ranged enemies don't trace every
	 *  pass at swarm scale (Game.MD §5). */
	bool IsRangedTokenAvailable(AFPSRPlayerController* TargetPC) const;

	/** Server: try to reserve a ranged-attack (charge) slot against TargetPC. Caps CONCURRENT ranged chargers per
	 *  player (attack token, Game.MD §2-6) — which also bounds in-flight enemy projectiles. Returns true + reserves
	 *  when under the cap. Released on fire / abort / enemy teardown via ReleaseRangedToken. */
	bool TryAcquireRangedToken(AFPSRPlayerController* TargetPC);

	/** Server: release a previously-reserved ranged-attack slot. Safe to call with a stale/expired controller. */
	void ReleaseRangedToken(const TWeakObjectPtr<AFPSRPlayerController>& TargetPC);

	/** Set the target alive count (director will spawn/release to maintain this). */
	void SetTargetAliveCount(int32 InTarget);

	/** Re-scan designer spawn points + rooms (multimap Tier 0). Spawn points are cached ONCE at world begin, so a
	 *  sublevel that streams in mid-run has none cached and would never spawn — the MapStreamSubsystem calls this on
	 *  a map's collision-ready so the new map's points become selectable (Codex streaming BLOCKER fix). */
	void RefreshSpawnPointCache();

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

	// --- P-E pure helpers (unit-testable; no world) — the exact front-budget arithmetic + rear classification + drain-dt
	//     clamp the director uses, exposed static so FPSRoguelite.Allocator can regression them headless (Codex/Opus P-E gate). ---
	/** Total front reserve for N front-active slots: min(FrontBudgetCeiling, PerFrontSlotBudget*N), 0 for N<=0. */
	static int32 ComputeFrontReserved(int32 FrontActiveSlots);
	/** Honest physical steady target: max(0, min(Target, GlobalAliveCap - SeedReserve - FrontReserved)) — the front reserve
	 *  is subtracted so the physical apportionment's own target never lies / "steals" the front's share (gate #1). */
	static int32 ComputePhysicalSteady(int32 TargetAliveCount, int32 FrontReserved);
	/** Rear-drain classification: true ONLY for a genuinely far OK reading (dist > ChaseExitCells) or an Unreachable one; a
	 *  SourceLess / OffGrid / NoGrid reading is HOLD (false) so a source-less window never drains the near-door front (gate #6). */
	static bool IsRearStatus(EFPSRFieldQuery Status, int32 Dist);
	/** Drain-clock elapsed clamp: Clamp(RawElapsed, 0, SpawnIntervalSeconds * DrainDtClampTicks) — bounds freeze-burst accrual (gate #4). */
	static float ClampDrainDt(float RawElapsed, float SpawnIntervalSeconds);

	/** U (P-H) net-cull radius (cm) for the unified multimap field, applied UNIFORMLY to every enemy at acquire. A symmetric
	 *  distance cull can't do per-slot "seam-only" relevancy without RepGraph (deferred); a uniform engagement/weapon-range
	 *  bubble — capped to the slot footprint — never undersizes a cross-slot chaser and bounds per-client relevancy. Pure /
	 *  unit-testable (FPSRoguelite.Enemy.NetCull): R = max(WeaponRangeCm, min(WeaponRangeCm + SeamMarginCm, MaxSlotDiagonalCm +
	 *  SeamMarginCm)). WeaponRangeCm is BOTH the bubble base and the shoot-ability floor (>= max authored weapon range, so an
	 *  in-range enemy is always replicated); the footprint cap keeps R off the whole 3x3 grid and scales it with content. */
	static float ComputeUnifiedNetCullRadius(float MaxSlotDiagonalCm, float WeaponRangeCm, float SeamMarginCm);

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
	bool TrySelectSpawnPoint(const FGameplayTag& TargetMapId, FVector& OutLocation, const AFPSREnemySpawnPoint*& OutPoint) const;

	/** Recompute per-map committed occupancy (server): for each player pawn, resolve the map its location is in (flow-field
	 *  registry AABB) and set its PlayerState CurrentMapId (idempotent, low-churn). Fills OutOccupiedMaps + OutPlayerCounts
	 *  (parallel arrays, occupied maps only). Single-map: everyone resolves to the Default (unset) map. Now = world seconds. */
	void ComputeOccupancy(TArray<FGameplayTag>& OutOccupiedMaps, TArray<int32>& OutPlayerCounts, float Now);

	/** Server (U P-E): true if Point passes the shared spawn eligibility gates (enabled + active zone + MinPlayerDistance vs
	 *  the given player VIEW locations). Extracted from TrySelectSpawnPoint so the front selector reuses the identical gate. */
	bool PassesCommonSpawnGates(const AFPSREnemySpawnPoint* Point, TConstArrayView<FVector> PlayerViewLocations) const;

	/** Server (U P-E): find the front-active adjacent slots + their near-door eligible spawn points. A slot is front-active
	 *  when it is NOT player-occupied yet holds >=1 spawn point whose UNIFIED path-distance to the nearest player is OK and
	 *  <= ChaseEnterCells (open-door-connected + near — GetFrontDistanceCells OK already implies open-grid connectivity, and
	 *  bounding to the chase-ENTER threshold means the spawned enemy immediately front-chases rather than sitting idle).
	 *  Populates OutFrontPointsByMap (slot MapId -> its eligible points). Unified-field only; empty when none / off authority. */
	void ComputeFrontState(const TArray<FGameplayTag>& OccupiedMaps,
		TMap<FGameplayTag, TArray<const AFPSREnemySpawnPoint*>>& OutFrontPointsByMap) const;

	/** Server (U P-E): one pass over alive enemies — bucket alive-by-map AND compute the front pressure counts (P-G: the
	 *  single alive-count path; single-map with no front degrades to a plain alive-by-map bucketing), updating each
	 *  front-spawned enemy's ONE-SHOT crossing credit (stamped on first entry into an occupied
	 *  slot; released when it expires). OutFrontAliveBySlot counts enemies physically in each front slot; OutFrontCountedGlobal
	 *  additionally counts still-credited crossers (the conveyor rate-limit). Mutates enemy credit state -> non-const. */
	void ComputeAliveAndFrontState(const TArray<FGameplayTag>& OccupiedMaps,
		const TMap<FGameplayTag, TArray<const AFPSREnemySpawnPoint*>>& FrontPointsByMap, float Now,
		TMap<FGameplayTag, int32>& OutAliveByMap, TMap<FGameplayTag, int32>& OutFrontAliveBySlot, int32& OutFrontCountedGlobal);

	/** Server (U P-E): release up to MaxToRelease REAR enemies (far, not front-connected, past their map's drain grace, not
	 *  chasing) back to the pool, farthest-first. P-G: the only drain path (multimap only). A SourceLess / OffGrid reading is
	 *  treated as HOLD (never drained — the source-less window mustn't drain the front). Returns the number released. */
	int32 DrainRearEnemies(const TArray<FGameplayTag>& OccupiedMaps,
		const TMap<FGameplayTag, TArray<const AFPSREnemySpawnPoint*>>& FrontPointsByMap, int32 MaxToRelease, float Now);

	/** Server (death-dwell): finish every dwelling corpse whose DeadlineWorldSeconds has passed (Deactivate + return
	 *  to DormantPool) and prune any entry whose actor went invalid. Called once per TickEnemyMovement pass, ABOVE
	 *  that function's ActiveEnemies==0 early-return (see the .cpp) so a corpse dwells out even as the last enemy
	 *  standing, gated on the SAME freeze/transition check the movement+attack pass uses (a new FTimerHandle would
	 *  need its own PauseTimer wiring against the global freeze gate; piggybacking on this already-gated per-frame
	 *  pass gets that for free). */
	void SweepDyingEnemies(float Now);

	/** Server: the shared "a corpse's dwell is over" recovery point (Deactivate + DormantPool.Add). Used by
	 *  SweepDyingEnemies (deadline reached), BeginDying (MaxDyingEnemies overflow eviction), and ReleaseAllEnemies
	 *  (bulk-flush the whole dwell list). No-op on an already-invalid Enemy. */
	void FinishDyingEnemy(AFPSREnemyBase* Enemy);

	/** Batched server movement pass with distance LOD (replaces per-actor enemy Tick). */
	void TickEnemyMovement(float DeltaTime);

	/** Sum a repulsion vector from nearby enemies (anti-clumping), using the per-pass spatial hash. SeparationRadius
	 *  must be the SAME value the pass built the hash with (it is the hash cell size — see TickEnemyMovement). */
	FVector ComputeSeparation(int32 AgentIndex, const TArray<FVector>& Locations, const TMap<FIntPoint, TArray<int32>>& SpatialHash, float SeparationRadius) const;

	/** Check if this subsystem has server authority. */
	bool HasServerAuthority() const;

	/** Try to compute a spawn location at a qualifying designer spawn point (out-of-view, weighted). Returns false
	 *  when none qualify this tick — the swarm spawns ONLY at designer points (no player-proximity/ring fallback,
	 *  removed 2026-06-24), so the director skips spawning until a point qualifies. Sets bOutSnapToGround=false (the
	 *  designer point's Z is authoritative — no ground re-snap). */
	bool ComputeSpawnLocation(const FGameplayTag& TargetMapId, FVector& OutLocation, bool& bOutSnapToGround, const AFPSREnemySpawnPoint*& OutPoint) const;

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

	/** ADR 0009 prep ("부유체 스웜" draft §3 invariant, piped 2026-08-15 ahead of the redesign): each tracked
	 *  player's Z the last time UCharacterMovementComponent::IsMovingOnGround() was true. PERSISTS ACROSS PASSES
	 *  (unlike the per-pass scratch above) — updated once per player in the collection loop (grounded -> refresh;
	 *  airborne -> keep the stale value, a jump/fall must not move the cached target), then read into
	 *  FFPSRServerMoveContext::TargetGroundedZ so a Seek3D altitude band (ADR 0008/0009) tracks where the target
	 *  last STOOD, never its instantaneous jump/fall Z (would otherwise jitter the whole swarm's altitude in
	 *  lockstep with every player hop). Stale entries (dead TWeakObjectPtr) are pruned in the same collection loop
	 *  so a departed/destroyed player's cache doesn't leak forever. */
	TMap<TWeakObjectPtr<AFPSRCharacter>, float> LastGroundedZByPlayer;

	// --- Net-cull relevancy (multimap U P-H, server-only). In the unified field the swarm's net-cull radius is sized to an
	//     engagement/weapon-range bubble capped to the slot footprint (ComputeUnifiedNetCullRadius) and applied UNIFORMLY at
	//     acquire — a symmetric distance cull can't do per-slot seam-only relevancy without RepGraph (deferred). PIE-tunable. ---
	/** Net-cull bubble base AND shoot-ability floor (cm): an enemy the server hitscan can reach is always replicated.
	 *  Contract: >= the MAX authored weapon range (= the default hitscan Range, FPSRWeaponTypes.h Range=10000). Raise if a
	 *  longer-range weapon is added, else a distant-but-in-range enemy would be culled on the client (alive-but-unshootable). */
	static constexpr float NetCullWeaponRangeCm = 10000.0f; // cm (100m)
	/** Across-seam lookahead (cm) added to the bubble so a cross-door chaser replicates a moment before it reaches the player. */
	static constexpr float NetCullSeamMarginCm = 4000.0f; // cm (40m)

	// Separation (anti-clumping) tuning lives in UFPSREnemySwarmSettings (Project Settings, designer knob — PIE
	// feedback 2026-08-21: the constexpr pair here made crowd spacing a recompile). Cell size for the spatial hash
	// == the settings' SeparationRadius, snapshotted ONCE per movement pass so hash build and query always agree.

	/** Max CONCURRENT ranged chargers per player (held attack token, Game.MD §2-6). Unlike the melee per-pass token,
	 *  this is held for the whole charge so it bounds simultaneous ranged threats AND in-flight enemy projectiles
	 *  (the primary projectile-pool limiter). Balance value (tune later). */
	static constexpr int32 RangedAttackTokenLimit = 3;

	/** Server-only: per-player count of enemies currently holding a ranged-charge token (keyed by controller so it
	 *  survives the per-pass PlayerPawns rebuild). Incremented in TryAcquireRangedToken, decremented in
	 *  ReleaseRangedToken; cleared on ReleaseAllEnemies. NOTE: the stage-transition path no longer goes through
	 *  ReleaseAllEnemies (CarryEnemiesToNewStage carries instead), so a stale key from a mid-charge leaver now
	 *  survives a stage swap and is only cleaned by the next full-release flow (run reset / mission release). The
	 *  keys are TObjectKey so nothing collides; do not read this map assuming per-stage clearing. */
	TMap<TObjectKey<AFPSRPlayerController>, int32> RangedChargeCountByPlayer;

	/** While an enemy's actor Z is more than this BELOW the player's, it is still climbing UP toward the player (e.g.
	 *  a stair to an overlapping upper deck, U7 multi-layer) and must NOT be halted by the stop-gate — otherwise, with
	 *  the player standing at the stair top (a chokepoint), the 3D stop triggers a step below the platform edge and the
	 *  swarm bunches on the stair instead of cresting onto the platform. Above the flat-ground capsule-height difference
	 *  (a few cm, so no flat-map regression) but below one step, so the enemy climbs to roughly the player's level then stops. */
	static constexpr float StopClimbBelowPlayer = 30.0f; // cm

	/** Z below which a fallen enemy (pit / no static floor under it) is recycled to the pool so the endless-fall
	 *  path can't pin a director alive-count/hard-cap slot forever (Codex review 2026-06-09). Far below any
	 *  playable floor on the fixed map (§1). */
	static constexpr float WorldKillZ = -10000.0f; // cm

	// Spawn ground-snap trace (decouples spawn Z from the player's jump height). Half-height matches
	// AFPSREnemyBase's capsule (InitCapsuleSize(40, 90)).
	static constexpr float SpawnGroundTraceUp = 500.0f;     // cm above the candidate to start the down-trace
	static constexpr float SpawnGroundTraceDown = 10000.0f; // cm below the candidate to end the down-trace
	static constexpr float SpawnGroundHalfHeight = 90.0f;   // cm; capsule half-height -> feet on floor

	/** Designer-configured enemy class to spawn (BP child of AFPSREnemyBase). Null = spawn AFPSREnemyBase. Used as the
	 *  fallback when EnemyRoster is unset or yields no eligible class. */
	UPROPERTY()
	TSubclassOf<AFPSREnemyBase> EnemyClass;

	/** Data-driven archetype mix (Game.MD §2-6). When set + non-empty, AcquireEnemy picks a class by weighted random
	 *  per spawn; otherwise it falls back to EnemyClass. Pushed by the run director from DA_RunSchedule.EnemyRoster. */
	UPROPERTY()
	TObjectPtr<UFPSREnemyRosterDataAsset> EnemyRoster;

	/** The active run schedule (C3 — elite-cap curve source only). AcquireEnemy's elite gate reads
	 *  ActiveSchedule->StageDifficulty via UFPSRRunScheduleDataAsset::EvalStageAt(...).MaxEliteAlive. Pushed by
	 *  AFPSRGameMode::BeginPlay (see SetActiveSchedule's own comment for why this duplicates a reference the
	 *  run director subsystem also holds). Null = the elite gate treats the curve as a 0 cap (no elites). */
	UPROPERTY()
	TObjectPtr<UFPSRRunScheduleDataAsset> ActiveSchedule;

	/** Pool of dormant (hidden, disabled) enemies ready for reuse — bucketed by EXACT class so acquiring one costs
	 *  O(1) in the requested archetype's bucket size, independent of how many OTHER classes/enemies the pool holds
	 *  (ADR 0013 불변식 7, "풀 취득 비용은 클래스 수와 무관하다" — a paid-down cost of adopting 안 A/티어 클래스,
	 *  not an optional optimization). See FFPSREnemyDormantPool's own comment for the engine precedent, why the
	 *  key must stay EXACT-match (not IsChildOf), and the known starvation-mode caveat (documented there, not
	 *  fixed here — ADR 0013 후속 행 3 의 결정 대상). */
	UPROPERTY(Transient)
	FFPSREnemyDormantPool DormantPool;

	/** C4 기아 축출 진단 카운터(서버 전용, 복제 안 함) — DormantPool.EvictOneFromLargestOtherBucket 이 실제로
	 *  액터 하나를 꺼내 AcquireEnemy 가 그것을 Destroy() 할 때마다 누적. 세션(런) 경계에서 리셋하지 않는다 —
	 *  이 값의 존재 이유 자체가 "축출이 상시화되면 보이게" 이므로, 개별 런보다 누적치가 그 신호에 더
	 *  적합하다. 드물게(0 근처) 머물면 정상(하드캡 근접 시의 안전판); 계속 늘면 사실상 풀링이 무효화되고
	 *  있다는 뜻 — 그때는 클래스별 캡이나 로스터 배합을 재검토해야 한다. */
	int32 EvictionCount = 0;

	/** Set of currently active (visible, enabled) enemies. */
	UPROPERTY(Transient)
	TSet<TObjectPtr<AFPSREnemyBase>> ActiveEnemies;

	// --- Death-dwell (server-only). A corpse leaves ActiveEnemies THE INSTANT it dies (BeginDying) but isn't
	//     returned to DormantPool until its Death cosmetic has had time to actually be seen. A THIRD bucket rather
	//     than a flag on the existing two: ActiveEnemies membership is exactly "may move/attack this pass" (the
	//     movement/attack loop's own iteration set, TickEnemyMovement) and DormantPool membership is exactly
	//     "available for AcquireEnemy" — a dying-but-not-yet-pooled corpse is neither, and forcing it into either
	//     would need extra per-enemy filtering added to the 500-enemy hot loop / the pool reuse scan. ---
	/** Corpses past EnterDyingState, pending their LATER Deactivate+DormantPool return. Bounded by MaxDyingEnemies
	 *  (BeginDying evicts the oldest entry over the cap via FinishDyingEnemy); swept every TickEnemyMovement pass
	 *  (SweepDyingEnemies). */
	UPROPERTY(Transient)
	TArray<FFPSRDyingEnemy> DyingEnemies;
	/** Hard cap on concurrent dwelling corpses — generous headroom at this project's ~200-300-alive swarm scale
	 *  (Game.MD §5: GlobalAliveCap=200); BeginDying finishes the OLDEST entry immediately (the same FinishDyingEnemy
	 *  path SweepDyingEnemies uses) on overflow rather than growing this list unbounded. */
	static constexpr int32 MaxDyingEnemies = 48;

	/** Timer handle for the director tick. */
	FTimerHandle DirectorTimerHandle;

	/** Target number of alive enemies (director spawns/releases to maintain this). */
	int32 TargetAliveCount = 0;

	/** Total enemies spawned (hard cap at MaxActiveEnemies). */
	int32 TotalSpawned = 0;

	/** Current count of active elite-tier (AFPSREnemyEliteBase child) enemies (server-authoritative, C3) —
	 *  AcquireEnemy's elite gate compares this against min(curve MaxEliteAlive, EliteHardCap) before letting one
	 *  more elite become active. Incremented in AcquireEnemy right next to ActiveEnemies.Add. Decremented in
	 *  exactly TWO places: BeginDying (the death path — it removes from ActiveEnemies directly and never calls
	 *  ReleaseEnemy, see BeginDying's own comment, so there is no double-decrement) and ReleaseEnemy (every OTHER
	 *  teardown path — pool release / rear-drain / kill-Z recycle / stage-carry overflow / ReleaseAllEnemies, all
	 *  of which route through ReleaseEnemy). Reset to 0 in ResetForNewRun as a defensive safety net (same
	 *  precedent as RangedChargeCountByPlayer.Reset() there) — by that point ReleaseAllEnemies has already
	 *  released every active elite, so that line is normally redundant; kept explicit so a future teardown path
	 *  added without wiring into this accounting can't leave the counter stuck above 0 into the next run. */
	int32 ActiveEliteCount = 0;

	/** Edge-trigger latch for the elite-cap log in AcquireEnemy — the director retries the fill every pass, so the
	 *  log fires on the blocking/released TRANSITION rather than once per blocked attempt. Diagnostics only: the
	 *  gate itself never reads this, and ResetForNewRun clears it alongside ActiveEliteCount. */
	bool bEliteCapBlocking = false;

	// --- Map-aware allocator (multimap Tier 0, Performance §5 / Codex consult 2026-07-06) ---

	/** Global alive cap across ALL maps (the host worst-case budget — per-map caps are forbidden). The allocator splits
	 *  this across occupied maps; the fill loop hard-gates every spawn on ActiveEnemies.Num() < GlobalAliveCap so the
	 *  total never exceeds it. Tunable Tier-0 value = SSOT §5 잠정 200 (was previously un-enforced; schedule could reach 300). */
	static constexpr int32 GlobalAliveCap = 200;

	/** Headroom held below GlobalAliveCap so a newly-occupied map can seed enemies immediately even when the rest of the
	 *  budget is saturated (Codex R3: the 0-3s entry-seed promise). = Clamp(ceil(200*0.04), 4, 10). The steady per-map
	 *  apportionment targets GlobalAliveCap - SeedReserve; the reserve is the free headroom for entry seeding. */
	static constexpr int32 SeedReserve = 8;

	/** Temp Tier-0 weight bonus for a map with 2+ players (aggregate 2+ front > solo, without per-capita starvation).
	 *  weight = players + (players>=2 ? MapGroupBonus : 0). The content-aware allocator policy is Tier 1. */
	static constexpr int32 MapGroupBonus = 1;

	/** Grace after a map loses its last player before its enemies start draining (multimap Tier 0, server-only). A player
	 *  who dips across a boundary and returns within this window finds the crowd intact — no drain thrash at the door. */
	static constexpr float MapDrainGraceSeconds = 3.0f;

	/** Server-only: world time each map last had a player (grace source for the empty-map drain). Not replicated. */
	TMap<FGameplayTag, float> MapLastOccupiedTime;

	// --- Front-chase (multimap U P-D, server-only) — an enemy chases a player in a DIFFERENT open-grid-connected slot
	//     (through an opened door) via the UNIFIED field, within a path-distance range. Generalizes the tracker cohort to
	//     the whole connected front. Cells = uniform-cost BFS steps; kept < a slot's ~66-cell width so the front is
	//     "near the door", not the entire adjacent slot (Codex R2 #11). Tunable (PIE). ---
	/** Enter the front-chase state when the enemy's unified path-distance to the nearest player is <= this (cells). */
	static constexpr int32 ChaseEnterCells = 40;
	/** Stay front-chasing until path-distance exceeds this (cells) — Schmitt hysteresis (> ChaseEnterCells) so a boundary
	 *  enemy doesn't chase/idle flip-flop every 0.2s recompute (Codex R2 #13). */
	static constexpr int32 ChaseExitCells = 50;
	/** How long a front-chase tag stays live after the last in-range pass (world seconds) — bounds a stale cohort so it
	 *  drains once the player is gone or the field goes persistently source-less without renewal (Codex R2 #5). */
	static constexpr float ChaseHoldSeconds = 2.0f;

	// --- Front-spawn pressure (multimap U P-E, server-only) — an open door reads as ONE combat front spanning both slots:
	//     the adjacent (open-door-connected, currently-unoccupied) slot also spawns a near-door cohort. Bounded by a SEPARATE
	//     reserve carved EXPLICITLY out of the steady budget (PhysicalSteady = Cap - SeedReserve - FrontReserved) so it never
	//     inflates the physical apportionment's own target (Codex P-E gate #1). Per-front-slot cap keeps one front from
	//     monopolising the reserve in a 4-player split (#5). All PIE-tunable. ---
	/** Max concurrent front-attributed enemies PHYSICALLY present per front-active slot (per-front cap; round-robin fair). */
	static constexpr int32 PerFrontSlotBudget = 12;
	/** Hard ceiling on the TOTAL front reserve across all fronts (bounds how much of the steady budget the front borrows). */
	static constexpr int32 FrontBudgetCeiling = 36;
	/** Max front enemies spawned per director tick (front FILL rate; separate from the physical MaxSpawnPerTick so front fill
	 *  never starves the physical fill's per-tick throughput, Codex P-E gate #B). */
	static constexpr int32 MaxFrontSpawnPerTick = 2;
	/** One-shot crossing credit (world seconds): a front enemy that crosses into an occupied slot keeps counting against the
	 *  front reserve for this long, so the front can't instantly re-manufacture (conveyor RATE-limit, #4). Never renewed —
	 *  so a player round-tripping a door can't keep a cohort drain-immune (attribution grants NO drain immunity). */
	static constexpr float CrossingCreditSeconds = 4.0f;

	// --- Connectivity-aware trickle drain (multimap U P-E, server-only) — the ONLY drain path (P-G: the hard empty-map pop is
	//     gone). A time-based token bucket: REAR (far, not front-connected) enemies drain at an ambient rate, accelerating only
	//     when they're hogging the global cap and the front/physical targets can't fill (Codex P-E gate #3). Multimap only;
	//     single-map (its one map is always occupied while a player lives) needs no drain. ---
	/** Ambient rear-drain rate (enemies/sec) — a recently-vacated rear region thins gently. */
	static constexpr float BaseDrainRatePerSec = 2.0f;
	/** Burst rear-drain rate (enemies/sec) when the swarm is cap-bound AND a physical/front deficit exists (rear is eating
	 *  the cap the live front needs). Deliberately aggressive so an all-open endgame doesn't starve the front. */
	static constexpr float BurstDrainRatePerSec = 20.0f;
	/** ActiveEnemies within this many of GlobalAliveCap counts as "cap-bound" for the burst-drain trigger. */
	static constexpr int32 CapBoundMargin = 10;
	/** Clamp on the drain clock's per-tick elapsed (in director intervals) so a long freeze/pause can't accrue a burst of
	 *  drain tokens that pops the whole rear on the first unfrozen tick (Codex P-E gate #4 / Opus P0). */
	static constexpr int32 DrainDtClampTicks = 2;

	/** Server-only (U P-E): world time of the previous director tick, for the trickle-drain token clock. Stamped on EVERY
	 *  TickDirector entry (incl. early returns) so a freeze can't make the next elapsed huge. -1 = not yet stamped. Not replicated. */
	float LastDirectorTime = -1.0f;
	/** Server-only (U P-E): accumulated fractional rear-drain tokens (enemies). Not replicated. */
	float DrainTokenBucket = 0.0f;

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
