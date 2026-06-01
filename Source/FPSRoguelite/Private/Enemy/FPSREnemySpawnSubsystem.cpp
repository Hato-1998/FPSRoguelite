// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Enemy/FPSREnemyBase.h"
#include "Enemy/FPSRFlowFieldSubsystem.h"
#include "Hero/FPSRCharacter.h"
#include "Core/FPSRLogChannels.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "CollisionQueryParams.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"

// FTickableGameObject implementation

void UFPSREnemySpawnSubsystem::Tick(float DeltaTime)
{
	TickEnemyMovement(DeltaTime);
}

TStatId UFPSREnemySpawnSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFPSREnemySpawnSubsystem, STATGROUP_Tickables);
}

ETickableTickType UFPSREnemySpawnSubsystem::GetTickableTickType() const
{
	// Never tick the CDO/template.
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional;
}

bool UFPSREnemySpawnSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->IsGameWorld();
}

UWorld* UFPSREnemySpawnSubsystem::GetTickableGameObjectWorld() const
{
	return GetWorld();
}

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

void UFPSREnemySpawnSubsystem::TickEnemyMovement(float DeltaTime)
{
	if (!HasServerAuthority())
	{
		return; // movement is server-authoritative; clients receive replicated transforms.
	}

	UWorld* World = GetWorld();
	if (!World || ActiveEnemies.Num() == 0)
	{
		return;
	}

	++MovementFrameCounter;

	// Cache alive player pawn locations and pawns once for this pass.
	TArray<APawn*, TInlineAllocator<4>> PlayerPawns;
	TArray<FVector, TInlineAllocator<4>> PlayerLocations;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (APawn* PlayerPawn = PC->GetPawn())
			{
				PlayerPawns.Add(PlayerPawn);
				PlayerLocations.Add(PlayerPawn->GetActorLocation());
			}
		}
	}
	if (PlayerPawns.Num() == 0)
	{
		return;
	}

	// Per-player attacker counters for this pass (attack token gating).
	TArray<int32, TInlineAllocator<4>> AttackersThisPass;
	AttackersThisPass.Init(0, PlayerPawns.Num());

	const float Now = World->GetTimeSeconds();

	const UFPSRFlowFieldSubsystem* FlowField = World->GetSubsystem<UFPSRFlowFieldSubsystem>();

	// Build the per-pass agent arrays + uniform-grid spatial hash (all valid active enemies) for separation.
	TArray<AFPSREnemyBase*> Agents;
	TArray<FVector> Locations;
	Agents.Reserve(ActiveEnemies.Num());
	Locations.Reserve(ActiveEnemies.Num());
	TMap<FIntPoint, TArray<int32>> SpatialHash;
	for (const TObjectPtr<AFPSREnemyBase>& EnemyPtr : ActiveEnemies)
	{
		AFPSREnemyBase* Enemy = EnemyPtr.Get();
		if (!IsValid(Enemy))
		{
			continue;
		}
		const int32 Index = Agents.Add(Enemy);
		const FVector Loc = Enemy->GetActorLocation();
		Locations.Add(Loc);

		const FIntPoint Key(FMath::FloorToInt(Loc.X / SeparationRadius), FMath::FloorToInt(Loc.Y / SeparationRadius));
		SpatialHash.FindOrAdd(Key).Add(Index);
	}

	for (int32 i = 0; i < Agents.Num(); ++i)
	{
		AFPSREnemyBase* Enemy = Agents[i];
		const FVector EnemyLocation = Locations[i];

		// Nearest player (2D).
		float BestDistSq = TNumericLimits<float>::Max();
		int32 BestPlayerIndex = 0;
		for (int32 p = 0; p < PlayerLocations.Num(); ++p)
		{
			const float DistSq = FVector::DistSquaredXY(PlayerLocations[p], EnemyLocation);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestPlayerIndex = p;
			}
		}
		const FVector BestPlayerLocation = PlayerLocations[BestPlayerIndex];

		// Distance LOD tier -> update stride + net update frequency (Game.MD §5).
		int32 UpdateStride;
		float NetFreq;
		if (BestDistSq <= TierS0RadiusSq)      { UpdateStride = 1; NetFreq = 30.0f; }
		else if (BestDistSq <= TierS1RadiusSq) { UpdateStride = 2; NetFreq = 10.0f; }
		else if (BestDistSq <= TierS2RadiusSq) { UpdateStride = 4; NetFreq = 5.0f;  }
		else                                   { UpdateStride = 8; NetFreq = 2.0f;  }

		Enemy->SetNetUpdateFrequency(NetFreq);

		// Contact attack: in range + cooldown elapsed + the target player's attack-token budget allows.
		const float AttackRange = Enemy->GetAttackRange();
		if (BestDistSq <= (AttackRange * AttackRange)
			&& Enemy->CanAttack(Now)
			&& AttackersThisPass[BestPlayerIndex] < AttackTokenLimit)
		{
			if (AFPSRCharacter* TargetChar = Cast<AFPSRCharacter>(PlayerPawns[BestPlayerIndex]))
			{
				TargetChar->ApplyContactDamage(Enemy->GetAttackDamage(), Enemy);
				Enemy->NotifyAttacked(Now);
				++AttackersThisPass[BestPlayerIndex];
			}
		}

		// Spread throttled updates across frames by the enemy's stable id.
		if (((MovementFrameCounter + static_cast<int32>(Enemy->GetUniqueID())) % UpdateStride) != 0)
		{
			continue;
		}

		// Flow-field direction toward players (fall back to direct-to-nearest if the field isn't ready).
		FVector FlowDir = FlowField ? FlowField->SampleFlowDirection(EnemyLocation) : FVector::ZeroVector;
		if (FlowDir.IsNearlyZero())
		{
			FlowDir = (BestPlayerLocation - EnemyLocation);
			FlowDir.Z = 0.0f;
			FlowDir = FlowDir.GetSafeNormal();
		}

		// Stop advancing toward the player within StopDistance (still separate to avoid stacking on them).
		const float StopDistSq = FMath::Square(Enemy->GetStopDistance());
		const FVector Desired = (BestDistSq > StopDistSq) ? FlowDir : FVector::ZeroVector;

		// Combine flow + separation; TickServerMovement normalizes and moves at CurrentMoveSpeed.
		FVector MoveDir = Desired + ComputeSeparation(i, Locations, SpatialHash) * SeparationStrength;
		MoveDir.Z = 0.0f;

		Enemy->TickServerMovement(MoveDir, DeltaTime * UpdateStride);
	}
}


