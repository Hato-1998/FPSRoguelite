// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMissionOrb.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "DrawDebugHelpers.h"

AFPSRMissionOrb::AFPSRMissionOrb()
{
	bReplicates = true;
	// Carry missions move the orb server-side every tick (it follows the carrier); replicate movement so clients
	// see the objective at the right place. Harmless for static collect-orbs (they never move).
	SetReplicateMovement(true);
	PrimaryActorTick.bCanEverTick = true;

	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	Sphere->InitSphereRadius(100.0f);
	Sphere->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Sphere->SetGenerateOverlapEvents(true);
	RootComponent = Sphere;

	Sphere->OnComponentBeginOverlap.AddDynamic(this, &AFPSRMissionOrb::OnSphereBeginOverlap);
}

void AFPSRMissionOrb::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFPSRMissionOrb, bCollected);
}

void AFPSRMissionOrb::SetCollected(bool bNewCollected)
{
	if (!HasAuthority())
	{
		return;
	}
	bCollected = bNewCollected;
	SetActorHiddenInGame(bNewCollected);
	SetActorEnableCollision(!bNewCollected);
}

void AFPSRMissionOrb::OnSphereBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority() || bCollected)
	{
		return;
	}
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || !Pawn->IsPlayerControlled())
	{
		return;
	}
	// Mark collected so it cannot retrigger; the owning mission decides what "collected" means (consume vs carry).
	bCollected = true;
	SetActorEnableCollision(false);
	OnCollectedNative.Broadcast(this, Pawn);
}

void AFPSRMissionOrb::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

#if ENABLE_DRAW_DEBUG
	if (!bCollected)
	{
		const float R = Sphere ? Sphere->GetScaledSphereRadius() : 100.0f;
		DrawDebugSphere(GetWorld(), GetActorLocation(), R, 12, FColor::Yellow, false, 0.0f);
	}
#endif
}
