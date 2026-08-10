// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hero/FPSRCharacterMovementComponent.h"

#include "Components/CapsuleComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Hero/FPSRCharacter.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

UFPSRCharacterMovementComponent::UFPSRCharacterMovementComponent()
{
	// The shared body AnimBP runs on every machine, so a teammate's slide and the wall they are holding have to reach
	// the machines that cannot derive them. Movement itself still travels in the move packets — this carries only the
	// handful of bytes that are pure presentation. Same reason UFPSRWeaponFireComponent replicates bIsAiming.
	SetIsReplicatedByDefault(true);
	bSlidingVisual = 0;

	// Crouch is engine-native (bWantsToCrouch is already predicted and already in the move packet), so the slide can
	// ride on it without a custom flag — but it only works if crouching is actually enabled on the component.
	NavAgentProps.bCanCrouch = true;

	// The engine default of 0.05 leaves essentially no air steering, which doesn't fit a design where the player
	// jumps out of slides, fights mid-air and lands into cover. Overridable per-hero in the Blueprint.
	AirControl = 0.4f;

	// Seed the cap from the layers rather than leaving the engine's own MaxWalkSpeed default in place, so the
	// composition below owns the value from the very first frame.
	RefreshWalkSpeedCap();
}

void UFPSRCharacterMovementComponent::SetAuthoredBaseWalkSpeed(float InSpeed)
{
	AuthoredBaseWalkSpeed = FMath::Max(0.0f, InSpeed);
	RefreshWalkSpeedCap();
}

void UFPSRCharacterMovementComponent::SetLoadoutWalkSpeed(float InSpeed)
{
	LoadoutWalkSpeed = FMath::Max(0.0f, InSpeed);
	RefreshWalkSpeedCap();
}

void UFPSRCharacterMovementComponent::SetMoveSpeedMultiplier(float InMultiplier)
{
	MoveSpeedMultiplier = FMath::Max(0.0f, InMultiplier);
	RefreshWalkSpeedCap();
}

void UFPSRCharacterMovementComponent::SetDownedLocomotion(bool bInDowned)
{
	bDownedLocomotion = bInDowned;
	RefreshWalkSpeedCap();
}

void UFPSRCharacterMovementComponent::RefreshWalkSpeedCap()
{
	// Downed wins outright — it is a hard stop, not something the other layers may scale back up. The loadout
	// baseline then takes precedence over the authored one when the equipped weapon states a speed of its own
	// (0 = "no opinion"), and the card multiplier scales whichever baseline won so a speed card keeps working on
	// the melee slot instead of dropping the player back to the authored 600.
	const float Baseline = bDownedLocomotion
		? DownedWalkSpeed
		: ((LoadoutWalkSpeed > 0.0f) ? LoadoutWalkSpeed : AuthoredBaseWalkSpeed);

	MaxWalkSpeed = Baseline * MoveSpeedMultiplier;
}

bool UFPSRCharacterMovementComponent::IsSpecialMovementAllowed() const
{
	const AFPSRCharacter* FPSROwner = Cast<AFPSRCharacter>(CharacterOwner);
	return FPSROwner ? FPSROwner->CanPerformSpecialMovement() : true;
}

bool UFPSRCharacterMovementComponent::CanEnterSlide() const
{
	// Every term below is either replicated or already predicted, so the client and the server evaluate this to the
	// same answer for the same move — that is what lets the derived slide state survive a correction replay.
	if (bIsSliding || !CharacterOwner)
	{
		return false;
	}
	if (!bWantsToCrouch || !IsMovingOnGround())
	{
		return false;
	}
	if (SlideCooldownRemaining > 0.0f)
	{
		return false; // anti-spam: see SlideCooldown
	}
	if (!IsSpecialMovementAllowed())
	{
		return false;
	}
	// Entry must be at least as strict as the exit. If a designer ever tunes SlideMinSpeed above SlideMinEnterSpeed the
	// raw values would let a slide start and immediately satisfy its own too-slow exit, flickering every frame; taking
	// the max makes a mistuned pair merely hard to trigger instead of visibly broken.
	const float EnterThreshold = FMath::Max(SlideMinEnterSpeed, SlideMinSpeed);
	return Velocity.SizeSquared2D() >= FMath::Square(EnterThreshold);
}

void UFPSRCharacterMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// SkipOwner throughout: the owning client derives every one of these from its own predicted move, exactly, and a
	// late server copy would only fight that. Non-owning clients have no way to derive them at all.
	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	Params.Condition = COND_SkipOwner;
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSRCharacterMovementComponent, bSlidingVisual, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSRCharacterMovementComponent, SlideVisualSerial, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSRCharacterMovementComponent, WallYawByte, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSRCharacterMovementComponent, WallSideSign, Params);
}

void UFPSRCharacterMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation,
	const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);

	// After the move, so what is read here is the settled result rather than a mid-replay intermediate. Simulated
	// proxies never reach this (they go through SimulateMovement), which is exactly right: they are the ones being
	// told.
	RefreshReplicatedVisualState();
}

void UFPSRCharacterMovementComponent::RefreshReplicatedVisualState()
{
	const ACharacter* Owner = CharacterOwner;
	if (!Owner)
	{
		return;
	}
	// AUTHORITY ONLY. The owning client must not touch these: they are not carried in FSavedMove_FPSR, so a correction
	// replay would re-run a past slide entry and bump the serial again, and the owner would then play a slide it had
	// just been corrected out of. The owner has no use for them anyway — IsSlidingForDisplay() hands it the exact
	// local value, never this copy.
	const bool bAuthority = Owner->HasAuthority();
	if (!bAuthority)
	{
		return;
	}

	if (bIsSliding != (bSlidingVisual != 0))
	{
		bSlidingVisual = bIsSliding ? 1 : 0;
		if (bIsSliding)
		{
			// Wraps, deliberately: the receiver only ever asks "is this different from the last one I saw".
			++SlideVisualSerial;
			MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRCharacterMovementComponent, SlideVisualSerial, this);
		}
		MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRCharacterMovementComponent, bSlidingVisual, this);
	}

	if (IsOnWall())
	{
		const FVector Normal2D = WallNormal.GetSafeNormal2D();
		if (!Normal2D.IsNearlyZero())
		{
			const float WallYaw = FMath::RadiansToDegrees(FMath::Atan2(Normal2D.Y, Normal2D.X));
			const uint8 NewByte = FRotator::CompressAxisToByte(WallYaw);
			if (NewByte != WallYawByte)
			{
				WallYawByte = NewByte;
				MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRCharacterMovementComponent, WallYawByte, this);
			}
		}
	}
}

bool UFPSRCharacterMovementComponent::IsSlidingForDisplay() const
{
	// Anywhere the exact value exists, use it — bSlidingVisual is a network copy and is by definition at best as fresh.
	const ACharacter* Owner = CharacterOwner;
	if (Owner && (Owner->HasAuthority() || Owner->IsLocallyControlled()))
	{
		return bIsSliding;
	}
	return bSlidingVisual != 0;
}

float UFPSRCharacterMovementComponent::GetWallYawForDisplay() const
{
	// The exact normal where it exists, the quantised copy where it does not. The two differ by well under a degree,
	// so nobody sees a different body angle depending on whose screen it is.
	const FVector Normal2D = WallNormal.GetSafeNormal2D();
	if (!Normal2D.IsNearlyZero())
	{
		return FMath::RadiansToDegrees(FMath::Atan2(Normal2D.Y, Normal2D.X));
	}
	return FRotator::DecompressAxisFromByte(WallYawByte);
}

void UFPSRCharacterMovementComponent::StartSliding()
{
	bIsSliding = true;
	SlideElapsed = 0.0f;
	SlideSlopeSpeedBonus = 0.0f;

	// Entry impulse ("순간 가속"): scale the existing planar velocity and keep the vertical component untouched so a
	// slide started while settling onto the floor doesn't cancel the landing.
	const FVector PlanarVelocity = FVector(Velocity.X, Velocity.Y, 0.0f);
	const float CurrentSpeed = PlanarVelocity.Size();

	// The impulse may only LIFT speed toward SlideMaxEntrySpeed, never pull it down. Capping the boosted value alone
	// would make a fast entry (downhill momentum carried through a jump) slower than just walking into the slide;
	// taking the max with the incoming speed keeps that momentum. Meanwhile re-sliding at or above the entry cap gains
	// nothing, which is what stops jump-slide cycles from ratcheting speed upward.
	const float BoostedSpeed = FMath::Min(
		FMath::Max(CurrentSpeed, FMath::Min(CurrentSpeed * SlideEnterSpeedMultiplier, SlideMaxEntrySpeed)),
		SlideMaxSpeed);

	const FVector SlideDirection = PlanarVelocity.GetSafeNormal();
	if (!SlideDirection.IsNearlyZero())
	{
		Velocity.X = SlideDirection.X * BoostedSpeed;
		Velocity.Y = SlideDirection.Y * BoostedSpeed;
	}
	SlideEntrySpeed = BoostedSpeed; // SlideSpeedCurve is relative to this
}

