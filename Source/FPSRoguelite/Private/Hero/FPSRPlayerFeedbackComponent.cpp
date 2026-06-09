// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hero/FPSRPlayerFeedbackComponent.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

UFPSRPlayerFeedbackComponent::UFPSRPlayerFeedbackComponent()
{
	// Mostly event-driven; the tick is enabled only while a ranged-target warning is active (to track direction).
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(false); // local cosmetic feedback only
}

void UFPSRPlayerFeedbackComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (ActiveRangedSources.Num() == 0)
	{
		SetComponentTickEnabled(false);
		return;
	}
	if (!IsLocalView())
	{
		// Lost local control (respawn / spectate) — clear so the HUD can't keep a warning stuck on.
		ActiveRangedSources.Reset();
		SetComponentTickEnabled(false);
		BroadcastRangedWarnings();
		return;
	}

	BroadcastRangedWarnings(); // re-point every source as the player turns / sources move
}

void UFPSRPlayerFeedbackComponent::NotifyHitConfirmed(EFPSRHitMarkerType MarkerType)
{
	if (!IsLocalView())
	{
		return;
	}
	OnHitMarker.Broadcast(MarkerType);
}

void UFPSRPlayerFeedbackComponent::ReceiveDamageFromWorld(const FVector& InstigatorWorldLocation)
{
	if (!IsLocalView())
	{
		return;
	}
	float AngleDeg = 0.0f;
	if (ComputeCameraRelativeAngle(InstigatorWorldLocation, AngleDeg))
	{
		OnDamageDirection.Broadcast(AngleDeg);
	}
}

void UFPSRPlayerFeedbackComponent::ReceiveRangedTarget(int32 SourceId, const FVector& SourceWorldLocation, bool bActive)
{
	if (!IsLocalView())
	{
		return;
	}

	if (bActive)
	{
		ActiveRangedSources.Add(SourceId, SourceWorldLocation); // add or update (moving source = re-send)
	}
	else
	{
		ActiveRangedSources.Remove(SourceId);
	}

	// Tick only while at least one source is active (so warnings track the player turning / sources moving).
	SetComponentTickEnabled(ActiveRangedSources.Num() > 0);
	BroadcastRangedWarnings();
}

void UFPSRPlayerFeedbackComponent::BroadcastRangedWarnings()
{
	TArray<float> AngleDegs;
	AngleDegs.Reserve(ActiveRangedSources.Num());
	for (const TPair<int32, FVector>& Source : ActiveRangedSources)
	{
		float AngleDeg = 0.0f;
		if (ComputeCameraRelativeAngle(Source.Value, AngleDeg))
		{
			AngleDegs.Add(AngleDeg);
		}
	}
	OnRangedTargetWarning.Broadcast(AngleDegs); // empty array = no active warnings
}

bool UFPSRPlayerFeedbackComponent::IsLocalView() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn != nullptr && Pawn->IsLocallyControlled() && Pawn->IsPlayerControlled();
}

bool UFPSRPlayerFeedbackComponent::ComputeCameraRelativeAngle(const FVector& WorldLocation, float& OutAngleDeg) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (PC == nullptr)
	{
		return false;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	FVector Forward2D = ViewRotation.Vector();
	Forward2D.Z = 0.0f;
	if (!Forward2D.Normalize())
	{
		return false;
	}

	FVector ToTarget = WorldLocation - ViewLocation;
	ToTarget.Z = 0.0f;
	if (!ToTarget.Normalize())
	{
		OutAngleDeg = 0.0f; // source is on top of the player — treat as directly ahead
		return true;
	}

	// Signed yaw vs camera forward: 0 = ahead, +90 = right, ±180 = behind, negative = left.
	const float ForwardDot = FVector::DotProduct(Forward2D, ToTarget);
	const float RightDot = FVector::DotProduct(FVector::CrossProduct(FVector::UpVector, Forward2D), ToTarget);
	OutAngleDeg = FMath::RadiansToDegrees(FMath::Atan2(RightDot, ForwardDot));
	return true;
}

#if !UE_BUILD_SHIPPING
namespace
{
	UFPSRPlayerFeedbackComponent* FPSRGetLocalFeedback(UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		return Pawn ? Pawn->FindComponentByClass<UFPSRPlayerFeedbackComponent>() : nullptr;
	}

	// Build a world location AngleDeg from the local camera forward (for testing the directional indicators).
	bool FPSRMakeTestWorldLocation(UWorld* World, float AngleDeg, FVector& OutLocation)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (PC == nullptr)
		{
			return false;
		}
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		FVector Forward2D = ViewRotation.Vector();
		Forward2D.Z = 0.0f;
		if (!Forward2D.Normalize())
		{
			return false;
		}
		const FVector Dir = Forward2D.RotateAngleAxis(AngleDeg, FVector::UpVector);
		OutLocation = ViewLocation + Dir * 500.0f;
		return true;
	}

	FAutoConsoleCommandWithWorldAndArgs GCmdTestDamageDir(
		TEXT("FPSR.TestDamageDir"),
		TEXT("Test the damage-direction indicator from the local view. Usage: FPSR.TestDamageDir [angleDeg]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const float Angle = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 135.0f;
			FVector Location;
			if (FPSRMakeTestWorldLocation(World, Angle, Location))
			{
				if (UFPSRPlayerFeedbackComponent* FB = FPSRGetLocalFeedback(World))
				{
					FB->ReceiveDamageFromWorld(Location);
				}
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GCmdTestRangedWarn(
		TEXT("FPSR.TestRangedWarn"),
		TEXT("Test the ranged-target warning. Usage: FPSR.TestRangedWarn [angleDeg] [active 0/1] [sourceId]. "
			 "Use distinct ids to test concurrent sources (e.g. ...90 1 1  then  ...-120 1 2)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const float Angle = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 90.0f;
			const bool bActive = Args.Num() > 1 ? (FCString::Atoi(*Args[1]) != 0) : true;
			const int32 SourceId = Args.Num() > 2 ? FCString::Atoi(*Args[2]) : 1;
			FVector Location;
			if (FPSRMakeTestWorldLocation(World, Angle, Location))
			{
				if (UFPSRPlayerFeedbackComponent* FB = FPSRGetLocalFeedback(World))
				{
					FB->ReceiveRangedTarget(SourceId, Location, bActive);
				}
			}
		}));
}
#endif
