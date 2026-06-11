// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMissionFleeTarget.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"

AFPSRMissionFleeTarget::AFPSRMissionFleeTarget()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(50.0f, 100.0f);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Capsule->SetCollisionObjectType(ECC_Pawn);
	Capsule->SetCollisionResponseToAllChannels(ECR_Block);
	SetRootComponent(Capsule);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Capsule);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Mesh asset assigned via content BP (no hardcoded engine path).

	HealthComponent = CreateDefaultSubobject<UFPSREnemyHealthComponent>(TEXT("HealthComponent"));
}
