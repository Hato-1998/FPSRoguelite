// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Pawn.h"
#include "GameplayTagContainer.h"
#include "Enemy/FPSRVATAnimParams.h"
#include "FPSREnemyBase.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;
class UFPSREnemyHealthComponent;
class UWidgetComponent;
class AFPSRCharacter;
class AFPSRPlayerController;
class APlayerController;
class UFPSREnemyAnimProfile;
class UMaterialInstanceDynamic;

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

/** Lightweight swarm enemy (P1 test version: manual chase steering, engine cube placeholder).
 *  P2 replaces movement with flow-field + pooling. NOT GAS-based. */
UCLASS()
class FPSROGUELITE_API AFPSREnemyBase : public APawn
{
	GENERATED_BODY()

public:
	AFPSREnemyBase();

	/** Server: reactivate a pooled enemy at Location (unhide, enable collision, reset health, randomize move speed).
	 *  Virtual so archetypes (e.g. ranged) can reset their own per-life state on reuse. */
	virtual void Activate(const FVector& Location);

	/** Server: deactivate and return to dormant pool state (hide, disable collision, net dormant). Virtual so
	 *  archetypes can release per-life state (e.g. a ranged enemy clears its warning + concurrency token) on EVERY
	 *  teardown path (pool release, death, kill-Z recycle all route through here). */
	virtual void Deactivate();

	/** Server: move along MoveDirection (XY world dir; magnitude ignored, normalized internally) at
	 *  CurrentMoveSpeed * ScaledDeltaSeconds. No-op if MoveDirection is ~zero. Driven by the enemy
	 *  movement subsystem's batched pass (flow-field + separation). ScaledDeltaSeconds is the real delta
	 *  times this enemy's LOD stride so throttled enemies still cover the right distance.
	 *  FaceDirection is what the enemy TURNS to face (XY) — the direction to the player, NOT MoveDirection:
	 *  at StopDistance MoveDirection is separation-only and jitters, which would spin the enemy in place. */
	void TickServerMovement(const FVector& MoveDirection, const FVector& FaceDirection, float ScaledDeltaSeconds);

	/** Server: assign an authored exit path (world-space waypoints) the enemy follows OUT of its spawn structure
	 *  (e.g. a pipe/box that the flow-field can't path out of) before reverting to flow-field player-chase at the
	 *  final waypoint. Empty = no path (immediate chase). Set right after Activate by the spawn subsystem from the
	 *  selected spawn point; cleared on Deactivate / overwritten on the next reuse. (C1) */
	void SetExitPath(const TArray<FVector>& InWaypoints);

	/** Server: true while the enemy is still following its authored exit path (not yet handed off to the flow-field). */
	bool IsFollowingExitPath() const { return bFollowingExitPath; }

	/** Server: if following the exit path, advance past any reached waypoint and output the unit XY steer direction to
	 *  the current target (returns true). Returns false once the path is exhausted or abandoned (stall timeout) — the
	 *  caller then steers via the flow-field. ScaledDeltaSeconds drives the stall safety timer. */
	bool ConsumeExitPathSteering(const FVector& MyLocation, float ScaledDeltaSeconds, FVector& OutDir);

	/** Distance at which the enemy stops advancing toward a player (used by the movement subsystem). */
	float GetStopDistance() const { return StopDistance; }

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

protected:
	virtual void BeginPlay() override;

	/** S4: unregister from the enemy readability-metrics registry (UFPSREnemyMetricsSubsystem). Pooled enemies are
	 *  hidden/DORM_DormantAll, never destroyed (see Deactivate) — EndPlay only fires once per actor's real lifetime
	 *  (level teardown / PIE end), mirroring BeginPlay's once-per-lifetime Register. CSV-gated (see .cpp); a no-op
	 *  in Shipping. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Server: ground-follow + gravity each movement update — a single down-trace snaps the enemy to the floor
	 *  (slopes/steps within GroundSnapTolerance) or lets it fall under gravity off a ledge / after a high spawn,
	 *  so enemies never float and rooftop-spawned enemies drop down before chasing. */
	void ApplyGravity(float ScaledDeltaSeconds);

