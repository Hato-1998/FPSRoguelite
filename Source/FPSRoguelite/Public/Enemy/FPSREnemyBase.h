// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Pawn.h"
#include "GameplayTagContainer.h"
#include "Enemy/FPSRAnimCPDParams.h"
#include "Enemy/FPSREnemyPursuit.h" // ADR 0008: FFPSRPursuitState/Params (PursuitState member, plain struct — no UObject dep)
#include "FPSREnemyBase.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;
class UFPSREnemyHealthComponent;
class UWidgetComponent;
class AFPSRCharacter;
class AFPSRPlayerController;
class APlayerController;
class UFPSREnemyAnimProfile;
class UFPSRFlowFieldSubsystem;

/** Outcome of a per-pass server attack decision, returned to the spawn subsystem so it can account the melee
 *  attack token. Ranged archetypes manage their own (held) token directly and return None. */
enum class EFPSRServerAttackResult : uint8
{
	None,
	MeleeAttacked,
};

/** Per-pass batch context the spawn subsystem hands to each enemy's ServerTickAttack. The subsystem owns target
 *  selection (nearest ALIVE player), the per-pass freeze gate (this is never called while run-paused), and the
 *  attack-token budgets; the enemy archetype owns the attack DECISION (melee contact vs. ranged charge->fire).
 *  DeltaSeconds is the real frame delta — it only accrues on non-frozen passes, so charge/cooldown accumulators
 *  built on it are freeze-paused for free. */
struct FFPSRServerAttackContext
{
	/** World time this pass (AFPSREnemyBase::CanAttack cooldown reference). */
	float Now = 0.0f;
	/** Real frame delta for this pass (ranged charge/cooldown accumulators). */
	float DeltaSeconds = 0.0f;
	/** Nearest alive player character (damage receiver). */
	AFPSRCharacter* TargetChar = nullptr;
	/** That player's controller (ranged-target warning RPC target). May be null. */
	AFPSRPlayerController* TargetController = nullptr;
	/** That player's world location. */
	FVector TargetLocation = FVector::ZeroVector;
	/** Squared XY distance to the target (matches the subsystem's nearest-player metric). */
	float DistSqToTarget = 0.0f;
	/** True if the vertical gap to the target is within the contact range (no through-floor melee hits). */
	bool bVerticalInRange = false;
	/** Time-scaled per-pass contact damage (melee). */
	float ContactDamage = 0.0f;
	/** True if the target player's per-pass melee attack-token budget still allows one more attacker. */
	bool bMeleeTokenAvailable = false;
};

/** Per-pass movement context the spawn subsystem hands to TickServerMovement (ADR 0008 — FFPSRServerAttackContext's
 *  struct-per-pass convention, so the per-pass inputs travel together instead of growing the parameter list). The
 *  subsystem owns target selection + flow sampling + the beeline fallback; the enemy owns steering + (ADR 0009 S3)
 *  folding SeekZ/bSeekValid into its vertical spring target.
 *
 *  bHasTarget/bFlowZero/TargetLocation are NOT currently read inside TickServerMovement — they fed the ADR 0008
 *  Seek3D pursuit judgment (FFPSRPursuitState::Tick + the beeline/step-up-skip branches), which was retired
 *  2026-08-21 (ADR 0009 P1) along with that call. Left in the struct (rather than deleted) because the SPAWN
 *  SUBSYSTEM still computes and meaningfully uses every one of them for its OWN purposes before constructing this
 *  context (target selection, the flow-zero beeline fallback, LOD tiering) — removing them here would mean either
 *  duplicating that computation or reaching back into the subsystem, for no benefit while they're merely unread by
 *  this one consumer. A future reactivation of the dormant pursuit judgment (see FFPSRPursuitState) would read them
 *  again without any new plumbing. */
struct FFPSRServerMoveContext
{
	/** Steering direction this pass (flow + separation combined, or the beeline fallback) — the former 1st param. */
	FVector MoveDir = FVector::ZeroVector;
	/** Direction to face/turn toward (stable — player-relative, not separation-jittery) — the former 2nd param. */
	FVector FaceDir = FVector::ZeroVector;
	/** Stride-scaled delta for this pass (LOD-throttled dt) — the former 3rd param. */
	float ScaledDelta = 0.0f;
	/** World time this pass (World->GetTimeSeconds()). */
	float Now = 0.0f;
	/** True if this enemy has a live move/attack target this pass (same-map or front-chase). False on the authored
	 *  exit-path branch and on a genuinely targetless enemy (empty map, pre-drain). See this struct's class comment
	 *  — currently unread by TickServerMovement itself. */
	bool bHasTarget = false;
	/** True when the subsystem's 2D flow-field sample for this enemy came back zero (a proven BFS-unreachable
	 *  column, e.g. a deck too tall to route to) — the spawn subsystem's own beeline-fallback trigger. See this
	 *  struct's class comment — currently unread by TickServerMovement itself. */
	bool bFlowZero = false;
	/** The current pursuit target's world location (BestPlayerLocation). See this struct's class comment —
	 *  currently unread by TickServerMovement itself. */
	FVector TargetLocation = FVector::ZeroVector;
	/** The target player's Z the last time it was GROUNDED (IsMovingOnGround), not its instantaneous Z (ADR 0009
	 *  invariant — "부유체 스웜" draft §3: a seek altitude target must not chase a jump/fall in real time, or the
	 *  whole swarm's altitude jitters in lockstep with every player hop). Falls back to the player's current Z if no
	 *  grounded sample was ever cached (e.g. a player who spawned mid-air). NOTE: UFPSRHoverWindowSubsystem keeps
	 *  its OWN separate grounded-Z cache for the SAME reason (its window-recentre source cell) rather than reading
	 *  this one — see that class's LastGroundedZByPawn comment — so this field, like its siblings above, is
	 *  currently unread by TickServerMovement itself now that the ADR 0008 computation that consumed it is gone. */
	float TargetGroundedZ = 0.0f;
	/** S3 (ADR 0009 P1): the hover window's vertical seek target for this pass (FFPSRFlowResult::SeekZ, forwarded
	 *  from UFPSRFlowFieldSubsystem::QueryFlow's Window3D answer) — meaningful only while bSeekValid. Supersedes the
	 *  retired ADR 0008 Seek3D altitude-band target (AFPSREnemyBase::SeekTargetZ/bSeekTargetZValid are now written
	 *  straight from this pair; see TickServerMovement). */
	float SeekZ = 0.0f;
	bool bSeekValid = false;
};

/** Lightweight server-authoritative swarm enemy — a cheap pooled APawn (NOT GAS-based), designed to run hundreds at
 *  once. Movement is the spawn subsystem's batched flow-field + separation pass (TickServerMovement), not per-actor AI;
 *  pooling reuses actors across lives (Activate/Deactivate with net dormancy). Also carries exit-path following,
 *  knockback, ranged-attack dispatch, front-chase / front-spawn credit, and VAT death/anim cosmetic state. The visual
 *  mesh comes from the content BP (VAT); the C++ base only falls back to a config placeholder mesh (Game.md §6-2). */
