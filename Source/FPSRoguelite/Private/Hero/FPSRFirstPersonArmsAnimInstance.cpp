// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hero/FPSRFirstPersonArmsAnimInstance.h"
#include "Hero/FPSRCharacter.h"

#include "Components/SkeletalMeshComponent.h"

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
	RightGripInGunLocation = bHasRightHandGrip ? RightGripInGun.GetLocation() : FVector::ZeroVector;
	RightGripInGunRotation = bHasRightHandGrip ? RightGripInGun.Rotator() : FRotator::ZeroRotator;

	FTransform LeftGripInGun;
	bHasLeftHandGrip = Character->GetLeftHandGripInGunFrame(GetOwningComponent(), LeftGripInGun);
	LeftGripInGunLocation = bHasLeftHandGrip ? LeftGripInGun.GetLocation() : FVector::ZeroVector;
	LeftGripInGunRotation = bHasLeftHandGrip ? LeftGripInGun.Rotator() : FRotator::ZeroRotator;

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