void UFPSRCharacterMovementComponent::StopSliding()
{
	// Velocity is deliberately left alone. That is what makes a jump-cancelled slide keep its speed: the character
	// leaves the ground carrying the slide velocity, and airborne friction/braking are both 0 by engine default, so
	// nothing bleeds it off mid-air.
	// The crouch itself keeps following bWantsToCrouch, so holding the key after a slide leaves the player crouched.
	if (!bIsSliding)
	{
		return; // don't let a redundant call refresh the cooldown
	}
	bIsSliding = false;
	SlideElapsed = 0.0f;
	SlideEntrySpeed = 0.0f;
	SlideSlopeSpeedBonus = 0.0f;
	// Charged on EVERY exit path, so jump-cancelling isn't a way to dodge the anti-spam lockout.
	SlideCooldownRemaining = SlideCooldown;

	// Hand the run ramp back at its END, not at zero. A slide exits faster than running speed, so resuming the curve
	// from t=0 would make GetMaxSpeed collapse and slam the player to a halt; from the end, the excess simply decays.
	// Whichever ramp the player is about to be on — a slide usually ends with the crouch key still held.
	if (const UCurveFloat* ResumeCurve = GetActiveGroundSpeedCurve())
	{
		float CurveMinTime = 0.0f, CurveMaxTime = 0.0f;
		ResumeCurve->GetTimeRange(CurveMinTime, CurveMaxTime);
		GroundAccelElapsed = CurveMaxTime;
	}
}

// --- Wall hang -----------------------------------------------------------------------------------------------------

bool UFPSRCharacterMovementComponent::ProbeWall(const FVector& Direction, float HeightAboveCapsuleBottom, FHitResult& OutHit) const
{
	OutHit.Reset();

	const UCapsuleComponent* Capsule = CharacterOwner ? CharacterOwner->GetCapsuleComponent() : nullptr;
	const UWorld* World = GetWorld();
	if (!Capsule || !World || !UpdatedComponent)
	{
		return false;
	}

	// Horizontal only: a wall probe that tilts with the aim would find the floor when looking down.
	const FVector ProbeDirection = Direction.GetSafeNormal2D();
	if (ProbeDirection.IsNearlyZero())
	{
		return false;
	}

	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();

	// Clamped so a shrunken (crouched) capsule can't probe from a point floating outside itself.
	const float ProbeHeight = FMath::Clamp(HeightAboveCapsuleBottom, 0.0f, 2.0f * HalfHeight);
	const FVector Start = UpdatedComponent->GetComponentLocation() + FVector(0.0f, 0.0f, ProbeHeight - HalfHeight);
	const FVector End = Start + (ProbeDirection * (CapsuleRadius + WallProbeDistance));

	// Starting from the capsule's AXIS rather than its surface: a sphere smaller than the capsule radius can't already
	// be inside a wall the capsule is merely touching, so the sweep reports a real surface instead of an overlap.
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRWallProbe), false, CharacterOwner);
	FCollisionResponseParams ResponseParams;
	InitCollisionParams(QueryParams, ResponseParams);

	return World->SweepSingleByChannel(OutHit, Start, End, FQuat::Identity, UpdatedComponent->GetCollisionObjectType(),
		FCollisionShape::MakeSphere(WallProbeRadius), QueryParams, ResponseParams);
}

bool UFPSRCharacterMovementComponent::IsSurfaceGrabbable(const FHitResult& Hit) const
{
	if (!Hit.bBlockingHit)
	{
		return false;
	}

	// A vertical face has a purely horizontal normal, and the more the surface leans the more vertical its normal
	// gets — so comparing |Normal.Z| against the cosine of the required steepness is the walkable-floor test read the
	// other way round. Absolute value so overhangs (normal tilted downward) are judged on steepness alone.
	const float MaxNormalZ = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(WallMinSteepnessDegrees, 0.0f, 90.0f)));
	return FMath::Abs(Hit.ImpactNormal.Z) <= MaxNormalZ;
}

bool UFPSRCharacterMovementComponent::IsInputIntoWall() const
{
	const FVector InputDirection = Acceleration.GetSafeNormal2D();
	if (InputDirection.IsNearlyZero() || WallNormal.IsNearlyZero())
	{
		return false;
	}
	return FVector::DotProduct(InputDirection, -WallNormal) >= WallClimbInputDot;
}

bool UFPSRCharacterMovementComponent::CanGrabWall(FVector& OutWallNormal) const
{
	OutWallNormal = FVector::ZeroVector;

	// Every term is either replicated or already predicted, so client and server reach the same answer for the same
	// move — the same property that makes the slide's derived state survive a correction replay.
	if (!CharacterOwner || !IsFalling() || bWallHangConsumed || !IsSpecialMovementAllowed())
	{
		return false;
	}

	// A grab is an intent, so it needs input. Falling past a wall with no keys held is just falling.
	const FVector InputDirection = Acceleration.GetSafeNormal2D();
	if (InputDirection.IsNearlyZero())
	{
		return false;
	}

	FHitResult WallHit;
	if (!ProbeWall(InputDirection, WallGrabProbeHeight, WallHit) || !IsSurfaceGrabbable(WallHit))
	{
		return false;
	}

	const FVector HitNormal = WallHit.ImpactNormal.GetSafeNormal2D();
	if (HitNormal.IsNearlyZero())
	{
		return false;
	}

	// Must be pushing INTO the wall, not merely alongside it — brushing past a corner while strafing is not a grab.
	if (FVector::DotProduct(InputDirection, -HitNormal) < WallClimbInputDot)
	{
		return false;
	}

	OutWallNormal = HitNormal;
	return true;
}

void UFPSRCharacterMovementComponent::StartWallHang(const FVector& InWallNormal)
{
	// Set before the mode change: OnMovementModeChanged does the rest of the entry bookkeeping and the wall is the one
	// piece it can't derive for itself.
	WallNormal = InWallNormal;

	// Latch which shoulder goes to the wall, ONCE, here. The clip is authored side-on, so there are two ways to stand
	// against any wall; picking the nearer one every frame would spin the whole body through 172 degrees the moment
	// the player's view crossed the midpoint. Choosing at entry means the body is committed for the length of the
	// hold, which is at most WallHangMaxDuration.
	const FVector Normal2D = WallNormal.GetSafeNormal2D();
	if (CharacterOwner && !Normal2D.IsNearlyZero())
	{
		const float WallYaw = FMath::RadiansToDegrees(FMath::Atan2(Normal2D.Y, Normal2D.X));
		const float ActorYaw = CharacterOwner->GetActorRotation().Yaw;
		const float ErrPlus = FMath::Abs(FRotator::NormalizeAxis((WallYaw + WallPoseSideAngle) - ActorYaw));
		const float ErrMinus = FMath::Abs(FRotator::NormalizeAxis((WallYaw - WallPoseSideAngle) - ActorYaw));
		const int8 NewSign = (ErrPlus <= ErrMinus) ? 1 : -1;
		WallSideSign = NewSign;
		// Dirty the yaw HERE as well as in RefreshReplicatedVisualState. The movement mode travels in the move packet
		// while these travel as properties, so they can never be made truly atomic — but marking them on the entry
		// frame is what keeps the gap to at most one update instead of leaving a proxy turning the body to the
		// PREVIOUS wall's angle until the next time the quantised value happens to change.
		WallYawByte = FRotator::CompressAxisToByte(WallYaw);
		if (CharacterOwner->HasAuthority())
		{
			MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRCharacterMovementComponent, WallSideSign, this);
			MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRCharacterMovementComponent, WallYawByte, this);
		}
	}

	SetMovementMode(MOVE_Custom, CMOVE_WallHang);
}

void UFPSRCharacterMovementComponent::StopWallHang()
{
	if (!IsOnWall())
	{
		return;
	}
	// Always into a fall, never straight into walking: the engine's falling step finds the floor and lands properly,
	// whereas dropping into MOVE_Walking from mid-air would snap the capsule to whatever is below.
	SetMovementMode(MOVE_Falling);
}

