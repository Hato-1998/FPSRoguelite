// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Pawn.h"
#include "FPSREnemyBase.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;
class UFPSREnemyHealthComponent;
class UWidgetComponent;
class AFPSRCharacter;
class AFPSRPlayerController;

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
	 *  times this enemy's LOD stride so throttled enemies still cover the right distance. */
	void TickServerMovement(const FVector& MoveDirection, float ScaledDeltaSeconds);

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

protected:
	virtual void BeginPlay() override;

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
	 *  fall under gravity. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Movement")
	float GroundSnapTolerance = 60.0f;

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
};
