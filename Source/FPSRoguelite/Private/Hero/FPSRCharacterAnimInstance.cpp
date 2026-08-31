// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hero/FPSRCharacterAnimInstance.h"
#include "Hero/FPSRCharacter.h"
#include "Hero/FPSRCharacterMovementComponent.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRLogChannels.h" // LogFPSR — explicit, not relied on transitively via unity (IWYU)

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h" // GetPlayerViewPoint — explicit, not relied on transitively via unity (IWYU)
#include "HAL/IConsoleManager.h" // TAutoConsoleVariable — explicit, not relied on transitively via unity (IWYU)
#include "Engine/Engine.h" // GEngine — explicit, not relied on transitively via unity (IWYU)

#if ENABLE_DRAW_DEBUG
// Per-character aim pitch source readout, permanent instrumentation (not removed after the AimOffset desync bug is
// closed) — the same class of proxy-vs-owner-vs-server pitch mismatch can recur any time a new pitch source is added,
// and this is the fastest way to see which source disagreed in a live PIE session. Mirrors CVarMovementDebug's shape
// (FPSRCharacter.cpp) so the two debug CVars read the same way.
static TAutoConsoleVariable<int32> CVarAimSyncDebug(
	TEXT("FPSR.Debug.AimSync"),
	0,
	TEXT("Per-character aim pitch source readout. 1 = on-screen line per character, 2 = also throttled LogFPSR dump (for PIE server/client comparison). 0 = off (default)."),
	ECVF_Cheat);
#endif

namespace
{
	/** Signed angle (deg) between a planar velocity and a reference yaw. Same result as
	 *  UKismetAnimationLibrary::CalculateDirection, inlined so this module doesn't take on AnimGraphRuntime for 4 lines. */
	float CalculatePlanarDirection(const FVector& Velocity, const FRotator& BaseRotation)
	{
		const FVector Planar(Velocity.X, Velocity.Y, 0.0f);
		if (Planar.IsNearlyZero())
		{
			return 0.0f;
		}
		return FRotator::NormalizeAxis(Planar.ToOrientationRotator().Yaw - BaseRotation.Yaw);
	}
}

void UFPSRCharacterAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	// The engine assigns the component's AnimScriptInstance BEFORE calling InitializeAnimation on it
	// (USkeletalMeshComponent), so this is already answerable here. A linked anim layer asking the same question gets
	// the MAIN instance back rather than itself, which is exactly the distinction needed.
	const USkeletalMeshComponent* OwningMesh = GetOwningComponent();
	bIsMainInstance = OwningMesh && (OwningMesh->GetAnimInstance() == this);
}

void UFPSRCharacterAnimInstance::PreUpdateLinkedInstances(float DeltaSeconds)
{
	Super::PreUpdateLinkedInstances(DeltaSeconds);
	UpdateAndPublish(DeltaSeconds);
}

void UFPSRCharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	UpdateAndPublish(DeltaSeconds);
}

void UFPSRCharacterAnimInstance::UpdateAndPublish(float DeltaSeconds)
{
	// Linked layers compute nothing — the main pushes into them. This early-out is what keeps the RootYawOffset
	// accumulator single and the game-thread-only grip query down to one caller.
	if (!bIsMainInstance)
	{
		return;
	}

	// Two hooks call this; the first one to arrive does the work. Without the guard the yaw accumulator would advance
	// twice per frame and the legs would lag at double rate.
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

	UpdateFromCharacter(*Character, DeltaSeconds);
	PushToLinkedLayers();
}