void UFPSRCharacterMovementComponent::UpdateWallHang(float DeltaSeconds)
{
	WallHangElapsed += DeltaSeconds;

	const bool bClimbing = IsInputIntoWall();
	WallSlipElapsed = bClimbing ? 0.0f : (WallSlipElapsed + DeltaSeconds);

	// Invariant 8: a freeze or a DBNO stops the movement already in progress, not only its start.
	if (!IsSpecialMovementAllowed())
	{
		StopWallHang();
		return;
	}

	// Invariant 7: an unconditional time bound. A wall taller than one grab can climb would otherwise be a state with
	// no way out as long as the player keeps holding the key.
	if (WallHangElapsed >= WallHangMaxDuration)
	{
		StopWallHang();
		return;
	}

	// Exit 2 — "미끄러져 떨어짐". Releasing the key slips down the wall for a moment before the hands come off, so a
	// stutter of the key resumes the climb instead of dropping the player.
	if (!bClimbing && WallSlipElapsed >= WallSlipMaxDuration)
	{
		StopWallHang();
		return;
	}

	// One probe per frame, answering two questions at once: is the wall still there (a door destroyed underneath the
	// player — ADR 0001's failure flow), and have the feet cleared its top (the climb succeeded). Re-reading the normal
	// also lets a hang follow a gently curving surface instead of fighting it.
	FHitResult WallHit;
	if (ProbeWall(-WallNormal, WallHoldProbeHeight, WallHit) && IsSurfaceGrabbable(WallHit))
	{
		const FVector RefreshedNormal = WallHit.ImpactNormal.GetSafeNormal2D();
		if (!RefreshedNormal.IsNearlyZero())
		{
			WallNormal = RefreshedNormal;
		}
		return;
	}

	// Exit 1 — the wall ran out while climbing into it, which means we went over the top. The nudge forward and up is
	// what actually puts the player ON the ledge: at this point the capsule is still beside the wall, so without it the
	// climb ends by falling straight back down and the whole state achieves nothing.
	// Losing the wall while NOT climbing is the destroyed-wall case instead, and that one just falls.
	if (bClimbing)
	{
		Velocity += (-WallNormal * WallTopBoostForward) + (FVector::UpVector * WallTopBoostUp);
	}
	StopWallHang();
}

void UFPSRCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	const bool bWasOnWall = (PreviousMovementMode == MOVE_Custom) && (PreviousCustomMode == CMOVE_WallHang);

	// Super clears the floor and the movement base for any non-walking mode, which is exactly what a wall hang wants;
	// it also leaves Velocity alone on the way into a fall, so an exit boost applied beforehand survives.
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

	const bool bOnWallNow = IsOnWall();
	if (bOnWallNow == bWasOnWall)
	{
		return;
	}

	if (bOnWallNow)
	{
		// Remember what the player arrived with BEFORE anything is thrown away — this is the whole basis of the entry
		// momentum, and after this frame the incoming velocity is gone.
		WallEntryVelocity = FVector(Velocity.X, Velocity.Y, 0.0f);
		WallHangElapsed = 0.0f;
		WallSlipElapsed = 0.0f;

		// The grab arrests the FALL — carrying the plunge into the hang would drag the player straight back down the
		// moment they caught the wall. What survives is the part that runs ALONG the wall, which is what the glide then
		// bleeds off over WallEntryMomentumDuration. Killing the horizontal too was what made a fast approach read as
		// hitting a brick wall.
		Velocity = FVector::VectorPlaneProject(WallEntryVelocity, WallNormal);

		if (CharacterOwner)
		{
			// Grabbing a wall hands back exactly one jump: the wall jump itself. It has to, or the jump exit is dead on
			// arrival — a player who jumped to REACH the wall has already spent their only jump when JumpMaxCount is 1,
			// and JumpIsAllowedInternal then refuses.
			// Min() rather than 0 on purpose: zeroing would ALSO refund the extra air jumps a double-jump card grants
			// (ADR 0001 card scope A), so jump -> air jump -> grab -> wall jump -> air jump would stack for free. This
			// only ever tops the budget up to "one left" and never takes away what the player already had.
			//
			// It happens on the GRAB rather than inside DoJump because ACharacter::CheckJumpInput evaluates CanJump()
			// — the budget check — before it calls DoJump at all, so a refund made there would arrive too late to
			// authorise the very jump it exists for. The visible consequence is that touching a wall and then slipping
			// off, instead of jumping, still leaves the refunded jump in hand. That is bounded at one per airborne
			// period by bWallHangConsumed (no second grab until landing), so it reads as "touching a wall refreshes
			// your jump" rather than as something to farm.
			CharacterOwner->JumpCurrentCount = FMath::Min(CharacterOwner->JumpCurrentCount, FMath::Max(0, CharacterOwner->JumpMaxCount - 1));
			CharacterOwner->JumpCurrentCountPreJump = CharacterOwner->JumpCurrentCount;
		}
	}
	else
	{
		// Charged on EVERY exit — climbed over, slipped off, jumped away, timed out, frozen — so no route dodges the
		// once-per-airborne budget.
		bWallHangConsumed = true;
		WallHangElapsed = 0.0f;
		WallSlipElapsed = 0.0f;
		WallNormal = FVector::ZeroVector;
		WallEntryVelocity = FVector::ZeroVector;
	}
}

float UFPSRCharacterMovementComponent::GetWallEntryMomentumAlpha() const
{
	if (WallEntryMomentumDuration <= 0.0f)
	{
		return 0.0f;
	}
	return FMath::Clamp(1.0f - (WallHangElapsed / WallEntryMomentumDuration), 0.0f, 1.0f);
}

FVector UFPSRCharacterMovementComponent::ComputeWallJumpDirection(const FVector& InWallNormal) const
{
	const FVector OutwardDirection = InWallNormal.GetSafeNormal2D();
	if (OutwardDirection.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}

	// Where the player is looking. Reading it off the pawn rather than the controller keeps this on the same footing as
	// the slide's steering: the pawn's rotation is part of the replicated move, so client and server agree.
	const FVector FacingDirection = UpdatedComponent ? UpdatedComponent->GetForwardVector().GetSafeNormal2D() : FVector::ZeroVector;

	FVector JumpDirection = (FacingDirection * WallJumpAimBlend) + (OutwardDirection * (1.0f - WallJumpAimBlend));
	JumpDirection = JumpDirection.GetSafeNormal2D();
	if (JumpDirection.IsNearlyZero())
	{
		// Looking exactly into the wall at a 50/50 blend cancels the two terms out entirely.
		return OutwardDirection;
	}

	// Guarantee a minimum push away from the surface. Split the blended direction into its along-the-wall and
	// away-from-the-wall parts, raise only the second, and re-normalize — this preserves as much of the player's aim as
	// the floor allows instead of snapping the whole jump back to the normal.
	const float OutwardAmount = FVector::DotProduct(JumpDirection, OutwardDirection);
	if (OutwardAmount >= WallJumpMinOutward)
	{
		return JumpDirection;
	}

	// Rebuilt from unit parts rather than by adding the shortfall on: the two components of a unit vector are
	// sin and cos of the same angle, so this lands on exactly WallJumpMinOutward instead of near it.
	const FVector AlongWall = FVector::VectorPlaneProject(JumpDirection, OutwardDirection).GetSafeNormal2D();
	if (AlongWall.IsNearlyZero())
	{
		return OutwardDirection; // aimed straight at (or straight out of) the wall — nothing sideways to preserve
	}
	const float OutwardShare = FMath::Clamp(WallJumpMinOutward, 0.0f, 1.0f);
	return ((AlongWall * FMath::Sqrt(1.0f - (OutwardShare * OutwardShare))) + (OutwardDirection * OutwardShare)).GetSafeNormal2D();
}

void UFPSRCharacterMovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{
	if (CustomMovementMode != CMOVE_WallHang)
	{
		Super::PhysCustom(deltaTime, Iterations); // leave any Blueprint-driven custom mode to the engine
		return;
	}
	if (deltaTime < MIN_TICK_TIME || !CharacterOwner || !UpdatedComponent)
	{
		return;
	}

	RestorePreAdditiveRootMotionVelocity();

	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		// The hang owns its velocity outright. There is no gravity here and CalcVelocity is deliberately never called,
		// which is what stops input acceleration from pushing the player off the wall it is holding.
		const bool bClimbing = IsInputIntoWall();

		// Climbing is a flat speed; slipping eases toward its terminal speed so letting go reads as sliding down the
		// surface rather than the hands snapping open.
		const float VerticalSpeed = bClimbing
			? WallClimbSpeed
			: FMath::FInterpConstantTo(Velocity.Z, -WallSlipSpeed, deltaTime, WallSlipAcceleration);

		// Limited sideways movement (ADR 0001: 등반 중 제한적 좌우 이동). Only the part of the input running ALONG the
		// wall counts, so pressing into it or away from it changes nothing about where the player goes.
		FVector LateralVelocity = FVector::ZeroVector;
		const FVector InputDirection = Acceleration.GetSafeNormal2D();
		if (!InputDirection.IsNearlyZero())
		{
			LateralVelocity = FVector::VectorPlaneProject(InputDirection, WallNormal).GetSafeNormal2D() * WallLateralSpeed;
		}

		// Entry momentum: for a moment after the grab the player keeps sliding along the wall at the speed they came in
		// with, then it hands over to the ordinary lateral speed. Crossfading rather than adding keeps the result
		// bounded by whichever of the two is larger, so a fast entry can never stack on top of the steering.
		const float MomentumAlpha = GetWallEntryMomentumAlpha();
		if (MomentumAlpha > 0.0f)
		{
			const FVector GlideVelocity = FVector::VectorPlaneProject(WallEntryVelocity, WallNormal);
			LateralVelocity = FMath::Lerp(LateralVelocity, GlideVelocity, MomentumAlpha);
		}

		// The pull toward the wall is what keeps the capsule in contact, and contact is what keeps the hold probe
		// finding the surface next frame.
		Velocity = LateralVelocity - (WallNormal * WallStickSpeed);
		Velocity.Z = VerticalSpeed;
	}

	ApplyRootMotionToVelocity(deltaTime);

	Iterations++;
	bJustTeleported = false;

	const FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FVector Adjusted = Velocity * deltaTime;
	FHitResult Hit(1.0f);
	SafeMoveUpdatedComponent(Adjusted, UpdatedComponent->GetComponentQuat(), true, Hit);

	// Read before sliding along: SlideAlongSurface re-uses Hit for its own sweep, so by the time it returns this is the
	// result of the SECOND move — and slipping straight down onto a floor leaves that second sweep hitting nothing,
	// which would silently lose the landing.
	bool bLandedOnFloor = false;
	if (Hit.Time < 1.0f)
	{
		bLandedOnFloor = IsWalkable(Hit);
		HandleImpact(Hit, deltaTime, Adjusted);
		SlideAlongSurface(Adjusted, (1.0f - Hit.Time), Hit.Normal, Hit, true);
	}

	if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		// Recover the velocity actually achieved, exactly as PhysFlying does: pressed against the wall, the into-wall
		// part resolves to zero instead of building up into a shove.
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / deltaTime;
	}

	// Slipped down onto something walkable. Handing over to the falling step lands the player properly instead of
	// grinding the capsule into the floor for the rest of the slip window.
	if (bLandedOnFloor)
	{
		StopWallHang();
	}
}

bool UFPSRCharacterMovementComponent::DoJump(bool bReplayingMoves, float DeltaTime)
{
	if (!IsOnWall())
	{
		return Super::DoJump(bReplayingMoves, DeltaTime);
	}

	// Read the wall BEFORE the engine's jump: DoJump switches to MOVE_Falling, and that clears this state.
	const FVector PushNormal = WallNormal;

	// CheckJumpInput runs before the movement step, so the last hold probe is a frame old. A door destroyed in that gap
	// would otherwise launch the player off a wall that no longer exists. One extra sweep, only on the frame jump is
	// pressed.
	FHitResult WallHit;
	if (PushNormal.IsNearlyZero() || !ProbeWall(-PushNormal, WallHoldProbeHeight, WallHit) || !IsSurfaceGrabbable(WallHit))
	{
		// There is nothing left to push off, so let go and fall. Falling through to Super here instead would hand out a
		// plain vertical jump off thin air — and it would be a jump the player only has because grabbing the wall
		// refunded one, which makes it look exactly like the wall jump that was just refused.
		StopWallHang();
		return false;
	}

	// Read the momentum before Super::DoJump, which changes the movement mode and clears it along with the rest.
	const float CarriedSpeed = WallEntryVelocity.Size() * GetWallEntryMomentumAlpha();
	const FVector JumpDirection = ComputeWallJumpDirection(PushNormal);

	if (!Super::DoJump(bReplayingMoves, DeltaTime))
	{
		return false;
	}

	// The jump OWNS the horizontal velocity rather than adding to it. Adding would leave the glide, the stick that held
	// the capsule to the wall, and the push all fighting over the direction; writing it outright means the player goes
	// exactly where ComputeWallJumpDirection says, at exactly the speed below.
	if (!JumpDirection.IsNearlyZero())
	{
		// Speed still in hand from the approach rides on top of the base push, so reacting fast to a fast impact pays
		// out — capped so a full-speed slide into a wall can't launch beyond the rest of the movement set.
		const float JumpSpeed = FMath::Min(WallJumpPushSpeed + CarriedSpeed, WallJumpMaxSpeed);
		Velocity.X = JumpDirection.X * JumpSpeed;
		Velocity.Y = JumpDirection.Y * JumpSpeed;
	}

	// Super already set Z to the character's ordinary jump height; only override when the wall jump has been given a
	// height of its own.
	if (WallJumpUpSpeed > 0.0f)
	{
		Velocity.Z = WallJumpUpSpeed;
	}
	return true;
}

void UFPSRCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	// Runs before the physics step on the owning client, the server, and every replayed move — the one place where the
	// derived slide and wall-hang states are decided, so all three agree.

	// Stance weights. Advanced here rather than on a tick of their own because this runs on every role — the engine
	// calls it from PerformMovement for the owning client and the server AND from SimulateMovement for remote proxies —
	// so one place keeps the camera, the speed cap and (later) every player's animation on the same clock. This is the
	// one piece of this function a proxy genuinely wants, which is why it sits ABOVE the role gate below.
	AdvanceStanceBlends(DeltaSeconds);

	// SIMULATED PROXIES STOP HERE. The engine calls this from SimulateMovement as well as from PerformMovement, and a
	// proxy satisfies every entry condition below: bWantsToCrouch arrives through ACharacter::OnRep_IsCrouched, and
	// MovementMode and Velocity are both replicated. Deriving the states there would RE-DECIDE them on the one machine
	// with no authority over them — StartSliding would re-apply the entry impulse to an already-replicated velocity and
	// SimulateMovement integrates the result through MoveSmooth, while wall hang would drive SetMovementMode against
	// the very mode replication is feeding it, and run WallHangMaxDuration on a clock of its own so the proxy lets go
	// while the server still says it is holding on.
	// Proxies are TOLD these states instead: bSlidingVisual, WallYawByte and WallSideSign replicate for exactly that,
	// and IsSlidingForDisplay()/GetWallYawForDisplay() read those copies. Leaving early also spares every remote player
	// a per-frame CanGrabWall sweep.
	if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
		return;
	}

	if (SlideCooldownRemaining > 0.0f)
	{
		SlideCooldownRemaining = FMath::Max(0.0f, SlideCooldownRemaining - DeltaSeconds);
	}

	// Wall hang. Sliding is ground-only and hanging is air-only, so this and the slide block below can never both be
	// active — but the budget does have to be refreshed on the ground, which is the one moment both are quiet.
	if (IsMovingOnGround())
	{
		bWallHangConsumed = false;
	}

	if (IsOnWall())
	{
		UpdateWallHang(DeltaSeconds);
	}
	else
	{
		FVector GrabbedWallNormal;
		if (CanGrabWall(GrabbedWallNormal))
		{
			StartWallHang(GrabbedWallNormal);
		}
	}

	// GroundSpeedCurve's X axis. Held while airborne so a mid-sprint jump doesn't land back at a standing start, and
	// cleared when there's no input so stopping really does mean re-accelerating from zero.
	if (IsMovingOnGround() && !bIsSliding)
	{
		GroundAccelElapsed = Acceleration.IsNearlyZero() ? 0.0f : (GroundAccelElapsed + DeltaSeconds);
	}

	if (bIsSliding)
	{
		// Slope, sampled once and used for both effects below. Positive downhill, negative uphill.
		const float SlopeAlignment = ComputeSlopeAlignment();

		// (1) Slope stretches or compresses the curve's timeline. Downhill the decay plays in slow motion, so the slide
		// lasts as long as the hill does rather than expiring at the curve's authored length; uphill it runs out early.
		const float SlopeTimeScale = FMath::Max(0.0f, 1.0f - (SlopeAlignment * SlopeTimeInfluence));
		SlideElapsed += DeltaSeconds * SlopeTimeScale;

		// (2) Slope also feeds real acceleration: g * sin(angle) along the direction of travel. Accumulated separately
		// from the curve because the curve is recomputed each frame and would wipe it out.
		// This frame's share of that accumulation, which is all the no-curve path below is allowed to add.
		float SlopeBonusDelta = 0.0f;
		if (SlopeAccelerationScale > 0.0f && !FMath::IsNearlyZero(SlopeAlignment))
		{
			const float GravityMagnitude = FMath::Abs(GetGravityZ());
			const float PreviousSlopeBonus = SlideSlopeSpeedBonus;
			SlideSlopeSpeedBonus += GravityMagnitude * SlopeAlignment * SlopeAccelerationScale * DeltaSeconds;
			// Bounded both ways: downhill tops out at the slide ceiling instead of accelerating without limit, and
			// uphill can't drive the total below zero.
			SlideSlopeSpeedBonus = FMath::Clamp(SlideSlopeSpeedBonus, -SlideMaxSpeed, SlideMaxSpeed);
			// Measured AFTER the clamp, so once the bonus saturates this goes to zero and stops contributing.
			SlopeBonusDelta = SlideSlopeSpeedBonus - PreviousSlopeBonus;
		}

		// Speed and heading are handled separately: the curve (or the braking deceleration) owns HOW FAST, this owns
		// WHICH WAY. Steering through input acceleration instead would fight whatever the curve just set.
		const FVector SlideHeading = ComputeSlideHeading(DeltaSeconds);
		if (!SlideHeading.IsNearlyZero())
		{
			// A slide steered backwards pays the same penalty as walking backwards.
			const bool bPenalizeBackward = (BackwardSpeedMultiplier < 1.0f) && IsDirectionBackward(SlideHeading);

			float TargetSpeed;
			if (SlideSpeedCurve)
			{
				// Curve is a NORMALIZED decay shape (1.0 at entry, falling toward 0), so the slide always decays from the
				// speed actually carried in — fast entries aren't clamped down and slow ones aren't snapped up. Values above
				// 1.0 are allowed and simply overshoot the entry speed.
				TargetSpeed = SlideEntrySpeed * FMath::Max(0.0f, SlideSpeedCurve->GetFloatValue(SlideElapsed));

				// Safe to multiply: the curve is re-read from scratch each frame, so this can't compound.
				if (bPenalizeBackward)
				{
					TargetSpeed *= BackwardSpeedMultiplier;
				}

				// Rebuilt from SlideEntrySpeed every frame, so this speed carries no history — the WHOLE accumulated
				// slope bonus has to ride on top again each time.
				TargetSpeed += SlideSlopeSpeedBonus;
			}
			else
			{
				// No curve: the magnitude is whatever braking has left it at, so multiplying would compound every
				// frame (900 -> 675 -> 506 -> ...) and stop the slide almost instantly. Clamp instead.
				TargetSpeed = Velocity.Size2D();
				if (bPenalizeBackward)
				{
					TargetSpeed = FMath::Min(TargetSpeed, SlideEntrySpeed * BackwardSpeedMultiplier);
				}

				// Only this frame's increment, NOT the running total. Unlike the curve branch, this speed was written
				// by the previous frame and so already carries every earlier increment; adding the accumulated bonus
				// here would re-apply the whole of it once per frame and accelerate super-linearly until the cap.
				TargetSpeed += SlopeBonusDelta;
			}

			// Capped on both paths — a long descent reaches the slide's terminal speed rather than growing
			// indefinitely, and an uphill bonus can't drive the result negative.
			TargetSpeed = FMath::Clamp(TargetSpeed, 0.0f, SlideMaxSpeed);

			Velocity.X = SlideHeading.X * TargetSpeed;
			Velocity.Y = SlideHeading.Y * TargetSpeed;
		}

		// Exits, in the order they matter. The elapsed-time bound is invariant 7's guarantee: a downhill slide that
		// keeps regaining speed would otherwise never satisfy the speed exit and the state would be inescapable.
		const bool bReleasedCrouch = !bWantsToCrouch;
		const bool bLeftGround = !IsMovingOnGround();
		const bool bTooSlow = Velocity.SizeSquared2D() < FMath::Square(SlideMinSpeed);
		const bool bTimedOut = SlideElapsed >= GetEffectiveSlideMaxDuration();
		const bool bGateClosed = !IsSpecialMovementAllowed();

		if (bReleasedCrouch || bLeftGround || bTooSlow || bTimedOut || bGateClosed)
		{
			StopSliding();
		}
	}
	else if (CanEnterSlide())
	{
		StartSliding();
	}

	// Let the engine apply the crouch capsule resize afterwards: while sliding bWantsToCrouch is by definition set, so
	// the slide uses the crouched capsule for free and un-crouches through the engine's own encroachment check.
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
}

const UCurveFloat* UFPSRCharacterMovementComponent::GetActiveGroundSpeedCurve() const
{
	return IsCrouching() ? CrouchSpeedCurve.Get() : GroundSpeedCurve.Get();
}

float UFPSRCharacterMovementComponent::FindCurveTimeForSpeed(const UCurveFloat* Curve, float TargetSpeed) const
{
	if (!Curve)
	{
		return 0.0f;
	}

	float MinTime = 0.0f, MaxTime = 0.0f;
	Curve->GetTimeRange(MinTime, MaxTime);
	if (MaxTime <= MinTime)
	{
		return MinTime;
	}

	// Curve values are fractions of the stance's max speed, so convert the real-world speed the same way.
	const float StanceMaxSpeed = Super::GetMaxSpeed();
	const float NormalizedTarget = (StanceMaxSpeed > KINDA_SMALL_NUMBER) ? (TargetSpeed / StanceMaxSpeed) : 0.0f;

	// Walk forward and take the first sample that reaches the target. A fixed sample count keeps the cost flat and
	// bounded, and it runs once per stance change rather than per frame. 32 steps is ~0.03s resolution on a 1s ramp —
	// far finer than the acceleration can act on anyway.
	constexpr int32 NumSamples = 32;
	const float Step = (MaxTime - MinTime) / static_cast<float>(NumSamples);
	for (int32 Index = 0; Index <= NumSamples; ++Index)
	{
		const float SampleTime = MinTime + (Step * Index);
		if (Curve->GetFloatValue(SampleTime) >= NormalizedTarget)
		{
			return SampleTime;
		}
	}
	return MaxTime; // already faster than this curve goes — start at the top
}

void UFPSRCharacterMovementComponent::RemapGroundAccelForStanceChange()
{
	if (const UCurveFloat* StanceCurve = GetActiveGroundSpeedCurve())
	{
		GroundAccelElapsed = FindCurveTimeForSpeed(StanceCurve, Velocity.Size2D());
	}
}

float UFPSRCharacterMovementComponent::GetStanceTransitionProgress() const
{
	const float Target = IsCrouching() ? 1.0f : 0.0f;
	const float Span = FMath::Abs(Target - StanceBlendStart);
	if (Span <= KINDA_SMALL_NUMBER)
	{
		return 1.0f; // nothing to travel — the blend was already sitting where it is headed
	}
	return FMath::Clamp(FMath::Abs(StanceBlend - StanceBlendStart) / Span, 0.0f, 1.0f);
}

void UFPSRCharacterMovementComponent::BeginStanceTransition(float SpeedBeforeChange)
{
	// Restart the progress clock from wherever the blend currently is, NOT from the far end. Crouching and standing
	// back up immediately then costs only the distance actually covered, and neither the camera nor the speed cap
	// jumps at the moment the change reverses.
	StanceBlendStart = StanceBlend;

	// A slide owns its speed cap outright (GetMaxSpeed returns early while sliding), so there is nothing to ease away
	// from — capturing the slide ceiling here would leave it in hand for a moment after the slide ended.
	StanceSpeedFrom = bIsSliding ? 0.0f : FMath::Max(0.0f, SpeedBeforeChange);
}

namespace
{
	/** Step toward Target at a constant 1/Duration per second: a full 0 -> 1 change takes exactly Duration, and a
	 *  partial one takes proportionally less. Fixing the RATE rather than the duration is what stops a barely-started
	 *  change from taking as long to undo as a complete one. */
	float AdvanceBlendTowards(float Current, float Target, float Duration, float DeltaSeconds)
	{
		if (Duration <= 0.0f)
		{
			return Target; // blending switched off
		}
		return FMath::FInterpConstantTo(Current, Target, DeltaSeconds, 1.0f / Duration);
	}
}

void UFPSRCharacterMovementComponent::AdvanceStanceBlends(float DeltaSeconds)
{
	// Both sources are correct on every role. IsCrouching() reads the replicated bIsCrouched; IsSlidingForDisplay()
	// hands the owner and the server the exact local bIsSliding and a proxy the replicated bSlidingVisual. Reading
	// bIsSliding directly here would peg a proxy's slide weight to 0, because deriving the slide is the owner's and the
	// server's job alone (see the role gate in UpdateCharacterStateBeforeMovement).
	StanceBlend = AdvanceBlendTowards(StanceBlend, IsCrouching() ? 1.0f : 0.0f, StanceBlendDuration, DeltaSeconds);
	SlideBlend = AdvanceBlendTowards(SlideBlend, IsSlidingForDisplay() ? 1.0f : 0.0f, SlideBlendDuration, DeltaSeconds);

	// Retire the transition once it has settled so GetMaxSpeed goes straight back to the plain stance cap.
	if (StanceSpeedFrom > 0.0f && GetStanceTransitionProgress() >= 1.0f)
	{
		StanceSpeedFrom = 0.0f;
	}
}

