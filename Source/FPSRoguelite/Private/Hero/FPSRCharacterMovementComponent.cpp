// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hero/FPSRCharacterMovementComponent.h"

#include "Curves/CurveFloat.h"
#include "GameFramework/Character.h"
#include "Hero/FPSRCharacter.h"

UFPSRCharacterMovementComponent::UFPSRCharacterMovementComponent()
{
	// Crouch is engine-native (bWantsToCrouch is already predicted and already in the move packet), so the slide can
	// ride on it without a custom flag — but it only works if crouching is actually enabled on the component.
	NavAgentProps.bCanCrouch = true;
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

void UFPSRCharacterMovementComponent::StartSliding()
{
	bIsSliding = true;
	SlideElapsed = 0.0f;

	// Entry impulse ("순간 가속"): scale the existing planar velocity and keep the vertical component untouched so a
	// slide started while settling onto the floor doesn't cancel the landing.
	const FVector PlanarVelocity = FVector(Velocity.X, Velocity.Y, 0.0f);
	const float BoostedSpeed = FMath::Min(PlanarVelocity.Size() * SlideEnterSpeedMultiplier, SlideMaxSpeed);
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
	// Charged on EVERY exit path, so jump-cancelling isn't a way to dodge the anti-spam lockout.
	SlideCooldownRemaining = SlideCooldown;

	// Hand the run ramp back at its END, not at zero. A slide exits faster than running speed, so resuming the curve
	// from t=0 would make GetMaxSpeed collapse and slam the player to a halt; from the end, the excess simply decays.
	if (GroundSpeedCurve)
	{
		float CurveMinTime = 0.0f, CurveMaxTime = 0.0f;
		GroundSpeedCurve->GetTimeRange(CurveMinTime, CurveMaxTime);
		GroundAccelElapsed = CurveMaxTime;
	}
}

void UFPSRCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	// Runs before the physics step on the owning client, the server, and every replayed move — the one place where the
	// slide's derived state is decided, so all three agree.
	if (SlideCooldownRemaining > 0.0f)
	{
		SlideCooldownRemaining = FMath::Max(0.0f, SlideCooldownRemaining - DeltaSeconds);
	}

	// GroundSpeedCurve's X axis. Held while airborne so a mid-sprint jump doesn't land back at a standing start, and
	// cleared when there's no input so stopping really does mean re-accelerating from zero.
	if (IsMovingOnGround() && !bIsSliding)
	{
		GroundAccelElapsed = Acceleration.IsNearlyZero() ? 0.0f : (GroundAccelElapsed + DeltaSeconds);
	}

	if (bIsSliding)
	{
		SlideElapsed += DeltaSeconds;

		// Authored decay: drive the speed straight off the curve so the shape lands exactly as drawn. Direction is
		// whatever the slide currently has, so this scales the slide without steering it.
		if (SlideSpeedCurve)
		{
			// Capped by the entry speed: a slow entry keeps its own speed instead of snapping up to the curve's
			// opening value, while a full-speed entry follows the curve exactly.
			const float CurveSpeed = FMath::Max(0.0f, SlideSpeedCurve->GetFloatValue(SlideElapsed)) * GetSpeedCurveScale();
			const float TargetSpeed = FMath::Min(CurveSpeed, SlideEntrySpeed);
			const FVector SlideDirection = Velocity.GetSafeNormal2D();
			if (!SlideDirection.IsNearlyZero())
			{
				Velocity.X = SlideDirection.X * TargetSpeed;
				Velocity.Y = SlideDirection.Y * TargetSpeed;
			}
		}

		// Exits, in the order they matter. The elapsed-time bound is invariant 7's guarantee: a downhill slide that
		// keeps regaining speed would otherwise never satisfy the speed exit and the state would be inescapable.
		const bool bReleasedCrouch = !bWantsToCrouch;
		const bool bLeftGround = !IsMovingOnGround();
		const bool bTooSlow = Velocity.SizeSquared2D() < FMath::Square(SlideMinSpeed);
		const bool bTimedOut = SlideElapsed >= SlideMaxDuration;
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

float UFPSRCharacterMovementComponent::GetSpeedCurveScale() const
{
	// The curves store literal cm/s authored against SpeedCurveReferenceSpeed. Rescale by however far MaxWalkSpeed has
	// been moved from that reference (cards raise it), so a +30% move-speed card really is +30% at every point of the
	// ramp instead of being clipped at the authored ceiling.
	return (SpeedCurveReferenceSpeed > KINDA_SMALL_NUMBER) ? (MaxWalkSpeed / SpeedCurveReferenceSpeed) : 1.0f;
}

float UFPSRCharacterMovementComponent::GetMaxSpeed() const
{
	if (bIsSliding)
	{
		// Flat ceiling while sliding: with SlideSpeedCurve assigned the curve writes the speed outright each frame
		// (UpdateCharacterStateBeforeMovement), so this only has to stay out of its way.
		return SlideMaxSpeed;
	}

	if (GroundSpeedCurve && IsMovingOnGround() && !IsCrouching())
	{
		// Authored standing -> running ramp. Returning it as the max speed (rather than writing Velocity) keeps
		// knockback, slopes and other external forces behaving normally.
		return FMath::Max(0.0f, GroundSpeedCurve->GetFloatValue(GroundAccelElapsed)) * GetSpeedCurveScale();
	}
	return Super::GetMaxSpeed();
}

float UFPSRCharacterMovementComponent::GetMaxAcceleration() const
{
	// A slide is committed movement: input acceleration is replaced by SlideSteerAcceleration (0 by default) so the
	// player can't simply crouch-run at slide speed.
	return bIsSliding ? SlideSteerAcceleration : Super::GetMaxAcceleration();
}

float UFPSRCharacterMovementComponent::GetMaxBrakingDeceleration() const
{
	if (bIsSliding)
	{
		// With a curve assigned the speed is written directly each frame; leaving braking on top of that would fight
		// the authored shape.
		return SlideSpeedCurve ? 0.0f : SlideBrakingDeceleration;
	}
	return Super::GetMaxBrakingDeceleration();
}

void UFPSRCharacterMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	// PhysWalking hands us GroundFriction, and the braking path multiplies it by BrakingFrictionFactor. Left alone that
	// is an effective friction of 16, which kills a slide in well under a tenth of a second — the slide would read as an
	// instant stop rather than a slide. Swap in the slide's own (near-zero) friction so SlideBrakingDeceleration is what
	// actually shapes the decay.
	if (bIsSliding)
	{
		Friction = SlideGroundFriction;
	}
	Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
}

bool UFPSRCharacterMovementComponent::CanCrouchInCurrentState() const
{
	// Super allows IsFalling() too; this design does not (no crouch, and therefore no slide, while airborne).
	return Super::CanCrouchInCurrentState() && IsMovingOnGround();
}

bool UFPSRCharacterMovementComponent::CanAttemptJump() const
{
	if (bIsSliding)
	{
		// Super's middle term is !bWantsToCrouch, which a slide always trips (the slide IS the crouch intent). Drop
		// exactly that term and keep the rest verbatim, so jump-count and hold-time rules stay engine-governed.
		return IsJumpAllowed() && (IsMovingOnGround() || IsFalling());
	}
	return Super::CanAttemptJump();
}

float UFPSRCharacterMovementComponent::GetPlanarSpeed() const
{
	return Velocity.Size2D();
}

FString UFPSRCharacterMovementComponent::GetLocomotionStateName() const
{
	FString StateName;
	if (IsFalling())        { StateName = TEXT("Air"); }
	else if (bIsSliding)    { StateName = TEXT("Slide"); }
	else if (IsCrouching()) { StateName = TEXT("Crouch"); }
	else                    { StateName = TEXT("Run"); }

	// Surface the slide lockout too — otherwise "crouch did nothing" is indistinguishable from "too slow to slide".
	if (!bIsSliding && SlideCooldownRemaining > 0.0f)
	{
		StateName += FString::Printf(TEXT("  (slide CD %.1f)"), SlideCooldownRemaining);
	}
	return StateName;
}

bool UFPSRCharacterMovementComponent::CanFireInCurrentState() const
{
	// Sliding, crouching, jumping and airborne all allow fire by design; only the wall-hang (later phase) will not,
	// because both hands are on the wall. Returning a constant today keeps the weapon code written against its final
	// contract instead of being rewired when that state lands.
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
}

bool FSavedMove_FPSR::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	// Never fold two moves that straddle a slide transition: combining them would erase the frame the state changed on
	// and the replay would diverge from what the server simulated.
	const FSavedMove_FPSR* Other = static_cast<const FSavedMove_FPSR*>(NewMove.Get());
	if (Other && bSavedIsSliding != Other->bSavedIsSliding)
	{
		return false;
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