UCLASS()
class FPSROGUELITE_API AFPSREnemyBase : public APawn
{
	GENERATED_BODY()

public:
#if !UE_BUILD_SHIPPING
	/** Debug canary (FPSR.Enemy.ForceAnimState): apply a state to the mesh with EVERY gate bypassed — the dedupe,
	 *  the one-shot re-entry guard, and the attack hold window. Splits "the material does not react" from "the state
	 *  driver never asked it to" in one PIE pass instead of a log-reading round trip. PUBLIC because the console
	 *  command that drives it is a free function, not a member. Never called by gameplay.
	 *
	 *  bPin additionally SUPPRESSES the state driver (SetAnimState early-returns) until a later call clears it.
	 *  Without the pin this is only a poke: the driver re-derives a state on the very next net update / movement
	 *  pass and overwrites it, which makes a driver-caused restart indistinguishable from a material-caused one —
	 *  precisely the ambiguity the canary exists to remove for the "progress resets by itself" symptom. Pinned, a
	 *  forced one-shot MUST play through once and stop; if it still rewinds, the material's progress math is the
	 *  culprit. */
	void DebugForceAnimState(EFPSRAnimState State, bool bPin);
#endif

	AFPSREnemyBase();

	/** Capsule half-height (cm) the swarm enemy is built with. Shared so tooling (e.g. the editor blockout validator's
	 *  spawn-clearance check) reads the real value instead of re-declaring the magic number. */
	static constexpr float DefaultCapsuleHalfHeight = 90.0f;

	/** Server: reactivate a pooled enemy at Location (unhide, enable collision, reset health, randomize move speed).
	 *  Virtual so archetypes (e.g. ranged) can reset their own per-life state on reuse. */
	virtual void Activate(const FVector& Location);

	/** Server: deactivate and return to dormant pool state (hide, disable collision, net dormant). Virtual so
	 *  archetypes can release per-life state (e.g. a ranged enemy clears its warning + concurrency token) on EVERY
	 *  teardown path (pool release, death-dwell completion, kill-Z recycle all route through here). Called by the
	 *  spawn subsystem either immediately (pool release / rear-drain / kill-Z / stage-carry overflow) or LATER, once
	 *  a death-dwell has elapsed (UFPSREnemySpawnSubsystem::SweepDyingEnemies) — see EnterDyingState for the earlier,
	 *  immediate half of the DEATH path specifically. */
	virtual void Deactivate();

	/** Server: end this enemy's GAMEPLAY immediately on death, WITHOUT the hide/collision-off/dormancy Deactivate()
	 *  does — called once, synchronously, from UFPSREnemySpawnSubsystem::BeginDying (itself called from HandleDeath),
	 *  so the very next movement/attack pass already excludes this enemy (BeginDying also pulls it out of
	 *  ActiveEnemies in the same call). Disables collision ONLY: a dying enemy must never deal further contact damage
	 *  or (as a front-row corpse with collision still on) shield the swarm behind it from LineTraceMulti, which stops
	 *  at the first blocking hit. Deliberately does NOT hide the actor or touch net dormancy — bDead has already
	 *  replicated by the time this runs (the health component's ApplyDamage->OnDeath fired before HandleDeath), so a
	 *  remote client's own OnRep_bDead -> HandleDeathCosmetic needs the actor to keep replicating/rendering for the
	 *  death-dwell window to actually be SEEN. Deactivate() (called later, once GetDeathDwellSeconds() has elapsed)
	 *  is the second half that ends presentation and returns the actor to the pool. Virtual so archetypes with their
	 *  own held server state (e.g. AFPSRRangedEnemyBase's ranged token) can release it here too — see that
	 *  override's comment for why every teardown path, not just Deactivate(), must close it. */
	virtual void EnterDyingState();

	/** This archetype's authored death-dwell duration (DeathDwellSeconds) — read by the spawn subsystem's
	 *  BeginDying/SweepDyingEnemies to schedule this corpse's LATER Deactivate()+pool-return deadline. */
	float GetDeathDwellSeconds() const { return DeathDwellSeconds; }

	/** Server: move along Ctx.MoveDir (XY world dir; magnitude ignored, normalized internally) at CurrentMoveSpeed *
	 *  Ctx.ScaledDelta. No-op if MoveDir is ~zero. Driven by the enemy movement subsystem's batched pass (flow-field
	 *  + separation, or the authored exit path). Ctx.FaceDir is what the enemy TURNS to face (XY) — the direction
	 *  to the player, NOT MoveDir: at StopDistance MoveDir is separation-only and jitters, which would spin the
	 *  enemy in place. ADR 0009 S3: also carries Ctx.SeekZ/bSeekValid into SeekTargetZ/bSeekTargetZValid for
	 *  ApplyGravity's spring (see the .cpp) — a single context struct (FFPSRServerAttackContext's precedent)
	 *  instead of growing this into a long parameter list. (ADR 0008's mode-switching Seek3D pursuit judgment that
	 *  used to run here — PursuitState.Tick + the beeline/step-up-skip branches — was retired 2026-08-21 once this
	 *  window replaced it; FFPSRPursuitState itself is preserved dormant, see its own field comments.) */
	void TickServerMovement(const FFPSRServerMoveContext& Ctx);

	/** Server: assign an authored exit path (world-space waypoints) the enemy follows OUT of its spawn structure
	 *  (e.g. a pipe/box that the flow-field can't path out of) before reverting to flow-field player-chase at the
	 *  final waypoint. Empty = no path (immediate chase). Set right after Activate by the spawn subsystem from the
	 *  selected spawn point; cleared on Deactivate / overwritten on the next reuse. (C1) */
	/** @param bPhaseThroughWorld 경로를 따라가는 동안 정적 지오메트리를 통과할지(구조형 스포너 — 막힌 메시
	 *         안에서 스폰돼 벽을 지나 나온다). 캡슐의 WorldStatic 응답 하나만 Ignore 로 바뀌고 경로가 끝나면
	 *         복구된다 — 그 사이에도 적은 맞고(Visibility) 플레이어를 막으며(ECC_FPSRPlayerPawn) 바닥도 밟는다. */
	void SetExitPath(const TArray<FVector>& InWaypoints, bool bPhaseThroughWorld);

	/** Server: true while the enemy is still following its authored exit path (not yet handed off to the flow-field). */
	bool IsFollowingExitPath() const { return bFollowingExitPath; }

	/** Server: if following the exit path, advance past any reached waypoint and output the unit XY steer direction to
	 *  the current target (returns true). Returns false once the path is exhausted or abandoned (stall timeout) — the
	 *  caller then steers via the flow-field. ScaledDeltaSeconds drives the stall safety timer. */
	bool ConsumeExitPathSteering(const FVector& MyLocation, float ScaledDeltaSeconds, FVector& OutDir);

	/** Distance at which the enemy stops advancing toward a player (used by the movement subsystem). */
	float GetStopDistance() const { return StopDistance; }

	/** This life's sampled hover height (0 = a ground archetype). The movement subsystem's QueryFlow gate reads it:
	 *  only hover archetypes may take the 3D window route — their spring is what EXECUTES the window's vertical
	 *  guidance; a ground agent has no such mover (merge-gate P1, see FFPSRFlowQuery::bHoverCapable). */
	float GetCurrentHoverHeight() const { return CurrentHoverHeight; }

	float GetAttackRange() const { return AttackRange; }
	float GetAttackDamage() const { return AttackDamage; }