void UFPSRCharacterMovementComponent::Crouch(bool bClientSimulation)
{
	const bool bWasCrouched = CharacterOwner && CharacterOwner->bIsCrouched;
	// Sampled BEFORE Super flips the stance, so it is the cap the player actually had — including one still easing
	// from an earlier change, which is what keeps a crouch/stand/crouch flicker continuous.
	const float SpeedBeforeStanceChange = GetMaxSpeed();

	Super::Crouch(bClientSimulation);
	// Only on an actual transition — Crouch() no-ops when the capsule can't change, and remapping then would move the
	// timer for a stance the character never entered.
	if (CharacterOwner && CharacterOwner->bIsCrouched != bWasCrouched)
	{
		RemapGroundAccelForStanceChange();
		BeginStanceTransition(SpeedBeforeStanceChange);
	}
}

void UFPSRCharacterMovementComponent::UnCrouch(bool bClientSimulation)
{
	const bool bWasCrouched = CharacterOwner && CharacterOwner->bIsCrouched;
	const float SpeedBeforeStanceChange = GetMaxSpeed();

	Super::UnCrouch(bClientSimulation);
	if (CharacterOwner && CharacterOwner->bIsCrouched != bWasCrouched)
	{
		RemapGroundAccelForStanceChange();
		BeginStanceTransition(SpeedBeforeStanceChange);
	}
}

float UFPSRCharacterMovementComponent::GetEffectiveSlideMaxDuration() const
{
	// An assigned curve defines the whole slide, so its length IS the duration. Otherwise a curve authored longer than
	// SlideMaxDuration would be cut off partway with no indication of why — which is exactly how a 5s decay curve ends
	// up looking like an instant drop: the slide ends early, and the still-crouched player is capped by
	// MaxWalkSpeedCrouched from that moment on.
	if (SlideSpeedCurve)
	{
		float CurveMinTime = 0.0f, CurveMaxTime = 0.0f;
		SlideSpeedCurve->GetTimeRange(CurveMinTime, CurveMaxTime);
		if (CurveMaxTime > 0.0f)
		{
			return CurveMaxTime;
		}
	}
	return SlideMaxDuration;
}

void UFPSRCharacterMovementComponent::PhysFalling(float deltaTime, int32 Iterations)
{
	Super::PhysFalling(deltaTime, Iterations);

	// Applied after the engine's integration so it caps the result rather than fighting gravity mid-step. Clamping a
	// predicted value deterministically, so client and server land on the same velocity.
	if (MaxFallSpeed > 0.0f && Velocity.Z < -MaxFallSpeed)
	{
		Velocity.Z = -MaxFallSpeed;
	}
}

FVector UFPSRCharacterMovementComponent::ComputeSlideHeading(float DeltaSeconds) const
{
	const FVector CurrentHeading = Velocity.GetSafeNormal2D();
	if (CurrentHeading.IsNearlyZero())
	{
		return FVector::ZeroVector; // nothing to rotate; the too-slow exit will end the slide anyway
	}

	// Steer toward the movement input; with no input, toward where the pawn is facing — that is what keeps a
	// hands-off slide tracking the camera as the player looks around.
	FVector DesiredHeading = Acceleration.GetSafeNormal2D();
	if (DesiredHeading.IsNearlyZero() && UpdatedComponent)
	{
		DesiredHeading = UpdatedComponent->GetForwardVector().GetSafeNormal2D();
	}
	if (DesiredHeading.IsNearlyZero() || SlideTurnRateDegrees <= 0.0f)
	{
		return CurrentHeading;
	}

	const float MaxTurnDegrees = SlideTurnRateDegrees * DeltaSeconds;
	const float CosAngle = FMath::Clamp(FVector::DotProduct(CurrentHeading, DesiredHeading), -1.0f, 1.0f);
	const float AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(CosAngle));
	if (AngleDegrees <= MaxTurnDegrees)
	{
		return DesiredHeading; // close enough to snap without overshooting
	}

	// Rotate about world up by the allowed amount. The cross product's sign picks the shorter way round; at exactly
	// 180 degrees it is zero and the >= makes the choice deterministic, which prediction requires.
	const float CrossZ = (CurrentHeading.X * DesiredHeading.Y) - (CurrentHeading.Y * DesiredHeading.X);
	const float TurnDirection = (CrossZ >= 0.0f) ? 1.0f : -1.0f;
	return CurrentHeading.RotateAngleAxis(MaxTurnDegrees * TurnDirection, FVector::UpVector).GetSafeNormal2D();
}

bool UFPSRCharacterMovementComponent::IsDirectionBackward(const FVector& Direction) const
{
	if (!UpdatedComponent)
	{
		return false;
	}
	const FVector Normalized = Direction.GetSafeNormal2D();
	if (Normalized.IsNearlyZero())
	{
		return false;
	}
	// Compare against the cosine of the threshold angle: at the default 90 degrees this is 0, so only genuinely
	// rearward motion counts and pure strafing keeps full speed.
	const FVector Facing = UpdatedComponent->GetForwardVector().GetSafeNormal2D();
	const float CosThreshold = FMath::Cos(FMath::DegreesToRadians(BackwardAngleThresholdDegrees));
	return FVector::DotProduct(Normalized, Facing) < CosThreshold;
}

bool UFPSRCharacterMovementComponent::IsMovingBackward() const
{
	return IsDirectionBackward(Acceleration);
}

float UFPSRCharacterMovementComponent::ComputeSlopeAlignment() const
{
	if (!CurrentFloor.IsWalkableFloor())
	{
		return 0.0f;
	}

	// A surface normal tilts away from vertical toward the DOWNHILL side, and the length of that horizontal part is
	// exactly sin(slope angle) — so one vector gives both the downhill direction and the steepness.
	const FVector FloorNormal = CurrentFloor.HitResult.ImpactNormal;
	const FVector DownhillVector(FloorNormal.X, FloorNormal.Y, 0.0f);
	const float SinSlope = DownhillVector.Size();
	if (SinSlope < KINDA_SMALL_NUMBER)
	{
		return 0.0f; // flat
	}

	const FVector Heading = Velocity.GetSafeNormal2D();
	if (Heading.IsNearlyZero())
	{
		return 0.0f;
	}

	// Project the heading onto the downhill direction and scale by steepness: heading straight down a 30-degree slope
	// gives 0.5, straight up gives -0.5, and moving across it gives ~0.
	return FVector::DotProduct(Heading, DownhillVector / SinSlope) * SinSlope;
}

bool UFPSRCharacterMovementComponent::IsSlidingBackward() const
{
	// Heading, not input: once a slide has been steered around, releasing the key doesn't stop it travelling backwards,
	// so testing the input would drop the penalty exactly while the player is still sliding in reverse.
	return bIsSliding && IsDirectionBackward(Velocity);
}

float UFPSRCharacterMovementComponent::GetMaxSpeed() const
{
	if (bIsSliding)
	{
		// Flat ceiling while sliding: with SlideSpeedCurve assigned the curve writes the speed outright each frame
		// (UpdateCharacterStateBeforeMovement), so this only has to stay out of its way. A slide holds its committed
		// direction, so the backpedal penalty deliberately does not apply here.
		return SlideMaxSpeed;
	}

	// Engine speed for this stance: MaxWalkSpeed standing, MaxWalkSpeedCrouched crouched — and already carrying any
	// card multiplier, which is why the normalized curve needs no scaling of its own.
	float MaxSpeed = Super::GetMaxSpeed();

	// Applying the ramp as a MAX SPEED (rather than writing Velocity) keeps knockback, slopes and other external
	// forces behaving normally.
	const UCurveFloat* ActiveSpeedCurve = GetActiveGroundSpeedCurve();
	if (ActiveSpeedCurve && IsMovingOnGround())
	{
		MaxSpeed *= FMath::Max(0.0f, ActiveSpeedCurve->GetFloatValue(GroundAccelElapsed));
	}

	// Backpedal penalty last, so it stacks on whatever the ramp (and any card scaling) produced.
	if (BackwardSpeedMultiplier < 1.0f && IsMovingBackward())
	{
		MaxSpeed *= BackwardSpeedMultiplier;
	}

	// Stance change: ease from the cap in force when the stance flipped to the one it flipped to, on the same clock the
	// camera uses, so the body slows down over the window the view sinks instead of in a couple of frames.
	//
	// Done by easing the RESULT rather than by blending MaxWalkSpeed against MaxWalkSpeedCrouched up front, because the
	// latter also feeds FindCurveTimeForSpeed's normalisation: standing up would then land the ramp at half its curve
	// against a still-crouched reference, and the cap would drop to 150 instead of holding at 300. Applying it here
	// leaves the curve remap — and its "carry the speed across the stance change" behaviour — untouched.
	if (StanceSpeedFrom > 0.0f && IsMovingOnGround())
	{
		const float TransitionProgress = GetStanceTransitionProgress();
		if (TransitionProgress < 1.0f)
		{
			MaxSpeed = FMath::Lerp(StanceSpeedFrom, MaxSpeed, TransitionProgress);
		}
	}
	return MaxSpeed;
}

void UFPSRCharacterMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (bIsSliding)
	{
		// A slide owns both its speed and its heading (UpdateCharacterStateBeforeMovement), so Super must not run:
		// it would add input acceleration on top and push the player off whatever the curve just set.
		//
		// Critically, this is also why MaxAcceleration is NOT forced to zero during a slide. Acceleration is built as
		// `GetMaxAcceleration() * InputVector`, so a zero max produces a zero vector — and then ComputeSlideHeading has
		// no WASD direction left to steer by, which silently reduced steering to "always face the camera".
		//
		// Without a curve the magnitude is still braking-driven, so apply just that. SlideGroundFriction stays near
		// zero on purpose: PhysWalking would otherwise hand us GroundFriction (8), which BrakingFrictionFactor doubles
		// to an effective 16 and ends the slide in under a tenth of a second.
		if (!SlideSpeedCurve)
		{
			ApplyVelocityBraking(DeltaTime, SlideGroundFriction, SlideBrakingDeceleration);
		}
		return;
	}

	if (IsFalling())
	{
		ApplyAirStrafeVelocity(DeltaTime);
		return;
	}

	Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
}

void UFPSRCharacterMovementComponent::ApplyAirStrafeVelocity(float DeltaTime)
{
	// PhysFalling zeroes Velocity.Z around this call and applies gravity separately, so everything here is lateral.
	//
	// Super's version accelerates the WHOLE velocity vector toward the input and then clamps the total, which is why
	// turning in mid-air costs speed today: any input that disagrees with the current heading subtracts from it. This
	// is the Quake-family model instead — add along the input direction only as much as the player is NOT already
	// travelling that way. Sideways, that pushes perpendicular to travel, which ROTATES the velocity while barely
	// changing its length; forwards, there is nothing left to add. Free turns, no free speed.
	const FVector WishDirection = Acceleration.GetSafeNormal2D();
	if (WishDirection.IsNearlyZero())
	{
		return; // no input: coast. Air friction stays at zero on purpose — carried momentum is the whole point.
	}

	FVector LateralVelocity(Velocity.X, Velocity.Y, 0.0f);

	// How much of the current speed already points where the player is asking to go. Near zero when strafing across
	// the direction of travel, which is what keeps the full allowance available for the turn.
	const float SpeedAlongWish = FVector::DotProduct(LateralVelocity, WishDirection);
	const float AddSpeed = AirStrafeWishSpeed - SpeedAlongWish;
	if (AddSpeed <= 0.0f)
	{
		return; // already moving that way at least this fast — holding forward in the air must not accelerate
	}

	// Note this function can run MORE THAN ONCE per frame (the falling step sub-steps, and re-runs after a mid-air
	// collision). It holds no state and the AddSpeed test bounds the total speed along any one direction, so repeat
	// calls converge instead of compounding — don't add anything here that assumes one call per frame.
	const float AccelSpeed = FMath::Min(AirStrafeAcceleration * DeltaTime, AddSpeed);
	const float SpeedBefore = LateralVelocity.Size();
	LateralVelocity += WishDirection * AccelSpeed;

	// The CEILING only ever raises toward AirStrafeMaxSpeed and never cuts speed carried in above it — a slide jump
	// arriving faster than the ceiling keeps every bit of it (same rule as SlideMaxEntrySpeed).
	//
	// That is a statement about the clamp, not about the whole step: input pointing AGAINST the direction of travel
	// still subtracts, because the push above is a vector addition. That is deliberate — holding the back key is how
	// a player sheds momentum in the air. Nothing takes speed away on its own; only the player can.
	LateralVelocity = LateralVelocity.GetClampedToMaxSize(FMath::Max(SpeedBefore, AirStrafeMaxSpeed));

	Velocity.X = LateralVelocity.X;
	Velocity.Y = LateralVelocity.Y;
}

FVector UFPSRCharacterMovementComponent::GetFallingLateralAcceleration(float DeltaTime)
{
	// Root motion owns the movement outright; leave the engine's handling of it alone.
	if (HasAnimRootMotion())
	{
		return Super::GetFallingLateralAcceleration(DeltaTime);
	}

	// Super scales the input by AirControl and clamps it to MaxAcceleration. Both are replaced here, so those two
	// properties (and the AirControlBoost pair) stop having any effect while falling — documented on the air-strafe
	// properties in the header.
	//
	// The magnitude matters even though ApplyAirStrafeVelocity only reads the direction: after a mid-air wall hit the
	// engine feeds this same vector to LimitAirControl() as the re-acceleration to apply. Returning the raw
	// Acceleration (sized by MaxAcceleration, 2048) would make grazing a wall shove the player far harder than the
	// strafe model ever does.
	const FVector WishDirection = Acceleration.GetSafeNormal2D();
	return WishDirection.IsNearlyZero() ? FVector::ZeroVector : (WishDirection * AirStrafeAcceleration);
}

bool UFPSRCharacterMovementComponent::CanCrouchInCurrentState() const
{
	// Super allows IsFalling() too; this design does not (no crouch, and therefore no slide, while airborne).
	return Super::CanCrouchInCurrentState() && IsMovingOnGround();
}

bool UFPSRCharacterMovementComponent::CanAttemptJump() const
{
	// Super's middle term is !bWantsToCrouch: the engine refuses to jump while the crouch key is held and expects the
	// player to un-crouch first. This design wants jump to break OUT of both crouch and slide immediately, so that one
	// term is dropped and everything else (jump count, hold time) stays engine-governed.
	//
	// Gating this on bIsSliding alone was not enough: the crouch key is usually still held when the slide ends, which
	// left the player in a plain crouch that could never jump.
	//
	// Standing back up is the engine's job and stays safe — going airborne makes CanCrouchInCurrentState() false, which
	// triggers UnCrouch, and that keeps the crouched capsule if a ceiling is in the way.
	//
	// IsOnWall() is added because a wall hang is neither on the ground nor falling, so Super's pair of states would
	// refuse the wall jump — one of the three exits the design requires.
	return IsJumpAllowed() && (IsMovingOnGround() || IsFalling() || IsOnWall());
}

float UFPSRCharacterMovementComponent::GetPlanarSpeed() const
{
	return Velocity.Size2D();
}

FString UFPSRCharacterMovementComponent::GetLocomotionStateName() const
{
	FString StateName;
	if (IsOnWall())
	{
		// Which half of the state we're in, and how close the hard time bound is — "the climb stopped" and "the wall
		// ran out" look the same on screen otherwise.
		StateName = FString::Printf(TEXT("Wall %s %.2f/%.2fs"),
			IsInputIntoWall() ? TEXT("climb") : TEXT("slip"), WallHangElapsed, WallHangMaxDuration);

		// How much launch speed is still on the table. Without it the entry-momentum window is invisible, and "I jumped
		// too late" looks identical to "the momentum isn't working".
		const float CarriedSpeed = WallEntryVelocity.Size() * GetWallEntryMomentumAlpha();
		if (CarriedSpeed > 0.0f)
		{
			StateName += FString::Printf(TEXT("  +%.0f"), CarriedSpeed);
		}
	}
	else if (IsFalling())   { StateName = TEXT("Air"); }
	else if (bIsSliding)
	{
		// Show elapsed / limit: without it, "the curve isn't being followed" and "the slide ended early" look identical.
		StateName = FString::Printf(TEXT("Slide %.2f/%.2fs  slope%+.2f %+.0f"),
			SlideElapsed, GetEffectiveSlideMaxDuration(), ComputeSlopeAlignment(), SlideSlopeSpeedBonus);
	}
	else if (IsCrouching()) { StateName = TEXT("Crouch"); }
	else                    { StateName = TEXT("Run"); }

	// While sliding the penalty follows the heading, so report the same thing the speed code used.
	if (bIsSliding ? IsSlidingBackward() : IsMovingBackward())
	{
		StateName += TEXT("  BACK");
	}
	// Surface the slide lockout too — otherwise "crouch did nothing" is indistinguishable from "too slow to slide".
	if (!bIsSliding && SlideCooldownRemaining > 0.0f)
	{
		StateName += FString::Printf(TEXT("  (slide CD %.1f)"), SlideCooldownRemaining);
	}
	// Same reasoning for the wall: "the grab did nothing" and "this airborne period's grab is already spent" are
	// otherwise the same silence.
	if (!IsOnWall() && bWallHangConsumed)
	{
		StateName += TEXT("  (wall used)");
	}
	// Only while it is actually moving: settled at 0 or 1 it says nothing, but mid-transition it separates "the blend
	// is running" from "the blend already finished and the speed still looks wrong".
	if (StanceBlend > KINDA_SMALL_NUMBER && StanceBlend < 1.0f - KINDA_SMALL_NUMBER)
	{
		StateName += FString::Printf(TEXT("  stance %.2f"), StanceBlend);
	}
	return StateName;
}

