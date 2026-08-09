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
	 *  matching bHas*HandGrip is false.
	 *
	 *  v3 (GunMotionTool_Spec.md §18-19, P1): no longer a straight cache pass-through. FPGM_HR_TX/TY/TZ/RP/RY/RR
	 *  (GUN space — already this exact frame, no M conversion) are composed on top of the cached grip, and when
	 *  FPGM_HR_Blend > 0 the result is lerped/slerped toward the currently-playing clip's attach-part frame
	 *  (AFPSRCharacter::GetWeaponPartFrameInGunSpace, keyed by the clip's RightHandAttachPartSocket metadata) — see
	 *  ComputeHandIKTarget. A clip with none of those curves authored reproduces the pre-v3 value exactly (0
	 *  offset, 0 blend), which is the P1 regression contract (§21). */
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

	/** v3 curve-channel authoring (GunMotionTool_Spec.md §18-19, P1): whole-gun offset read from FPGM_Gun_TX/TY/TZ/
	 *  RP/RY/RR (CAMERA space) and converted to COMPONENT space via the runtime M = ArmsRot^-1 * CamRot (both
	 *  components' actual world rotations, recomputed every publish — see ComputeCameraToArmsRotation). Feed
	 *  straight into a Component-space additive TransformBone(ik_hand_gun) node's Translation/Rotation pins
	 *  (§19-2 — that ABP node is out of this change's scope; only the published names have to match). Because the
	 *  two-hand IK targets below are expressed in GUN space (ik_hand_gun bone-parent space, same as
	 *  Right/LeftGripInGun*), and ik_hand_l/r sit as CHILDREN of ik_hand_gun in the retarget rig, this offset
	 *  carries both hands along automatically without touching their targets (§19-1) — no curve here means
	 *  identity (zero), which is a pure pass-through of the existing gun-anchor pose.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arms")
	FVector GunOffsetLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "FPSR|Arms")
	FRotator GunOffsetRotation = FRotator::ZeroRotator;

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

	/** v3 §18-19 P1: the runtime camera->arms rotation M, from the two components' ACTUAL WORLD rotations (never a
	 *  CDO/editor path — this has to stay correct as the owner turns their view every frame). M = ArmsRot^-1 *
	 *  CamRot (GunMotionTool_Spec.md §19-1 / §3-2's Ocomp = M * Ocam * M^-1 formula, reused verbatim). Identity
	 *  when either component is missing. Cost: two GetComponentQuat() reads + one quaternion multiply, once per
	 *  publish — negligible next to the rest of this owner-local pass. */
	FQuat ComputeCameraToArmsRotation(const class AFPSRCharacter& Character) const;

	/** v3 §18-19 P1: compose the FPGM_H{L,R}_TX..RR gun-space offset onto GripInGun, then — when the matching
	 *  Blend curve is > 0 AND ActiveClipMeta resolves an attach-part socket for this hand AND that part is
	 *  currently attached (AFPSRCharacter::GetWeaponPartFrameInGunSpace) — lerp/slerp the result toward the same
	 *  offset composed onto the part's frame instead (§18 Blend semantics: 0 = grip, 1 = part). Zeroes the output
	 *  when bHasGrip is false (mirrors the pre-v3 bHas*HandGrip contract exactly — a weapon with no grip on this
	 *  hand has no gun-space frame to compose against in the first place). */
	void ComputeHandIKTarget(class AFPSRCharacter& Character, bool bIsLeftHand, bool bHasGrip,
		const FTransform& GripInGun, const class UFPSRGunMotionAuthoringData* ActiveClipMeta,
		FVector& OutLocation, FRotator& OutRotation) const;

	/** v3 §18-19 P1: AssetUserData of the currently-playing leaf AnimSequence on this instance's highest-priority
	 *  active montage (any slot) — i.e. the gun-motion clip metadata (Left/RightHandAttachPartSocket,
	 *  FPSRGunMotionAuthoringData.h) that ComputeHandIKTarget's Blend rebase needs. Null when no montage is active
	 *  or the active leaf carries none (ordinary poses — the overwhelming common case; §19-1's "absent curve = no
	 *  offset" convention extends the same way to this metadata: no data found, no rebase, ComputeHandIKTarget
	 *  just returns the grip-based target). GAME THREAD ONLY, same class of caveat as GetCurveValue itself. */
	const class UFPSRGunMotionAuthoringData* FindActiveGunMotionClipMeta() const;

	/** True only for the instance the arms component itself owns. Answerable in NativeInitializeAnimation because the
	 *  engine assigns the component's AnimScriptInstance before calling InitializeAnimation on it. */
	bool bIsMainInstance = false;

	/** Frame guard shared by the two update hooks, so whichever arrives first does the work exactly once. */
	uint64 LastPublishedFrame = 0;
};
