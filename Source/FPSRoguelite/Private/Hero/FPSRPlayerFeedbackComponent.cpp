// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hero/FPSRPlayerFeedbackComponent.h"
#include "Enemy/FPSREnemyBase.h"
#include "Enemy/FPSREnemyHealthComponent.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"

UFPSRPlayerFeedbackComponent::UFPSRPlayerFeedbackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = ThreatScanInterval; // throttled threat scan cadence
	SetIsReplicatedByDefault(false); // local cosmetic feedback only
}

void UFPSRPlayerFeedbackComponent::BeginPlay()
{
	Super::BeginPlay();

	PrimaryComponentTick.TickInterval = ThreatScanInterval;

	// Tick only with a local view. Possession (server) and controller replication (client) can land AFTER
	// BeginPlay, so re-evaluate on controller changes rather than latching the BeginPlay result — otherwise the
	// scan would stay permanently disabled for a normally-spawned local player.
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveControllerChangedDelegate.AddDynamic(this, &UFPSRPlayerFeedbackComponent::HandleControllerChanged);
	}
	SetComponentTickEnabled(IsLocalView());
}

void UFPSRPlayerFeedbackComponent::HandleControllerChanged(APawn* InPawn, AController* OldController, AController* NewController)
{
	if (IsLocalView())
	{
		SetComponentTickEnabled(true);
	}
	else
	{
		DisableAndClearThreats();
	}
}

void UFPSRPlayerFeedbackComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsLocalView())
	{
		// Possession can change (respawn / spectate) — stop scanning + clear stale indicators if no longer local.
		DisableAndClearThreats();
		return;
	}

	ScanThreats();
}

void UFPSRPlayerFeedbackComponent::NotifyHitConfirmed(EFPSRHitMarkerType MarkerType)
{
	if (!IsLocalView())
	{
		return;
	}
	OnHitMarker.Broadcast(MarkerType);
}

bool UFPSRPlayerFeedbackComponent::IsLocalView() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn != nullptr && Pawn->IsLocallyControlled() && Pawn->IsPlayerControlled();
}

void UFPSRPlayerFeedbackComponent::DisableAndClearThreats()
{
	SetComponentTickEnabled(false);
	if (ActiveThreats.Num() > 0)
	{
		ActiveThreats.Reset();
		OnThreatsUpdated.Broadcast(ActiveThreats);
	}
}

void UFPSRPlayerFeedbackComponent::ScanThreats()
{
	ActiveThreats.Reset();

	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	UWorld* World = GetWorld();
	if (PC == nullptr || World == nullptr)
	{
		OnThreatsUpdated.Broadcast(ActiveThreats);
		return;
	}

	// View basis (camera), flattened to the horizontal plane for screen-edge direction.
	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	FVector Forward2D = ViewRotation.Vector();
	Forward2D.Z = 0.0f;
	if (!Forward2D.Normalize())
	{
		OnThreatsUpdated.Broadcast(ActiveThreats);
		return;
	}

	const float RadiusSq = ThreatRadius * ThreatRadius;
	const float CosHalfView = FMath::Cos(FMath::DegreesToRadians(ThreatViewHalfAngleDeg));

	for (TActorIterator<AFPSREnemyBase> It(World); It; ++It)
	{
		const AFPSREnemyBase* Enemy = *It;
		if (Enemy == nullptr || Enemy->IsHidden()) // hidden == pooled/dormant (replicates to clients)
		{
			continue;
		}
		if (const UFPSREnemyHealthComponent* Health = Enemy->FindComponentByClass<UFPSREnemyHealthComponent>())
		{
			if (Health->IsDead())
			{
				continue;
			}
		}

		FVector ToEnemy = Enemy->GetActorLocation() - ViewLocation;
		ToEnemy.Z = 0.0f;
		const float DistSq = ToEnemy.SizeSquared();
		if (DistSq > RadiusSq || DistSq <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const float Dist = FMath::Sqrt(DistSq);
		const FVector Dir = ToEnemy / Dist;
		const float ForwardDot = FVector::DotProduct(Forward2D, Dir);
		if (ForwardDot >= CosHalfView)
		{
			continue; // inside the forward view cone — visible, not a blind-spot threat
		}

		// Signed yaw of the threat relative to camera forward (-180..180; negative = left).
		const float RightDot = FVector::DotProduct(FVector::CrossProduct(FVector::UpVector, Forward2D), Dir);
		FFPSRThreatDir Threat;
		Threat.AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(RightDot, ForwardDot));
		Threat.Severity01 = FMath::Clamp(1.0f - Dist / ThreatRadius, 0.0f, 1.0f);
		ActiveThreats.Add(Threat);
	}

	// Strongest (closest) first, then cap the reported set.
	ActiveThreats.Sort([](const FFPSRThreatDir& A, const FFPSRThreatDir& B)
	{
		return A.Severity01 > B.Severity01;
	});
	if (ActiveThreats.Num() > MaxThreats)
	{
		ActiveThreats.SetNum(MaxThreats);
	}

	OnThreatsUpdated.Broadcast(ActiveThreats);
}
