// Copyright Epic Games, Inc. All Rights Reserved.

#include "Map/FPSRBoundaryBlocker.h"
#include "FPSRCollisionChannels.h"
#include "Components/BoxComponent.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

AFPSRBoundaryBlocker::AFPSRBoundaryBlocker()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	BlockBox = CreateDefaultSubobject<UBoxComponent>(TEXT("BlockBox"));
	SetRootComponent(BlockBox);
	BlockBox->SetBoxExtent(FVector(200.0f, 400.0f, 300.0f)); // designer resizes to span the doorway
	BlockBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	// WorldDynamic (not WorldStatic) so the flow-field bake's WorldStatic traces ignore it; block ONLY the player object
	// channel so the swarm passes (boundary = flow/MapId). A player's capsule blocks WorldDynamic by default -> stopped.
	BlockBox->SetCollisionObjectType(ECC_WorldDynamic);
	BlockBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	BlockBox->SetCollisionResponseToChannel(ECC_FPSRPlayerPawn, ECR_Block);
}

void AFPSRBoundaryBlocker::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRBoundaryBlocker, bBlocking, Params);
}

void AFPSRBoundaryBlocker::BeginPlay()
{
	Super::BeginPlay();
	// Apply the (possibly already-replicated) initial state so a late-joining client that receives bBlocking=false at
	// relevance opens the passage, and everyone starts sealed otherwise. OnRep won't fire for the initial replicated value.
	ApplyBlockingState();
}

void AFPSRBoundaryBlocker::SetBlocking(bool bNewBlocking)
{
	if (!HasAuthority() || bBlocking == bNewBlocking)
	{
		return;
	}
	bBlocking = bNewBlocking;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRBoundaryBlocker, bBlocking, this);
	// Listen-server host gets no OnRep — apply directly. ForceNetUpdate so remote clients drop the wall promptly.
	ApplyBlockingState();
	ForceNetUpdate();
}

void AFPSRBoundaryBlocker::OnRep_Blocking()
{
	ApplyBlockingState();
}

void AFPSRBoundaryBlocker::ApplyBlockingState()
{
	if (BlockBox)
	{
		BlockBox->SetCollisionEnabled(bBlocking ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
}
