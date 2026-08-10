// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "FPSRCharacterAnimInstance.generated.h"

class AFPSRCharacter;
class UFPSRCharacterMovementComponent;

/**
 * Body AnimInstance base for the player character (ADR 0002 step 4). Reads the movement/weapon state ONCE per frame and
 * publishes it as plain values the anim graph binds to, so the graph never queries gameplay objects itself.
 *
 * Runs on EVERY machine — one mesh, one anim graph for the owner and for remote observers (ADR 0002 invariant 4). All of
 * it is derived locally from already-replicated state: nothing here is replicated (invariant 8), and nothing here writes
 * movement or gameplay state (invariants 3 and 7). The anim graph is a consumer, full stop.
 *
 * MAIN vs LINKED LAYER. The same class backs both the character's main AnimBP and the per-weapon linked anim layers
 * (AFPSRCharacter::RefreshBodyAnimLayer links them from the weapon DataAsset). Only the MAIN instance computes, and it
 * PUSHES the result into its linked layers: the stateful RootYawOffset then has exactly one accumulator, and the
 * game-thread-only grip query exactly one caller.
 *
 * The push direction is not a style choice. The engine updates linked instances BEFORE the main one
 * (USkeletalMeshComponent::TickAnimInstances), so a layer that pulled for itself would read last frame's values and its
 * RootYawOffset compensation would lag one frame behind the Rotate Root Bone that consumes it. Pushing from the main's
 * update lands the values before the graphs evaluate.
 */