	/** The enemy's non-GAS health component — exposed so the on-damage HP bar (B11) and floating damage numbers (B20)
	 *  WBPs can bind OnHealthChanged (client-fired via B12) and read GetHealth()/GetMaxHealth(). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Enemy")
	UFPSREnemyHealthComponent* GetHealthComponent() const { return HealthComponent; }

	/** The enemy's visual mesh — exposed read-only so the S4 readability metrics (UFPSREnemyMetricsSubsystem) can read
	 *  this primitive's GetLastRenderTimeOnScreen(). The actor-level AActor::WasRecentlyRendered is NOT usable there:
	 *  it reads AActor::LastRenderTime, which the SHADOW passes also stamp (ShadowSetup.cpp calls
	 *  UpdateComponentLastRenderTime with bUpdateLastRenderTimeOnScreen=false), so an enemy BEHIND the player that
	 *  merely casts a shadow into view would count as "on screen" (measured: it over-reported on 48% of frames). */
	const UStaticMeshComponent* GetMesh() const { return Mesh; }

	/** Cosmetic, local-machine only (UFPSREnemyShadowLODSubsystem): drive this enemy's dynamic shadow from a distance
	 *  band instead of leaving it at UMeshComponent's constructor default of always-on. Exposed as a purpose-named
	 *  setter rather than a mutable GetMesh so the mesh stays read-only to everyone else. No local guard is needed —
	 *  UPrimitiveComponent::SetCastShadow already early-outs when the value is unchanged, so it never redundantly
	 *  dirties the render state. */
	void SetShadowCasting(bool bEnabled);

	/** Server: true if the attack cooldown has elapsed at time Now. */
	bool CanAttack(float Now) const { return (Now - LastAttackTime) >= AttackInterval; }

	/** Server: stamp the time of an attack (called by the movement/attack subsystem). */
	void NotifyAttacked(float Now) { LastAttackTime = Now; }

	/** Server: per-pass attack decision, called by the spawn subsystem's batched pass for this enemy's nearest alive
	 *  player. Base = melee contact damage (in range + vertical gap + cooldown + melee token). Ranged archetypes
	 *  override this to drive a charge->fire cycle instead. Returns whether a melee token was consumed so the
	 *  subsystem can account it. Never called while the run is globally frozen (the pass early-returns). */
	virtual EFPSRServerAttackResult ServerTickAttack(const FFPSRServerAttackContext& Ctx);

	/** Server: add a knockback impulse (cm/s, from an explosion). The horizontal part decays over KnockbackDecayTime
	 *  while applied each movement tick; the vertical part feeds the gravity integrator so the enemy arcs up and
	 *  falls back. Lightweight (velocity add, no physics) — cheap at swarm scale. */
	void ApplyKnockback(const FVector& Velocity);

	/** The map this enemy belongs to (multimap Tier 0). Server-only (not replicated — cross-map relevancy is handled by
	 *  the distance net-cull (NetCullRadius / ApplyNetCullRadius), not this tag). Assigned by the spawn subsystem from the
	 *  selected spawn point at spawn, and refreshed by the movement pass (AABB) when the enemy crosses a map boundary.
	 *  Unset = the Default single-map. Used to gate the enemy's nearest-player target / flow sample / attack to same-map
	 *  players + the cross-map combat guard. */
	const FGameplayTag& GetMapId() const { return MapId; }
	void SetMapId(const FGameplayTag& InMapId) { MapId = InMapId; }

	/** Server (U P-H): set the actor's net-cull radius (cm) at spawn. In the unified multimap field the spawn subsystem calls
	 *  this with the footprint-derived UNIFORM radius (UFPSREnemySpawnSubsystem::ComputeUnifiedNetCullRadius); a single-map run
	 *  never calls it, so the ctor default (NetCullRadius) stands (byte no-regression). Applied AFTER Activate wakes net
	 *  dormancy — the default net driver reads NetCullDistanceSquared live each relevancy pass, so a per-acquire change takes
	 *  effect. Clamps ONLY a 0/negative/NaN caller (MinNetCullRadiusCm); the gameplay floor is owned by the compute helper. */
	void ApplyNetCullRadius(float RadiusCm);

	// --- Front-chase (multimap U P-D) — server-only, NOT replicated. The movement pass tags an enemy chasing a player in a
	//     DIFFERENT open-grid-connected slot (through an opened door) via the unified flow field, within the front range.
	//     Read by the empty-map drain (a front-chaser is a live cohort, exempt like a tracker) and by tracker mutual
	//     exclusion. Expiry-bounded (ChaseHoldSeconds) so a stale/departed chaser eventually drains. Cleared on Activate. ---
	/** Server: mark this enemy as a front-chaser until ExpireTime (world seconds). */
	void SetFrontChasing(float ExpireTime) { FrontChaseExpireTime = ExpireTime; }
	/** Server: true if this enemy holds a live front-chase tag at time Now. */
	bool IsFrontChasing(float Now) const { return Now < FrontChaseExpireTime; }
	/** Server: drop the front-chase tag (handoff to same-map target / pool reuse). */
	void ClearFrontChasing() { FrontChaseExpireTime = -1.0f; }

	// --- Front-spawn attribution (multimap U P-E) — server-only, NOT replicated. An enemy the director spawns into a
	//     front-active (open-door-connected, currently-unoccupied) slot is TAGGED here so the front pressure budget can keep
	//     counting it for a bounded window even AFTER it crosses into the player's occupied slot (crossing credit), which
	//     rate-limits the front's refill so an open door can't become an infinite conveyor (Codex P-E gate #4). The credit is
	//     ONE-SHOT (stamped once at first crossing, never renewed) so a player round-tripping a door can't keep a cohort
	//     drain-immune — attribution grants NO drain immunity (only IsFrontChasing / physical front-slot presence does).
	//     Cleared on Activate (pool reuse), like the tracker / front-chase tags. ---
	/** Server: mark this enemy as front-spawned (fresh attribution, uncredited). Called by the spawn subsystem right after
	 *  the enemy's MapId is set, so a pooled reuse in a non-front slot never carries a stale tag. */
	void MarkFrontSpawned() { bFrontSpawned = true; FrontCreditExpireTime = -1.0f; }
	/** Server: is this enemy still attributed to a front (front-spawned and not yet released)? */
	bool IsFrontSpawned() const { return bFrontSpawned; }
	/** Server: whether this front-spawned enemy has had its one-shot crossing credit stamped yet (false = not crossed). */
	bool HasFrontCreditStamp() const { return FrontCreditExpireTime >= 0.0f; }
	/** Server: stamp the one-shot crossing credit (world seconds) as the enemy first enters an occupied slot. */
	void StampFrontCredit(float ExpireTime) { FrontCreditExpireTime = ExpireTime; }
	/** Server: is the crossing credit still live at time Now (only meaningful once stamped)? */
	bool IsFrontCreditLive(float Now) const { return Now < FrontCreditExpireTime; }
	/** Server: release front attribution (credit consumed / caught up) — the enemy becomes a normal slot enemy. */
	void ClearFrontSpawn() { bFrontSpawned = false; FrontCreditExpireTime = -1.0f; }

