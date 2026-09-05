// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hero/FPSRCharacterMovementComponent.h"

#include "Components/CapsuleComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Hero/FPSRCharacter.h"
#include "Weapon/FPSRWeaponFragment.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

//~ Playtest speed scale. Defined at the TOP of the file on purpose: the wall/air movement code that reads it sits
//~ several hundred lines above GetMaxSpeed(), and a definition placed next to its "main" user would be invisible to
//~ everything earlier in the translation unit.

#if !UE_BUILD_SHIPPING
static float GFPSRPlayerSpeedScale = 1.0f;
static FAutoConsoleVariableRef CVarFPSRPlayerSpeedScale(
	TEXT("FPSR.Debug.PlayerSpeedScale"),
	GFPSRPlayerSpeedScale,
	TEXT("Playtest multiplier on every player speed cap AND acceleration — ground, slide, air-strafe and wall alike\n"
	     "(both speed and acceleration, so time-to-top-speed is unchanged).\n"
	     "Deliberately does NOT touch:\n"
	     "  MaxStepHeight   - mirrored into the flow field's ClimbableStepHeight; scaling it would let the player\n"
	     "                    walk over props the swarm cannot, breaking the 45/60 authoring band (ADR 0010 inv. 4).\n"
	     "  Jump / wall-jump UP speed - jump HEIGHT goes with v^2, so a 3x scale would be a 9x jump.\n"
	     "  Wall probe distances/radii - geometry, not speed; scaling them is a different feature, not a faster one.\n"
	     "  Wall-jump cooldown - a beat, not a distance; the per-airborne cap is a count.\n"
	     "Set the SAME value on server and client: CharacterMovement replays moves on correction, so a mismatch\n"
	     "reads as constant rubber-banding. In multiplayer the WORST case is setting it on the listen host\n"
	     "alone: the server replays every remote client's ServerMove against the scaled cap, so one\n"
	     "host-side change rubber-bands EVERY other player rather than the host. 1 = off."),
	ECVF_Cheat);
#endif

/** 1.0 outside of dev builds, so shipping keeps the authored numbers with no branch worth mentioning. */
static FORCEINLINE float FPSRPlayerSpeedScale()
{
#if !UE_BUILD_SHIPPING
	return FMath::Max(0.0f, GFPSRPlayerSpeedScale);
#else
	return 1.0f;
#endif
}

/**
 * Wraps an authored SPEED or ACCELERATION that the movement code reads directly — the air-strafe and wall values,
 * which never pass through GetMaxSpeed() and so would otherwise stay at 1x while the ground game scaled away from
 * them. Scaling only some of them is worse than scaling none: raise WallJumpMaxSpeed alone and the ceiling moves
 * while the push that reaches for it does not.
 *
 * NOT applied to probe distances/radii (geometry), angles/dots (dimensionless), durations (a beat, not a distance),
 * or vertical jump speeds (height goes with v^2) — see the cvar help.
 */
static FORCEINLINE float FPSRScaled(float AuthoredSpeed)
{
	return AuthoredSpeed * FPSRPlayerSpeedScale();
}