UCLASS()
class FPSROGUELITE_API UFPSRCharacterAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// --- Locomotion (read by the anim graph) ---

	/** Planar speed, cm/s (UFPSRCharacterMovementComponent::GetPlanarSpeed). */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	float Speed = 0.0f;

	/** Velocity direction, -180..180, relative to the pose AFTER RootYawOffset is applied — the offset is subtracted
	 *  here so the feet still point along the real velocity once Rotate Root Bone turns the pose. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	float Direction = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	bool bIsMoving = false;

	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	bool bIsFalling = false;

	/** Wall-hang (CMOVE_WallHang). Derived from the movement mode, so it is already correct on a simulated proxy.
	 *  While this is true the body is turned to face the wall via RootYawOffset — see UpdateRootYawOffset. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	bool bIsOnWall = false;

	/** Falling OR wall-hung — i.e. "not on the ground", which is the question the locomotion state machine actually asks.
	 *  Combined HERE rather than in the graph because a state-machine transition rule can only read ONE variable: the
	 *  ground/air pair needs `bIsFalling || bIsOnWall` on the way out and the negation of BOTH on the way back, and the
	 *  negation is not expressible as two separate transitions. Without this, a wall-hung player keeps a grounded idle
	 *  (bIsFalling is false in a custom movement mode) — the most visible way this graph can be wrong. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	bool bIsAirborne = false;

	/** 0 fully standing .. 1 fully crouched. Continuous on purpose — blend the stance with it instead of switching on a
	 *  bool, or the body snaps while the camera's own stance blend is still easing (UpdateStanceCamera). */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	float StanceBlend = 0.0f;

	/** True while a slide is in progress. Presentation only — invariant 3 forbids driving movement state off it.
	 *  Valid on EVERY machine: fed by UFPSRCharacterMovementComponent::IsSlidingForDisplay (a replicated copy on
	 *  proxies, the exact local value elsewhere) and held for SlideVisualMinDuration when the slide serial says a
	 *  slide happened too fast to survive the gap between net updates. ⏳ Still wants a 2-client PIE pass. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	bool bIsSliding = false;

	/** 0 not sliding .. 1 fully sliding. The owner and the server take the movement component's own blend; a proxy has
	 *  only the replicated flag, so it eases its own over SlideVisualBlendDuration rather than spending bandwidth on
	 *  a curve that is just a ramp of known length. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	float SlideBlend = 0.0f;

	/** True while the player is actively pushing a movement input. Derived from acceleration, not velocity — the 8-way
	 *  start/stop transitions need to know where the player is STEERING, which is not where a decelerating character is
	 *  still travelling. Published now so wiring those transitions stays content work. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	bool bHasMovementInput = false;

	/** Input direction, -180..180, RootYawOffset-compensated exactly like Direction. Falls back to Direction while
	 *  there is no input, so a stop transition doesn't snap to zero the moment the stick is released. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	float InputDirection = 0.0f;

	// --- Aim ---

	/** Aim-down-sights state, valid on every machine (replicated presentation bit — see AFPSRCharacter::IsAiming). */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	bool bIsAiming = false;

	/** View pitch relative to the actor, -90..90. Correct on simulated proxies for free: the engine already replicates
	 *  APawn::RemoteViewPitch16 (COND_SkipOwner) and GetBaseAimRotation folds it in. No extra replication.
	 *  The -90..90 range is enforced by an explicit clamp in UpdateFromCharacter, not just assumed. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	float AimPitch = 0.0f;

	/** View yaw relative to the pose AFTER RootYawOffset is applied. Because the capsule yaw is the view yaw
	 *  (bUseControllerRotationYaw), this is essentially -RootYawOffset: the further the legs lag, the further the upper
	 *  body twists to keep the sight on the crosshair. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	float AimYaw = 0.0f;

	// --- Lower-body yaw offset (ADR 0002 axis 1: visual rotation split) ---

	/** Feed this to Rotate Root Bone's Yaw. The capsule turns with the view instantly; this turns the POSE back by the
	 *  same amount so the legs lag and the pack's turn-in-place clips have an angle error to play against. Never
	 *  replicated (invariant 8) — every machine accumulates its own from the actor rotation it already has. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	float RootYawOffset = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	bool bTurningInPlace = false;

	/** -1 turning left, +1 turning right, 0 not turning. Picks the turn-in-place clip. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	float TurnDirection = 0.0f;

	// --- Left-hand IK ---

	/** WORLD transform of the equipped weapon's left-hand grip. Set the Two Bone IK effector space to World Space.
	 *  Meaningless while bHasLeftHandGrip is false. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	FTransform LeftHandGripWorld;

	/** Same grip as a bare WORLD location — bind this straight to Two Bone IK's EffectorLocation, which takes a vector
	 *  rather than a transform. Zero while bHasLeftHandGrip is false. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	FVector LeftHandGripLocation = FVector::ZeroVector;

	/** False when this weapon authors no left-hand grip (melee / unarmed / the pack's bullpup handguards have no
	 *  SOCKET_LeftHand). Gate the IK alpha on it — there is no target to reach for. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	bool bHasLeftHandGrip = false;

	/** Authored per-montage weight for the left-hand IK, so a reload/equip that legitimately takes the hand off the
	 *  weapon can release the IK. 1 when the curve is absent, which is what locomotion wants. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	float LeftHandIKWeight = 1.0f;

	/** Elbow pole for the left-hand Two Bone IK, WORLD space — bind to its JointTargetLocation. Defaults to the elbow's
	 *  OWN current position, which preserves the arm's existing bend plane; a Two Bone IK left without a usable joint
	 *  target is where elbow flipping comes from. Steer it with LeftElbowPoleOffset below — no code change needed. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	FVector LeftHandJointTargetLocation = FVector::ZeroVector;

	/** The value to bind to Two Bone IK's Alpha — grip present, not downed, times the montage curve. Folded here rather
	 *  than assembled from three pins in the graph: the graph is a consumer, and an alpha that silently reads 1 because
	 *  someone rewired a Select node is the kind of bug that only shows up as an arm stuck to a missing weapon. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	float LeftHandIKAlpha = 0.0f;

	// --- Life state ---

	/** DBNO or dead. The downed pose overrides locomotion, and the IK / turn-in-place stop. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	bool bIsDowned = false;

	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	bool bIsDead = false;

protected:
	virtual void NativeInitializeAnimation() override;

	/** Where the frame's work actually happens. The engine calls this on the game thread BEFORE it updates the linked
	 *  instances (USkeletalMeshComponent::TickAnimInstances), which is the only point early enough: a layer's own update
	 *  advances its state machine, so pushing any later would have it transition on last frame's Speed / RootYawOffset.
	 *  Called whenever the mesh has a main instance, linked layers or not. */
	virtual void PreUpdateLinkedInstances(float DeltaSeconds) override;

	/** Fallback only. Guarded to run at most once per frame together with PreUpdateLinkedInstances, so an update path
	 *  that skips that hook still publishes, and the normal path doesn't advance the yaw accumulator twice. */
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/** How far the legs may lag behind the view, degrees. Bound to the AimOffset's yaw range (+-90) on purpose: past it
	 *  the upper body can no longer twist far enough to cancel the offset, and the aim visibly drags behind the
	 *  crosshair. Raising this needs an AimOffset authored wider first. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Anim", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float RootYawOffsetMax = 90.0f;

	/** Angle error, degrees, that starts a turn in place. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Anim", meta = (ClampMin = "0.0"))
	float TurnInPlaceStartAngle = 45.0f;

	/** Seconds for the body to turn into the wall alignment on grabbing, instead of snapping there. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Anim|Wall", meta = (ClampMin = "0.0"))
	float WallAlignBlendDuration = 0.15f;

	/** Shortest time a slide is shown for once the serial says one happened. A slide can be over in two frames
	 *  (jump-cancel); without a floor the pose would flicker or never appear at all on a teammate's screen. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Anim|Slide", meta = (ClampMin = "0.0"))
	float SlideVisualMinDuration = 0.2f;

	/** Seconds the slide weight takes to ease in/out on a machine that only has the replicated flag. Mirrors the
	 *  movement component's own SlideBlendDuration, which is what the owner and server use directly. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Anim|Slide", meta = (ClampMin = "0.0"))
	float SlideVisualBlendDuration = 0.15f;

	/** How fast a turn in place eats the offset, degrees/sec. The pack's turn loops are in-place conversions with the
	 *  root motion stripped, so there is no root delta to consume the offset — this does it instead, which also keeps
	 *  the offset's owner in one place (invariant 8). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Anim", meta = (ClampMin = "1.0"))
	float TurnInPlaceRateDegPerSec = 180.0f;

	/** Speed above which the legs must follow the capsule, so the offset is driven out while moving. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Anim", meta = (ClampMin = "0.0"))
	float MovingSpeedThreshold = 3.0f;

	/** Animation curve a montage authors to release the left-hand IK (0 = released). Data, not a literal, for the same
	 *  reason bone names are: this project has already changed characters twice (invariant 9). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Anim")
	FName LeftHandIKWeightCurve = FName(TEXT("LeftHandIKWeight"));

	/** Bone the left-hand IK's elbow pole is measured from. Data rather than a literal for the same reason every other
	 *  bone name here is: this project has already changed characters twice (invariant 9).
	 *  None, or a name the skeleton doesn't have, DISABLES the left-hand IK (LeftHandIKAlpha goes to 0) — a world-space
	 *  IK with no pole aims the elbow at the world origin, so off is the only safe failure. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Anim")
	FName LeftElbowBoneName = FName(TEXT("lower_arm_L"));

	/** Component-space nudge added to the elbow pole. Zero leaves the pole on the elbow itself, i.e. the current bend is
	 *  preserved — the safe default. Dial it here if a weapon's grip needs the elbow steered out or down. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Anim")
	FVector LeftElbowPoleOffset = FVector::ZeroVector;

private:
	/** True on the character's own AnimBP instance, false on a linked anim layer. Decided in
	 *  NativeInitializeAnimation by asking the mesh which instance is ITS anim instance — the engine assigns that member
	 *  before calling InitializeAnimation on it, so the answer is already valid there. */
	bool bIsMainInstance = false;

	/** Actor yaw last frame, for the RootYawOffset accumulation. */
	float PreviousActorYaw = 0.0f;
	bool bHasPreviousActorYaw = false;

	/** Remaining seconds of the enforced slide display. See SlideVisualMinDuration. */
	float SlideVisualHold = 0.0f;

	/** Last slide serial seen from the movement component; a change means a slide began, even one already over.
	 *  bHasSlideSerial keeps the FIRST value from counting as a change — otherwise every spawn, and every proxy that
	 *  joins mid-slide, would play a slide it never actually saw start. */
	uint8 LastSlideVisualSerial = 0;
	bool bHasSlideSerial = false;

	/** So a missing elbow bone is reported once, not every frame. */
	bool bWarnedMissingElbowBone = false;

	/** So actor-pitch pollution (which silently kills the proxy RemoteViewPitch fallback) is reported once, not every frame. */
	bool bWarnedActorPitchPollution = false;

	/** Accumulator for the FPSR.Debug.AimSync >= 2 throttled log. Debug-only in effect; kept unconditional so the
	 *  header doesn't change shape between build configs. */
	float AimSyncLogAccum = 0.0f;

	/** Frame the values were last published, so the two hooks above can't both run. Sentinel = "never". */
	uint64 LastPublishedFrame = TNumericLimits<uint64>::Max();

	/** Compute + push, at most once per frame, main instance only. */
	void UpdateAndPublish(float DeltaSeconds);

	/** Full recompute. Main instance only. */
	void UpdateFromCharacter(AFPSRCharacter& Character, float DeltaSeconds);

	/** Copy every published value into each linked layer of this class. Main instance only. */
	void PushToLinkedLayers() const;

	void UpdateRootYawOffset(const AFPSRCharacter& Character, float DeltaSeconds);

	/** Resolve the left-hand IK's elbow pole from the elbow bone plus the authored offset. Main instance only. */
	void UpdateLeftElbowPole(const AFPSRCharacter& Character);
};