	/** Server (Phase A stage-transition carry-over): relocate this ACTIVE (not pooled/dormant) enemy across a stage
	 *  swap to NewLocation. Clears any leftover exit path — and its WorldStatic collision-ignore override — BEFORE
	 *  moving (ClearExitPath is the single recovery point, protected; this is the public entry point a caller
	 *  outside the class uses to reach it), then teleports (no sweep — the caller already resolved an open cell via
	 *  UFPSRFlowFieldSubsystem::FindNearestOpenLocation; NewLocation.Z is that cell's FLOOR SURFACE Z, and this
	 *  function converts it to the capsule's rest Z — floor + HalfHeight + GroundRestClearance, ApplyGravity's own
	 *  TargetZ convention — so the enemy lands standing instead of half-buried) and resets the physics-contact state
	 *  Activate() resets for
	 *  a pooled reuse ("may spawn on a rooftop" there == "may land somewhere new" here — a KnockbackVelocityXY /
	 *  VerticalVelocity / bGrounded / GroundRecheckTimer computed against the OLD arena's geometry is meaningless in
	 *  the new one). Deliberately narrower than Activate(): does NOT touch health, front-chase/front-spawn state,
	 *  MapId, or anim/cosmetic state — this is the SAME life, just relocated (see
	 *  UFPSREnemySpawnSubsystem::CarryEnemiesToNewStage for which per-enemy state is safe to leave untouched and
	 *  why). No-op off authority. */
	void ServerRelocateForStageCarry(const FVector& NewLocation);

protected:
	virtual void BeginPlay() override;

	/** S4: unregister from the enemy readability-metrics registry (UFPSREnemyMetricsSubsystem). Pooled enemies are
	 *  hidden/DORM_DormantAll, never destroyed (see Deactivate) — EndPlay only fires once per actor's real lifetime
	 *  (level teardown / PIE end), mirroring BeginPlay's once-per-lifetime Register. CSV-gated (see .cpp); a no-op
	 *  in Shipping. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Server: ground-follow + gravity each movement update — a single down-trace snaps the enemy to the floor
	 *  (slopes/steps within GroundSnapTolerance) or lets it fall under gravity off a ledge / after a high spawn,
	 *  so enemies never float and rooftop-spawned enemies drop down before chasing. A hovering archetype
	 *  (CurrentHoverHeight > 0) tries the v2 flow-field array height sampler FIRST (zero scene query) whenever
	 *  VerticalVelocity <= 0 — i.e. descending OR resting, not just already grounded (2026-08-14 follow-up: a
	 *  cliff/step-down GLIDES down via the spring instead of free-falling; still ballistic while rising under a
	 *  knockback launch). FlowDirXY (the enemy's current flow/face direction) drives the sampler's 1-cell look-ahead
	 *  so a floater starts rising before it reaches a step instead of snapping up at the cell boundary. Defaults to
	 *  ZeroVector for callers that don't steer (no look-ahead, straight-down sample only — matches v1 behavior).
	 *  ADR 0009 S3: while bSeekTargetZValid, the spring's TargetZ is max(this terrain-relative target, SeekTargetZ) —
	 *  ONE integrator, no separate code path for a seek target (invariant carried over from ADR 0008's original
	 *  "Seek3D never gets its own Z code path"). SeekTargetZ is now written from the hover window's QueryFlow answer
	 *  (see SeekTargetZ's own field comment), not the retired ADR 0008 pursuit-mode computation. */
	void ApplyGravity(float ScaledDeltaSeconds, const FVector& FlowDirXY = FVector::ZeroVector);

	UFUNCTION()
	void HandleDeath(AActor* DeadActor, AActor* Killer);

	/** Server + clients: force the world-space health-bar widget (a BP-added UWidgetComponent) to exist now — it can
	 *  otherwise be created lazily on first render, after BeginPlay, leaving the BP bind on a null widget — then fire
	 *  OnHealthBarReady so the BP binds it to the health component (A1/B20). Runs on clients too: the bar is a client
	 *  visual and OnHealthChanged is client-fired via OnRep_Health (B12). The widget + health component persist across
	 *  pooling (the actor is reused, not destroyed), so this once-per-lifetime bind stays valid for every reuse. */
	void InitHealthBarWidget();

	/** Show/hide the world-space health bar. Called on BOTH sides from the same places the death cosmetic runs, which
	 *  is why it lives on the cosmetic path and not in EnterDyingState: EnterDyingState is server-only (BeginDying),
	 *  and the bar is a CLIENT visual — hiding it only there would leave every remote client staring at a full-looking
	 *  bar floating over a corpse for the whole death dwell, which reads as "it didn't die" (PIE 2026-08-25).
	 *  Re-shown on Activate() / the client unhide reset, since the widget survives pooling (InitHealthBarWidget's
	 *  bind is once-per-lifetime and must stay valid). */
	void SetHealthBarVisible(bool bVisible);


	/** BP hook (fired by InitHealthBarWidget): the BP does GetUserWidgetObject -> Cast(WBP_EnemyHealthBar) ->
	 *  InitHealthComp(GetHealthComponent()) so the bar/floating-damage widget binds OnHealthChanged. */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Enemy")
	void OnHealthBarReady();

	/** Reset exit-path follow state (on Deactivate / before a new SetExitPath). */
	void ClearExitPath();

	// --- Animation (U20 domain C) — cosmetic procedural-mesh state driver. DORMANT (zero cost) until an AnimProfile
	//     is assigned to the archetype. State source: authority (standalone / listen-server host) = the server batch
	//     pass below; clients = the replicated transform (PostNetReceiveLocationAndRotation). Never replicated
	//     (Performance §5). ---

	/** True for a ONE-SHOT animation state (plays once and holds/dwells on its final pose) vs. a LOOPING one (Idle/
	 *  Walk, cycles indefinitely at the given rate). SetAnimState uses this to (a) bypass its dedupe for a state that
	 *  must restart every time it's re-entered — a melee attacker re-entering Attack every cooldown at the SAME
	 *  playrate bucket would otherwise never replay past its first cycle — and (b) pick a duration-derived rate
	 *  default (AttackAnimHoldSeconds / DeathDwellSeconds) instead of a flat 1.0 when the caller doesn't pass one
	 *  explicitly. */
	static bool IsOneShotState(EFPSRAnimState InState);

	/** Set the current animation state (+ explicit playrate). PlayRate < 0 (the default) means "the caller didn't
	 *  pass one": a LOOPING state (Idle/Walk) falls back to 1.0 (normal speed, unchanged from the old hardcoded
	 *  default); a ONE-SHOT state (Attack/Death) falls back to 1 / AttackAnimHoldSeconds or DeathDwellSeconds, so the
	 *  material's (Time-EnterTime)*Rate progress reaches 1.0 exactly at the authored hold/dwell length instead of
	 *  playing at an arbitrary guessed speed. A caller that DOES pass an explicit rate (>= 0, e.g. a speed-scaled
	 *  walk cycle or the 0.0 distance-LOD freeze) is always respected as-is.
	 *  Event-driven: a no-op when the state and quantized playrate bucket are unchanged. A one-shot state RE-ENTERED
	 *  from itself is handled separately (see SetAnimState's re-entry guard): Attack restarts only once its previous
	 *  cycle has finished, and Death never restarts at all. A no-op entirely when no AnimProfile is assigned or on a
	 *  dedicated server (no local rendering). Applies via the profile. */
	void SetAnimState(EFPSRAnimState NewState, float PlayRate = -1.0f);

