// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pickup/FPSRXPPickup.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"

#include "AbilitySystem/Attributes/FPSRCombatSet.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
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

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const FVector PickupLocation = GetActorLocation();
	const float CollectRadiusSq = CollectRadius * CollectRadius;

	// Single pass over players. Collection target = nearest player within the (unscaled) CollectRadius.
	// Magnet target = the player with the strongest claim, ranked by distance relative to their OWN
	// effective radius (DistSq / EffRadiusSq). This lets a far player with an upgraded PickupRadius out-pull
	// a closer default-radius player, so the attribute stays meaningful in co-op (Codex P2).
	APawn* CollectPlayer = nullptr;
	float CollectBestDistSq = TNumericLimits<float>::Max();
	APawn* MagnetPlayer = nullptr;
	FVector MagnetPlayerLocation = FVector::ZeroVector;
	float MagnetBestRatio = TNumericLimits<float>::Max();

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
		if (PlayerPawn == nullptr)
		{
			continue;
		}

		// Dead players don't collect or magnet XP (U2).
		if (const AFPSRPlayerState* PS = PC->GetPlayerState<AFPSRPlayerState>())
		{
			if (PS->IsDead())
			{
				continue;
			}
		}

		const FVector PlayerLocation = PlayerPawn->GetActorLocation();
		const float DistSq = FVector::DistSquaredXY(PlayerLocation, PickupLocation);

		if (DistSq <= CollectRadiusSq && DistSq < CollectBestDistSq)
		{
			CollectBestDistSq = DistSq;
			CollectPlayer = PlayerPawn;
		}

		const float EffMagnetRadius = MagnetRadius * GetCollectorPickupRadiusMult(PlayerPawn);
		const float EffMagnetRadiusSq = EffMagnetRadius * EffMagnetRadius;
		if (EffMagnetRadiusSq > KINDA_SMALL_NUMBER && DistSq <= EffMagnetRadiusSq)
		{
			const float Ratio = DistSq / EffMagnetRadiusSq; // 0 = on the player, 1 = at the radius edge
			if (Ratio < MagnetBestRatio)
			{
				MagnetBestRatio = Ratio;
				MagnetPlayer = PlayerPawn;
				MagnetPlayerLocation = PlayerLocation;
			}
		}
	}

	if (CollectPlayer != nullptr)
	{
		if (AFPSRGameState* GameState = World->GetGameState<AFPSRGameState>())
		{
			const float XPMult = GetCollectorXPGainMult(CollectPlayer);
			const int32 EffectiveXP = FMath::Max(1, FMath::RoundToInt(XPValue * XPMult));
			GameState->AddSharedXP(EffectiveXP);
		}
		Destroy();
		return;
	}

	if (MagnetPlayer != nullptr)
	{
		const FVector ToPlayer = (MagnetPlayerLocation - PickupLocation).GetSafeNormal();
		AddActorWorldOffset(ToPlayer * MagnetSpeed * DeltaSeconds, true);
	}
}

float AFPSRXPPickup::GetCollectorPickupRadiusMult(class APawn* Pawn) const
{
	if (Pawn == nullptr)
	{
		return 1.0f;
	}

	IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Pawn);
	UAbilitySystemComponent* ASC = ASI ? ASI->GetAbilitySystemComponent() : nullptr;
	if (ASC == nullptr)
	{
		return 1.0f;
	}

	const UFPSRCombatSet* CombatSet = ASC->GetSet<UFPSRCombatSet>();
	if (CombatSet == nullptr)
	{
		return 1.0f;
	}

	return FMath::Max(0.01f, CombatSet->GetPickupRadius());
}

float AFPSRXPPickup::GetCollectorXPGainMult(class APawn* Pawn) const
{
	if (Pawn == nullptr)
	{
		return 1.0f;
	}

	IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Pawn);
	UAbilitySystemComponent* ASC = ASI ? ASI->GetAbilitySystemComponent() : nullptr;
	if (ASC == nullptr)
	{
		return 1.0f;
	}

	const UFPSRCombatSet* CombatSet = ASC->GetSet<UFPSRCombatSet>();
	if (CombatSet == nullptr)
	{
		return 1.0f;
	}

	return FMath::Max(0.0f, CombatSet->GetXPGain());
}
