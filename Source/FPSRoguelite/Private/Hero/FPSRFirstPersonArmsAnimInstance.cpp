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

	// Game thread only (it walks component attachment state). Asking for THIS component is what makes the answer false
	// whenever the weapon is in the body's hand instead — going down, spectating, or simply no arms authored — so the
	// IK turns off rather than reaching across the map at a gun that isn't here (ADR 0003 invariant 11).
	bHasLeftHandGrip = Character->GetLeftHandGripTransform(GetOwningComponent(), LeftHandGripWorld);
	LeftHandGripLocation = bHasLeftHandGrip ? LeftHandGripWorld.GetLocation() : FVector::ZeroVector;

	// A reload / equip that legitimately takes the hand off the weapon authors this curve to 0. No curve = 1, which is
	// what an ordinary pose wants, so an unauthored montage keeps the hand on the grip instead of silently dropping it.
	float CurveValue = 0.0f;
	const float Weight = (!LeftHandIKWeightCurve.IsNone() && GetCurveValue(LeftHandIKWeightCurve, CurveValue))
		? CurveValue
		: 1.0f;
	LeftHandIKAlpha = bHasLeftHandGrip ? FMath::Clamp(Weight, 0.0f, 1.0f) : 0.0f;

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
		Layer->LeftHandGripWorld = LeftHandGripWorld;
		Layer->LeftHandGripLocation = LeftHandGripLocation;
		Layer->LeftHandIKAlpha = LeftHandIKAlpha;
	}
}