	/** Client: derive the animation state from the replicated transform when new location data arrives (walk/idle from
	 *  position delta, a melee-attack tell from proximity to the nearest local player, distance LOD freeze). Runs only
	 *  off-authority; the authority drives state from its server movement/attack pass instead. */
	virtual void PostNetReceiveLocationAndRotation() override;

	/** Client: reset the cosmetic anim state on the pool-reuse "became visible again" edge (bHidden true -> false).
	 *  AActor::bHidden replicates with NO OnRep/RepNotify (Actor.h) — the client instead applies it by calling this
	 *  virtual setter from PostNetReceive (ActorReplication.cpp: PreNetReceive saves the old value, PostNetReceive
	 *  exchanges it back in and calls SetActorHiddenInGame(NewValue) if it differs), so overriding it is the correct
	 *  client-side hook. Mirrors Activate()'s authority-side reset (cpp) so a reused enemy's stale CPD/anim state (a
	 *  prior life's Death clip, a huge dormant-period dt) doesn't leak into the new life. No-op on authority — the
	 *  server-side reset is Activate()'s job. */
	virtual void SetActorHiddenInGame(bool bNewHidden) override;

	/** Bound to the health component's OnDeathCosmetic (client death edge) — enters the Death animation state. */
	UFUNCTION()
	void HandleDeathCosmetic();

