// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hero/FPSRFirstPersonArmsAnimInstance.h"
#include "Hero/FPSRCharacter.h"
#include "Anim/FPSRGunMotionCurves.h"
#include "Anim/FPSRGunMotionStudioData.h"

#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"

void UFPSRFirstPersonArmsAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	// The engine assigns the component's AnimScriptInstance BEFORE calling InitializeAnimation on it
	// (USkeletalMeshComponent), so this is already answerable here. A linked anim layer asking the same question gets
	// the MAIN instance back rather than itself, which is exactly the distinction needed.
	const USkeletalMeshComponent* OwningMesh = GetOwningComponent();
	bIsMainInstance = OwningMesh && (OwningMesh->GetAnimInstance() == this);
}

void UFPSRFirstPersonArmsAnimInstance::PreUpdateLinkedInstances(float DeltaSeconds)
{
	Super::PreUpdateLinkedInstances(DeltaSeconds);
	UpdateAndPublish(DeltaSeconds);
}

void UFPSRFirstPersonArmsAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	UpdateAndPublish(DeltaSeconds);
}

void UFPSRFirstPersonArmsAnimInstance::UpdateAndPublish(float DeltaSeconds)
{
	// Linked layers compute nothing — the main pushes into them. This early-out is also what keeps the game-thread-only
	// grip query down to a single caller.
	if (!bIsMainInstance)
	{
		return;
	}

	// Two hooks call this; the first to arrive does the work. Harmless to repeat today (nothing here accumulates), but
	// the guard is the contract: it is what lets a future accumulator be added here without a double-rate bug.
	if (LastPublishedFrame == GFrameCounter)
	{
		return;
	}
	LastPublishedFrame = GFrameCounter;

	AFPSRCharacter* Character = Cast<AFPSRCharacter>(TryGetPawnOwner());
	if (!Character)
	{
		return;
	}

	bIsAiming = Character->IsAiming();
	bIsReloading = Character->IsReloading();

	// Gun-anchor IK (fparms-gunanchor-ik): read the grip frame in ik_hand_gun BONE SPACE, not world. A world-space
	// effector read the weapon component's transform, which attachment only refreshes BEFORE this frame's animation
	// update runs — a structural one-frame lag (visible as the grip "swimming" on a fast camera turn). The gun-frame
	// value only changes when the equipped weapon/parts change (AFPSRCharacter caches it there, not per frame), so
	// there is no per-frame source left to lag behind. Game thread only, same reason as before (it walks component
	// attachment state on the cache-refresh path) — asking for THIS component is what makes the answer false whenever
	// the weapon is in the body's hand instead, so the IK turns off rather than reaching for a gun that isn't here
	// (ADR 0003 invariant 11).
	FTransform RightGripInGun;
	bHasRightHandGrip = Character->GetRightHandGripInGunFrame(GetOwningComponent(), RightGripInGun);

	FTransform LeftGripInGun;
	bHasLeftHandGrip = Character->GetLeftHandGripInGunFrame(GetOwningComponent(), LeftGripInGun);

	// P2 curve-channel authoring (GunMotionTool_Spec.md §2-1/§4-1): gun-space offset composed onto the cached grip,
	// re-based toward the currently-playing clip's attach-part LIVE frame when that clip's Blend curve calls for it.
	// Both hands share one clip-metadata lookup (studio data carries both Left/RightHandAttachPartSocket) rather than
	// resolving it twice. A clip with none of the FPGM_H{L,R}_* curves reproduces the pre-P2 value exactly (§8-1).
	const UFPSRGunMotionStudioData* ActiveClipMeta = FindActiveGunMotionClipMeta();
	ComputeHandIKTarget(*Character, /*bIsLeftHand=*/true, bHasLeftHandGrip, LeftGripInGun, ActiveClipMeta,
		LeftGripInGunLocation, LeftGripInGunRotation);
	ComputeHandIKTarget(*Character, /*bIsLeftHand=*/false, bHasRightHandGrip, RightGripInGun, ActiveClipMeta,
		RightGripInGunLocation, RightGripInGunRotation);

	// A reload / equip that legitimately takes a hand off the weapon authors its curve to 0. No curve = 1, which is
	// what an ordinary pose wants, so an unauthored montage keeps the hand on the grip instead of silently dropping it.
	float LeftCurveValue = 0.0f;
	const float LeftWeight = (!LeftHandIKWeightCurve.IsNone() && GetCurveValue(LeftHandIKWeightCurve, LeftCurveValue))
		? LeftCurveValue
		: 1.0f;
	LeftHandIKAlpha = bHasLeftHandGrip ? FMath::Clamp(LeftWeight, 0.0f, 1.0f) : 0.0f;

	float RightCurveValue = 0.0f;
	const float RightWeight = (!RightHandIKWeightCurve.IsNone() && GetCurveValue(RightHandIKWeightCurve, RightCurveValue))
		? RightCurveValue
		: 1.0f;
	RightHandIKAlpha = bHasRightHandGrip ? FMath::Clamp(RightWeight, 0.0f, 1.0f) : 0.0f;

	PushToLinkedLayers();
}

