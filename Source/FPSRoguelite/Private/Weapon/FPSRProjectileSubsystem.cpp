// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRProjectileSubsystem.h"
#include "Weapon/FPSRProjectile.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRLogChannels.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"

// FTickableGameObject — drives the global-freeze suspension of active projectiles (server-authoritative).

void UFPSRProjectileSubsystem::Tick(float DeltaTime)
{
	if (!HasServerAuthority())
	{
		return; // freeze is applied on the server; clients follow via replicated movement.
	}

	const UWorld* World = GetWorld();
	const AFPSRGameState* GameState = World ? World->GetGameState<AFPSRGameState>() : nullptr;
	const bool bPaused = GameState && GameState->IsRunPaused();

	// Only act on the pause/resume transition (Game.MD §2-2): suspend/resume every active projectile once.
	if (bPaused == bProjectilesPaused)
	{
		return;
	}
	bProjectilesPaused = bPaused;

	for (const TObjectPtr<AFPSRProjectile>& ProjectilePtr : ActiveProjectiles)
	{
		AFPSRProjectile* Projectile = ProjectilePtr.Get();
		if (IsValid(Projectile))
		{
			Projectile->SetSimulationPaused(bPaused);
		}
	}
}

TStatId UFPSRProjectileSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFPSRProjectileSubsystem, STATGROUP_Tickables);
}

ETickableTickType UFPSRProjectileSubsystem::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional;
}

bool UFPSRProjectileSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->IsGameWorld();
}

UWorld* UFPSRProjectileSubsystem::GetTickableGameObjectWorld() const
{
	return GetWorld();
}

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

	// Drop stale entries first — a projectile destroyed outside the pool leaves an invalid entry (a pending-kill
	// actor stays non-null until GC, so a plain null check misses it). IsValid catches both null and
	// pending-kill. Without this, the cap loop below could spin on an unremovable entry or pool a dead actor.
	ActiveProjectiles.RemoveAll([](const TObjectPtr<AFPSRProjectile>& P) { return !IsValid(P); });

	// Enforce cap: release the oldest active projectiles (FIFO) until we're under the limit.
	while (ActiveProjectiles.Num() >= MaxReplicatedProjectiles && ActiveProjectiles.Num() > 0)
	{
		ReleaseProjectile(ActiveProjectiles[0]);
	}

	// Reuse a dormant projectile of the SAME class as requested (so a later request never gets a different
	// subclass's mesh/components/overridden behavior). Drop any stale nulls encountered. Pool is small (<=64),
	// so the linear scan is cheap. Order is irrelevant, so RemoveAtSwap.
	AFPSRProjectile* Projectile = nullptr;
	for (int32 i = DormantPool.Num() - 1; i >= 0; --i)
	{
		AFPSRProjectile* Candidate = DormantPool[i];
		if (!IsValid(Candidate)) // drop null or pending-kill (destroyed) entries
		{
			DormantPool.RemoveAtSwap(i);
			continue;
		}
		if (Candidate->GetClass() == ClassToSpawn)
		{
			Projectile = Candidate;
			DormantPool.RemoveAtSwap(i);
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

	// Track BEFORE activating: enabling collision inside Activate can fire a synchronous point-blank overlap
	// that detonates and calls ReleaseToPool -> ReleaseProjectile(this). Adding first keeps that release
	// consistent (it finds and removes the entry) rather than orphaning a still-active projectile.
	ActiveProjectiles.Add(Projectile);
	Projectile->Activate(Location, InParams, Direction);

	// If acquired while the run is already frozen, suspend immediately (the Tick transition already fired, so it
	// wouldn't otherwise be paused until the next freeze cycle). No-op if Activate already released it.
	if (bProjectilesPaused)
	{
		Projectile->SetSimulationPaused(true);
	}

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

	// Only pool a still-valid actor — a pending-kill projectile (destroyed outside the pool) is removed from the
	// active list but must not be deactivated/reused.
	if (IsValid(Projectile))
	{
		Projectile->Deactivate();
		DormantPool.AddUnique(Projectile);
	}
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