	/** Bound to the health component's OnHealthChanged (server: fired from ApplyDamage; client: fired from
	 *  OnRep_Health) — stamps CPDSlot_LastHitTime on a genuine damage EDGE only (NewHealth < LastHealthForHitFlash),
	 *  which the assigned material can read to drive a short hit-flash pulse. Guarded against ResetForReuse()'s
	 *  broadcast of the SAME delegate on pool reuse (Health snaps 0 -> MaxHealth there — an INCREASE, not a hit) by
	 *  LastHealthForHitFlash (see that field's comment). Same dormant/dedicated-server gate as SetAnimState — an
	 *  archetype with no AnimProfile pays nothing, and a dedicated server never renders so it never needs the write.
	 *  Fires on both server and client; purely cosmetic, so each side stamping its own GetTimeSeconds() is fine (no
	 *  new replication). */
	UFUNCTION()
	void HandleHealthChangedForHitFlash(float NewHealth, float MaxHealth);

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Enemy")
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Enemy")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Enemy")
	TObjectPtr<UFPSREnemyHealthComponent> HealthComponent;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy")
	float MoveSpeed = 750.0f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy")
	float StopDistance = 120.0f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Attack")
	float AttackRange = 150.0f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Attack")
	float AttackDamage = 8.0f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Attack")
	float AttackInterval = 1.0f;

	/** Server-only: world time of last attack (init far in the past so the first attack is allowed). */
	float LastAttackTime = -1000.0f;

	/** World time until which the walk/idle branch must NOT override the cosmetic anim state — holds an Attack
	 *  one-shot / ranged charge tell for its full duration instead of the very next movement pass or net update
	 *  stomping it back to Walk/Idle. NOT replicated: each side stamps its OWN copy for the driver that owns this
	 *  instance, and the two drivers never run for the same instance (PostNetReceiveLocationAndRotation fires only
	 *  on a remote client, TickServerMovement only on authority), so one field serves both without contention.
	 *   - AUTHORITY driver, gating TickServerMovement's walk/idle branch: stamped by ServerTickAttack (melee: Now +
	 *     AttackAnimHoldSeconds) and AFPSRRangedEnemyBase::ServerTickAttack's Idle->Charging transition (Now +
	 *     RangedChargeTime, matching the charge-length SetAnimState rate set alongside it).
	 *   - CLIENT driver, gating PostNetReceiveLocationAndRotation's walk/idle branch: stamped by that function's own
	 *     melee proximity tell and by AFPSRRangedEnemyBase::OnRep_Charging. The client used to have no hold at all,
	 *     which is what let a flickering bMoving bounce Attack->Walk->Attack; a re-entry from a DIFFERENT state
	 *     skips SetAnimState's one-shot guard and restamps CPD EnterTime, rewinding the material's
	 *     (Time-EnterTime)*Rate progress to 0 mid-play.
	 *  Deliberately a raw timestamp rather than a read of CurrentAnimState: SetAnimState early-returns before ever
	 *  WRITING CurrentAnimState on a dedicated server (see its NM_DedicatedServer gate), so CurrentAnimState is
	 *  permanently stuck at its Idle default there — a guard built on it is correct only on a listen-server host.
	 *  This field is written unconditionally by the attack decision (gated on nothing cosmetic), so the hold window
	 *  is correct on every net mode. Reset on Activate (authority) and in SetActorHiddenInGame's became-visible edge
	 *  (client) so a pooled reuse never inherits a prior life's hold. */
	float AttackAnimHoldUntil = -1.0f;

	/** Server-only: this enemy's map (multimap Tier 0). See GetMapId. Not replicated. */
	FGameplayTag MapId;

	/** Server-only (U P-D): world time until which this enemy is a live front-chaser (chasing a cross-slot connected player
	 *  via the unified field). -1 = not front-chasing. See SetFrontChasing. Not replicated. */
	float FrontChaseExpireTime = -1.0f;

	/** Server-only (U P-E): true while this enemy is attributed to a front (spawned into a front-active adjacent slot).
	 *  FrontCreditExpireTime = the one-shot crossing-credit deadline; -1 = not yet crossed into an occupied slot. Both are
	 *  reset on Activate (pool reuse). See MarkFrontSpawned. Not replicated (server-only AI budget accounting). */
	bool bFrontSpawned = false;
	float FrontCreditExpireTime = -1.0f;

	/** Server-tunable net-cull radius (cm) written to NetCullDistanceSquared in the ctor (single-map / archetype fallback).
	 *  Enemies spawn into the PERSISTENT level (always level-relevant to every connection), so distance is the SOLE lever that
	 *  culls a swarm enemy from a distant player (RepGraph — spatial grid relevancy — is the production fix, a separate phase).
	 *  In the U unified multimap field the spawn subsystem OVERRIDES this per-acquire with a footprint-derived uniform radius
	 *  (UFPSREnemySpawnSubsystem::ComputeUnifiedNetCullRadius — an engagement/weapon-range bubble capped to the slot footprint),
	 *  so this default only applies to a plain single-map run (byte no-regression). Contract: >= the max authored weapon range,
	 *  so an enemy the server hitscan can reach is always replicated (never alive-but-unshootable). A symmetric distance cull
	 *  can't do per-slot "seam-only" relevancy without RepGraph, so a client sees far same-slot / cross-seam enemies pop in as
	 *  they approach — an accepted Tier-0 visual limitation (D3), not a logic bug (the server chase is seamless). Boss is
	 *  separately bAlwaysRelevant. Designers can raise this per-archetype in BP — honored via the BeginPlay re-derive (the ctor
	 *  runs before BP defaults apply). See ApplyNetCullRadius. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Network")
	float NetCullRadius = 20000.0f; // cm (200m)

	/** Tiny safety floor for ApplyNetCullRadius — defends ONLY against a caller passing 0 / negative / NaN. The gameplay
	 *  net-cull floor (>= weapon range) is owned SOLELY by ComputeUnifiedNetCullRadius (single source), not re-imported here. */
	static constexpr float MinNetCullRadiusCm = 100.0f; // cm (1m)

	/** XP dropped on death (editor-tunable per enemy type / DataAsset). Balance value. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy")
	int32 XPReward = 5;

	/** Per-instance move speed (MoveSpeed * random ±10% on Activate). Game.MD §2-6. */
	UPROPERTY(Transient)
	float CurrentMoveSpeed = MoveSpeed;

	/** CurrentMoveSpeed with the FPSR.Debug.EnemySpeedScale playtest multiplier applied. Deliberately applied at USE
	 *  rather than folded into CurrentMoveSpeed at spawn: the knob exists to sweep 1x/2x/3x while a swarm is already
	 *  on the field, and a spawn-time fold would only affect enemies spawned after the change. Identical to
	 *  CurrentMoveSpeed at scale 1 and in shipping. */
	float GetEffectiveMoveSpeed() const;

	/** Gravity acceleration (cm/s^2) applied while airborne (fall off ledges / land after a high spawn). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Movement")
	float GravityAccel = 1800.0f;

	/** Hover height (cm) the CAPSULE rests above the floor — a floating archetype hovers with its COLLISION (not a
	 *  visual mesh offset, which would desync hits from the silhouette: shots under the mesh would hit, shots at the
	 *  raised tip would miss). Applied at the single grounding anchor in ApplyGravity, so stepping, falling and
	 *  landing all inherit it. KEEP WELL BELOW the melee/projectile vertical attack gate (150cm) and the flow field's
	 *  layer-pick window (UFPSRFlowFieldComputer::MaxLayerPickDrop = 200cm) — a hovering foot sits CurrentHoverHeight
	 *  above its floor surface, and both the v1 foot-Z layer pick and the v2 height sampler's anchor rank resolve
	 *  from that raised foot, so a taller hover risks snapping to the WRONG storey. 0 = walks on the ground. This is
	 *  the FALLBACK value used when HoverHeightMin/Max don't define a per-instance range (see CurrentHoverHeight) —
	 *  runtime code reads CurrentHoverHeight, never this field directly. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Movement", meta = (ClampMin = "0.0"))
	float HoverHeight = 0.0f;

	/** Per-instance hover-height range (cm, spec item 4 — variety across a swarm, e.g. a bipyramid archetype at
	 *  120~180). When HoverHeightMax > HoverHeightMin, Activate samples CurrentHoverHeight uniformly from this range;
	 *  otherwise (the 0/0 default = "no range authored") CurrentHoverHeight falls back to HoverHeight, so an existing
	 *  archetype BP needs no re-authoring (no-regression). Content sets these; there is no code-side default range. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Movement", meta = (ClampMin = "0.0"))
	float HoverHeightMin = 0.0f;
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Movement", meta = (ClampMin = "0.0"))
	float HoverHeightMax = 0.0f;

	/** Undamped spring frequency (Hz) for a hovering archetype's vertical follow (v2 — replaces v1's constant-speed
	 *  FInterpConstantTo glide with FMath::SpringDamper, UnrealMathUtility.h:1665). Higher = stiffer/snappier catch-up
	 *  to the target height. SpringDamper stays numerically stable across the swarm's stride-scaled (ScaledDelta-
	 *  Seconds) timestep, so no custom integrator is needed here. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Movement", meta = (ClampMin = "0.1"))
	float HoverSpringFrequency = 1.5f;

	/** Damping ratio for the same vertical spring. 1.0 = critically damped (reaches the target height with no
	 *  overshoot — the default "solid hover" feel); below 1.0 under-damps (bounces past the target before settling),
	 *  an intentional data-only knob for a looser/wobblier floater. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Movement", meta = (ClampMin = "0.2"))
	float HoverSpringDampingRatio = 1.0f;

	/** Server-only: this instance's actual hover height, sampled from [HoverHeightMin, HoverHeightMax] on Activate
	 *  (or copied from HoverHeight when no range is authored). ApplyGravity/TickServerMovement read THIS — never
	 *  HoverHeight directly — so per-instance variety (spec item 4) reaches every hover code path uniformly. */
	UPROPERTY(Transient)
	float CurrentHoverHeight = 0.0f;

	/** Server-only: FMath::SpringDamper's velocity state for the vertical hover follow. ONE spring shared by the v2
	 *  array-sampled path and the v1 scene-query fallback (see ApplyGravity), so a mid-flight handoff between them
	 *  doesn't reset the motion. Reset to 0 on Activate; seeded from VerticalVelocity on a fall landing so a knocked-
	 *  back / spawn-dropped hover archetype eases into its rest height instead of restarting from rest. */
	UPROPERTY(Transient)
	float HoverSpringRateZ = 0.0f;

	/** Server-only: the world's flow-field subsystem, cached ONCE in BeginPlay — a world subsystem outlives every
	 *  pooled enemy (world lifetime >= actor lifetime), so caching avoids a GetSubsystem<>() lookup on every enemy's
	 *  every movement update at swarm scale (Performance §5-2). Feeds the v2 hover height sampler; null on a world
	 *  with no flow field (e.g. pre-content) falls back to the v1 scene-query path unconditionally. */
	UPROPERTY(Transient)
	TObjectPtr<UFPSRFlowFieldSubsystem> CachedFlowField;

	/** Server-only: cached rest-height target for the v1 SCENE-QUERY FALLBACK path only (ApplyGravity re-traces every
	 *  GroundRecheckInterval; gliding only on those ticks would quantize the motion). The v2 array sampler needs no
	 *  cache of its own — SampleHoverFloorZ is O(1) array math, so it re-samples fresh every movement update. */
	float HoverRestZ = 0.0f;

	/** If the floor is within this of the feet (up or down), snap to it (slopes/steps); beyond it (a real drop),
	 *  fall under gravity. Also the BASE increment the movement step-up lifts over a stair riser (see MaxCrestStepUp). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Movement")
	float GroundSnapTolerance = 60.0f;

	/** Max lift the movement step-up tries when cresting off a SLOPE. A swept-move blocking hit steeper than a walkable
	 *  slope (normal Z < WalkableSlopeNormalZ — a stair riser / ledge / ramp-crest lip) triggers a STEP-UP so the enemy
	 *  climbs what the flow field routed it toward (the field only opens climbable height changes). Enemies are lightweight
	 *  Pawns without CharacterMovement's StepUp, so the minimal equivalent: lift, re-advance, let ApplyGravity settle onto
	 *  the top. The lift is tried in GroundSnapTolerance increments up to this max, taking the SMALLEST that clears (no
	 *  over-hop). A ramp/stair top onto a platform can present a lip taller than one flat step, so on a SLOPE we allow up to
	 *  here; on FLAT ground the lift stays capped at one GroundSnapTolerance so enemies don't scale walls the field routes around. */
	static constexpr float MaxCrestStepUp = 180.0f; // cm (== 3 x GroundSnapTolerance)

	/** Max DOWNWARD snap for a GROUNDED enemy — the descent mirror of the step-up so an enemy walking off a small ledge
	 *  or down a stair snaps onto the surface below instead of free-falling (ApplyGravity's old symmetric ±GroundSnap-
	 *  Tolerance window had step-UP logic but no step-DOWN, so any drop > 60cm free-fell — and MaxFallStep ~= a storey,
	 *  so a deck enemy dropped a whole floor). Kept WELL BELOW a storey (a true deck-edge cliff still falls; the flow
	 *  routes to the stair). Only widens the DOWN side; the UP snap stays GroundSnapTolerance so enemies can't scale walls. */
	static constexpr float MaxStepDownHeight = 180.0f; // cm (== MaxCrestStepUp; symmetric climb/descend budget)

	/** A swept-move blocking hit whose surface normal Z is >= this is a WALKABLE SLOPE (ramp / stair simple-collision
	 *  incline): instead of stalling flat against it, the enemy slides the blocked remainder UP along the surface so it
	 *  ascends. 0.5 = cos 60deg — matches the flow field's walkable slope (slightly more permissive so the enemy always
	 *  climbs what the field routed it up). Below this is a wall / riser / ramp-crest lip — the step-up (MaxCrestStepUp) handles it. */
	static constexpr float WalkableSlopeNormalZ = 0.5f;

	/** Short down-trace length for the ground check. Falling is incremental (re-traced each airborne update),
	 *  so this only needs to reach a bit past the feet — keeps the per-enemy scene query cheap at swarm scale. */
	static constexpr float GroundProbeDistance = 700.0f; // cm

	/** Tiny gap kept between the capsule bottom and the floor when grounded. Resting flush makes the horizontal
	 *  swept move start-penetrating the floor (capsule contact offset), which UE rejects → the enemy can't move
	 *  at all. A small clearance keeps the sweep free. (Same reason CharacterMovement keeps a floor distance.) */
	static constexpr float GroundRestClearance = 5.0f; // cm

	/** Seconds a GROUNDED enemy skips the ground trace before re-checking (amortizes the cost across the swarm;
	 *  airborne enemies trace every update). Bounds ledge-walk-off detection lag. */
	static constexpr float GroundRecheckInterval = 0.15f;

	/** Server-only vertical velocity for gravity/falling (reset to 0 on landing / while grounded). */
	UPROPERTY(Transient)
	float VerticalVelocity = 0.0f;

	/** Server-only: true while resting on the floor (gates the amortized ground re-check). */
	UPROPERTY(Transient)
	bool bGrounded = false;

	/** Server-only: countdown until the next ground re-check while grounded. */
	UPROPERTY(Transient)
	float GroundRecheckTimer = 0.0f;

	/** Server-only: surface normal of the ground under the enemy from the last ApplyGravity trace (up while airborne).
	 *  TickServerMovement projects the steering onto this plane so the enemy walks smoothly UP/DOWN ramps/stairs instead
	 *  of jamming flat against them (the swarm equivalent of CharacterMovement's MoveAlongFloor). */
	UPROPERTY(Transient)
	FVector GroundNormal = FVector::UpVector;

	/** Server-only horizontal knockback velocity (cm/s), decayed each tick. Vertical knockback lives in
	 *  VerticalVelocity (integrated by ApplyGravity). */
	UPROPERTY(Transient)
	FVector KnockbackVelocityXY = FVector::ZeroVector;

	/** Time constant (s) for the exponential decay of horizontal knockback (~0.25s feels like a shove, not a slide). */
	static constexpr float KnockbackDecayTime = 0.18f;

	// --- ADR 0008 (Docs/Architecture/0008-hover-enemy-pursuit-reachability-modes.md): reachability-based pursuit
	// mode — RETIRED 2026-08-21 (ADR 0009 P1) now that the S3 hover window is live. The mode-switching Seek3D
	// escape (straight-line XY beeline + an incrementally-climbing altitude override, opened only when the 2D flow
	// couldn't route or the enemy stalled) never actually shipped past its 2026-08-15 emergency lockout (its
	// bEnableSeek3D gate defaulted OFF and stayed that way until removal) — ADR 0009's player-centred 3D window is the
	// structural replacement the ADR 0008 comment already called out, and it needs no mode switch: SeekZ is just
	// ANOTHER vertical target ApplyGravity's existing spring already max()s against every pass (see SeekTargetZ/
	// bSeekTargetZValid below), with the window as the only remaining source. The judgment core (FFPSRPursuitState,
	// FPSREnemyPursuit.h) and its EditDefaultsOnly hysteresis knob (SeekModeMinHold below) are PRESERVED DORMANT —
	// not called from TickServerMovement any more, but left unit-tested (FPSRoguelite.Enemy.Pursuit) and reset on
	// every Activate() — ADR 0009 explicitly leaves the mode/escalation state machine as reusable "재활용 여지" if a
	// future need for an explicit escape mode (rather than a passive Z target) resurfaces. All tuning that WAS
	// climb/stall-specific (StallTime/StallMinMove/ClimbStep/ClimbCeiling/ClearDecayTime) had no consumer left once
	// the Tick() call was removed and is deleted with it. ---

	/** Hysteresis floor (seconds) a pursuit mode is held before it may transition out again — ModeMinHold
	 *  (invariant 6: no tick-to-tick mode flapping). Preserved DORMANT alongside FFPSRPursuitState (see the block
	 *  comment above) — currently unread by any live code path (PursuitState.Tick is no longer called), kept for
	 *  the same "재활용 여지" reason as the state machine it configures. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Seek3D")
	float SeekModeMinHold = 1.0f;

	/** Per-instance altitude-band knobs (cm above the target player's Z) — sampled uniformly on Activate when Max >
	 *  Min (HoverHeightMin/Max's exact pattern), else SeekAltitudeAbovePlayerMin is used directly. UNCONSUMED since
	 *  the ADR 0008 Seek3D retirement above (nothing currently reads CurrentSeekAltitude — the S3 window's SeekZ is
	 *  a single gradient-derived target, not a per-instance band over it). Preserved per-instance anti-clumping
	 *  variety, ADR §무리 분산 candidate: a future pass could fold CurrentSeekAltitude into how SeekZ is consumed
	 *  (e.g. an offset applied where AFPSREnemyBase reads Ctx.SeekZ) without re-adding archetype data. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Seek3D", meta = (ClampMin = "0.0"))
	float SeekAltitudeAbovePlayerMin = 80.0f;
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Seek3D", meta = (ClampMin = "0.0"))
	float SeekAltitudeAbovePlayerMax = 220.0f;

	/** Server-only: this instance's sampled altitude-band value, from [SeekAltitudeAbovePlayerMin, Max] on Activate.
	 *  See that UPROPERTY pair's comment — currently unconsumed, same reason. */
	UPROPERTY(Transient)
	float CurrentSeekAltitude = 0.0f;

	/** Server-only: the ADR 0008 pursuit judgment core (Flow/Seek3D mode, stall window, climb escalation) — pure
	 *  data, DORMANT since the ADR 0008 retirement above (no longer Tick()ed from TickServerMovement). NOT a
	 *  UPROPERTY: FFPSRPursuitState is a plain, UObject-independent struct by design (so it's constructible/testable
	 *  with no engine dependency at all — see FPSREnemyPursuit.h), which UHT can't reflect. Reset() still runs on
	 *  every Activate (pool-reuse hygiene, cheap even dormant) so a reactivation of this state machine would start
	 *  clean rather than needing its own reset wiring added first. */
	FFPSRPursuitState PursuitState;

	/** Server-only: this pass's vertical seek target for ApplyGravity's v2 spring, meaningful only while
	 *  bSeekTargetZValid. Written straight from Ctx.SeekZ/Ctx.bSeekValid (ADR 0009 S3 — UFPSRFlowFieldSubsystem::
	 *  QueryFlow's Window3D answer, forwarded by the spawn subsystem into FFPSRServerMoveContext); the old ADR 0008
	 *  computation (TargetLocation.Z + CurrentSeekAltitude + PursuitState.ClimbOffset) is retired along with the
	 *  Seek3D mode switch above. ApplyGravity's v2 path still takes max(terrain-relative target, this) so a window
	 *  seek target never gets glided BELOW even when the terrain sampler also succeeds — see ApplyGravity. Written
	 *  by TickServerMovement, consumed the SAME pass by ApplyGravity (called from within TickServerMovement) —
	 *  never stale across passes. */
	UPROPERTY(Transient)
	float SeekTargetZ = 0.0f;
	UPROPERTY(Transient)
	bool bSeekTargetZValid = false;

	/** Server-only: whether THIS pass's horizontal movement sweep hit a riser/wall (the step-up branch (b) in
	 *  TickServerMovement). DORMANT since the ADR 0008 retirement above — FFPSRPursuitState::Tick (its only reader,
	 *  as bForwardBlocked) is no longer called, so this is presently write-only. Kept as the same "막힘" signal a
	 *  reactivated pursuit judgment would need, rather than removed and re-added later. */
	UPROPERTY(Transient)
	bool bLastForwardBlocked = false;

	// --- Authored exit path (C1) — server-only. Guides enemies spawned inside a structure (pipe/box) out to its
	// mouth along designer waypoints before flow-field player-chase takes over, so they never jam inside concave
	// geometry the flow-field can't path out of. ---

	/** XY distance (cm) within which the current waypoint counts as reached (advance to the next). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Movement")
	float ExitWaypointReachRadius = 80.0f;

	/** Safety: abandon a stalled exit path after this many seconds without reaching the current waypoint (misplaced /
	 *  blocked waypoint) and hand off to the flow-field, so a bad path can never soft-lock an enemy. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Movement")
	float ExitPathTimeout = 15.0f;

	/** Server-only: remaining authored waypoints (world space). Empty once handed off to the flow-field. */
	UPROPERTY(Transient)
	TArray<FVector> ExitPath;

	/** Server-only: index of the current target waypoint within ExitPath. */
	UPROPERTY(Transient)
	int32 ExitPathIndex = 0;

	/** Server-only: true while still following the exit path. */
	UPROPERTY(Transient)
	bool bFollowingExitPath = false;

	/** Server-only: seconds since the last waypoint advance (stall timer for ExitPathTimeout). */
	UPROPERTY(Transient)
	float ExitPathElapsed = 0.0f;

	// --- Animation (U20 domain C) ---

	/** Data-driven procedural-mesh render/animation backend for this archetype. NULL (the default) = the anim driver
	 *  is DORMANT (no scalar written) so the current render is untouched. Content assigns a
	 *  UFPSREnemyAnimProfile_Proc to enable state-driven animation. Instanced/polymorphic (no central switch). */
	UPROPERTY(EditDefaultsOnly, Instanced, Category = "FPSR|Enemy|Anim")
	TObjectPtr<UFPSREnemyAnimProfile> AnimProfile;

	/** Seconds the Attack one-shot plays before the next movement pass may revert it to Walk/Idle. Gameplay data, so
	 *  it lives on the ACTOR (not the cosmetic AnimProfile, which can be null and is skipped on a dedicated server) —
	 *  the SERVER LIFECYCLE hold (AttackAnimHoldUntil, gating TickServerMovement's walk/idle branch in
	 *  ServerTickAttack/TickServerMovement) reads this SAME value, not a separate one. Also feeds SetAnimState's
	 *  duration-derived PlayRate default (1 / this) for an Attack call that doesn't pass an explicit rate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Enemy|Anim", meta = (ClampMin = "0.05"))
	float AttackAnimHoldSeconds = 0.4f;

	/** Seconds the Death one-shot plays before the pooled actor may actually be released back to the pool. Same
	 *  reasoning as AttackAnimHoldSeconds: gameplay data on the actor because the SERVER LIFECYCLE (delaying the
	 *  actual pool-return so the death pose is visible instead of instantly hidden — UFPSREnemySpawnSubsystem::
	 *  BeginDying/SweepDyingEnemies) reads it too, via GetDeathDwellSeconds(). Also feeds SetAnimState's
	 *  duration-derived PlayRate default (1 / this) for a Death call with no explicit rate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Enemy|Anim", meta = (ClampMin = "0.05"))
	float DeathDwellSeconds = 0.8f;

	/** Current cosmetic animation state (not replicated). */
	EFPSRAnimState CurrentAnimState = EFPSRAnimState::Idle;