UFPSRCharacterMovementComponent::UFPSRCharacterMovementComponent()
{
	// The shared body AnimBP runs on every machine, so a teammate's slide has to reach the machines that cannot derive
	// it. Movement itself still travels in the move packets — this carries only the
	// handful of bytes that are pure presentation. Same reason UFPSRWeaponFireComponent replicates bIsAiming.
	SetIsReplicatedByDefault(true);
	bSlidingVisual = 0;

	// Crouch is engine-native (bWantsToCrouch is already predicted and already in the move packet), so the slide can
	// ride on it without a custom flag — but it only works if crouching is actually enabled on the component.
	NavAgentProps.bCanCrouch = true;

	// Engine default is FALSE, and leaving it there caught every slide on every ledge (velocity 0, dead stop).
	// The slide lives in MOVE_Walking on bWantsToCrouch (ADR 0001 axis 1), so IsCrouching() is true for its whole
	// length — which is precisely what the engine's CanWalkOffLedges() refuses on (engine cpp:5303). PhysWalking then
	// takes its ledge branch (engine cpp:5679): the sideways GetLedgeMove is zero for a square-on approach, CheckFall
	// asks CanWalkOffLedges() again and so never starts the fall, and RevertMove(bFailMove=true) zeroes Velocity AND
	// Acceleration. The next frame ComputeSlideHeading has no direction left to normalize, so the curve cannot restore
	// the speed either, and the player is left parked on the lip for as long as crouch is held.
	// The default's premise is a stealth game's crouch — "don't let the player fall while sneaking". Here crouch IS the
	// slide entry, so that premise does not hold; standing already walks off ledges, so this also removes a
	// stand/crouch inconsistency rather than introducing one.
	// A CDO config flag, not simulation state: both machines hold the same value and every replayed move reads it
	// identically, so it costs no compressed flag, no SavedMove field and nothing per frame. Overridable per-hero in
	// the Blueprint — the engine already marks it EditAnywhere.
	bCanWalkOffLedgesWhenCrouching = true;

	// The engine default of 0.05 leaves essentially no air steering, which doesn't fit a design where the player
	// jumps out of slides, fights mid-air and lands into cover. Overridable per-hero in the Blueprint.
	AirControl = 0.4f;

	// Raised with the 2026-08-16 x1.5 speed pass (engine default 2048 -> 3070, rounded to 10 like the rest). Speed and
	// acceleration have to move together: leaving the engine default here would make a faster player take 1.5x as long
	// to reach the new cap, which reads as sluggish rather than fast. Overridable per-hero in the Blueprint.
	MaxAcceleration = 3070.0f;

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
	const float EnterThreshold = FPSRScaled(FMath::Max(SlideMinEnterSpeed, SlideMinSpeed));
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

			// CRIT1 card 5 (Slide Focus): this branch is the authoritative slide-ENTRY edge (only reached on the
			// false->true transition), so it is the one hook point for the timed crit buff. The bridge resolves
			// Avatar -> inventory -> current weapon itself, keeping this component from having to know weapons exist.
			FPSRWeaponHooks::NotifySlideStarted(CharacterOwner);
		}
		MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRCharacterMovementComponent, bSlidingVisual, this);
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
		FMath::Max(CurrentSpeed, FMath::Min(CurrentSpeed * SlideEnterSpeedMultiplier, FPSRScaled(SlideMaxEntrySpeed))),
		FPSRScaled(SlideMaxSpeed));

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
	// Charged on every exit the slide's OWN RULES produced — crouch released, too slow, timed out, gate closed. Those
	// all happen with the player still on the floor, which is why the ground test is the whole condition.
	//
	// Leaving the ground is not one of those verdicts. It is traversal: a jump, a step down, a ledge. Charging the
	// lockout there was destroying the player's speed on ordinary terrain, and the loss is much larger than "no slide
	// for 0.8s" suggests. On landing the crouch key is normally still held, so a refused entry leaves a PLAIN CROUCH,
	// and that is capped by MaxWalkSpeedCrouched (300 — the engine derives it from its own 600 default, and
	// RefreshWalkSpeedCap only ever raises MaxWalkSpeed). PhysWalking then hands CalcVelocity GroundFriction 8, which
	// BrakingFrictionFactor doubles to an effective 16 — the same constant ADR 0001 already records as taking
	// 900 -> 250 cm/s in about 0.08s. So a 1200 cm/s landing was down to 300 within a tenth of a second.
	//
	// Anti-spam survives intact: tapping crouch on flat ground exits through bReleasedCrouch, which IS on the ground
	// and still charges, so the entry impulse cannot be mashed. A slide-hop chain saturates at SlideMaxEntrySpeed
	// rather than ratcheting, because StartSliding's impulse may only lift speed TOWARD that cap.
	if (IsMovingOnGround())
	{
		SlideCooldownRemaining = SlideCooldown;
	}

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

// --- Auto wall jump ------------------------------------------------------------------------------------------------

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

