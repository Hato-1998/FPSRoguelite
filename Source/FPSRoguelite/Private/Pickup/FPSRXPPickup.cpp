// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pickup/FPSRXPPickup.h"
#include "Core/FPSRGameState.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"

AFPSRXPPickup::AFPSRXPPickup()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetRelativeScale3D(FVector(0.3f, 0.3f, 0.3f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Mesh->SetStaticMesh(SphereMesh.Object);
	}
	SetRootComponent(Mesh);
}

void AFPSRXPPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		return; // collection/magnetism is server-authoritative; clients receive the replicated transform.
	}

	APawn* NearestPlayer = FindNearestPlayer();
	if (NearestPlayer == nullptr)
	{
		return;
	}

	const FVector PickupLocation = GetActorLocation();
	const FVector PlayerLocation = NearestPlayer->GetActorLocation();
	const float DistSq = FVector::DistSquaredXY(PlayerLocation, PickupLocation);

	if (DistSq <= (CollectRadius * CollectRadius))
	{
		if (UWorld* World = GetWorld())
		{
			if (AFPSRGameState* GameState = World->GetGameState<AFPSRGameState>())
			{
				GameState->AddSharedXP(XPValue);
			}
		}
		Destroy();
		return;
	}

	if (DistSq <= (MagnetRadius * MagnetRadius))
	{
		const FVector ToPlayer = (PlayerLocation - PickupLocation).GetSafeNormal();
		AddActorWorldOffset(ToPlayer * MagnetSpeed * DeltaSeconds, true);
	}
}

APawn* AFPSRXPPickup::FindNearestPlayer() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	const FVector PickupLocation = GetActorLocation();
	APawn* BestPawn = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APlayerController* PC = It->Get())
		{
			if (APawn* PlayerPawn = PC->GetPawn())
			{
				const float DistSq = FVector::DistSquaredXY(PlayerPawn->GetActorLocation(), PickupLocation);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					BestPawn = PlayerPawn;
				}
			}
		}
	}

	return BestPawn;
}
