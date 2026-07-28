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
	virtual float GetMaxAcceleration() const override;
	virtual float GetMaxBrakingDeceleration() const override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;

	/** Ground-only crouch. The engine's version also allows crouching while FALLING; this design forbids crouch and
	 *  slide in the air, and returning false here additionally makes the engine un-crouch automatically the moment the
	 *  player leaves the ground (so jumping out of a crouch stands you up instead of keeping a crouched capsule). */
	virtual bool CanCrouchInCurrentState() const override;

	/** Allow jumping OUT of a slide. The engine refuses to jump whenever bWantsToCrouch is set, and a slide is entered
	 *  from the crouch intent — so without this a slide could never be jump-cancelled. Only the crouch term is lifted;
	 *  jump count and hold-time rules stay exactly as the engine defines them. */
	virtual bool CanAttemptJump() const override;
	//~End UCharacterMovementComponent

	/** Force the slide to end now, keeping current velocity. Used by the global run-freeze so a slide in progress can't
	 *  carry the player across the frozen card screen (invariant 8 — gating the START is not enough). Safe to call when
	 *  not sliding. */
	void StopSliding();

protected:
	// --- Slide tuning (invariant 9: data, not C++ constants — designers tune these per-hero in the BP defaults) ---

	/** Minimum planar speed required to ENTER a slide. Below this, the crouch input is just a crouch. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Slide", meta = (ClampMin = "0.0"))
	float SlideMinEnterSpeed = 450.0f;

	/** Planar speed at which an ongoing slide ends (it has decayed into a crouch-walk). Kept strictly below
	 *  SlideMinEnterSpeed so a slide can't immediately re-trigger the instant it ends while crouch is still held. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Slide", meta = (ClampMin = "0.0"))
	float SlideMinSpeed = 250.0f;

	/** Entry impulse: current planar speed is multiplied by this on the frame the slide starts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Slide", meta = (ClampMin = "1.0"))
	float SlideEnterSpeedMultiplier = 1.5f;

	/** Hard ceiling on slide speed, applied to the entry impulse and to the per-frame max speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Slide", meta = (ClampMin = "0.0"))
	float SlideMaxSpeed = 1400.0f;

	/** Deceleration (cm/s²) applied while sliding. This is the "속도 감소 곡선" in its simplest form; assign
	 *  SlideSpeedCurve below for a shaped decay instead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Slide", meta = (ClampMin = "0.0"))
	float SlideBrakingDeceleration = 900.0f;

	/** Ground friction while sliding. MUST stay low or there is no slide to see: PhysWalking feeds GroundFriction
	 *  (engine default 8) into the braking math, where BrakingFrictionFactor doubles it — an effective 16, which decays
	 *  900 -> 250 cm/s in roughly 0.08s. At 0 the engine switches to constant deceleration, so SlideBrakingDeceleration
	 *  alone shapes the decay and the numbers become predictable (900 -> 250 at 900 cm/s² = ~0.72s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Slide", meta = (ClampMin = "0.0"))
	float SlideGroundFriction = 0.0f;

	/** Lockout after a slide ENDS before another may start. Without it, tapping crouch repeatedly re-triggers the entry
	 *  impulse over and over and the player rides a permanent speed boost. Applied on EVERY exit (released, too slow,
	 *  timed out, jumped, gate closed) so no exit route dodges it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Slide", meta = (ClampMin = "0.0"))
	float SlideCooldown = 0.8f;

	/** Steering authority during a slide (cm/s²). 0 = fully committed (no course correction). Small values let the
	 *  player nudge the slide without turning it into a crouch-run. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Slide", meta = (ClampMin = "0.0"))
	float SlideSteerAcceleration = 0.0f;

	/** Hard time limit on a single slide (invariant 7: every state needs a time bound or an unconditional exit).
	 *  Without this a downhill slide that keeps regaining speed would never satisfy the speed-based exit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Slide", meta = (ClampMin = "0.1"))
	float SlideMaxDuration = 1.6f;

	/** Slide speed over time, in cm/s. X = seconds since the slide started, Y = the actual speed (e.g. 900 -> 300).
	 *  The curve is CAPPED BY THE ENTRY SPEED, so sliding in slower than the curve's opening value starts from that
	 *  slower speed instead of snapping up to it; a full-speed entry follows the curve exactly. When assigned this
	 *  drives the speed directly and SlideBrakingDeceleration is bypassed, so the shape lands as authored instead of
	 *  being approximated by a braking force. Null (the default) = constant deceleration via SlideBrakingDeceleration —
	 *  the curve is a designer refinement, never a requirement, so an unassigned asset can never break locomotion. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Slide")
	TObjectPtr<UCurveFloat> SlideSpeedCurve = nullptr;

	/** Standing -> running speed ramp, in cm/s. X = seconds of continuous ground movement input, Y = the actual speed
	 *  (e.g. 0 -> 600). Drives the max speed each frame, so the character accelerates along the authored shape. Null =
	 *  the engine's flat MaxWalkSpeed with constant acceleration. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Ground")
	TObjectPtr<UCurveFloat> GroundSpeedCurve = nullptr;

	/** The max walk speed the speed curves above were AUTHORED against. Both curves hold literal cm/s values, so when
	 *  a card raises MaxWalkSpeed (600 -> 780) their values are scaled by MaxWalkSpeed / this (x1.3) instead of
	 *  capping the player at the authored numbers. Keep it equal to the character's BaseWalkSpeed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Ground", meta = (ClampMin = "1.0"))
	float SpeedCurveReferenceSpeed = 600.0f;

	// --- Spread multipliers by locomotion state (invariant 5: multipliers only) ---

	/** Spread multiplier while crouching (design: ×0.8 — steadier). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Spread", meta = (ClampMin = "0.0"))
	float CrouchSpreadMultiplier = 0.8f;

	/** Spread multiplier while airborne (design: ×1.6 — LESS accurate; spread scales up). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Spread", meta = (ClampMin = "0.0"))
	float AirborneSpreadMultiplier = 1.6f;

	/** Spread multiplier while sliding. Firing mid-slide is allowed by design; this is the cost. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Spread", meta = (ClampMin = "0.0"))
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

	/** Seconds of continuous ground movement input — the X axis of GroundSpeedCurve. Held (not reset) while airborne
	 *  so a jump mid-sprint doesn't drop the player back to a standing start on landing, and pushed to the end of the
	 *  curve when a slide ends so the faster-than-running slide speed decays instead of being slammed down. */
	float GroundAccelElapsed = 0.0f;

	/** Scale applied to the authored speed curves so card-raised move speed still works. See SpeedCurveReferenceSpeed. */
	float GetSpeedCurveScale() const;

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
};

/** Client prediction data that allocates FSavedMove_FPSR instead of the stock saved move. */
class FNetworkPredictionData_Client_FPSR : public FNetworkPredictionData_Client_Character
{
public:
	typedef FNetworkPredictionData_Client_Character Super;

	explicit FNetworkPredictionData_Client_FPSR(const UCharacterMovementComponent& ClientMovement);

	virtual FSavedMovePtr AllocateNewMove() override;
};
