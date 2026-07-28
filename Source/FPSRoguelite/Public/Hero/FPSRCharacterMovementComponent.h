// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/CharacterMovementComponent.h"
#include "FPSRCharacterMovementComponent.generated.h"

class UCurveFloat;

/**
 * Player locomotion component — the SINGLE OWNER of the player's movement state (ADR 0001 invariant 1).
 *
 * Nothing outside this component decides what locomotion state the player is in. The character forwards INPUT INTENT
 * (crouch pressed/released) and everything else — firing, the arms AnimBP, the HUD — only QUERIES this component
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
 * tracking, step-up, slope handling and wall sliding. Wall-hang (a later phase) will use MOVE_Custom instead, since
 * it has neither gravity nor a floor and would reuse nothing from PhysWalking.
 *
 * Network cost: ZERO custom compressed flags. The slide is entered from the engine's own bWantsToCrouch intent (the
 * design calls for "crouch input while running = slide"), which is already part of every move packet. The derived
 * slide state and its elapsed timer ride along in FSavedMove_FPSR for local replay only — they are never sent.
 */
UCLASS()
class FPSROGUELITE_API UFPSRCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UFPSRCharacterMovementComponent();

	//~ Read-only surface for firing / AnimBP / HUD. This is the ENTIRE public contract (ADR 0001 module boundary):
	//~ callers never ask "which state am I in?", they ask what they actually need to know.

	/** True when the current locomotion state permits firing. Wall-hang (later phase) is the state that returns false —
	 *  both hands are on the wall. Everything else (stand/run/crouch/slide/airborne) allows it. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	bool CanFireInCurrentState() const;

	/** Weapon-spread multiplier contributed by the current locomotion state (1.0 = no change). Invariant 5: this is a
	 *  MULTIPLIER ONLY — the heat system remains the single owner of the final spread calculation. States are resolved
	 *  exclusively (airborne > slide > crouch) rather than multiplied, so the value can't compound unexpectedly. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	float GetSpreadMultiplier() const;

	/** True while sliding. For the arms AnimBP and the HUD — presentation only; nothing may drive state off this. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	bool IsSliding() const { return bIsSliding; }

	/** True while hanging on a wall. Always false until the wall-hang phase lands; declared now so the AnimBP and HUD
	 *  can be authored against the final contract instead of being rewired later. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	bool IsOnWall() const { return false; }

	/** Planar speed (cm/s). For the movement debug readout. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	float GetPlanarSpeed() const;

	/** Short name of the current locomotion state ("Run" / "Crouch" / "Slide" / "Air"). Debug readout only. */
	FString GetLocomotionStateName() const;

	//~UCharacterMovementComponent
	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual float GetMaxSpeed() const override;

	/** Takes over velocity entirely while sliding — see the implementation for why MaxAcceleration is deliberately
	 *  left alone instead of being zeroed. */
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;

	/** Clamps descent to MaxFallSpeed after the engine's falling step. */
	virtual void PhysFalling(float deltaTime, int32 Iterations) override;

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

protected:
	// --- Slide tuning (invariant 9: data, not C++ constants — designers tune these per-hero in the BP defaults) ---

	/** Minimum planar speed required to ENTER a slide. Below this, the crouch input is just a crouch. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide", meta = (ClampMin = "0.0"))
	float SlideMinEnterSpeed = 450.0f;

	/** Planar speed at which an ongoing slide ends (it has decayed into a crouch-walk). Kept strictly below
	 *  SlideMinEnterSpeed so a slide can't immediately re-trigger the instant it ends while crouch is still held. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide", meta = (ClampMin = "0.0"))
	float SlideMinSpeed = 250.0f;

	/** Entry impulse: current planar speed is multiplied by this on the frame the slide starts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide", meta = (ClampMin = "1.0"))
	float SlideEnterSpeedMultiplier = 1.5f;

	/** How fast the ENTRY IMPULSE alone may take the player. Deliberately lower than SlideMaxSpeed: with a single
	 *  ceiling, jump-cancelling and re-sliding kept re-applying the multiplier and ratcheted the speed up every cycle
	 *  (900 -> 1350 -> cap) for free. This only ever RAISES speed toward the limit — entering already faster than it
	 *  (downhill momentum carried through a jump) keeps the higher speed rather than being cut down to it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide", meta = (ClampMin = "0.0"))
	float SlideMaxEntrySpeed = 900.0f;

	/** Hard ceiling on slide speed from ANY source (entry impulse, slope acceleration, carried momentum). Speeds above
	 *  SlideMaxEntrySpeed have to be earned — typically by accelerating down a slope. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide", meta = (ClampMin = "0.0"))
	float SlideMaxSpeed = 1400.0f;

	/** Deceleration (cm/s²) applied while sliding. This is the "속도 감소 곡선" in its simplest form; assign
	 *  SlideSpeedCurve below for a shaped decay instead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide", meta = (ClampMin = "0.0"))
	float SlideBrakingDeceleration = 900.0f;

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
	float SlopeAccelerationScale = 1.0f;

	/** How strongly a slope stretches (downhill) or compresses (uphill) the slide's sense of time. The speed curve is
	 *  advanced by DeltaTime * (1 - slope * this), so a downhill slide decays in slow motion and keeps going for as
	 *  long as the hill lasts, while an uphill one runs through the curve early and ends sooner. 0 disables it and the
	 *  slide always lasts exactly the curve's length. Needed alongside SlopeAccelerationScale because acceleration
	 *  alone can't extend a slide past its time limit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Slide", meta = (ClampMin = "0.0"))
	float SlopeTimeInfluence = 1.0f;

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

	/** Hard time limit on a single slide (invariant 7: every state needs a time bound or an unconditional exit).
	 *  Without this a downhill slide that keeps regaining speed would never satisfy the speed-based exit.
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

	/** Terminal fall speed in cm/s; 0 = no extra limit (the physics volume's own TerminalVelocity, engine default
	 *  4000, still applies). Exists because that volume value is per-VOLUME, so it can't express a per-character fall
	 *  speed — which is what the design asks for. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement|Air", meta = (ClampMin = "0.0"))
	float MaxFallSpeed = 0.0f;

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

	friend class FSavedMove_FPSR;
};

/**
 * Saved move carrying the slide's derived state so a correction replay reproduces it bit-for-bit.
 *
 * Note there is no GetCompressedFlags override: the slide is driven by the engine's own bWantsToCrouch intent, which
 * FSavedMove_Character already sends. bSavedIsSliding / SavedSlideElapsed are LOCAL replay storage — they never touch
 * the wire, so this component costs zero network bandwidth over a stock CharacterMovementComponent.
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
};

/** Client prediction data that allocates FSavedMove_FPSR instead of the stock saved move. */
class FNetworkPredictionData_Client_FPSR : public FNetworkPredictionData_Client_Character
{
public:
	typedef FNetworkPredictionData_Client_Character Super;

	explicit FNetworkPredictionData_Client_FPSR(const UCharacterMovementComponent& ClientMovement);

	virtual FSavedMovePtr AllocateNewMove() override;
};
