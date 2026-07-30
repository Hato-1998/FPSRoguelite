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

	/** Wall-hang (CMOVE_WallHang). No wall animation exists yet: the layer routes this to the air state so a wall-hung
	 *  player is not left running a grounded locomotion pose. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	bool bIsOnWall = false;

	/** 0 fully standing .. 1 fully crouched. Continuous on purpose — blend the stance with it instead of switching on a
	 *  bool, or the body snaps while the camera's own stance blend is still easing (UpdateStanceCamera). */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	float StanceBlend = 0.0f;

	// NOTE: no bIsSliding / SlideBlend yet. The slide pose is a later step (no slide animation exists in the project at
	// all), and whether the movement component's slide state reads correctly on a SIMULATED PROXY is unverified — that
	// is the first thing to check when the slide pose lands, since this graph runs on every machine.

	// --- Aim ---

	/** Aim-down-sights state, valid on every machine (replicated presentation bit — see AFPSRCharacter::IsAiming). */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	bool bIsAiming = false;

	/** View pitch relative to the actor, -90..90. Correct on simulated proxies for free: the engine already replicates
	 *  APawn::RemoteViewPitch16 (COND_SkipOwner) and GetBaseAimRotation folds it in. No extra replication. */
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

	/** False when this weapon authors no left-hand grip (melee / unarmed / the pack's bullpup handguards have no
	 *  SOCKET_LeftHand). Gate the IK alpha on it — there is no target to reach for. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	bool bHasLeftHandGrip = false;

	/** Authored per-montage weight for the left-hand IK, so a reload/equip that legitimately takes the hand off the
	 *  weapon can release the IK. 1 when the curve is absent, which is what locomotion wants. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Anim")
	float LeftHandIKWeight = 1.0f;

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

private:
	/** True on the character's own AnimBP instance, false on a linked anim layer. Decided in
	 *  NativeInitializeAnimation by asking the mesh which instance is ITS anim instance — the engine assigns that member
	 *  before calling InitializeAnimation on it, so the answer is already valid there. */
	bool bIsMainInstance = false;

	/** Actor yaw last frame, for the RootYawOffset accumulation. */
	float PreviousActorYaw = 0.0f;
	bool bHasPreviousActorYaw = false;

	/** Frame the values were last published, so the two hooks above can't both run. Sentinel = "never". */
	uint64 LastPublishedFrame = TNumericLimits<uint64>::Max();

	/** Compute + push, at most once per frame, main instance only. */
	void UpdateAndPublish(float DeltaSeconds);

	/** Full recompute. Main instance only. */
	void UpdateFromCharacter(AFPSRCharacter& Character, float DeltaSeconds);

	/** Copy every published value into each linked layer of this class. Main instance only. */
	void PushToLinkedLayers() const;

	void UpdateRootYawOffset(const AFPSRCharacter& Character, float DeltaSeconds);
};