void UFPSRCharacterAnimInstance::UpdateFromCharacter(AFPSRCharacter& Character, float DeltaSeconds)
{
	const UFPSRCharacterMovementComponent* Move = Cast<UFPSRCharacterMovementComponent>(Character.GetCharacterMovement());

	// Life state first — the downed branch drops the yaw offset and the IK below. Read straight off the PlayerState
	// (replicated, so this is right on every machine) rather than the character's IsIncapacitatedLocal: that one is an
	// ACTION gate, and this graph is a presentation consumer. Same predicate, different job.
	const AFPSRPlayerState* PS = Character.GetPlayerState<AFPSRPlayerState>();
	bIsDowned = !PS || !PS->IsAlive();
	bIsDead = PS && PS->IsDead();

	const FVector Velocity = Character.GetVelocity();
	Speed = Move ? Move->GetPlanarSpeed() : FVector(Velocity.X, Velocity.Y, 0.0f).Size();
	bIsMoving = Speed > MovingSpeedThreshold;
	bIsFalling = Move && Move->IsFalling();
	// Wall hanging no longer exists (ADR 0001, 2026-08-31): the wall is an instantaneous impulse, so there is never a
	// frame the character is "on" one. Both flags are kept and still published because ABP_Blu_Body reads them —
	// removing them from C++ would break that graph's compile, which is a separate editor-side pass.
	bIsOnWall = false;
	bIsAirborne = bIsFalling;
	StanceBlend = Move ? Move->GetStanceBlend() : 0.0f;

	// IsSlidingForDisplay, not IsSliding: this graph runs on every machine and a teammate's slide is not derivable
	// locally. On top of that, a slide can be shorter than the interval between net updates (jump-cancel), so the
	// replicated flag alone can arrive already false. The serial catches that case — a value we have not seen before
	// means a slide happened — and holds the pose for SlideVisualMinDuration so it is actually visible.
	const bool bSlidingNow = Move && Move->IsSlidingForDisplay();
	// Proxies only. The owner and the server hold the exact flag, so the minimum-display hold could only ever ADD a
	// slide they are not in — most visibly as a 0.2s tail after a correction cancels one.
	const bool bHasExactSlide = Character.HasAuthority() || Character.IsLocallyControlled();
	if (Move && !bHasExactSlide)
	{
		const uint8 Serial = Move->GetSlideVisualSerial();
		if (bHasSlideSerial && Serial != LastSlideVisualSerial)
		{
			SlideVisualHold = SlideVisualMinDuration;
		}
		LastSlideVisualSerial = Serial;
		bHasSlideSerial = true;
	}
	else
	{
		SlideVisualHold = 0.0f;
		bHasSlideSerial = false;
	}
	SlideVisualHold = FMath::Max(0.0f, SlideVisualHold - DeltaSeconds);
	bIsSliding = bSlidingNow || (SlideVisualHold > 0.0f);

	// The blend is eased locally on every machine rather than replicated: it is a ramp over a known duration, so a
	// receiver that knows when the slide started can reproduce it without spending bandwidth on the curve.
	if (Move && bHasExactSlide)
	{
		SlideBlend = Move->GetSlideBlend();
	}
	else
	{
		const float Target = bIsSliding ? 1.0f : 0.0f;
		const float Rate = (SlideVisualBlendDuration > KINDA_SMALL_NUMBER) ? (1.0f / SlideVisualBlendDuration) : 1.0f;
		SlideBlend = FMath::FInterpConstantTo(SlideBlend, Target, DeltaSeconds, Rate);
	}

	// Steering intent, where it exists. Acceleration is NOT replicated — the engine ships
	// p.EnableCharacterAccelerationReplication = 0 (Character.cpp) — and a simulated proxy rebuilds it from velocity
	// (CharacterMovementComponent.cpp: Acceleration = Velocity.GetSafeNormal()). So asking a proxy for acceleration
	// returns a worse copy of Direction while looking like input.
	// Input intent is therefore owner/server-authoritative BY NATURE, and a proxy approximates from travel. That is the
	// right model, not a stopgap: the owner needs the input frame for responsiveness, and no remote observer can tell.
	// Do NOT "fix" this by enabling acceleration replication — it is a global CVar and this game runs 200-300 enemy
	// characters through the same class (project principle 1).
	const bool bHasRealInput = Character.IsLocallyControlled() || Character.HasAuthority();
	const FVector InputAccel = (bHasRealInput && Move) ? Move->GetCurrentAcceleration() : FVector::ZeroVector;
	bHasMovementInput = bHasRealInput ? !InputAccel.IsNearlyZero() : bIsMoving;

	bIsAiming = Character.IsAiming();

	const FRotator ActorRotation = Character.GetActorRotation();
	const FRotator AimRotation = Character.GetBaseAimRotation();

	UpdateRootYawOffset(Character, DeltaSeconds);

	// AimPitch needs no compensation (Rotate Root Bone only turns yaw). On a simulated proxy this is still correct
	// without any work of ours: the engine replicates APawn::RemoteViewPitch16 and GetBaseAimRotation folds it in.
	// The clamp only enforces the -90..90 contract the header already promises (PlayerCameraManager's ViewPitchMin/Max
	// is ±89.9 by default, but nothing in THIS function guarantees a sane source) — it is a contract guard, not a fix.
	AimPitch = FMath::Clamp(FRotator::NormalizeAxis(AimRotation.Pitch - ActorRotation.Pitch), -90.0f, 90.0f);

	// GetBaseAimRotation only falls back to the replicated RemoteViewPitch16 while the actor pitch is nearly zero
	// (APawn::GetBaseAimRotation, UE 5.7). bUseControllerRotationPitch is false so nothing should ever write actor
	// pitch — if something does (ragdoll, root motion, a future slope-align), proxies silently lose their aim pitch.
	// Detect that instead of failing quietly. Say it once (same pattern as the elbow-bone warning below).
	if (!bWarnedActorPitchPollution && !FMath::IsNearlyZero(ActorRotation.Pitch, 0.1))
	{
		bWarnedActorPitchPollution = true;
		UE_LOG(LogFPSR, Warning,
			TEXT("[Anim] %s: actor pitch is %.2f (expected 0) — GetBaseAimRotation's RemoteViewPitch fallback is bypassed on simulated proxies; their AimPitch is now wrong."),
			*GetNameSafe(&Character), ActorRotation.Pitch);
	}

#if ENABLE_DRAW_DEBUG
	// Instrumentation for the AimPitch desync bug: break the final value down by source so a PIE server/client pair
	// can be compared line-for-line instead of guessing which stage disagreed.
	if (CVarAimSyncDebug.GetValueOnGameThread() >= 1 && GEngine)
	{
		const bool bAuthority = Character.HasAuthority();
		const bool bLocal = Character.IsLocallyControlled();
		const TCHAR* RoleTag = (bAuthority && bLocal) ? TEXT("SRV+LOC") : bAuthority ? TEXT("SRV") : bLocal ? TEXT("LOC") : TEXT("SIM");

		const APlayerController* Controller = Character.GetController<APlayerController>();
		FVector ViewLoc = FVector::ZeroVector;
		FRotator ViewRot = FRotator::ZeroRotator;
		if (Controller)
		{
			// void return — fills the outs from the camera cache (or falls back to the view target's actor transform).
			Controller->GetPlayerViewPoint(ViewLoc, ViewRot);
		}

		// Built as FStrings rather than dereferenced Printf temporaries so nothing here relies on a temporary's
		// lifetime surviving into the outer Printf call below.
		const FString CtrlStr = Controller ? FString::Printf(TEXT("%.1f"), Controller->GetControlRotation().Pitch) : TEXT("--");
		const FString ViewStr = Controller ? FString::Printf(TEXT("%.1f"), ViewRot.Pitch) : TEXT("--");

		const FString Msg = FString::Printf(
			TEXT("AIM [%s nm%d] Ctrl %s View %s Base %.1f Actor %.1f => Pitch %.1f"),
			RoleTag, (int32)Character.GetNetMode(), *CtrlStr, *ViewStr,
			AimRotation.Pitch, ActorRotation.Pitch, AimPitch);

		GEngine->AddOnScreenDebugMessage((uint64)(UPTRINT)this, 0.0f,
			Character.IsLocallyControlled() ? FColor::Green : FColor::Orange, Msg);

		if (CVarAimSyncDebug.GetValueOnGameThread() >= 2)
		{
			AimSyncLogAccum += DeltaSeconds;
			if (AimSyncLogAccum >= 0.5f)
			{
				AimSyncLogAccum = 0.0f;
				UE_LOG(LogFPSR, Log, TEXT("%s"), *Msg);
			}
		}
	}
#endif

	// These two subtract the SAME post-clamp RootYawOffset that goes to Rotate Root Bone this frame. The node turns the
	// whole pose by it; subtracting here cancels that, so the feet keep pointing along the real velocity and the upper
	// body keeps facing the crosshair. Get the sign wrong and BOTH compensate backwards at once.
	AimYaw = FRotator::NormalizeAxis(FRotator::NormalizeAxis(AimRotation.Yaw - ActorRotation.Yaw) - RootYawOffset);
	Direction = FRotator::NormalizeAxis(CalculatePlanarDirection(Velocity, ActorRotation) - RootYawOffset);
	InputDirection = (bHasRealInput && bHasMovementInput)
		? FRotator::NormalizeAxis(CalculatePlanarDirection(InputAccel, ActorRotation) - RootYawOffset)
		: Direction;

	// Game thread only (it walks component attachment state) — the reason only the main instance computes at all.
	// Downed drops it so the IK can't drag a collapsed body's arm toward a weapon. Asking for THIS mesh (rather than
	// "the weapon, wherever it is") is what stops the body reaching into camera space when the owner's first-person
	// arms are the ones holding the gun — this graph never needs to know those arms exist (ADR 0003 invariant 11).
	bHasLeftHandGrip = !bIsDowned && Character.GetLeftHandGripTransform(GetOwningComponent(), LeftHandGripWorld);
	LeftHandGripLocation = bHasLeftHandGrip ? LeftHandGripWorld.GetLocation() : FVector::ZeroVector;

	// A reload / equip that legitimately takes the hand off the weapon authors this curve to 0. No curve = 1, which is
	// what locomotion wants, so an unauthored montage keeps the hand on the grip instead of silently dropping it.
	float CurveValue = 0.0f;
	LeftHandIKWeight = (!LeftHandIKWeightCurve.IsNone() && GetCurveValue(LeftHandIKWeightCurve, CurveValue))
		? CurveValue
		: 1.0f;

	LeftHandIKAlpha = bHasLeftHandGrip ? FMath::Clamp(LeftHandIKWeight, 0.0f, 1.0f) : 0.0f;

	UpdateLeftElbowPole(Character);
}

