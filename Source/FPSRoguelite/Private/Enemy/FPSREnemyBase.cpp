// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyBase.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Core/FPSRLogChannels.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

AFPSREnemyBase::AFPSREnemyBase()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(40.0f, 90.0f);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Capsule->SetCollisionObjectType(ECC_Pawn);
	Capsule->SetCollisionResponseToAllChannels(ECR_Block);
	SetRootComponent(Capsule);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Capsule);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));
	Mesh->SetRelativeScale3D(FVector(0.8f, 0.8f, 1.8f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}

	HealthComponent = CreateDefaultSubobject<UFPSREnemyHealthComponent>(TEXT("HealthComponent"));
}

void AFPSREnemyBase::BeginPlay()
{
	Super::BeginPlay();

	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddDynamic(this, &AFPSREnemyBase::HandleDeath);
	}
}

void AFPSREnemyBase::HandleDeath(AActor* DeadActor, AActor* Killer)
{
	if (UWorld* World = GetWorld())
	{
		if (UFPSREnemySpawnSubsystem* Sub = World->GetSubsystem<UFPSREnemySpawnSubsystem>())
		{
			Sub->ReleaseEnemy(this);
			return;
		}
	}
	Destroy();
}

void AFPSREnemyBase::Activate(const FVector& Location)
{
	SetActorLocation(Location);
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	FlushNetDormancy();

	if (HealthComponent)
	{
		HealthComponent->ResetForReuse();
	}

	CurrentMoveSpeed = MoveSpeed * FMath::FRandRange(0.9f, 1.1f);
}

void AFPSREnemyBase::Deactivate()
{
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetNetDormancy(DORM_DormantAll);
}

void AFPSREnemyBase::TickServerMovement(const FVector& TargetLocation, float ScaledDeltaSeconds)
{
	if (!HasAuthority() || (HealthComponent && HealthComponent->IsDead()))
	{
		return;
	}

	FVector ToTarget = TargetLocation - GetActorLocation();
	ToTarget.Z = 0.0f;
	if (ToTarget.SizeSquared() > StopDistance * StopDistance)
	{
		const FVector Dir = ToTarget.GetSafeNormal();
		AddActorWorldOffset(Dir * CurrentMoveSpeed * ScaledDeltaSeconds, true);
		SetActorRotation(Dir.Rotation());
	}
}
