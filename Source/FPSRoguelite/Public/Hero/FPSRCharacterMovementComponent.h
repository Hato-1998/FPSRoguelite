// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/CharacterMovementComponent.h"
#include "FPSRCharacterMovementComponent.generated.h"

class UCurveFloat;

/**
 * Sub-modes used with MOVE_Custom. Exactly one is needed: the slide stays in MOVE_Walking (ADR 0001 axis 1) because it
 * is still ground movement, whereas hanging on a wall has neither gravity nor a floor and would reuse nothing from
 * PhysWalking.
 */
UENUM(BlueprintType)
enum EFPSRCustomMovementMode : uint8
{
	/** CustomMovementMode's engine default value; never entered deliberately. */
	CMOVE_None		UMETA(Hidden),

	/** Attached to a wall: climbing while the input pushes into it, slipping down when it doesn't, jumping off it. */
	CMOVE_WallHang	UMETA(DisplayName = "Wall Hang"),
};

/**
 * Player locomotion component — the SINGLE OWNER of the player's movement state (ADR 0001 invariant 1).
 *
 * Nothing outside this component decides what locomotion state the player is in. The character forwards INPUT INTENT
 * (crouch pressed/released) and everything else — firing, the body AnimBP, the HUD — only QUERIES this component
 * through the small read-only surface below. That one-way dependency is what keeps a new locomotion state from
 * rippling into the weapon code (invariant 4).
 *
 * Prediction (invariant 2): every state here is client-predicted through the engine's saved-move replay. The client
 * simulates immediately and the server re-runs the same code from the same inputs; a correction rewinds and replays.
 * There is NO "send an RPC and wait for the server to move you" path — that was the old dash, and it rubber-banded at
 * 46ms ping (see ADR 0001, rejected option ㉰).
 *
 * Axis-1 decision (ADR 0001, deferred to implementation and resolved here): SLIDE stays in MOVE_Walking and only
 * overrides the speed/friction hooks, because sliding is ground movement that still needs the engine's floor
 * tracking, step-up, slope handling and wall sliding. WALL-HANG uses MOVE_Custom (CMOVE_WallHang) instead, since it
 * has neither gravity nor a floor and would reuse nothing from PhysWalking.
 *
 * Network cost: ZERO custom compressed flags. Both states are entered from intents the engine already puts in every
 * move packet — bWantsToCrouch for the slide (the design calls for "crouch input while running = slide"), and the
 * movement input direction plus bPressedJump for the wall. Their derived state (timers, entry speed, the wall being
 * held) rides along in FSavedMove_FPSR for local replay only and is never sent.
 */