bool UFPSRCharacterMovementComponent::CanFireInCurrentState() const
{
	// Sliding, crouching, jumping and airborne all allow fire by design. The wall hang is the single exception: both
	// hands are on the wall, which the reference footage shows directly — the weapon leaves the screen entirely for the
	// duration of the grab.
	return !IsOnWall();
}

float UFPSRCharacterMovementComponent::GetSpreadMultiplier() const
{
	// Exclusive, not multiplied: a crouched player who walks off a ledge is "airborne", not "airborne × crouched".
	// A wall hang counts as airborne even though IsFalling() is false there — firing is blocked anyway, so this only
	// matters to anything that reads the multiplier for its own reasons, and "off the ground" is the honest answer.
	if (IsFalling() || IsOnWall())
	{
		return AirborneSpreadMultiplier;
	}
	if (bIsSliding)
	{
		return SlideSpreadMultiplier;
	}
	if (IsCrouching())
	{
		return CrouchSpreadMultiplier;
	}
	return 1.0f;
}

FNetworkPredictionData_Client* UFPSRCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UFPSRCharacterMovementComponent* MutableThis = const_cast<UFPSRCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_FPSR(*this);
	}
	return ClientPredictionData;
}

// --- FSavedMove_FPSR ---------------------------------------------------------------------------------------------

FSavedMove_FPSR::FSavedMove_FPSR()
	: bSavedIsSliding(0)
	, bSavedOnWall(0)
	, bSavedWallHangConsumed(0)
{
	// Bitfields get no in-class initializer, and a pooled move is reused before Clear() is guaranteed to have run on
	// it — so initialize here rather than relying on the allocation being zeroed.
}

void FSavedMove_FPSR::Clear()
{
	Super::Clear();
	bSavedIsSliding = 0;
	SavedSlideElapsed = 0.0f;
	SavedSlideEntrySpeed = 0.0f;
	SavedSlideCooldownRemaining = 0.0f;
	SavedGroundAccelElapsed = 0.0f;
	SavedSlideSlopeSpeedBonus = 0.0f;

	bSavedOnWall = 0;
	bSavedWallHangConsumed = 0;
	SavedWallHangElapsed = 0.0f;
	SavedWallSlipElapsed = 0.0f;
	SavedWallNormal = FVector::ZeroVector;
	SavedWallEntryVelocity = FVector::ZeroVector;

	SavedStanceBlend = 0.0f;
	SavedStanceBlendStart = 0.0f;
	SavedStanceSpeedFrom = 0.0f;
	SavedSlideBlend = 0.0f;
}

bool FSavedMove_FPSR::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	const FSavedMove_FPSR* Other = static_cast<const FSavedMove_FPSR*>(NewMove.Get());
	if (Other)
	{
		// Never fold two moves that straddle a slide transition: combining them would erase the frame the state changed
		// on and the replay would diverge from what the server simulated.
		if (bSavedIsSliding != Other->bSavedIsSliding)
		{
			return false;
		}

		// Wall hangs are excluded from combining outright, not just across their edges. Super's test never looks at the
		// packed movement mode, and everything that decides how a hang plays out — which wall, how long, whether this
		// airborne period's grab is spent — lives only on this saved move. A hang lasts under two seconds, so giving up
		// combining for its duration costs nothing measurable and removes the whole class of divergence.
		if (bSavedOnWall || Other->bSavedOnWall)
		{
			return false;
		}
		if (bSavedWallHangConsumed != Other->bSavedWallHangConsumed)
		{
			return false;
		}

		// A stance change in flight is easing the speed cap every frame, and folding two of those moves would replay
		// the whole span at one cap instead of the two the client used. Super's own guard (MaxSpeed differing by more
		// than MaxSpeedThresholdCombine = 10) happens to catch this at 60fps, where the cap moves ~33 cm/s per frame —
		// but that margin shrinks with framerate and with a longer StanceBlendDuration, so don't lean on it. The window
		// is 0.15s, so refusing outright costs nothing measurable.
		if (SavedStanceSpeedFrom > 0.0f || Other->SavedStanceSpeedFrom > 0.0f)
		{
			return false;
		}
	}
	return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

void FSavedMove_FPSR::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);

	if (const UFPSRCharacterMovementComponent* Movement = C ? Cast<UFPSRCharacterMovementComponent>(C->GetCharacterMovement()) : nullptr)
	{
		bSavedIsSliding = Movement->bIsSliding ? 1 : 0;
		SavedSlideElapsed = Movement->SlideElapsed;
		SavedSlideEntrySpeed = Movement->SlideEntrySpeed;
		SavedSlideCooldownRemaining = Movement->SlideCooldownRemaining;
		SavedGroundAccelElapsed = Movement->GroundAccelElapsed;
		SavedSlideSlopeSpeedBonus = Movement->SlideSlopeSpeedBonus;

		bSavedOnWall = Movement->IsOnWall() ? 1 : 0;
		bSavedWallHangConsumed = Movement->bWallHangConsumed ? 1 : 0;
		SavedWallHangElapsed = Movement->WallHangElapsed;
		SavedWallSlipElapsed = Movement->WallSlipElapsed;
		SavedWallNormal = Movement->WallNormal;
		SavedWallEntryVelocity = Movement->WallEntryVelocity;

		SavedStanceBlend = Movement->StanceBlend;
		SavedStanceBlendStart = Movement->StanceBlendStart;
		SavedStanceSpeedFrom = Movement->StanceSpeedFrom;
		SavedSlideBlend = Movement->SlideBlend;
	}
}

void FSavedMove_FPSR::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);

	if (UFPSRCharacterMovementComponent* Movement = C ? Cast<UFPSRCharacterMovementComponent>(C->GetCharacterMovement()) : nullptr)
	{
		// Restore the derived state before this move is re-simulated, so the replay resumes mid-slide with the same
		// elapsed time rather than re-deriving entry across a float boundary and drifting.
		Movement->bIsSliding = (bSavedIsSliding != 0);
		Movement->SlideElapsed = SavedSlideElapsed;
		Movement->SlideEntrySpeed = SavedSlideEntrySpeed;
		Movement->SlideCooldownRemaining = SavedSlideCooldownRemaining;
		Movement->GroundAccelElapsed = SavedGroundAccelElapsed;
		Movement->SlideSlopeSpeedBonus = SavedSlideSlopeSpeedBonus;

		// Whether we are on a wall is NOT restored here — that is the movement mode, which the correction itself has
		// already set (ApplyNetworkMovementMode) and which the replayed moves re-derive from there. What has to come
		// back is everything the replay cannot re-derive: the wall being held (the jump exit reads it before this
		// frame's probe runs), the two timers, and the once-per-airborne budget.
		Movement->bWallHangConsumed = (bSavedWallHangConsumed != 0);
		Movement->WallHangElapsed = SavedWallHangElapsed;
		Movement->WallSlipElapsed = SavedWallSlipElapsed;
		Movement->WallNormal = SavedWallNormal;
		Movement->WallEntryVelocity = SavedWallEntryVelocity;

		// The stance weight feeds the walk-speed cap, so a replay has to resume from the same point on the blend or it
		// re-simulates at a different speed. The slide weight rides along for a lesser reason: a replay re-runs time
		// that was already simulated, so without this it would advance twice over that span and the pose would land
		// early.
		Movement->StanceBlend = SavedStanceBlend;
		Movement->StanceBlendStart = SavedStanceBlendStart;
		Movement->StanceSpeedFrom = SavedStanceSpeedFrom;
		Movement->SlideBlend = SavedSlideBlend;
	}
}

// --- FNetworkPredictionData_Client_FPSR --------------------------------------------------------------------------

FNetworkPredictionData_Client_FPSR::FNetworkPredictionData_Client_FPSR(const UCharacterMovementComponent& ClientMovement)
	: Super(ClientMovement)
{
}

FSavedMovePtr FNetworkPredictionData_Client_FPSR::AllocateNewMove()
{
	return MakeShared<FSavedMove_FPSR>();
}