bool UFPSRCharacterMovementComponent::TryAutoWallJump()
{
	// Every term is either replicated or already predicted, so client and server reach the same answer for the same
	// move — the property that used to make the derived hang state replay-safe, kept for the impulse that replaced it.
	if (!CharacterOwner || !IsFalling() || !IsSpecialMovementAllowed())
	{
		return false;
	}

	// Bounds before geometry, so an ordinary airborne frame costs two comparisons instead of a sphere sweep.
	if (!FPSRWallJumpBounds::CanFire(WallJumpCooldownRemaining, WallJumpsUsedThisAirborne, MaxWallJumpsPerAirborne))
	{
		return false;
	}

	// A bounce is an intent, so it needs input. Falling past a wall with no keys held is just falling — and now that
	// the launch fires by itself, this test is the ENTIRE difference between "wall jump" and "shoved by scenery".
	const FVector InputDirection = Acceleration.GetSafeNormal2D();
	if (InputDirection.IsNearlyZero())
	{
		return false;
	}

	FHitResult WallHit;
	if (!ProbeWall(InputDirection, WallJumpProbeHeight, WallHit) || !IsSurfaceGrabbable(WallHit))
	{
		return false;
	}

	// The normal has to come from THIS probe. There is no stored wall any more, which is why the dot test below cannot
	// be hoisted above the sweep the way the old grab check read a normal it already had.
	const FVector HitNormal = WallHit.ImpactNormal.GetSafeNormal2D();
	if (HitNormal.IsNearlyZero())
	{
		return false;
	}

	// Must be pushing INTO the wall, not merely alongside it — brushing past a corner while strafing is not a bounce.
	if (FVector::DotProduct(InputDirection, -HitNormal) < WallJumpInputDot)
	{
		return false;
	}

	const FVector JumpDirection = ComputeWallJumpDirection(HitNormal);
	if (!JumpDirection.IsNearlyZero())
	{
		// The jump OWNS the horizontal velocity rather than adding to it, so the player goes exactly where
		// ComputeWallJumpDirection says at exactly the speed below.
		//
		// Speed already in hand rides on top of the base push, so arriving fast still pays out. With the hang gone
		// there is no entry velocity to capture and bleed off over a window: the velocity at the moment of contact IS
		// the approach speed. Same reward, none of the bookkeeping.
		//
		// Both terms scale together: the carried speed comes from live velocity (already scaled), so leaving the
		// authored pair at 1x would make a scaled-up player hit the ceiling instantly and lose the carry entirely.
		const float CarriedSpeed = Velocity.Size2D();
		const float JumpSpeed = FMath::Min(FPSRScaled(WallJumpPushSpeed) + CarriedSpeed, FPSRScaled(WallJumpMaxSpeed));
		Velocity.X = JumpDirection.X * JumpSpeed;
		Velocity.Y = JumpDirection.Y * JumpSpeed;
	}

	if (WallJumpUpSpeed > 0.0f)
	{
		// An authored height of its own: assigned, so the bounce reads identically however the player arrived.
		Velocity.Z = WallJumpUpSpeed;
	}
	else
	{
		// Mirrors the engine's own DoJump, which takes the MAX rather than assigning. That distinction did not matter
		// on the old jump-key path (a hang had already killed the climb before the jump could read it) but it matters
		// now: this fires while RISING too, so assigning would let a wall graze CUT a jump the player already earned.
		Velocity.Z = FMath::Max<FVector::FReal>(Velocity.Z, JumpZVelocity);
	}

	// Charged unconditionally on the one path that can fire, which is what makes the two bounds impossible to dodge.
	FPSRWallJumpBounds::Consume(WallJumpCooldownRemaining, WallJumpsUsedThisAirborne, WallJumpCooldown);
	return true;
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

void UFPSRCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	// Runs before the physics step on the owning client, the server, and every replayed move — the one place where the
	// derived slide state and the auto wall jump are decided, so all three agree.

	// Stance weights. Advanced here rather than on a tick of their own because this runs on every role — the engine
	// calls it from PerformMovement for the owning client and the server AND from SimulateMovement for remote proxies —
	// so one place keeps the camera, the speed cap and (later) every player's animation on the same clock. This is the
	// one piece of this function a proxy genuinely wants, which is why it sits ABOVE the role gate below.
	AdvanceStanceBlends(DeltaSeconds);

	// SIMULATED PROXIES STOP HERE. The engine calls this from SimulateMovement as well as from PerformMovement, and a
	// proxy satisfies every entry condition below: bWantsToCrouch arrives through ACharacter::OnRep_IsCrouched, and
	// MovementMode and Velocity are both replicated. Deriving the states there would RE-DECIDE them on the one machine
	// with no authority over them — StartSliding would re-apply the entry impulse to an already-replicated velocity and
	// SimulateMovement integrates the result through MoveSmooth, while the wall jump would overwrite a replicated
	// Velocity with an impulse the server never simulated, spending a bound on a machine that owns neither.
	// Proxies are TOLD the slide instead: bSlidingVisual and SlideVisualSerial replicate for exactly that, and
	// IsSlidingForDisplay() reads those copies. The wall jump needs no such copy — it is an impulse, not a pose, so
	// what a proxy has to see is the resulting velocity, which movement replication already carries. Leaving early
	// also spares every remote player a per-frame wall sweep.
	if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
		return;
	}

	if (SlideCooldownRemaining > 0.0f)
	{
		SlideCooldownRemaining = FMath::Max(0.0f, SlideCooldownRemaining - DeltaSeconds);
	}

	// Auto wall jump. Both bounds advance every frame — the cooldown decays off the predicted delta exactly like
	// SlideCooldownRemaining above, and landing refills the per-airborne budget.
	FPSRWallJumpBounds::Advance(WallJumpCooldownRemaining, WallJumpsUsedThisAirborne, DeltaSeconds, IsMovingOnGround());

	// !bIsSliding is load-bearing, NOT a redundant "a slide is on the ground anyway" check. There are frames where the
	// character is airborne and the slide has not exited yet, because the slide's own exit is evaluated further down
	// THIS function:
	//   - sliding off a ledge — the previous frame's physics set MOVE_Falling, and bLeftGround is only read below;
	//   - a slide jump-cancel — ControlledCharacterMove calls CheckJumpInput (engine cpp:6347) BEFORE PerformMovement
	//     reaches UpdateCharacterStateBeforeMovement (engine cpp:2226), so DoJump has already switched to MOVE_Falling
	//     while bIsSliding is still true.
	// On such a frame the launch would fire and charge both bounds, and then the slide block below would overwrite
	// Velocity.X/Y with the slide heading before exiting — silently eating the push and one of the two wall jumps.
	// Deferring by the single frame the slide takes to exit costs nothing visible and keeps "these two never both act
	// on one frame" true rather than merely intended.
	if (!IsMovingOnGround() && !bIsSliding)
	{
		TryAutoWallJump();
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

		// (1) Slope stretches or compresses the curve's timeline. A gentle downhill plays the decay in slow motion; a
		// steep one (past sin(angle) = 1/SlopeTimeInfluence) drives the scale negative and the timer REWINDS toward
		// zero, restoring duration — and through the curve, speed — for as long as the hill lasts. Uphill runs out
		// early. The scale is one straight line, so slowdown, freeze and rewind meet with no seam at the threshold;
		// the rewind is bounded below so a cliff-steep slope can't snap the timer back instantly.
		const float SlopeTimeScale = FMath::Max(-SlideSlopeTimeRecoveryCap, 1.0f - (SlopeAlignment * SlopeTimeInfluence));
		SlideElapsed = FMath::Max(0.0f, SlideElapsed + DeltaSeconds * SlopeTimeScale);

		// (2) Slope also feeds real acceleration: g * sin(angle) along the direction of travel. Accumulated separately
		// from the curve because the curve is recomputed each frame and would wipe it out.
		// This frame's contribution on its own, which is what the no-curve path below adds. Taken BEFORE the running
		// total is clamped, deliberately: that clamp bounds a gross counter of everything the slope has ever added,
		// while braking is removing speed the whole time, so on a long descent the counter saturates while the actual
		// speed is still far below the ceiling. Deriving this from the clamped total would therefore switch slope
		// acceleration off partway down a hill. What the property contract bounds is the RESULT (see
		// SlopeAccelerationScale), and the final clamp below is what delivers that.
		float SlopeBonusThisFrame = 0.0f;
		if (SlopeAccelerationScale > 0.0f && !FMath::IsNearlyZero(SlopeAlignment))
		{
			const float GravityMagnitude = FMath::Abs(GetGravityZ());
			// Scaled like every other acceleration: gravity is a world constant, so an unscaled slope bonus would
			// become proportionally negligible the moment the player speeds up, and downhill slides would stop
			// feeling like downhill slides.
			SlopeBonusThisFrame = FPSRScaled(GravityMagnitude * SlopeAlignment * SlopeAccelerationScale) * DeltaSeconds;
			SlideSlopeSpeedBonus += SlopeBonusThisFrame;
			// Bounded both ways: downhill tops out at the slide ceiling instead of accelerating without limit, and
			// uphill can't drive the total below zero. Only the curve path reads this running total.
			SlideSlopeSpeedBonus = FMath::Clamp(SlideSlopeSpeedBonus, -FPSRScaled(SlideMaxSpeed), FPSRScaled(SlideMaxSpeed));
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

				// Only this frame's contribution, NOT the running total. Unlike the curve branch, this speed was
				// written by the previous frame and so already carries every earlier contribution; adding the
				// accumulated bonus here would re-apply the whole of it once per frame and accelerate super-linearly
				// until the cap.
				TargetSpeed += SlopeBonusThisFrame;
			}

			// Capped on both paths — a long descent reaches the slide's terminal speed rather than growing
			// indefinitely, and an uphill bonus can't drive the result negative.
			// THE slide ceiling. The curve path writes Velocity outright a couple of lines below, so GetMaxSpeed()
			// never sees a slide — scaling only there left this 1400 in force and the whole slide unscaled.
			TargetSpeed = FMath::Clamp(TargetSpeed, 0.0f, FPSRScaled(SlideMaxSpeed));

			Velocity.X = SlideHeading.X * TargetSpeed;
			Velocity.Y = SlideHeading.Y * TargetSpeed;
		}

		// Exits, in the order they matter. The elapsed-time bound is invariant 7's guarantee: a downhill slide that
		// keeps regaining speed would otherwise never satisfy the speed exit and the state would be inescapable. A
		// steep downhill only DEFERS it — the slope block above rewinds the timer frame by frame — so once the hill ends the timer resumes and this exit is
		// unconditional again, with crouch-release, leaving the ground and the gate live the whole way down.
		const bool bReleasedCrouch = !bWantsToCrouch;
		const bool bLeftGround = !IsMovingOnGround();
		const bool bTooSlow = Velocity.SizeSquared2D() < FMath::Square(FPSRScaled(SlideMinSpeed));
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
		return FPSRScaled(SlideMaxSpeed);
	}

	// Engine speed for this stance: MaxWalkSpeed standing, MaxWalkSpeedCrouched crouched — and already carrying any
	// card multiplier, which is why the normalized curve needs no scaling of its own.
	// Scaled HERE rather than on the way out, so everything below works in one consistent space. StanceSpeedFrom is
	// captured by calling this very function (Crouch/UnCrouch sample it before the stance flips), so it arrives
	// already scaled — a scale applied after the Lerp below would hit it a second time and a 3x setting would ease
	// down from 9x for the length of a crouch. The curve ramp and backpedal penalty are multiplicative and do not
	// care which space they are in.
	float MaxSpeed = FPSRScaled(Super::GetMaxSpeed());

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