FVector UFPSREnemySpawnSubsystem::ComputeSeparation(int32 AgentIndex, const TArray<FVector>& Locations, const TMap<FIntPoint, TArray<int32>>& SpatialHash) const
{
	const FVector Origin = Locations[AgentIndex];
	const int32 CX = FMath::FloorToInt(Origin.X / SeparationRadius);
	const int32 CY = FMath::FloorToInt(Origin.Y / SeparationRadius);
	const float RadiusSq = SeparationRadius * SeparationRadius;

	FVector Separation = FVector::ZeroVector;
	for (int32 dx = -1; dx <= 1; ++dx)
	{
		for (int32 dy = -1; dy <= 1; ++dy)
		{
			const FIntPoint Key(CX + dx, CY + dy);
			if (const TArray<int32>* Cell = SpatialHash.Find(Key))
			{
				for (int32 OtherIndex : *Cell)
				{
					if (OtherIndex == AgentIndex)
					{
						continue;
					}
					FVector Diff = Origin - Locations[OtherIndex];
					Diff.Z = 0.0f;
					const float DistSq = Diff.SizeSquared();
					if (DistSq > KINDA_SMALL_NUMBER && DistSq < RadiusSq)
					{
						const float Dist = FMath::Sqrt(DistSq);
						Separation += (Diff / Dist) * (1.0f - Dist / SeparationRadius); // stronger when closer
					}
				}
			}
		}
	}
	return Separation;
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

FVector UFPSREnemySpawnSubsystem::SnapToGround(const FVector& Location) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return Location;
	}

	const FVector TraceStart(Location.X, Location.Y, Location.Z + SpawnGroundTraceUp);
	const FVector TraceEnd(Location.X, Location.Y, Location.Z - SpawnGroundTraceDown);

	// Trace ONLY against static world geometry so other enemy capsules (ECC_Pawn) are never mistaken for floor.
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSREnemySpawnGround), false);

	FHitResult Hit;
	if (World->LineTraceSingleByObjectType(Hit, TraceStart, TraceEnd, ObjectParams, QueryParams))
	{
		return FVector(Location.X, Location.Y, Hit.ImpactPoint.Z + SpawnGroundHalfHeight);
	}
	return Location; // no floor found (e.g. off-map): keep the original candidate
}

AFPSREnemyBase* UFPSREnemySpawnSubsystem::AcquireEnemy(const FVector& Location)
{
	UWorld* World = GetWorld();
	if (!World || !HasServerAuthority())
	{
		return nullptr;
	}

	const FVector SpawnLocation = SnapToGround(Location);

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
		Enemy = World->SpawnActor<AFPSREnemyBase>(AFPSREnemyBase::StaticClass(), SpawnLocation, FRotator::ZeroRotator, SpawnParams);
		if (Enemy == nullptr)
		{
			return nullptr;
		}
		++TotalSpawned;
	}

	// Activate and add to active set.
	Enemy->Activate(SpawnLocation);
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

// ---- Console Commands (debug; excluded from shipping) ----

#if !UE_BUILD_SHIPPING
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
#endif // !UE_BUILD_SHIPPING