void UFPSRCharacterAnimInstance::UpdateLeftElbowPole(const AFPSRCharacter& Character)
{
	// No usable pole means no safe solve, so the IK goes OFF rather than running with a bad target. A world-space
	// effector chain with a zeroed joint target aims the elbow at the world origin — an arm folded across the body,
	// which reads as a rig bug rather than as a misconfiguration. Failing quiet-and-wrong is worse than failing off.
	const USkeletalMeshComponent* OwningMesh = GetOwningComponent();
	const bool bPoleResolvable = OwningMesh
		&& !LeftElbowBoneName.IsNone()
		&& OwningMesh->GetBoneIndex(LeftElbowBoneName) != INDEX_NONE;

	if (!bPoleResolvable)
	{
		LeftHandJointTargetLocation = FVector::ZeroVector;
		LeftHandIKAlpha = 0.0f;

		// HideBoneByName taught this project that the engine fails bone lookups silently (invariant 9). Say it once.
		if (!bWarnedMissingElbowBone && OwningMesh && !LeftElbowBoneName.IsNone())
		{
			bWarnedMissingElbowBone = true;
			UE_LOG(LogFPSR, Warning,
				TEXT("[Anim] %s: elbow bone '%s' is not on the body skeleton — left-hand IK disabled (no joint target). Set LeftElbowBoneName on the AnimBP."),
				*GetNameSafe(&Character), *LeftElbowBoneName.ToString());
		}
		return;
	}

	// Pole on the elbow itself = the arm's current bend plane, which is the non-flipping default. The offset is the
	// content-side dial for the cases that need steering.
	const FVector ElbowWorld = OwningMesh->GetBoneLocation(LeftElbowBoneName);
	LeftHandJointTargetLocation = LeftElbowPoleOffset.IsNearlyZero()
		? ElbowWorld
		: ElbowWorld + OwningMesh->GetComponentRotation().RotateVector(LeftElbowPoleOffset);
}

