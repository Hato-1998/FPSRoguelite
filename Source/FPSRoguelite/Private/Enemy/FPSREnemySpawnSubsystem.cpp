// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Enemy/FPSREnemyBase.h"
#include "Core/FPSRLogChannels.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"

bool UFPSREnemySpawnSubsystem::ShouldCreateSubsystem(UObject* Outer) const
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

void UFPSREnemySpawnSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (HasServerAuthority())
	{
		InWorld.GetTimerManager().SetTimer(
			DirectorTimerHandle,
			this,
			&UFPSREnemySpawnSubsystem::TickDirector,
			SpawnInterval,
			true
		);
	}
}

void UFPSREnemySpawnSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DirectorTimerHandle);
	}
	Super::Deinitialize();
}

bool UFPSREnemySpawnSubsystem::HasServerAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

void UFPSREnemySpawnSubsystem::SetTargetAliveCount(int32 InTarget)
{
	TargetAliveCount = FMath::Clamp(InTarget, 0, MaxActiveEnemies);
}

void UFPSREnemySpawnSubsystem::TickDirector()
{
	if (!HasServerAuthority())
	{
		return;
	}

	int32 SpawnedThisTick = 0;
	while (ActiveEnemies.Num() < TargetAliveCount && ActiveEnemies.Num() < MaxActiveEnemies && SpawnedThisTick < MaxSpawnPerTick)
	{
		if (AcquireEnemy(ComputeSpawnLocation()) == nullptr)
		{
			break;
		}
		++SpawnedThisTick;
	}
}

FVector UFPSREnemySpawnSubsystem::ComputeSpawnLocation() const
{
	FVector Center = FVector::ZeroVector;

	// Find first player pawn location as center.
	if (UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (const APlayerController* PC = It->Get())
			{
				if (const APawn* PlayerPawn = PC->GetPawn())
				{
					Center = PlayerPawn->GetActorLocation();
					break;
				}
			}
		}
	}

	// Random angle and radius in ring pattern.
	const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
	const float Radius = FMath::FRandRange(SpawnRadiusInner, SpawnRadiusOuter);

	return Center + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 100.0f);
}

AFPSREnemyBase* UFPSREnemySpawnSubsystem::AcquireEnemy(const FVector& Location)
{
	UWorld* World = GetWorld();
	if (!World || !HasServerAuthority())
	{
		return nullptr;
	}

	AFPSREnemyBase* Enemy = nullptr;

	// Reuse a dormant actor (skip any stale nulls).
	while (DormantPool.Num() > 0 && Enemy == nullptr)
	{
		Enemy = DormantPool.Pop();
	}

	if (Enemy == nullptr)
	{
		// Hard cap reached.
		if (TotalSpawned >= MaxActiveEnemies)
		{
			return nullptr;
		}

		// Spawn a new enemy.
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Enemy = World->SpawnActor<AFPSREnemyBase>(AFPSREnemyBase::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
		if (Enemy == nullptr)
		{
			return nullptr;
		}
		++TotalSpawned;
	}

	// Activate and add to active set.
	Enemy->Activate(Location);
	ActiveEnemies.Add(Enemy);
	return Enemy;
}

void UFPSREnemySpawnSubsystem::ReleaseEnemy(AFPSREnemyBase* Enemy)
{
	if (Enemy == nullptr)
	{
		return;
	}

	ActiveEnemies.Remove(Enemy);
	Enemy->Deactivate();
	DormantPool.Add(Enemy);
}

// ---- Console Commands ----

static FAutoConsoleCommandWithWorldAndArgs GFPSRSpawnEnemiesCmd(
	TEXT("FPSR.SpawnEnemies"),
	TEXT("Burst-spawn N test enemies via the pool around the local player. Usage: FPSR.SpawnEnemies [count]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}

		UFPSREnemySpawnSubsystem* Sub = World->GetSubsystem<UFPSREnemySpawnSubsystem>();
		if (!Sub)
		{
			return;
		}

		int32 Count = 5;
		if (Args.Num() > 0)
		{
			Count = FMath::Max(1, FCString::Atoi(*Args[0]));
		}

		// Find first player pawn as center.
		FVector Center = FVector::ZeroVector;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (const APlayerController* PC = It->Get())
			{
				if (const APawn* PlayerPawn = PC->GetPawn())
				{
					Center = PlayerPawn->GetActorLocation();
					break;
				}
			}
		}

		// Spawn in ring pattern.
		for (int32 i = 0; i < Count; ++i)
		{
			const float Angle = (2.0f * PI * i) / FMath::Max(1, Count);
			const FVector Offset(FMath::Cos(Angle) * 600.0f, FMath::Sin(Angle) * 600.0f, 100.0f);
			Sub->AcquireEnemy(Center + Offset);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSREnemyTargetCmd(
	TEXT("FPSR.EnemyTarget"),
	TEXT("Set the spawn director target alive count (0 = stop spawning). Usage: FPSR.EnemyTarget [count]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}

		UFPSREnemySpawnSubsystem* Sub = World->GetSubsystem<UFPSREnemySpawnSubsystem>();
		if (!Sub)
		{
			return;
		}

		int32 Target = 0;
		if (Args.Num() > 0)
		{
			Target = FMath::Max(0, FCString::Atoi(*Args[0]));
		}

		Sub->SetTargetAliveCount(Target);
	}));