UCLASS()
class FPSROGUELITE_API UFPSRCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UFPSRCharacterMovementComponent();

	//~ Read-only surface for firing / AnimBP / HUD. This is the ENTIRE public contract (ADR 0001 module boundary):
	//~ callers never ask "which state am I in?", they ask what they actually need to know.

	/** True when the current locomotion state permits firing. Wall-hang is the one state that returns false — both
	 *  hands are on the wall. Everything else (stand/run/crouch/slide/airborne) allows it. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	bool CanFireInCurrentState() const;

	/** Weapon-spread multiplier contributed by the current locomotion state (1.0 = no change). Invariant 5: this is a
	 *  MULTIPLIER ONLY — the heat system remains the single owner of the final spread calculation. States are resolved
	 *  exclusively (airborne > slide > crouch) rather than multiplied, so the value can't compound unexpectedly. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	float GetSpreadMultiplier() const;

	/** True while sliding. For the body AnimBP and the HUD — presentation only; nothing may drive state off this. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	bool IsSliding() const { return bIsSliding; }

	/** Stance as a weight rather than a switch: 0 fully standing, 1 fully crouched, moving between them at a constant
	 *  rate over StanceBlendDuration. The camera, the walk-speed cap and (later) the AnimBP's stance poses all read
	 *  THIS, which is what keeps the view, the body and the animation on one clock instead of three.
	 *  Raw and linear on purpose — shaping belongs to whoever consumes it.
	 *  Invariant 6: this is a weight to blend WITH, never a state to decide FROM. "Is the player crouched" is
	 *  IsCrouching(); a half-finished blend is not a stance. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	float GetStanceBlend() const { return StanceBlend; }

	/** Same idea for the slide pose: 0 not sliding, 1 fully sliding, over SlideBlendDuration.
	 *  Follows IsSlidingForDisplay(), so it is right on every role — the exact local state on the owning client and the
	 *  server, the replicated copy on a simulated proxy.
	 *  NOTE for animation work: on a proxy that copy is a net-update snapshot, so a slide that both starts and ends
	 *  between two updates (a jump-cancel is two frames) never raises this weight at all. GetSlideVisualSerial() is
	 *  what catches those; GetStanceBlend() has no such limit. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	float GetSlideBlend() const { return SlideBlend; }

	/** "Is this character sliding" answered correctly on EVERY machine, unlike IsSliding().
	 *
	 *  bIsSliding is derived locally and, worse, is rewound by FSavedMove_FPSR::PrepMoveFor during a correction replay,
	 *  so it is the wrong thing to hand to the network. bSlidingVisual is a separate server-authored copy that exists
	 *  only to be looked at. Invariant 1 is intact: the movement component still owns the state; this is a picture of
	 *  it, and nothing is allowed to decide movement from it. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	bool IsSlidingForDisplay() const;

	/** Bumped once per slide ENTRY, replicated alongside the flag.
	 *
	 *  Replication sends the value that is current at each net update, so a slide that starts and ends between two
	 *  updates (a jump-cancel is two frames) collapses to no visible change and a teammate never sees it at all. The
	 *  serial survives that: the receiver sees a number it has not seen before and knows a slide happened even though
	 *  the flag it was carrying is already false again. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	uint8 GetSlideVisualSerial() const { return SlideVisualSerial; }

	/** Outward wall normal for the wall the character is holding. Local physics value: valid on the owner and the
	 *  server, zero on a simulated proxy. For animation use GetWallYawForDisplay(). */
	FVector GetWallNormal() const { return WallNormal; }

	/** Wall facing as a yaw in degrees, valid on every machine.
	 *
	 *  A proxy receives the custom movement mode (so IsOnWall() works there) but nothing that says WHICH wall, and the
	 *  normal cannot be recovered from the mode. Quantised to a byte: 1.4 degrees per step, under a degree of error
	 *  after rounding, which is far below what the pose itself is authored to.
	 *  Deliberately NOT fed back into WallNormal — the physics keeps its exact local value, because letting a
	 *  quantised number into the wall-stick would need its own correctness pass. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	float GetWallYawForDisplay() const;

	/** Which shoulder the wall pose presents to the wall: +1 or -1, chosen once when the hold begins.
	 *
	 *  The wall clip is authored side-on, so the body's target yaw is the wall yaw plus or minus that authored angle.
	 *  Picking the nearer side every frame would flip the whole body whenever the player's view crosses the midpoint,
	 *  so the choice is latched at entry and replicated with the yaw. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	float GetWallSideSign() const { return (WallSideSign < 0) ? -1.0f : 1.0f; }

	/** The authored side angle of the wall pose. Exposed because the AnimBP needs the SAME number this component used
	 *  to latch the side — two copies would eventually disagree and put the body on the wrong shoulder. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	float GetWallPoseSideAngle() const { return WallPoseSideAngle; }

	/** How far the CURRENT stance change has run, 0 at the moment it started to 1 when it settles. Distinct from
	 *  GetStanceBlend(): a change interrupted halfway restarts this at 0 from wherever the blend had reached, which is
	 *  what lets the camera and the speed cap ease from their present values instead of jumping to a fresh curve. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	float GetStanceTransitionProgress() const;

	/** True while hanging on a wall. Derived from the movement mode rather than kept in a flag of its own: the engine
	 *  already ships the mode in every move packet and restores it on a correction, so a second copy could only ever
	 *  drift out of agreement with it. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	bool IsOnWall() const { return (MovementMode == MOVE_Custom) && (CustomMovementMode == CMOVE_WallHang); }

	/** Planar speed (cm/s). For the movement debug readout. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	float GetPlanarSpeed() const;

	/** Short name of the current locomotion state ("Run" / "Crouch" / "Slide" / "Air"). Debug readout only. */
	FString GetLocomotionStateName() const;

	//~ Walk-speed layers. Everything that wants to affect how fast the player walks PUSHES its layer in through one of
	//~ these; RefreshWalkSpeedCap() is the ONLY thing that ever writes MaxWalkSpeed (see its comment for why). This
	//~ mirrors the direction GAS already pushes numbers in (ADR 0001 module boundary) — the component still knows
	//~ nothing about weapons, cards or the DBNO state, only about the numbers they hand it.

	/** The character's authored baseline (AFPSRCharacter::BaseWalkSpeed). Pushed once on construction; the property
	 *  deliberately stays on the character so its Blueprint override, the ADS sway reference and any GameplayEffect
	 *  referencing it are untouched — owning the WRITE is what invariant 1 asks for, not owning the field. */
	void SetAuthoredBaseWalkSpeed(float InSpeed);

	/** Baseline contributed by the equipped weapon (its DataAsset's WalkSpeed); 0 = use the authored baseline.
	 *  Pushed by the inventory on every equip, on the server and on each client's OnRep alike. */
	void SetLoadoutWalkSpeed(float InSpeed);

	/** Card/meta multiplier, from the CombatSet's MoveSpeedMultiplier attribute. Multiplies whichever baseline is in
	 *  force, so a speed card scales the equipped weapon's speed rather than reverting it to the authored one. */
	void SetMoveSpeedMultiplier(float InMultiplier);

	/** Downed (DBNO) locomotion override — clamps to DownedWalkSpeed regardless of the other layers. Being a LAYER
	 *  rather than a direct write is what stops a speed card landing mid-DBNO from standing the player back up. */
	void SetDownedLocomotion(bool bInDowned);

	/** The authored baseline as last pushed. For callers that need the un-multiplied reference speed (e.g. the ADS
	 *  sway's "am I moving" normalisation) rather than the current cap. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	float GetBaseWalkSpeed() const { return AuthoredBaseWalkSpeed; }

	//~UCharacterMovementComponent
	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual float GetMaxSpeed() const override;

	/** Overridden only to carry the FPSR.Debug.PlayerSpeedScale playtest multiplier — acceleration has to move with
	 *  the speed cap or a scaled-up player just feels slow to get going. No behaviour change at scale 1. */
	virtual float GetMaxAcceleration() const override;

	/** Takes over velocity entirely while sliding — see the implementation for why MaxAcceleration is deliberately
	 *  left alone instead of being zeroed. */
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;

	/** Clamps descent to MaxFallSpeed after the engine's falling step. */
	virtual void PhysFalling(float deltaTime, int32 Iterations) override;

	/** Hands the falling step the AIR-STRAFE acceleration instead of the engine's AirControl-scaled one. The engine
	 *  does not only use this to reach CalcVelocity — after a mid-air wall hit it feeds the same vector to
	 *  LimitAirControl() as the re-acceleration magnitude, so returning the raw (MaxAcceleration-sized) input here
	 *  would make grazing a wall accelerate the player far harder than the model intends. */
	virtual FVector GetFallingLateralAcceleration(float DeltaTime) override;

	/** Wall-hang physics. The engine's PhysCustom only fires a Blueprint event, so a custom mode has to move the
	 *  capsule itself; this follows PhysFlying's shape (no gravity, no floor) rather than PhysWalking's. */
	virtual void PhysCustom(float deltaTime, int32 Iterations) override;

	/** Single funnel for entering and leaving the wall. Every exit — jump, slip, wall destroyed, timeout, freeze —
	 *  goes through a movement-mode change, so putting the bookkeeping here is what stops one route from forgetting it. */
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

	//~ Overriding one of an overload set hides the rest; the 1-argument DoJump forwards to this one, so keep both
	//~ visible for anything that calls it through this class.
	using UCharacterMovementComponent::DoJump;

	/** Adds the push away from the wall on top of the engine's jump, and re-checks the wall first — see the
	 *  implementation for why a frame-old wall is not safe to launch off. */
	virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;

	/** Ground-only crouch. The engine's version also allows crouching while FALLING; this design forbids crouch and
	 *  slide in the air, and returning false here additionally makes the engine un-crouch automatically the moment the
	 *  player leaves the ground (so jumping out of a crouch stands you up instead of keeping a crouched capsule). */
	virtual bool CanCrouchInCurrentState() const override;

	//~ Both stances read the same GroundAccelElapsed timer but off DIFFERENT curves, so a stance change has to move the
	//~ timer to the equivalent point on the new curve — otherwise standing up from a long crouch-walk lands at the far
	//~ end of the standing curve and the speed cap jumps straight to maximum.
	virtual void Crouch(bool bClientSimulation = false) override;
	virtual void UnCrouch(bool bClientSimulation = false) override;

	/** Allow jumping OUT of a crouch or a slide. The engine refuses to jump whenever bWantsToCrouch is set and expects
	 *  the player to un-crouch first; this design wants jump to break out of both immediately. Only the crouch term is
	 *  lifted — jump count and hold-time rules stay exactly as the engine defines them. */
	virtual bool CanAttemptJump() const override;
	//~End UCharacterMovementComponent

	/** Force the slide to end now, keeping current velocity. Used by the global run-freeze so a slide in progress can't
	 *  carry the player across the frozen card screen (invariant 8 — gating the START is not enough). Safe to call when
	 *  not sliding. */
	void StopSliding();

	/** Force the wall-hang to end now, dropping into a normal fall. Used by the global run-freeze for the same reason
	 *  as StopSliding (invariant 8 — gating the START of a state is not enough). Safe to call when not on a wall. */
	void StopWallHang();

	/** How far the wall clip's body is turned from facing the wall, degrees. The shipped pose stands side-on, so the
	 *  body's target is the wall yaw plus this (times the latched side sign) rather than the wall yaw itself.
	 *  ⚠️ Bounded by the AnimBP's RootYawOffsetMax (90): at 86 the upper body has almost no twist budget left, so a
	 *  player looking the other way along the wall clamps. Raising this makes that worse, not better. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall",
		meta = (ClampMin = "-90.0", ClampMax = "90.0"))
	float WallPoseSideAngle = 86.0f;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;

protected:
	// --- Slide tuning (invariant 9: data, not C++ constants — designers tune these per-hero in the BP defaults) ---

	/** Minimum planar speed required to ENTER a slide. Below this, the crouch input is just a crouch. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide", meta = (ClampMin = "0.0"))
	float SlideMinEnterSpeed = 680.0f;

	/** Planar speed at which an ongoing slide ends (it has decayed into a crouch-walk). Kept strictly below
	 *  SlideMinEnterSpeed so a slide can't immediately re-trigger the instant it ends while crouch is still held. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide", meta = (ClampMin = "0.0"))
	float SlideMinSpeed = 380.0f;

	/** Entry impulse: current planar speed is multiplied by this on the frame the slide starts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide", meta = (ClampMin = "1.0"))
	float SlideEnterSpeedMultiplier = 1.5f;

	/** How fast the ENTRY IMPULSE alone may take the player. Deliberately lower than SlideMaxSpeed: with a single
	 *  ceiling, jump-cancelling and re-sliding kept re-applying the multiplier and ratcheted the speed up every cycle
	 *  (900 -> 1350 -> cap) for free. This only ever RAISES speed toward the limit — entering already faster than it
	 *  (downhill momentum carried through a jump) keeps the higher speed rather than being cut down to it.
	 *
	 *  This is a CEILING, not the slide speed: the impulse is (current speed x SlideEnterSpeedMultiplier), so what a
	 *  slide actually starts at is derived from how fast the player was already going. That is why the per-weapon
	 *  loadout only authors a WALK speed — 600 walk gives 900 either way (600 x 1.5 never reaches this), and a 700
	 *  walk gives 1000. Authoring a second per-weapon number would mean two values to keep in agreement, and would
	 *  make the slide depend on WHEN the equip replicated instead of on the velocity both machines already agree on.
	 *  Raised 900 -> 1000 on 2026-07-29 for that reason; note it also lengthens slides entered between 600 and 1000
	 *  cm/s (speed cards, downhill momentum, a wall-jump landing). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide", meta = (ClampMin = "0.0"))
	float SlideMaxEntrySpeed = 1500.0f;

	/** Hard ceiling on slide speed from ANY source (entry impulse, slope acceleration, carried momentum). Speeds above
	 *  SlideMaxEntrySpeed have to be earned — typically by accelerating down a slope. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide", meta = (ClampMin = "0.0"))
	float SlideMaxSpeed = 2100.0f;

	/** Deceleration (cm/s²) applied while sliding. This is the "속도 감소 곡선" in its simplest form; assign
	 *  SlideSpeedCurve below for a shaped decay instead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide", meta = (ClampMin = "0.0"))
	float SlideBrakingDeceleration = 1350.0f;

	/** Ground friction while sliding. MUST stay low or there is no slide to see: PhysWalking feeds GroundFriction
	 *  (engine default 8) into the braking math, where BrakingFrictionFactor doubles it — an effective 16, which decays
	 *  900 -> 250 cm/s in roughly 0.08s. At 0 the engine switches to constant deceleration, so SlideBrakingDeceleration
	 *  alone shapes the decay and the numbers become predictable (900 -> 250 at 900 cm/s² = ~0.72s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide", meta = (ClampMin = "0.0"))
	float SlideGroundFriction = 0.0f;

	/** How strongly a slope speeds up (downhill) or slows down (uphill) a slide, as a fraction of real gravity.
	 *  1.0 = the physical value, g * sin(angle); 0 disables slope influence entirely. One formula covers both
	 *  directions — travelling uphill simply produces a negative contribution — so no separate uphill friction rule is
	 *  needed. The result is still bounded by SlideMaxSpeed, matching how a slide tops out rather than accelerating
	 *  forever down a long hill. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide", meta = (ClampMin = "0.0"))
	float SlopeAccelerationScale = 1.5f;

	/** How strongly a slope stretches (downhill) or compresses (uphill) the slide's sense of time. The timer advances
	 *  by DeltaTime * (1 - sin(angle) * this): a gentle downhill plays the decay in slow motion, at sin(angle) =
	 *  1/this it freezes, and past that it REWINDS (rate capped by SlideSlopeTimeRecoveryCap), so the slide regains
	 *  duration — and through the curve, speed — which is then kept and spent on the flat (momentum by design).
	 *  The default 3.0 puts the freeze point at asin(1/3) ~ 19.5 degrees of straight-downhill travel, but the stretch
	 *  diverges well below it: effective duration is 1/(1 - this * sin(angle)) times the authored length — with 3.0
	 *  that is ~2x at 10 degrees and ~4.5x at 15 — so tune against that curve, not just the freeze angle. Uphill
	 *  still runs the curve out early. 0 disables slope time entirely and the slide always lasts exactly the curve's
	 *  length. Needed alongside SlopeAccelerationScale because acceleration alone can't extend a slide past its time
	 *  limit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide", meta = (ClampMin = "0.0"))
	float SlopeTimeInfluence = 3.0f;

	/** Fastest the slide timer may REWIND on a steep downhill, as a multiple of real time (1.0 = one second of
	 *  duration regained per second spent on the hill). Rewinding — not just stalling — is what hands the player a
	 *  recovered slide after a long descent instead of merely postponing the timeout at the freeze angle.
	 *  0 disables rewinding entirely and restores the old floor-at-freeze behaviour. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide", meta = (ClampMin = "0.0"))
	float SlideSlopeTimeRecoveryCap = 1.0f;

	/** Lockout after a slide ENDS before another may start. Without it, tapping crouch repeatedly re-triggers the entry
	 *  impulse over and over and the player rides a permanent speed boost. Applied on EVERY exit (released, too slow,
	 *  timed out, jumped, gate closed) so no exit route dodges it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide", meta = (ClampMin = "0.0"))
	float SlideCooldown = 0.8f;

	/** How fast a slide can be steered, in degrees per second. The slide turns toward the movement input (WASD), or —
	 *  with no input — toward wherever the pawn is facing, so it keeps tracking the camera as you look around. Only
	 *  the DIRECTION is steered; speed stays owned by SlideSpeedCurve / SlideBrakingDeceleration, which is why this is
	 *  a turn rate and not an acceleration. 0 = fully committed to the entry direction. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide", meta = (ClampMin = "0.0"))
	float SlideTurnRateDegrees = 270.0f;

	/** Time limit on a single slide. Without it a downhill slide that keeps regaining speed would never satisfy the
	 *  speed-based exit. A steep downhill defers it INDEFINITELY — SlopeTimeInfluence can stall or rewind the timer —
	 *  so unlike wall-hang (whose max-duration cap is unconditional) this is no longer invariant 7's "time bound"
	 *  branch: while the hill lasts the invariant rides its other branch, the exits re-checked every frame regardless
	 *  of slope (crouch-release, leaving the ground, the special-movement gate), and the bound resumes with the flat.
	 *  IGNORED when SlideSpeedCurve is assigned — the curve's own length becomes the limit instead, so that drawing a
	 *  5-second decay really gives a 5-second slide rather than being silently cut off here. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide", meta = (ClampMin = "0.1"))
	float SlideMaxDuration = 1.6f;

	/** Slide decay shape. X = seconds since the slide started, Y = 0..1 as a FRACTION OF THE ENTRY SPEED (so Y=1 at
	 *  X=0 holds the speed the slide began with, and it falls off from there). Normalizing against the entry speed is
	 *  what lets a fast entry — downhill speed carried through a jump — decay from ITS OWN speed instead of being
	 *  clamped to a fixed opening value, while a slow entry still can't snap upward. Values above 1 overshoot.
	 *  When assigned this drives the speed directly and SlideBrakingDeceleration is bypassed, so the shape lands as
	 *  authored instead of being approximated by a braking force. Null (the default) = constant deceleration via
	 *  SlideBrakingDeceleration — the curve is a refinement, never a requirement. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide")
	TObjectPtr<UCurveFloat> SlideSpeedCurve = nullptr;

	/** Standing -> running speed ramp. X = seconds of continuous ground movement input, Y =
	 *  0..1 as a FRACTION of MaxWalkSpeed (0 = standstill, 1 = full speed). Normalized rather than literal cm/s so the
	 *  same shape survives any speed change — a move-speed card raises MaxWalkSpeed and the whole ramp follows with no
	 *  edit. Values above 1 are allowed and overshoot. Null = the engine's flat MaxWalkSpeed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Ground")
	TObjectPtr<UCurveFloat> GroundSpeedCurve = nullptr;

	/** Same idea while CROUCHED: Y = 0..1 as a fraction of MaxWalkSpeedCrouched. Without it a crouch-walk snaps
	 *  straight to that flat value and never ramps. Because both curves are normalized, this can point at the SAME
	 *  asset as GroundSpeedCurve to share one shape — each stance just multiplies by its own max speed.
	 *  Null = the engine's flat MaxWalkSpeedCrouched (previous behaviour).
	 *  Shares GroundAccelElapsed with the standing ramp, so crouching mid-run doesn't restart the acceleration. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Ground")
	TObjectPtr<UCurveFloat> CrouchSpeedCurve = nullptr;

	/** Speed multiplier while moving BACKWARDS (0.75 = 450 cm/s against the default 600). Applied on top of whatever
	 *  the ground ramp produced, so it scales with cards too. Also applies to a slide that has been steered backwards.
	 *  1.0 disables it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Ground", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BackwardSpeedMultiplier = 0.75f;

	/** How far off the pawn's facing the movement input must point to count as backpedalling, in degrees.
	 *  90 (default) = the rear half only, so pure strafing keeps full speed. LOWER widens it — 60 also penalises
	 *  diagonal-back input. Higher narrows it toward straight-back only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Ground", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float BackwardAngleThresholdDegrees = 90.0f;

	/** Walk-speed cap while downed (DBNO). 0 = the downed player cannot move at all, which is the design (§2-13: a
	 *  downed player holds position and spectates an ally). Exists as a tunable rather than a literal so a future
	 *  "crawl while downed" is a data change. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Ground", meta = (ClampMin = "0.0"))
	float DownedWalkSpeed = 0.0f;

	/** Terminal fall speed in cm/s; 0 = no extra limit (the physics volume's own TerminalVelocity, engine default
	 *  4000, still applies). Exists because that volume value is per-VOLUME, so it can't express a per-character fall
	 *  speed — which is what the design asks for. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Air", meta = (ClampMin = "0.0"))
	float MaxFallSpeed = 0.0f;

	//~ Air strafing — turning in mid-air WITHOUT paying for it in speed.
	//~
	//~ The engine accelerates the whole velocity vector toward the input and then clamps the total, so any input that
	//~ disagrees with where you are already going SUBTRACTS speed; you cannot round a corner in the air without
	//~ arriving slower. These three replace that with the Quake-family model: a push is added along the input
	//~ direction only to the extent the player is NOT already travelling that way. Pushed sideways, that rotates the
	//~ velocity instead of fighting it — the magnitude barely changes, so the turn is free. Pushed forwards, there is
	//~ nothing left to add, so holding W in the air buys nothing. That asymmetry IS the technique.
	//~
	//~ Consequence to know: while falling, AirControl, AirControlBoostMultiplier and AirControlBoostVelocityThreshold
	//~ no longer do anything (they still show in the details panel), and MaxWalkSpeed no longer caps air speed —
	//~ AirStrafeMaxSpeed does.

	/** How much speed may be gained toward a direction the player is NOT already moving in. This is the difficulty
	 *  dial, and it reads directly as "how far the heading turns on the movement keys alone, with no mouse": at
	 *  600 cm/s, 120 gives about 11 degrees (authentic Quake — the mouse does the work), 300 about 30, 400 about 42
	 *  (near-automatic, and speed starts growing rather than being preserved). Turning further than this is always
	 *  possible by sweeping the mouse to keep the input perpendicular to travel — skill extends it, but is not
	 *  required to get the basic turn. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Air", meta = (ClampMin = "0.0"))
	float AirStrafeWishSpeed = 450.0f;

	/** How fast that allowance is spent (cm/s²) — the responsiveness of an air turn, and of braking with the back
	 *  key. Comparable to ground acceleration (2048), which is why it is an absolute rate rather than the Quake
	 *  formula's multiplier on the wish speed: there, changing the wish speed would silently change this too. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Air", meta = (ClampMin = "0.0"))
	float AirStrafeAcceleration = 3000.0f;

	/** Ceiling on the speed air strafing may BUILD. Speed carried in above it (a fast slide jump) is not cut by the
	 *  ceiling — the same "only ever raises, never lowers" rule as SlideMaxEntrySpeed. It can still be spent by
	 *  steering against the direction of travel, which is how a player brakes in the air; nothing bleeds it off on its
	 *  own. Kept equal to SlideMaxSpeed and WallJumpMaxSpeed so the game has one top speed rather than three that
	 *  drift apart. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Air", meta = (ClampMin = "0.0"))
	float AirStrafeMaxSpeed = 2100.0f;

	// --- Stance blending (presentation + the walk-speed cap; the stance itself still changes instantly) ---

	/** How long a full stand <-> crouch change takes to settle. The stance, the capsule and the slide entry all still
	 *  flip on the input frame — this only governs how long the camera, the walk-speed cap and the AnimBP take to
	 *  catch up, so the controls stay as responsive as they are today.
	 *  The rate is constant, so a change interrupted a third of the way through takes a third of this to undo. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Stance", meta = (ClampMin = "0.0"))
	float StanceBlendDuration = 0.15f;

	/** Same for the slide pose weight. Separate from the stance because entering a slide should read as a sharper
	 *  commitment than simply crouching. Presentation only — it feeds no speed or capsule decision. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Stance", meta = (ClampMin = "0.0"))
	float SlideBlendDuration = 0.12f;

	// --- Wall hang (ADR 0001: one state, three exits — climb / slip off / jump off) ---

	/** Height above the capsule's feet, in cm, at which the GRAB probe looks for a wall. Chest height on purpose: a
	 *  knee-high crate is not something to hang off, and probing low would grab one. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "0.0"))
	float WallGrabProbeHeight = 120.0f;

	/** Height above the capsule's feet, in cm, at which the HOLD probe re-checks the wall every frame. Deliberately
	 *  near the feet, which is what makes "the feet cleared the top" and "the wall was destroyed" the same missed
	 *  probe — one check covers reaching the top AND invariant 7's escape route. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "0.0"))
	float WallHoldProbeHeight = 25.0f;

	/** How far PAST the capsule's own radius the probe reaches, in cm. Larger values grab from further away. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "0.0"))
	float WallProbeDistance = 45.0f;

	/** Radius of the probe sphere. Keep it well below the capsule radius so the sweep starts clear of any wall the
	 *  capsule is already touching (otherwise it reports an initial overlap instead of a surface). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "1.0"))
	float WallProbeRadius = 20.0f;

	/** How steep a surface has to be to count as a wall, in degrees (90 = perfectly vertical). Below this it is a
	 *  ramp and the player should be walking or sliding up it, not hanging off it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float WallMinSteepnessDegrees = 70.0f;

	/** How squarely the movement input has to point into the wall to count as "holding W" — 1 = dead-on, 0 = anything
	 *  not pointing away. This one value decides both whether a wall can be grabbed and, once on it, whether the
	 *  player is climbing or slipping. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float WallClimbInputDot = 0.35f;

	/** Climb speed up the wall (cm/s) while the input pushes into it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "0.0"))
	float WallClimbSpeed = 390.0f;

	/** Downward speed (cm/s) the player settles to after letting go of the wall. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "0.0"))
	float WallSlipSpeed = 270.0f;

	/** How fast the vertical speed moves toward WallSlipSpeed (cm/s²). Snapping straight to it reads as the hands
	 *  coming off rather than sliding — the design word is "미끄러져", a slip. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "0.0"))
	float WallSlipAcceleration = 1350.0f;

	/** Sideways speed along the wall face (cm/s) — the "제한적 좌우 이동" the design allows during a climb. Only the
	 *  part of the input running ALONG the wall contributes, so this can never push the player off it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "0.0"))
	float WallLateralSpeed = 180.0f;

	/** Gentle pull toward the wall (cm/s) that keeps the capsule in contact, which is what keeps the hold probe
	 *  finding the surface. 0 lets the player drift off the wall on any bump. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "0.0"))
	float WallStickSpeed = 90.0f;

	/** Hard time limit on a single hang (invariant 7: every state needs a time bound or an unconditional exit).
	 *  Also caps how far one grab can climb, since climb speed is constant. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "0.1"))
	float WallHangMaxDuration = 1.5f;

	/** How long the player may keep slipping after releasing the input before the hands come off. Pressing back into
	 *  the wall within this window resumes the climb, so a momentary stutter of the key doesn't drop them. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "0.0"))
	float WallSlipMaxDuration = 0.35f;

	/** How long the speed carried into the wall survives the grab. During this window the player keeps gliding along
	 *  the wall at the speed they arrived with, and a wall jump taken inside it launches with that speed added on top —
	 *  so hitting a wall fast and reacting fast is rewarded instead of being a full stop. Both effects fade linearly to
	 *  nothing across the window. 0 disables it and the grab kills all momentum outright. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "0.0"))
	float WallEntryMomentumDuration = 0.25f;

	/** Base speed (cm/s) of the push when jumping off. Entry momentum still in hand is added to this. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "0.0"))
	float WallJumpPushSpeed = 780.0f;

	/** Where a wall jump goes: 0 = straight out along the wall normal, 1 = wherever the player is looking, 0.5 = an
	 *  even blend of the two. Looking is what makes the exit feel aimed rather than scripted; the wall's share is what
	 *  stops a player facing the wall from jumping back into it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallJumpAimBlend = 0.5f;

	/** Smallest share of the wall jump that must point AWAY from the wall, regardless of where the player looks
	 *  (0 = along the wall face, 1 = straight out). Without a floor here, looking directly into the wall cancels the
	 *  blend and the jump either goes nowhere or drives straight back into the surface. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallJumpMinOutward = 0.35f;

	/** Ceiling on the horizontal speed a wall jump can produce (push + carried momentum). Held equal to SlideMaxSpeed
	 *  and AirStrafeMaxSpeed on purpose: a player who built speed by air strafing and then jumps off a wall should not
	 *  silently lose it at the wall, so the game has ONE top speed rather than three that disagree. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "0.0"))
	float WallJumpMaxSpeed = 2100.0f;

	/** Upward speed (cm/s) of a wall jump. 0 = use the character's normal JumpZVelocity, so a jump-height card scales
	 *  the wall jump too; set it above 0 to give the wall jump its own height independent of the ordinary jump. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "0.0"))
	float WallJumpUpSpeed = 0.0f;

	/** Forward speed (cm/s) granted when a climb clears the top of the wall. Without it the capsule is still beside
	 *  the wall when the probe loses it and simply falls back down — the climb could never actually get anywhere. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "0.0"))
	float WallTopBoostForward = 260.0f;

	/** Upward speed (cm/s) granted alongside WallTopBoostForward, so the capsule rises over the lip instead of
	 *  clipping its edge on the way across. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Wall", meta = (ClampMin = "0.0"))
	float WallTopBoostUp = 260.0f;

	/** True when the given world direction points behind the pawn, per BackwardAngleThresholdDegrees. */
	bool IsDirectionBackward(const FVector& Direction) const;

	/** True when the movement INPUT points behind the pawn — the walking backpedal test. Reads Acceleration (already
	 *  predicted) against the pawn's facing, so server and client agree. */
	bool IsMovingBackward() const;

	/** True when a slide is currently travelling backwards. Judged on the slide's actual heading rather than the input,
	 *  because a slide steered around keeps going that way after the key is released. */
	bool IsSlidingBackward() const;

	/** Slope component along the current heading: sin(slope angle) projected onto the direction of travel.
	 *  POSITIVE downhill, NEGATIVE uphill, 0 on flat ground or with no heading. Derived from the walkable floor's
	 *  impact normal — its horizontal part points downhill and its length is sin(angle) — so no extra trace is needed.
	 *  Reads the floor found during the PREVIOUS move, which is a frame behind but identically so on client and
	 *  server, keeping prediction consistent. */
	float ComputeSlopeAlignment() const;

	/** Slide heading for this frame: the current heading rotated toward the steer target by at most
	 *  SlideTurnRateDegrees * DeltaSeconds. Target = movement input when there is any, otherwise the pawn's facing.
	 *  Every input (velocity, acceleration, control rotation) is already part of the predicted move, so client and
	 *  server derive the same heading. Returns a zero vector when there is no heading to rotate. */
	FVector ComputeSlideHeading(float DeltaSeconds) const;

	// --- Spread multipliers by locomotion state (invariant 5: multipliers only) ---

	/** Spread multiplier while crouching (design: ×0.8 — steadier). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Spread", meta = (ClampMin = "0.0"))
	float CrouchSpreadMultiplier = 0.8f;

	/** Spread multiplier while airborne (design: ×1.6 — LESS accurate; spread scales up). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Spread", meta = (ClampMin = "0.0"))
	float AirborneSpreadMultiplier = 1.6f;

	/** Spread multiplier while sliding. Firing mid-slide is allowed by design; this is the cost. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Spread", meta = (ClampMin = "0.0"))
	float SlideSpreadMultiplier = 1.3f;

	/** Evaluate the slide entry conditions for THIS frame. Pure read of already-predicted state (crouch intent, ground
	 *  contact, planar speed) plus the owner's action gate, so the client and the server reach the same answer from the
	 *  same inputs — which is what makes the derived slide state replay-safe. */
	bool CanEnterSlide() const;

	/** Begin the slide: apply the entry impulse and reset the elapsed timer. */
	void StartSliding();

	/** True when the owning pawn may perform special locomotion at all (not frozen for card selection, not downed).
	 *  Reads replicated state through the character, so server and owning client agree. */
	bool IsSpecialMovementAllowed() const;

	/** Derived state, NOT replicated: both machines compute it from the same predicted inputs, and it rides in
	 *  FSavedMove_FPSR so a correction replay restores it exactly instead of re-deriving it across a float boundary. */
	bool bIsSliding = false;

	// --- Replicated PICTURE of the state above, for machines that cannot derive it (simulated proxies) ---
	// Written by the server only, in OnMovementUpdated, from the post-move truth. Kept apart from bIsSliding because
	// that one is rewound by PrepMoveFor on a correction replay; a replicated property must not be rewound under the
	// networking layer's feet.

	/** Server's view of "sliding", replicated to everyone but the owner (who derives it, exactly, itself). */
	UPROPERTY(Transient, Replicated)
	uint8 bSlidingVisual : 1;

	/** Incremented on every slide entry so a slide shorter than the net update interval still registers. Wrapping is
	 *  fine and intended — the receiver compares against the last value it saw, never against zero. */
	UPROPERTY(Transient, Replicated)
	uint8 SlideVisualSerial = 0;

	/** Wall facing, quantised to 1/256 of a turn. Refreshed only when the quantised value actually changes: the
	 *  underlying normal is re-probed every frame, and marking a replicated property dirty at that rate is churn the
	 *  correction path does not need. */
	UPROPERTY(Transient, Replicated)
	uint8 WallYawByte = 0;

	/** Latched side the body presents to the wall, +1 / -1. See GetWallSideSign(). */
	UPROPERTY(Transient, Replicated)
	int8 WallSideSign = 1;

	/** Server-side: push the three values above into agreement with the real state. Called after movement. */
	void RefreshReplicatedVisualState();

	/** Seconds elapsed in the current slide. Accumulated from the movement delta (never from world time — server and
	 *  client clocks differ), and likewise saved/restored for replay. */
	float SlideElapsed = 0.0f;

	/** Planar speed the current slide started at (after the entry impulse). SlideSpeedCurve is expressed relative to
	 *  this, so a slide entered at a lower speed decays through the same shape rather than snapping to a fixed number. */
	float SlideEntrySpeed = 0.0f;

	/** Seconds left before another slide may start. Counted down from the movement delta, same reasoning as
	 *  SlideElapsed, and saved/restored so a replay can't hand the player a free re-entry. */
	float SlideCooldownRemaining = 0.0f;

	/** Speed (cm/s) the slope has added to (downhill) or taken from (uphill) the current slide so far. Kept apart from
	 *  the curve because the curve is re-read from scratch every frame — this is the part that has to accumulate. */
	float SlideSlopeSpeedBonus = 0.0f;

	/** Seconds of continuous ground movement input — the X axis of GroundSpeedCurve. Held (not reset) while airborne
	 *  so a jump mid-sprint doesn't drop the player back to a standing start on landing, and pushed to the end of the
	 *  curve when a slide ends so the faster-than-running slide speed decays instead of being slammed down. */
	float GroundAccelElapsed = 0.0f;

	/** Time limit actually enforced on a slide: the curve's length when one is assigned, otherwise SlideMaxDuration.
	 *  Always finite, so invariant 7 (no inescapable state) holds either way. */
	float GetEffectiveSlideMaxDuration() const;

	/** Ground speed ramp for the current stance: CrouchSpeedCurve while crouched, GroundSpeedCurve otherwise.
	 *  Null when that stance has no curve assigned. */
	const UCurveFloat* GetActiveGroundSpeedCurve() const;

	/** Earliest time on Curve whose value reaches TargetSpeed (cm/s), accounting for the card scale. Used to carry
	 *  momentum across a stance change: 300 cm/s maps to wherever the standing curve first hits 300, and acceleration
	 *  resumes from there instead of restarting or jumping to the end. Assumes a rising ramp; returns the curve's end
	 *  when the current speed already exceeds everything it describes. */
	float FindCurveTimeForSpeed(const UCurveFloat* Curve, float TargetSpeed) const;

	/** Move GroundAccelElapsed onto the equivalent point of the stance's curve. No-op without a curve. */
	void RemapGroundAccelForStanceChange();

	/** The air-strafe step, run in place of Super::CalcVelocity while falling. Lateral only — the falling step zeroes
	 *  Velocity.Z around the call and applies gravity itself. */
	void ApplyAirStrafeVelocity(float DeltaTime);

	// --- Walk-speed layers (invariant 1 applied to the NUMBER, not just the state) ---

	/** The ONLY place MaxWalkSpeed is ever written. Four separate writers used to exist (the character's constructor,
	 *  the move-speed multiplier, and the DBNO down/revive branches); each knew only its own layer and overwrote the
	 *  whole value, so they erased one another. Adding the equipped weapon as a fifth would have made "pick up a speed
	 *  card while holding the melee slot and lose its bonus" a certainty. Composing all four layers here instead means
	 *  each caller only has to state ITS layer and the order can't be gotten wrong.
	 *
	 *  It also removed a live bug: a speed card applied while downed used to revive MaxWalkSpeed from 0 and let a
	 *  downed player walk. The downed layer is part of the composition now, so that is impossible by construction. */
	void RefreshWalkSpeedCap();

	/** Authored baseline pushed from the character (its BaseWalkSpeed). The literal only exists so a component used
	 *  without a character still moves; the character overwrites it on construction. */
	float AuthoredBaseWalkSpeed = 900.0f;

	/** Equipped weapon's baseline; 0 = fall back to AuthoredBaseWalkSpeed. */
	float LoadoutWalkSpeed = 0.0f;

	/** Card/meta multiplier applied on top of whichever baseline wins. */
	float MoveSpeedMultiplier = 1.0f;

	/** True while the owner is downed (DBNO). */
	bool bDownedLocomotion = false;

	// --- Stance blending ---

	/** Step both blend weights toward the stance the player is actually in, and retire a finished transition. */
	void AdvanceStanceBlends(float DeltaSeconds);

	/** Open a new stance transition, remembering the speed cap in force a moment ago so GetMaxSpeed can ease away from
	 *  it rather than dropping to the new stance's cap in a couple of frames. */
	void BeginStanceTransition(float SpeedBeforeChange);

	/** Stance weight, 0 standing .. 1 crouched. Saved for replay because the walk-speed cap reads it — it stopped
	 *  being presentation-only the moment it could change where the player ends up (invariant 2). */
	float StanceBlend = 0.0f;

	/** StanceBlend at the instant the current transition began; the denominator of GetStanceTransitionProgress(). */
	float StanceBlendStart = 0.0f;

	/** Ground speed cap at that same instant, or 0 when no transition is in flight. GetMaxSpeed eases from here to
	 *  the stance's normal cap, which is what turns the old two-frame drop into a full StanceBlendDuration. */
	float StanceSpeedFrom = 0.0f;

	/** Slide pose weight, 0 .. 1. Nothing physical reads it, but it is saved alongside the stance weights all the same:
	 *  a correction replays moves whose time has already been simulated once, so an unsaved weight would advance twice
	 *  over that span and the slide pose would arrive early. Cheap to keep the animation clock honest. */
	float SlideBlend = 0.0f;

	// --- Wall hang ---

	/** One sphere sweep straight ahead, starting at HeightAboveCapsuleBottom cm up the capsule and reaching
	 *  WallProbeDistance past its radius. Heights are measured from the feet so the same numbers keep meaning "chest"
	 *  and "ankles" whatever the capsule is scaled to. Uses the capsule's own collision channel, so anything the
	 *  player can bump into is also something they can grab — no second collision setup to keep in sync. */
	bool ProbeWall(const FVector& Direction, float HeightAboveCapsuleBottom, FHitResult& OutHit) const;

	/** True when the hit surface is steep enough to be a wall rather than a ramp (WallMinSteepnessDegrees). */
	bool IsSurfaceGrabbable(const FHitResult& Hit) const;

	/** True when the movement input pushes into the wall currently held — "is W still held" for a state that already
	 *  knows which way the wall faces. Reads Acceleration, which is part of the predicted move, so client and server
	 *  reach the same answer. */
	bool IsInputIntoWall() const;

	/** Evaluate the grab conditions for THIS frame and fill OutWallNormal when they pass. Pure read of already
	 *  predicted state (airborne, input, the once-per-airborne budget) plus one probe, which is what makes the derived
	 *  hang state replay-safe. */
	bool CanGrabWall(FVector& OutWallNormal) const;

	/** Enter the hang against the given wall. */
	void StartWallHang(const FVector& InWallNormal);

	/** Advance the hang: timers, the three exits, and the per-frame wall re-check. */
	void UpdateWallHang(float DeltaSeconds);

	/** How much of the speed carried into the wall is still in hand: 1 at the moment of the grab, falling linearly to
	 *  0 over WallEntryMomentumDuration. Drives both the glide and the wall jump's bonus speed off one number, so the
	 *  two can't disagree about how much momentum is left. */
	float GetWallEntryMomentumAlpha() const;

	/** Direction a wall jump travels: the pawn's facing and the wall normal mixed per WallJumpAimBlend, then re-aimed
	 *  if needed so at least WallJumpMinOutward of it still points away from the wall. Horizontal. */
	FVector ComputeWallJumpDirection(const FVector& InWallNormal) const;

	/** Derived state, NOT replicated — same reasoning as the slide: both machines compute it from the same predicted
	 *  inputs, and it rides in FSavedMove_FPSR so a correction replay restores it rather than re-deriving it.
	 *  (Whether we are on a wall at all is NOT here — that is the movement mode, which the engine already owns.) */
	float WallHangElapsed = 0.0f;

	/** Seconds spent slipping since the input last pushed into the wall; reset to 0 whenever it does. */
	float WallSlipElapsed = 0.0f;

	/** Outward normal of the wall being held, horizontal. Saved and restored because the jump exit reads it from
	 *  CheckJumpInput, which runs BEFORE the frame's wall probe — a replay would otherwise read a stale or empty one. */
	FVector WallNormal = FVector::ZeroVector;

	/** Planar velocity the player arrived at the wall with, captured on the grab. Its along-the-wall part becomes the
	 *  glide and its full LENGTH becomes the wall jump's bonus — keeping the length is what makes a head-on impact,
	 *  whose along-the-wall part is nearly zero, still pay out on the jump. Saved for the same reason as WallNormal. */
	FVector WallEntryVelocity = FVector::ZeroVector;

	/** True once this airborne period's single wall grab has been spent; cleared on landing. A cooldown alone would
	 *  not do: climbing for WallHangMaxDuration gains more height than the cooldown's fall loses it, so any wall could
	 *  be climbed indefinitely a grab at a time. Requiring a landing makes that impossible by construction, and the
	 *  same flag stops an instant re-grab of the wall just jumped off. */
	bool bWallHangConsumed = false;

	friend class FSavedMove_FPSR;
};