#if !UE_BUILD_SHIPPING
	/** Debug canary pin (FPSR.Enemy.ForceAnimState <state> 1): while set, SetAnimState writes nothing, so the state
	 *  the canary pushed survives instead of being re-derived on the next net update. Debug-only isolation aid —
	 *  never set by gameplay, and compiled out of shipping along with DebugForceAnimState itself. */
	bool bDebugAnimPinned = false;
#endif

	/** Quantized walk-speed bucket of the last applied state (so playrate is re-written only on a bucket change). */
	int32 CurrentSpeedBucket = -1;

	/** World seconds at which the CURRENT one-shot state was last applied, or -1 while no one-shot is running. Read
	 *  only by SetAnimState's re-entry guard: a one-shot must not rewind to frame 0 while it is still playing. The
	 *  material derives its own progress from the CPD EnterTime slot, so this is a CPU-side mirror of that stamp,
	 *  not a second source of truth. Reset by Activate() / the client unhide reset alongside CurrentAnimState. */
	float AnimOneShotEnterTime = -1.0f;

	/** Cycle length (seconds) of the one-shot currently running — captured WHEN IT WAS APPLIED, not recomputed from
	 *  the incoming call's rate. Those differ the moment two call sites drive the same state at different rates (the
	 *  planned ranged charge tell enters Attack at 1/RangedChargeTime while the client proximity tell would re-assert
	 *  it at the default 1/AttackAnimHoldSeconds), and judging "is it still playing?" with the WRONG length rewinds a
	 *  long clip early. -1 while no one-shot is running. */
	float AnimOneShotCycleSeconds = -1.0f;

	/** Per-actor animation phase offset (0..1, set once on Activate from the actor id) so the swarm doesn't lockstep. */
	float AnimPhase = 0.0f;

	/** Server+client: Health value HandleHealthChangedForHitFlash last observed — the ONLY way that handler can tell
	 *  a real damage edge (NewHealth < this) apart from ResetForReuse()'s broadcast of the same delegate on pool
	 *  reuse (Health snaps 0 -> MaxHealth there, an INCREASE). Starts at -1 (below any real Health) so a fresh actor's
	 *  very first broadcast is never misread as a decrease; resynced to the post-reset value in Activate() so a
	 *  reused actor's first real hit this life is judged against that life's own starting Health, never a stale
	 *  prior-life one. */
	float LastHealthForHitFlash = -1.0f;

	/** Client-only: last replicated location + world time, to derive movement speed for the walk/idle state. */
	FVector LastRecvLocation = FVector::ZeroVector;
	float LastRecvTime = -1.0f;
};
