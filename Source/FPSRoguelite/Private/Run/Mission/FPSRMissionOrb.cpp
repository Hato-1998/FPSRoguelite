// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMissionOrb.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

AFPSRMissionOrb::AFPSRMissionOrb()
{
	bReplicates = true;
	// Carry missions move the orb server-side every tick (it follows the carrier); replicate movement so clients
	// see the objective at the right place. Harmless for static collect-orbs (they never move).
	SetReplicateMovement(true);
	// No per-frame work: collection is overlap-event driven (OnSphereBeginOverlap) and carry-mission movement is
	// driven by the owning mission actor (OnMissionTickServer), not this orb — so it stays tickless (avoids an idle
	// per-frame dispatch on every live orb).
	PrimaryActorTick.bCanEverTick = false;

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
	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRMissionOrb, bCollected, Params);
}

void AFPSRMissionOrb::SetCollected(bool bNewCollected)
{
	if (!HasAuthority())
	{
		return;
	}
	bCollected = bNewCollected;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRMissionOrb, bCollected, this);
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
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRMissionOrb, bCollected, this);
	SetActorEnableCollision(false);
	OnCollectedNative.Broadcast(this, Pawn);
}