/**
 * Saved move carrying the derived state of the slide and the wall hang, so a correction replay reproduces them
 * bit-for-bit.
 *
 * Note there is no GetCompressedFlags override: the slide is driven by the engine's own bWantsToCrouch intent and the
 * wall hang by the movement mode, the input direction and bPressedJump — all of which FSavedMove_Character already
 * sends. Everything below is LOCAL replay storage that never touches the wire, so this component still costs zero
 * network bandwidth over a stock CharacterMovementComponent.
 */
class FSavedMove_FPSR : public FSavedMove_Character
{
public:
	typedef FSavedMove_Character Super;

	FSavedMove_FPSR();

	virtual void Clear() override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, class FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* C) override;

	uint8 bSavedIsSliding : 1;
	float SavedSlideElapsed = 0.0f;
	float SavedSlideEntrySpeed = 0.0f;
	float SavedSlideCooldownRemaining = 0.0f;
	float SavedGroundAccelElapsed = 0.0f;
	float SavedSlideSlopeSpeedBonus = 0.0f;

	//~ Wall hang. bSavedOnWall duplicates the movement mode on purpose: the combine test below needs to know whether a
	//~ move was a hang, and the engine's own test never looks at the packed movement mode.
	uint8 bSavedOnWall : 1;
	uint8 bSavedWallHangConsumed : 1;
	float SavedWallHangElapsed = 0.0f;
	float SavedWallSlipElapsed = 0.0f;
	FVector SavedWallNormal = FVector::ZeroVector;
	FVector SavedWallEntryVelocity = FVector::ZeroVector;

	//~ Stance blending. The first three drive the walk-speed cap; the slide weight rides along so a replay can't run the
	//~ animation clock at double speed over the span it re-simulates.
	float SavedStanceBlend = 0.0f;
	float SavedStanceBlendStart = 0.0f;
	float SavedStanceSpeedFrom = 0.0f;
	float SavedSlideBlend = 0.0f;
};

/** Client prediction data that allocates FSavedMove_FPSR instead of the stock saved move. */
class FNetworkPredictionData_Client_FPSR : public FNetworkPredictionData_Client_Character
{
public:
	typedef FNetworkPredictionData_Client_Character Super;

	explicit FNetworkPredictionData_Client_FPSR(const UCharacterMovementComponent& ClientMovement);

	virtual FSavedMovePtr AllocateNewMove() override;
};