void UFPSRCharacterAnimInstance::UpdateRootYawOffset(const AFPSRCharacter& Character, float DeltaSeconds)
{
	const float ActorYaw = Character.GetActorRotation().Yaw;
	if (!bHasPreviousActorYaw)
	{
		PreviousActorYaw = ActorYaw;
		bHasPreviousActorYaw = true;
	}
	const float DeltaYaw = FRotator::NormalizeAxis(ActorYaw - PreviousActorYaw);
	PreviousActorYaw = ActorYaw;

	// Downed first: a dead or downed body has no business holding a turn or a wall.
	if (bIsDowned)
	{
		RootYawOffset = 0.0f;
		bTurningInPlace = false;
		TurnDirection = 0.0f;
		return;
	}

	// Moving, airborne or downed: the legs belong under the capsule, so there is no lag to hold.
	if (bIsMoving || bIsFalling)
	{
		RootYawOffset = 0.0f;
		bTurningInPlace = false;
		TurnDirection = 0.0f;
		return;
	}

	// The capsule turned with the view; turn the POSE back by the same amount so the feet stay planted in the world.
	// Clamped to the AimOffset's yaw range: past it the upper body can no longer twist far enough to cancel the offset
	// and the aim drags visibly behind the crosshair.
	RootYawOffset = FMath::Clamp(FRotator::NormalizeAxis(RootYawOffset - DeltaYaw), -RootYawOffsetMax, RootYawOffsetMax);

	if (!bTurningInPlace && FMath::Abs(RootYawOffset) >= TurnInPlaceStartAngle)
	{
		bTurningInPlace = true;
	}

	if (bTurningInPlace)
	{
		// A NEGATIVE offset means the capsule turned right while the pose was held left, so the body turns RIGHT to catch up.
		TurnDirection = (RootYawOffset < 0.0f) ? 1.0f : -1.0f;

		// The pack's turn-in-place clips are in-place conversions with the root motion stripped, so there is no root
		// delta to consume the offset — this does it, which also keeps the offset's single owner in C++ (invariant 8).
		const float Step = TurnInPlaceRateDegPerSec * DeltaSeconds;
		RootYawOffset = (RootYawOffset > 0.0f)
			? FMath::Max(0.0f, RootYawOffset - Step)
			: FMath::Min(0.0f, RootYawOffset + Step);

		if (FMath::IsNearlyZero(RootYawOffset, 0.5f))
		{
			RootYawOffset = 0.0f;
			bTurningInPlace = false;
			TurnDirection = 0.0f;
		}
	}
}

