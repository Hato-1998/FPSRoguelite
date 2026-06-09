// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRProjectileSubsystem.h"
#include "Weapon/FPSRProjectile.h"
#include "Core/FPSRLogChannels.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"

bool UFPSRProjectileSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}

	return false;
}

void UFPSRProjectileSubsystem::Deinitialize()
{
	ReleaseAllProjectiles();
	DormantPool.Empty();
	ActiveProjectiles.Empty();

	Super::Deinitialize();
}

bool UFPSRProjectileSubsystem::HasServerAuthority() const
{
	const UWorld* World = GetWorld();
	return World && (World->GetNetMode() != NM_Client);
}

AFPSRProjectile* UFPSRProjectileSubsystem::AcquireProjectile(
	TSubclassOf<AFPSRProjectile> ProjectileClass,
	const FVector& Location,
	const FVector& Direction,
	const FFPSRProjectileParams& InParams)
{
	if (!HasServerAuthority())
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// Determine the class to spawn
	UClass* ClassToSpawn = ProjectileClass ? ProjectileClass.Get() : AFPSRProjectile::StaticClass();

	// Enforce cap: release oldest active projectiles until we're under the limit
	while (ActiveProjectiles.Num() >= MaxReplicatedProjectiles)
	{
		if (ActiveProjectiles.Num() > 0)
		{
			ReleaseProjectile(ActiveProjectiles[0]);
		}
		else
		{
			break;
		}
	}

	// Try to reuse from dormant pool (skip any stale nulls). A1 keeps a single pool of the AFPSRProjectile
	// base regardless of requested subclass; per-class pooling is a future optimization (TODO).
	AFPSRProjectile* Projectile = nullptr;
	while (DormantPool.Num() > 0)
	{
		Projectile = DormantPool.Pop();
		if (Projectile)
		{
			break;
		}
	}

	// Spawn a new projectile if no dormant actor available
	if (!Projectile)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		FRotator Rotation = Direction.Rotation();
		Projectile = World->SpawnActor<AFPSRProjectile>(ClassToSpawn, Location, Rotation, SpawnParams);

		if (!Projectile)
		{
			UE_LOG(LogFPSR, Warning, TEXT("[Projectile] Failed to spawn projectile actor."));
			return nullptr;
		}
	}

	// Activate and add to active list
	Projectile->Activate(Location, InParams, Direction);
	ActiveProjectiles.Add(Projectile);

	return Projectile;
}

void UFPSRProjectileSubsystem::ReleaseProjectile(AFPSRProjectile* Projectile)
{
	if (!Projectile)
	{
		return;
	}

	// Idempotent: if the projectile wasn't in the active list it has already been released — don't deactivate
	// or re-pool it again (a re-entrant impact could otherwise double-add it to the dormant pool, handing the
	// same actor out twice on the next Acquire).
	if (ActiveProjectiles.Remove(Projectile) == 0)
	{
		return;
	}

	Projectile->Deactivate();
	DormantPool.AddUnique(Projectile);
}

void UFPSRProjectileSubsystem::ReleaseAllProjectiles()
{
	// Copy the active list so we can release while iterating
	TArray<TObjectPtr<AFPSRProjectile>> ActiveCopy = ActiveProjectiles;
	for (AFPSRProjectile* Projectile : ActiveCopy)
	{
		ReleaseProjectile(Projectile);
	}
}

// Debug console command
#if !UE_BUILD_SHIPPING
namespace
{
	FAutoConsoleCommandWithWorldAndArgs GProjSpawnCmd(
		TEXT("FPSR.SpawnProjectile"),
		TEXT("Spawn a test projectile from the local player. Usage: FPSR.SpawnProjectile"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}

			UFPSRProjectileSubsystem* Sub = World->GetSubsystem<UFPSRProjectileSubsystem>();
			if (!Sub)
			{
				UE_LOG(LogFPSR, Warning, TEXT("[Projectile] No subsystem found."));
				return;
			}

			// Find first player pawn
			FVector PlayerLocation = FVector::ZeroVector;
			FVector PlayerDirection = FVector::ForwardVector;
			bool bFoundPlayer = false;

			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				if (const APlayerController* PC = It->Get())
				{
					if (const APawn* PlayerPawn = PC->GetPawn())
					{
						PlayerLocation = PlayerPawn->GetActorLocation();
						PlayerDirection = PlayerPawn->GetActorForwardVector();
						bFoundPlayer = true;
						break;
					}
				}
			}

			if (!bFoundPlayer)
			{
				UE_LOG(LogFPSR, Warning, TEXT("[Projectile] No player pawn found."));
				return;
			}

			// Create default params
			FFPSRProjectileParams DefaultParams;
			DefaultParams.Team = EFPSRProjectileTeam::Player;
			DefaultParams.InitialSpeed = 3000.0f;
			DefaultParams.Lifetime = 5.0f;

			// Spawn projectile
			AFPSRProjectile* Proj = Sub->AcquireProjectile(
				nullptr,
				PlayerLocation + PlayerDirection * 100.0f,
				PlayerDirection,
				DefaultParams
			);

			if (Proj)
			{
				UE_LOG(LogFPSR, Log, TEXT("[Projectile] Test projectile spawned. Active count: %d"), Sub->GetActiveCount());
			}
		})
	);
}
#endif
