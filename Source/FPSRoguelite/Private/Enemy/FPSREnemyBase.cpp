// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyBase.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Core/FPSRLogChannels.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"
#include "HAL/IConsoleManager.h"

AFPSREnemyBase::AFPSREnemyBase()
{
	PrimaryActorTick.bCanEverTick = true;
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

void AFPSREnemyBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority() || (HealthComponent && HealthComponent->IsDead()))
	{
		return;
	}

	if (const APawn* Target = FindNearestPlayer())
	{
		FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
		ToTarget.Z = 0.0f;
		if (ToTarget.SizeSquared() > StopDistance * StopDistance)
		{
			const FVector Dir = ToTarget.GetSafeNormal();
			AddActorWorldOffset(Dir * MoveSpeed * DeltaSeconds, true);
			SetActorRotation(Dir.Rotation());
		}
	}
}

void AFPSREnemyBase::HandleDeath(AActor* DeadActor, AActor* Killer)
{
	// P2: return to pool instead of destroying.
	Destroy();
}

APawn* AFPSREnemyBase::FindNearestPlayer() const
{
	APawn* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (APawn* PlayerPawn = PC->GetPawn())
			{
				const float DistSq = FVector::DistSquared(PlayerPawn->GetActorLocation(), GetActorLocation());
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					Best = PlayerPawn;
				}
			}
		}
	}
	return Best;
}

// ---- Debug: spawn test enemies around the local player ----
static FAutoConsoleCommandWithWorldAndArgs GFPSRSpawnEnemiesCmd(
	TEXT("FPSR.SpawnEnemies"),
	TEXT("Spawn N test enemies in a ring around the local player. Usage: FPSR.SpawnEnemies [count]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}

		int32 Count = 5;
		if (Args.Num() > 0)
		{
			Count = FMath::Max(1, FCString::Atoi(*Args[0]));
		}

		APawn* Player = World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;
		const FVector Center = Player ? Player->GetActorLocation() : FVector::ZeroVector;

		for (int32 i = 0; i < Count; ++i)
		{
			const float Angle = (2.0f * PI * i) / FMath::Max(1, Count);
			const FVector Offset(FMath::Cos(Angle) * 600.0f, FMath::Sin(Angle) * 600.0f, 100.0f);
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			World->SpawnActor<AFPSREnemyBase>(AFPSREnemyBase::StaticClass(), Center + Offset, FRotator::ZeroRotator, SpawnParams);
		}
	}));