	UFUNCTION()
	void HandleDeath(AActor* DeadActor, AActor* Killer);

	/** Server + clients: force the world-space health-bar widget (a BP-added UWidgetComponent) to exist now — it can
	 *  otherwise be created lazily on first render, after BeginPlay, leaving the BP bind on a null widget — then fire
	 *  OnHealthBarReady so the BP binds it to the health component (A1/B20). Runs on clients too: the bar is a client
	 *  visual and OnHealthChanged is client-fired via OnRep_Health (B12). The widget + health component persist across
	 *  pooling (the actor is reused, not destroyed), so this once-per-lifetime bind stays valid for every reuse. */
	void InitHealthBarWidget();

	/** BP hook (fired by InitHealthBarWidget): the BP does GetUserWidgetObject -> Cast(WBP_EnemyHealthBar) ->
	 *  InitHealthComp(GetHealthComponent()) so the bar/floating-damage widget binds OnHealthChanged. */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Enemy")
	void OnHealthBarReady();

	/** Reset exit-path follow state (on Deactivate / before a new SetExitPath). */
	void ClearExitPath();

	// --- Animation (U20 domain C) — cosmetic VAT state driver. DORMANT (zero cost) until an AnimProfile is assigned
	//     to the archetype. State source: authority (standalone / listen-server host) = the server batch pass below;
	//     clients = the replicated transform (PostNetReceiveLocationAndRotation). Never replicated (Performance §5). ---

	/** Set the current animation state (+ explicit playrate: 1.0 normal, speed-scaled for walk, 0.0 to FREEZE the clip
	 *  for distance LOD). Event-driven: a no-op when the state and quantized playrate bucket are unchanged, and a no-op
	 *  entirely when no AnimProfile is assigned or on a dedicated server (no local rendering). Applies via the profile. */
	void SetAnimState(EFPSRAnimState NewState, float PlayRate = 1.0f);

	/** Client: derive the animation state from the replicated transform when new location data arrives (walk/idle from
	 *  position delta, a melee-attack tell from proximity to the nearest local player, distance LOD freeze). Runs only
	 *  off-authority; the authority drives state from its server movement/attack pass instead. */
	virtual void PostNetReceiveLocationAndRotation() override;

	/** Bound to the health component's OnDeathCosmetic (client death edge) — enters the Death animation state. */
	UFUNCTION()
	void HandleDeathCosmetic();

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Enemy")
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Enemy")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Enemy")
	TObjectPtr<UFPSREnemyHealthComponent> HealthComponent;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy")
	float MoveSpeed = 250.0f;

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

	/** Gravity acceleration (cm/s^2) applied while airborne (fall off ledges / land after a high spawn). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Movement")
	float GravityAccel = 1800.0f;

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

	/** Data-driven VAT render/animation backend for this archetype. NULL (the default) = the anim driver is DORMANT
	 *  (no MID created, no scalar written) so the current cube/VAT render is untouched. Content assigns a
	 *  UFPSREnemyAnimProfile_VAT (Stage 3) to enable state-driven animation. Instanced/polymorphic (no central switch). */
	UPROPERTY(EditDefaultsOnly, Instanced, Category = "FPSR|Enemy|Anim")
	TObjectPtr<UFPSREnemyAnimProfile> AnimProfile;

	/** Per-actor MID lazily created by the AnimProfile on first state application (reused across the actor's life). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AnimMID;

	/** Current cosmetic animation state (not replicated). */
	EFPSRAnimState CurrentAnimState = EFPSRAnimState::Idle;

	/** Quantized walk-speed bucket of the last applied state (so playrate is re-written only on a bucket change). */
	int32 CurrentSpeedBucket = -1;

	/** Per-actor animation phase offset (0..1, set once on Activate from the actor id) so the swarm doesn't lockstep. */
	float AnimPhase = 0.0f;

	/** Client-only: last replicated location + world time, to derive movement speed for the walk/idle state. */
	FVector LastRecvLocation = FVector::ZeroVector;
	float LastRecvTime = -1.0f;
};
