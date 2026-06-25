// Copyright Epic Games, Inc. All Rights Reserved.

#include "Door/FPSRDoor.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "FPSRCollisionChannels.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

AFPSRDoor::AFPSRDoor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true; // bBroken replicates so late joiners / clients see the door open

	// Neutral scene root so the leaves and frame are SIBLINGS — hiding the leaves on break (propagated to their own
	// sub-meshes) never hides the frame.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Breakable leaves — the weapon target + barrier. ECC_FPSRPlayerPawn (see header): gathered by every weapon
	// object-query (-> auto damage via HealthComponent), blocks players + enemies, dash-proof. QueryOnly is enough
	// (movement sweeps + weapon queries are all query-based; no physics simulation needed).
	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(Root);
	DoorMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DoorMesh->SetCollisionObjectType(ECC_FPSRPlayerPawn);
	DoorMesh->SetCollisionResponseToAllChannels(ECR_Block);
	DoorMesh->SetGenerateOverlapEvents(false);
	// No mesh asset assigned here — Game.MD §2 forbids hardcoding asset paths; the designer assigns SM in BP.

	// Frame (문틀) — inert wall. WorldStatic object type is NEVER gathered by the weapon object-queries
	// (ECC_Pawn / ECC_FPSRPlayerPawn / weakpoint), so shots stop on it as cover with no damage; it still blocks
	// movement and the weapon Visibility wall-trace like normal static geometry. Empty by default (frameless door).
	FrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrameMesh"));
	FrameMesh->SetupAttachment(Root);
	FrameMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	FrameMesh->SetCollisionObjectType(ECC_WorldStatic);
	FrameMesh->SetCollisionResponseToAllChannels(ECR_Block);
	FrameMesh->SetGenerateOverlapEvents(false);

	HealthComponent = CreateDefaultSubobject<UFPSREnemyHealthComponent>(TEXT("HealthComponent"));
	HealthComponent->SetCountsAsKill(false); // destructible, but NOT an enemy (no kill credit / on-kill / lifesteal)
}

void AFPSRDoor::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		if (HealthComponent)
		{
			// Size HP to the designer's durability (overrides the component default), then listen for death.
			HealthComponent->InitializeMaxHealth(Durability);
			HealthComponent->OnDeath.AddDynamic(this, &AFPSRDoor::HandleBroken);
		}
	}
	else if (bBroken)
	{
		// Late-joining client: the door was already broken when it became net-relevant — apply the open state now
		// (OnRep won't fire for the initial replicated value).
		ApplyBrokenState();
	}
}

void AFPSRDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRDoor, bBroken, Params);
}

void AFPSRDoor::HandleBroken(AActor* DeadActor, AActor* Killer)
{
	if (!HasAuthority() || bBroken)
	{
		return;
	}

	bBroken = true;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRDoor, bBroken, this);

	ApplyBrokenState(); // server: open the passage (collision off) + hide the leaves
	OnDoorBroken();     // BP presentation (server)
}

void AFPSRDoor::OnRep_Broken()
{
	if (bBroken)
	{
		ApplyBrokenState();
		OnDoorBroken(); // BP presentation (clients)
	}
}

void AFPSRDoor::ApplyBrokenState()
{
	// Disable collision + hide the LEAVES only (propagate to their sub-meshes); the frame is a sibling and stays
	// solid/visible. Not SetActorHiddenInGame/SetActorEnableCollision — those would also take down the frame.
	if (DoorMesh)
	{
		DoorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DoorMesh->SetVisibility(false, /*bPropagateToChildren*/ true);
	}
}