void UFPSRCharacterAnimInstance::PushToLinkedLayers() const
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
		UFPSRCharacterAnimInstance* Layer = Cast<UFPSRCharacterAnimInstance>(Linked);
		if (!Layer || Layer == this)
		{
			continue;
		}

		Layer->Speed = Speed;
		Layer->Direction = Direction;
		Layer->bIsMoving = bIsMoving;
		Layer->bIsFalling = bIsFalling;
		Layer->bIsOnWall = bIsOnWall;
		Layer->bIsAirborne = bIsAirborne;
		Layer->StanceBlend = StanceBlend;
		Layer->bIsSliding = bIsSliding;
		Layer->SlideBlend = SlideBlend;
		Layer->bHasMovementInput = bHasMovementInput;
		Layer->InputDirection = InputDirection;
		Layer->bIsAiming = bIsAiming;
		Layer->AimPitch = AimPitch;
		Layer->AimYaw = AimYaw;
		Layer->RootYawOffset = RootYawOffset;
		Layer->bTurningInPlace = bTurningInPlace;
		Layer->TurnDirection = TurnDirection;
		Layer->LeftHandGripWorld = LeftHandGripWorld;
		Layer->LeftHandGripLocation = LeftHandGripLocation;
		Layer->LeftHandJointTargetLocation = LeftHandJointTargetLocation;
		Layer->bHasLeftHandGrip = bHasLeftHandGrip;
		Layer->LeftHandIKWeight = LeftHandIKWeight;
		Layer->LeftHandIKAlpha = LeftHandIKAlpha;
		Layer->bIsDowned = bIsDowned;
		Layer->bIsDead = bIsDead;
	}
}