float UFPSRCharacterMovementComponent::GetMaxAcceleration() const
{
	// Acceleration scales WITH speed on purpose. Scaling the cap alone would leave the player taking three times as
	// long to reach it, which reads as sluggish rather than fast — the opposite of what the experiment is asking.
	return Super::GetMaxAcceleration() * FPSRPlayerSpeedScale();
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
			// Scales with speed so the slide keeps its SHAPE: an unscaled deceleration would stretch every slide out
			// three times as far, which is a different move rather than the same move faster.
			ApplyVelocityBraking(DeltaTime, SlideGroundFriction, FPSRScaled(SlideBrakingDeceleration));
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
	const float AddSpeed = FPSRScaled(AirStrafeWishSpeed) - SpeedAlongWish;
	if (AddSpeed <= 0.0f)
	{
		return; // already moving that way at least this fast — holding forward in the air must not accelerate
	}

	// Note this function can run MORE THAN ONCE per frame (the falling step sub-steps, and re-runs after a mid-air
	// collision). It holds no state and the AddSpeed test bounds the total speed along any one direction, so repeat
	// calls converge instead of compounding — don't add anything here that assumes one call per frame.
	const float AccelSpeed = FMath::Min(FPSRScaled(AirStrafeAcceleration) * DeltaTime, AddSpeed);
	const float SpeedBefore = LateralVelocity.Size();
	LateralVelocity += WishDirection * AccelSpeed;

	// The CEILING only ever raises toward AirStrafeMaxSpeed and never cuts speed carried in above it — a slide jump
	// arriving faster than the ceiling keeps every bit of it (same rule as SlideMaxEntrySpeed).
	//
	// That is a statement about the clamp, not about the whole step: input pointing AGAINST the direction of travel
	// still subtracts, because the push above is a vector addition. That is deliberate — holding the back key is how
	// a player sheds momentum in the air. Nothing takes speed away on its own; only the player can.
	LateralVelocity = LateralVelocity.GetClampedToMaxSize(FMath::Max(SpeedBefore, FPSRScaled(AirStrafeMaxSpeed)));

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
	return WishDirection.IsNearlyZero() ? FVector::ZeroVector : (WishDirection * FPSRScaled(AirStrafeAcceleration));
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
	// No wall term here any more: the wall jump no longer goes through the jump input at all (it fires from
	// UpdateCharacterStateBeforeMovement), so it neither needs this permission nor spends the jump budget.
	return IsJumpAllowed() && (IsMovingOnGround() || IsFalling());
}