void UFPSRFirstPersonArmsAnimInstance::PushToLinkedLayers() const
{
	const USkeletalMeshComponent* OwningMesh = GetOwningComponent();
	if (!OwningMesh)
	{
		return;
	}

	// Every linked instance of this class, not just one: which layer functions share an instance depends on their anim
	// layer group, and this shouldn't care.
	for (UAnimInstance* Linked : OwningMesh->GetLinkedAnimInstances())
	{
		UFPSRFirstPersonArmsAnimInstance* Layer = Cast<UFPSRFirstPersonArmsAnimInstance>(Linked);
		if (!Layer || Layer == this)
		{
			continue;
		}

		Layer->bIsAiming = bIsAiming;
		Layer->bIsReloading = bIsReloading;
		Layer->bHasLeftHandGrip = bHasLeftHandGrip;
		Layer->bHasRightHandGrip = bHasRightHandGrip;
		Layer->RightGripInGunLocation = RightGripInGunLocation;
		Layer->RightGripInGunRotation = RightGripInGunRotation;
		Layer->LeftGripInGunLocation = LeftGripInGunLocation;
		Layer->LeftGripInGunRotation = LeftGripInGunRotation;
		Layer->LeftHandIKAlpha = LeftHandIKAlpha;
		Layer->RightHandIKAlpha = RightHandIKAlpha;
	}
}

void UFPSRFirstPersonArmsAnimInstance::ComputeHandIKTarget(AFPSRCharacter& Character, bool bIsLeftHand,
	bool bHasGrip, const FTransform& GripInGun, const UFPSRGunMotionStudioData* ActiveClipMeta,
	FVector& OutLocation, FRotator& OutRotation) const
{
	if (!bHasGrip)
	{
		// No grip on this hand at all — nothing to compose an offset onto (mirrors the pre-P2 contract exactly).
		OutLocation = FVector::ZeroVector;
		OutRotation = FRotator::ZeroRotator;
		return;
	}

	using namespace FPSRGunMotionCurves;
	float TX = 0.0f, TY = 0.0f, TZ = 0.0f, RP = 0.0f, RY = 0.0f, RR = 0.0f, Blend = 0.0f;
	GetCurveValue(bIsLeftHand ? HL_TX : HR_TX, TX);
	GetCurveValue(bIsLeftHand ? HL_TY : HR_TY, TY);
	GetCurveValue(bIsLeftHand ? HL_TZ : HR_TZ, TZ);
	GetCurveValue(bIsLeftHand ? HL_RP : HR_RP, RP);
	GetCurveValue(bIsLeftHand ? HL_RY : HR_RY, RY);
	GetCurveValue(bIsLeftHand ? HL_RR : HR_RR, RR);
	GetCurveValue(bIsLeftHand ? HL_Blend : HR_Blend, Blend);
	Blend = FMath::Clamp(Blend, 0.0f, 1.0f);

	// Gun-space offset (already the same frame GripInGun lives in — no space conversion needed, unlike a camera-space
	// channel would require). Composed the way §4-1 specifies: rotation multiplies (left), translation adds directly
	// (both already expressed in that one shared frame).
	const FVector OffsetLoc(TX, TY, TZ);
	const FQuat OffsetQuat = FRotator(RP, RY, RR).Quaternion();

	FVector TargetLoc = GripInGun.GetLocation() + OffsetLoc;
	FQuat TargetQuat = (OffsetQuat * GripInGun.GetRotation()).GetNormalized();

	if (Blend > KINDA_SMALL_NUMBER && ActiveClipMeta)
	{
		const FName AttachSocket = bIsLeftHand
			? ActiveClipMeta->LeftHandAttachPartSocket
			: ActiveClipMeta->RightHandAttachPartSocket;
		FTransform PartFrame;
		if (!AttachSocket.IsNone() && Character.GetWeaponPartFrameInGunSpace(AttachSocket, PartFrame))
		{
			const FVector PartTargetLoc = PartFrame.GetLocation() + OffsetLoc;
			const FQuat PartTargetQuat = (OffsetQuat * PartFrame.GetRotation()).GetNormalized();
			TargetLoc = FMath::Lerp(TargetLoc, PartTargetLoc, Blend);
			TargetQuat = FQuat::Slerp(TargetQuat, PartTargetQuat, Blend).GetNormalized();
		}
		// No attached part for this socket (weapon config without it, or the clip authored none): Blend has nothing
		// to rebase onto, so the grip-based target above stands — the same "absent data -> no-op" convention as an
		// absent curve.
	}

	OutLocation = TargetLoc;
	OutRotation = TargetQuat.Rotator();
}

const UFPSRGunMotionStudioData* UFPSRFirstPersonArmsAnimInstance::FindActiveGunMotionClipMeta() const
{
	// GetActiveMontageInstance returns the first (highest-priority) active montage on this instance — the documented
	// caveat (there might be more than one) is an acceptable P2 approximation: gun-motion clips play either as a solo
	// dynamic montage (studio preview, §5 — out of this scope) or as the arms' one reload montage, and this project
	// doesn't stack a second arms montage on top of either.
	FAnimMontageInstance* MontageInst = GetActiveMontageInstance();
	if (!MontageInst || !MontageInst->Montage)
	{
		return nullptr;
	}

	// GetAnimationData is FAnimSegment's API (not FAnimTrack's) — walk the slot's track to the segment at the current
	// position and read its referenced sequence.
	const float Position = MontageInst->GetPosition();
	for (const FSlotAnimationTrack& SlotTrack : MontageInst->Montage->SlotAnimTracks)
	{
		if (const FAnimSegment* Segment = SlotTrack.AnimTrack.GetSegmentAtTime(Position))
		{
			// GetAssetUserData<T>() is a non-const interface method — the sequence pointer is taken non-const.
			if (UAnimSequenceBase* ActiveSeq = Segment->GetAnimReference())
			{
				if (const UFPSRGunMotionStudioData* Meta = ActiveSeq->GetAssetUserData<UFPSRGunMotionStudioData>())
				{
					return Meta;
				}
			}
		}
	}
	return nullptr;
}
