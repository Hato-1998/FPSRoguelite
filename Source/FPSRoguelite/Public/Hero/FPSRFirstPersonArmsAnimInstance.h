// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "FPSRFirstPersonArmsAnimInstance.generated.h"

/**
 * Owner-local AnimInstance base for the first-person ARMS mesh (ADR 0003).
 *
 * Third in the same series as UFPSRCharacterAnimInstance (body) and UFPSRWeaponAnimInstance (weapon mesh): each mesh
 * gets a native base that reads gameplay state once per frame and publishes plain values, so the anim graph never
 * interrogates gameplay objects itself. The arms are the third mesh, so they get the third base.
 *
 * Deliberately thin. The arms' procedural motion — ADS glue, sway, bob, fire kick — is NOT computed here: it lives in
 * AFPSRCharacter::UpdateAimDownSights and is applied to the whole component's transform. That is the reason this graph
 * only needs a weapon-group pose, a montage slot and the two-hand grip IK targets (ADR 0003 axis 1: the pack's poses
 * are adopted, its procedural kernel is not — this project already owns recoil and sight alignment).
 *
 * Grip IK (fparms-gunanchor-ik): both hands read a grip frame in ik_hand_gun BONE SPACE, not world — the weapon is
 * anchored to the ik_hand_gun bone (AFPSRCharacter::AttachWeaponMeshes), which sits OUTSIDE the arm's FK chain, so the
 * hand IK reaching for it can no longer drag it around. Feed RightGripInGunLocation/Rotation and
 * LeftGripInGunLocation/Rotation into Transform (Modify) Bone nodes on ik_hand_r / ik_hand_l with Bone Space = Parent
 * Bone Space.
 *
 * Runs on the OWNER'S MACHINE ONLY and never feeds a gameplay decision (ADR 0003 invariant 12). Nothing here is
 * replicated; the state it reads (aiming, reloading) is replicated elsewhere by its owner.
 *
 * Wiring: assign this class (or a Blueprint child) as the Anim Class on the character's FirstPersonArms COMPONENT, in
 * the Blueprint — the component owns its mesh and graph, no code overwrites them. Per-weapon poses arrive
 * as a linked anim layer from the weapon DataAsset's ArmsAnimLayerClass, exactly as the body's BodyAnimLayerClass works,
 * so a new weapon is a data change with no central code edit.
 */
UCLASS()
class FPSROGUELITE_API UFPSRFirstPersonArmsAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** True while the owner holds aim. Read for the aim pose / additive; the SIGHT ALIGNMENT itself is not this graph's
	 *  job — the character solves it on the component transform so the reticle stays pinned (ADR 0003). */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arms")
	bool bIsAiming = false;

	/** True while a reload is in flight (replicated weapon state). The reload montage plays through the graph's slot;
	 *  this is for graph branching that has to survive a montage that hasn't started or has already blended out. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arms")
	bool bIsReloading = false;

	/** Whether the equipped weapon offers a left-hand grip AND it is currently on THESE arms. False for one-handed /
	 *  melee / unarmed weapons, and false whenever the weapon is in the body's hand instead — branch the IK on it
	 *  rather than assuming a target exists. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arms")
	bool bHasLeftHandGrip = false;

	/** Right-hand mirror of bHasLeftHandGrip above. Every weapon has SOME right-hand grip in practice, but this is
	 *  still gated the same way (false whenever the grip isn't actually resolved on these arms — e.g. no weapon
	 *  equipped) rather than assumed true. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arms")
	bool bHasRightHandGrip = false;

	/** Grip frame in ik_hand_gun BONE SPACE (gun-anchor refactor, ADR: fparms-gunanchor-ik) — feed straight into the
	 *  TransformBone node on ik_hand_r / ik_hand_l with Bone Space = "Parent Bone Space" (ik_hand_gun IS that parent
	 *  in the retarget rig: ik_hand_root > ik_hand_gun > ik_hand_l/r).
	 *
	 *  This replaces a WORLD-space effector on purpose: the world version read the weapon component's transform,
	 *  which is only refreshed by attachment BEFORE this frame's animation update runs — a structural one-frame lag,
	 *  most visible as the grip "swimming" during fast camera turns. The gun-frame value is a CONSTANT between
	 *  equip/part changes (AFPSRCharacter caches it there, not per frame — see GetRightHandGripInGunFrame), so there
	 *  is no per-frame source to lag behind in the first place.
	 *
	 *  Rotator, not FTransform/FQuat, because that is what TransformBone's pins take. Meaningless (zero) when the
	 *  matching bHas*HandGrip is false. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arms")
	FVector RightGripInGunLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arms")
	FRotator RightGripInGunRotation = FRotator::ZeroRotator;

	/** Left-hand mirror of RightGripInGunLocation/RightGripInGunRotation above — same ik_hand_gun bone space, same
	 *  reason, feeds ik_hand_l instead of ik_hand_r. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arms")
	FVector LeftGripInGunLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arms")
	FRotator LeftGripInGunRotation = FRotator::ZeroRotator;

	/** Final IK alpha: the grip weight curve, zeroed when there is no grip to reach for. Feed the IK node's alpha. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arms")
	float LeftHandIKAlpha = 0.0f;

	/** Right-hand mirror of LeftHandIKAlpha above. */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arms")
	float RightHandIKAlpha = 0.0f;

	/** Curve a montage authors to take the left hand OFF the weapon (reload, equip). Absent = 1, which is what an
	 *  ordinary pose wants — an unauthored montage keeps the hand on the grip instead of silently dropping it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Arms")
	FName LeftHandIKWeightCurve = FName(TEXT("LeftHandIKWeight"));

	/** Right-hand mirror of LeftHandIKWeightCurve above. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Arms")
	FName RightHandIKWeightCurve = FName(TEXT("RightHandIKWeight"));

protected:
	virtual void NativeInitializeAnimation() override;

	/** Where the work happens. The engine updates LINKED instances before the main one, so a layer that pulled its
	 *  values would transition on last frame's state — the main instance pushes instead (same shape as the body's
	 *  graph, which measured this). */
	virtual void PreUpdateLinkedInstances(float DeltaSeconds) override;

	/** Fallback for the no-layer case, guarded to run at most once per frame alongside the hook above. */
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	void UpdateAndPublish(float DeltaSeconds);
	void PushToLinkedLayers() const;

	/** True only for the instance the arms component itself owns. Answerable in NativeInitializeAnimation because the
	 *  engine assigns the component's AnimScriptInstance before calling InitializeAnimation on it. */
	bool bIsMainInstance = false;

	/** Frame guard shared by the two update hooks, so whichever arrives first does the work exactly once. */
	uint64 LastPublishedFrame = 0;
};