float UFPSRCharacterMovementComponent::GetPlanarSpeed() const
{
	return Velocity.Size2D();
}

FString UFPSRCharacterMovementComponent::GetLocomotionStateName() const
{
	FString StateName;
	if (IsFalling())        { StateName = TEXT("Air"); }
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
	// Same reasoning for the wall: "nothing happened when I hit that wall" has two quite different causes — the budget
	// for this airborne period is spent, or the cooldown has not elapsed — and they are otherwise the same silence.
	// Either bound alone is a reason the wall did nothing, so BOTH have to be able to speak. Nesting the cooldown
	// inside the used-count left one silence intact: landing clears the count but not the cooldown, so a wall touched
	// within 0.35s of a landing was refused with nothing on screen.
	if (WallJumpsUsedThisAirborne > 0 || WallJumpCooldownRemaining > 0.0f)
	{
		StateName += FString::Printf(TEXT("  (wall %d/%d"), WallJumpsUsedThisAirborne, MaxWallJumpsPerAirborne);
		if (WallJumpCooldownRemaining > 0.0f)
		{
			StateName += FString::Printf(TEXT(" CD %.2f"), WallJumpCooldownRemaining);
		}
		StateName += TEXT(")");
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
	// Every locomotion state allows fire. The wall hang was the single exception — both hands on the wall, the weapon
	// off screen for the duration of the grab — and it no longer exists: the wall is an instantaneous impulse, so
	// there is no span to take the weapon away for. Kept as a predicate rather than folded into its callers; see the
	// header for why five systems sharing one judgment source is worth a function that currently only says yes.
	return true;
}

float UFPSRCharacterMovementComponent::GetSpreadMultiplier() const
{
	// Exclusive, not multiplied: a crouched player who walks off a ledge is "airborne", not "airborne × crouched".
	if (IsFalling())
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

	SavedWallJumpCooldownRemaining = 0.0f;
	SavedWallJumpsUsedThisAirborne = 0;

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

		// The auto wall jump needs a guard of its own, and this is the one place that can supply it.
		//
		// Super's test DOES compare the packed movement mode (engine CharacterMovementComponent.cpp:12930-12939), and
		// that is what used to catch the old wall hang for free — a hang was MOVE_Custom on one side and not on the
		// other. An impulse changes no mode: Falling on both sides. What is left of Super's test that could notice this
		// is INPUT EDGES — bPressedJump, bWasJumping, the compressed flags — and the auto wall jump has none, because
		// it fires from state with the movement input unchanged. The acceleration-direction test passes especially
		// well, since the trigger REQUIRES the input to keep pointing into the wall.
		// (The slide escapes all this by having a bWantsToCrouch edge the engine does notice.)
		//
		// Folding across that frame is not merely a lost frame: FSavedMove_Character::CombineWith rewinds the pawn to
		// the start of the older move and never calls PrepMoveFor, so the character goes back to before the launch
		// while these two counters keep their after-the-launch values. The re-simulation then refuses the jump the
		// client has already drawn, and the player sees it swallowed. Combining happens whenever client framerate
		// exceeds the send rate, which is the normal case.
		//
		// Same shape and same justification as the stance guard below — a 0.35s window, so refusing outright costs
		// nothing measurable and removes the whole class of divergence.
		if (SavedWallJumpCooldownRemaining > 0.0f || Other->SavedWallJumpCooldownRemaining > 0.0f)
		{
			return false;
		}
		if (SavedWallJumpsUsedThisAirborne != Other->SavedWallJumpsUsedThisAirborne)
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

		SavedWallJumpCooldownRemaining = Movement->WallJumpCooldownRemaining;
		SavedWallJumpsUsedThisAirborne = Movement->WallJumpsUsedThisAirborne;

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

		// The wall itself is NOT restored — there is nothing to restore. The bounce leaves no state behind; what it
		// leaves is velocity, which the correction has already set. Only the two BOUNDS come back, and they have to:
		// they are history, not something a replayed frame can re-derive, and without them a replay would hand out a
		// second launch the server never simulated.
		Movement->WallJumpCooldownRemaining = SavedWallJumpCooldownRemaining;
		Movement->WallJumpsUsedThisAirborne = SavedWallJumpsUsedThisAirborne;

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
