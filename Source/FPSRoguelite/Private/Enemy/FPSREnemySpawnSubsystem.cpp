// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Enemy/FPSREnemyBase.h"
#include "Enemy/FPSREnemySpawnPoint.h"
#include "Enemy/FPSRFlowFieldSubsystem.h"
#include "Hero/FPSRCharacter.h"
#include "Core/FPSRLogChannels.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"
#include "Run/FPSRRunDirectorSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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
		CacheSpawnPoints();

		InWorld.GetTimerManager().SetTimer(
			DirectorTimerHandle,
			this,
			&UFPSREnemySpawnSubsystem::TickDirector,
			SpawnInterval,
			true
		);
	}
}

void UFPSREnemySpawnSubsystem::SetSpawnInterval(float InSeconds)
{
	SpawnInterval = FMath::Max(0.02f, InSeconds);

	// The director timer is armed once at OnWorldBeginPlay (before the director pushes the schedule), so re-arm it
	// here with the new interval so a schedule change takes effect immediately. Server-only, mirroring the timer setup.
	if (HasServerAuthority())
	{
		if (UWorld* World = GetWorld())
		{
			if (World->GetTimerManager().IsTimerActive(DirectorTimerHandle))
			{
				World->GetTimerManager().SetTimer(
					DirectorTimerHandle,
					this,
					&UFPSREnemySpawnSubsystem::TickDirector,
					SpawnInterval,
					true
				);
			}
		}
	}
}

void UFPSREnemySpawnSubsystem::CacheSpawnPoints()
{
	SpawnPoints.Reset();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AFPSREnemySpawnPoint> It(World); It; ++It)
	{
		if (AFPSREnemySpawnPoint* Point = *It)
		{
			SpawnPoints.Add(Point);
		}
	}

	UE_LOG(LogFPSR, Log, TEXT("[Spawn] Cached %d enemy spawn point(s)."), SpawnPoints.Num());
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
				// Skip dead players (U2): they're not targeted and take no contact damage (no corpse hits). U9 (DBNO)
				// revisits whether downed players remain targetable.
				if (const AFPSRPlayerState* PS = PC->GetPlayerState<AFPSRPlayerState>())
				{
					if (PS->IsDead())
					{
						continue;
					}
				}
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

	const AFPSRGameState* GameState = World->GetGameState<AFPSRGameState>();
	// Global freeze (card selection): enemies are frozen in place — skip the whole movement+attack pass.
	// (Enemies move/attack during both Combat and Boss phases.)
	if (GameState && GameState->IsRunPaused())
	{
		return;
	}

	// Time-scaled contact damage, computed ONCE per pass (one global scalar — keeps the swarm a batch, no per-enemy
	// state): 25 for the first minute, linear ramp 25->50 by BossTime, 50 thereafter (incl. the boss phase, where the
	// run clock is pinned at BossTime). Overrides the per-enemy AttackDamage for the player-contact path.
	float ContactDamage = 25.0f;
	{
		const float RunClock = GameState ? GameState->GetRunClockSeconds() : 0.0f;
		float BossTime = 300.0f;
		if (const UFPSRRunDirectorSubsystem* Director = World->GetSubsystem<UFPSRRunDirectorSubsystem>())
		{
			BossTime = Director->GetBossTime();
		}
		constexpr float RampStart = 60.0f;
		const float RampEnd = FMath::Max(BossTime, RampStart + 1.0f);
		const float Alpha = FMath::Clamp((RunClock - RampStart) / (RampEnd - RampStart), 0.0f, 1.0f);
		ContactDamage = FMath::Lerp(25.0f, 50.0f, Alpha);
	}

	const UFPSRFlowFieldSubsystem* FlowField = World->GetSubsystem<UFPSRFlowFieldSubsystem>();

	// Build the per-pass agent arrays + uniform-grid spatial hash (all valid active enemies) for separation.
	// Reuse the member scratch (Reset keeps capacity) so the 500-enemy batch doesn't realloc every frame (W1 P2-4).
	TArray<AFPSREnemyBase*>& Agents = MovementAgentsScratch;
	TArray<FVector>& Locations = MovementLocationsScratch;
	TMap<FIntPoint, TArray<int32>>& SpatialHash = MovementSpatialHashScratch;
	Agents.Reset();
	Locations.Reset();
	SpatialHash.Reset();
	Agents.Reserve(ActiveEnemies.Num());
	Locations.Reserve(ActiveEnemies.Num());
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

		// Contact attack: in horizontal range + within a vertical gap (no through-floor hits) + cooldown elapsed
		// + the target player's attack-token budget allows. The XY nearest test ignores Z, so gate Z explicitly.
		const float AttackRange = Enemy->GetAttackRange();
		const float AttackVertGap = FMath::Abs(EnemyLocation.Z - BestPlayerLocation.Z);
		if (BestDistSq <= (AttackRange * AttackRange)
			&& AttackVertGap <= AttackVerticalRange
			&& Enemy->CanAttack(Now)
			&& AttackersThisPass[BestPlayerIndex] < AttackTokenLimit)
		{
			if (AFPSRCharacter* TargetChar = Cast<AFPSRCharacter>(PlayerPawns[BestPlayerIndex]))
			{
				TargetChar->ApplyContactDamage(ContactDamage, Enemy);
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

		// Recycle an enemy that has fallen out of the playable world (walked into a pit / no static floor under
		// it) so the endless-fall path can't pin a director slot forever. Safe here: Agents/Locations/SpatialHash
		// are snapshots; ReleaseEnemy only mutates ActiveEnemies/DormantPool and this enemy isn't touched again
		// this pass.
		if (Enemy->GetActorLocation().Z < WorldKillZ)
		{
			ReleaseEnemy(Enemy);
		}
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
					if (DistSq >= RadiusSq)
					{
						continue; // outside the separation radius
					}
					if (DistSq > KINDA_SMALL_NUMBER)
					{
						const float Dist = FMath::Sqrt(DistSq);
						Separation += (Diff / Dist) * (1.0f - Dist / SeparationRadius); // stronger when closer
					}
					else
					{
						// Exactly co-located (e.g. two enemies spawned on the same designer point in one tick): a
						// zero vector has no direction, so push this agent along a deterministic golden-angle heading
						// unique to its index. Co-located agents fan out instead of staying stuck — fixes the stacking
						// at its source so spawn locations never have to be jittered into unsafe geometry (Codex 2026-06-09).
						const float Heading = static_cast<float>(AgentIndex) * 2.39996323f; // golden angle (radians)
						Separation += FVector(FMath::Cos(Heading), FMath::Sin(Heading), 0.0f);
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

	const AFPSRGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	if (GameState && (GameState->IsRunPaused()
		|| (!GameState->IsCombatPhase() && GameState->GetRunPhase() != ERunPhase::Boss)))
	{
		// Spawn during Combat AND Boss (the swarm persists + keeps ramping through the boss fight); never while
		// frozen for card selection, and not in pre-combat/menu phases (Game.MD §2-2).
		return;
	}

	int32 SpawnedThisTick = 0;
	while (ActiveEnemies.Num() < TargetAliveCount && ActiveEnemies.Num() < MaxActiveEnemies && SpawnedThisTick < MaxSpawnPerTick)
	{
		FVector SpawnAt;
		bool bSnapToGround = true;
		// Spawn ONLY at designer spawn points: when none qualifies this tick (e.g. all in view), stop and retry on the
		// next director tick rather than falling back to a player-proximity ring (removed 2026-06-24).
		if (!ComputeSpawnLocation(SpawnAt, bSnapToGround))
		{
			break;
		}
		if (AcquireEnemy(SpawnAt, bSnapToGround) == nullptr)
		{
			break;
		}
		++SpawnedThisTick;
	}
}

bool UFPSREnemySpawnSubsystem::ComputeSpawnLocation(FVector& OutLocation, bool& bOutSnapToGround) const
{
	// The swarm spawns ONLY at designer-placed spawn points that are out of every player's view (Game.MD §2-8, §1
	// fixed map). The player-proximity/ring fallback was removed (user 2026-06-24): when no point qualifies this tick
	// (none placed, or all currently in view / too close), return false so the director skips spawning and retries
	// next tick. The designer point is authoritative — keep its exact Z (no ground re-snap onto a ceiling/roof for
	// indoor placements, Codex review 2026-06-09).
	if (TrySelectSpawnPoint(OutLocation))
	{
		bOutSnapToGround = false;
		return true;
	}
	return false;
}

bool UFPSREnemySpawnSubsystem::TrySelectSpawnPoint(FVector& OutLocation) const
{
	const UWorld* World = GetWorld();
	if (!World || SpawnPoints.Num() == 0)
	{
		return false;
	}

	// Gather each local/remote player's view (camera) + pawn location once. Spawn points are filtered against
	// ALL players so an enemy never pops into anyone's view.
	struct FPlayerView { FVector CamLocation; FVector CamForward2D; };
	TArray<FPlayerView, TInlineAllocator<4>> PlayerViews;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (PC == nullptr || PC->GetPawn() == nullptr)
		{
			continue;
		}
		FVector CamLocation;
		FRotator CamRotation;
		PC->GetPlayerViewPoint(CamLocation, CamRotation);
		FVector Forward2D = CamRotation.Vector();
		Forward2D.Z = 0.0f;
		if (!Forward2D.Normalize())
		{
			continue;
		}
		PlayerViews.Add({ CamLocation, Forward2D });
	}

	if (PlayerViews.Num() == 0)
	{
		return false; // no players to gate against — let the caller fall back to the ring pattern
	}

	const float CosHalfVis = FMath::Cos(FMath::DegreesToRadians(SpawnPointVisibilityHalfAngleDeg));
	const bool bZoneFilter = ActiveSpawnZone.IsValid();

	// Build the eligible candidate set with cumulative weights for a single weighted-random draw.
	TArray<const AFPSREnemySpawnPoint*, TInlineAllocator<32>> Candidates;
	TArray<float, TInlineAllocator<32>> CumulativeWeights;
	float WeightTotal = 0.0f;

	for (const TObjectPtr<AFPSREnemySpawnPoint>& PointPtr : SpawnPoints)
	{
		const AFPSREnemySpawnPoint* Point = PointPtr;
		if (Point == nullptr || !Point->IsEnabled() || Point->GetWeight() <= 0.0f)
		{
			continue;
		}
		if (bZoneFilter && !Point->GetZoneTag().MatchesTag(ActiveSpawnZone))
		{
			continue;
		}

		const FVector PointLocation = Point->GetActorLocation();

		// Nearest-player distance (XY) for the MinPlayerDistance gate + distance weighting.
		float NearestDistSq = TNumericLimits<float>::Max();
		bool bVisibleToAnyPlayer = false;
		for (const FPlayerView& View : PlayerViews)
		{
			FVector ToPoint = PointLocation - View.CamLocation;
			ToPoint.Z = 0.0f;
			const float DistSq = ToPoint.SizeSquared();
			NearestDistSq = FMath::Min(NearestDistSq, DistSq);

			if (DistSq > KINDA_SMALL_NUMBER)
			{
				const FVector Dir = ToPoint * FMath::InvSqrt(DistSq);
				if (FVector::DotProduct(View.CamForward2D, Dir) > CosHalfVis)
				{
					bVisibleToAnyPlayer = true;
					break; // inside this player's view cone — exclude the point
				}
			}
			else
			{
				bVisibleToAnyPlayer = true; // a player is standing on the point
				break;
			}
		}

		if (bVisibleToAnyPlayer)
		{
			continue;
		}

		const float NearestDist = FMath::Sqrt(NearestDistSq);
		if (Point->GetMinPlayerDistance() > 0.0f && NearestDist < Point->GetMinPlayerDistance())
		{
			continue;
		}

		// Favor points nearer the fight without hard-excluding far ones.
		const float DistanceWeight = 1.0f / (1.0f + NearestDist / SpawnPointFalloffDistance);
		const float EffectiveWeight = Point->GetWeight() * DistanceWeight;
		if (EffectiveWeight <= 0.0f)
		{
			continue;
		}

		WeightTotal += EffectiveWeight;
		Candidates.Add(Point);
		CumulativeWeights.Add(WeightTotal);
	}

	if (Candidates.Num() == 0 || WeightTotal <= 0.0f)
	{
		return false;
	}

	// Weighted-random draw: first cumulative weight >= roll. The exact designer anchor is used (no jitter): it is
	// the validated, authoritative spawn transform (§1 fixed map). If the same point is picked more than once in a
	// tick, the co-located enemies are pushed apart at the source by ComputeSeparation's coincident handling rather
	// than by moving the spawn into possibly-unsafe wall/ledge geometry.
	const float Roll = FMath::FRandRange(0.0f, WeightTotal);
	for (int32 i = 0; i < Candidates.Num(); ++i)
	{
		if (Roll <= CumulativeWeights[i])
		{
			OutLocation = Candidates[i]->GetActorLocation();
			return true;
		}
	}

	OutLocation = Candidates.Last()->GetActorLocation(); // float-rounding guard
	return true;
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

AFPSREnemyBase* UFPSREnemySpawnSubsystem::AcquireEnemy(const FVector& Location, bool bSnapToGround)
{
	UWorld* World = GetWorld();
	if (!World || !HasServerAuthority())
	{
		return nullptr;
	}

	const FVector SpawnLocation = bSnapToGround ? SnapToGround(Location) : Location;

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

		// Spawn a new enemy of the configured class (designer BP child), falling back to the C++ base.
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		UClass* ClassToSpawn = EnemyClass ? EnemyClass.Get() : AFPSREnemyBase::StaticClass();
		Enemy = World->SpawnActor<AFPSREnemyBase>(ClassToSpawn, SpawnLocation, FRotator::ZeroRotator, SpawnParams);
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

void UFPSREnemySpawnSubsystem::ReleaseAllEnemies()
{
	if (!HasServerAuthority())
	{
		return;
	}

	// Copy out first: ReleaseEnemy mutates ActiveEnemies, so we can't iterate it directly.
	TArray<AFPSREnemyBase*> ToRelease;
	ToRelease.Reserve(ActiveEnemies.Num());
	for (const TObjectPtr<AFPSREnemyBase>& EnemyPtr : ActiveEnemies)
	{
		if (AFPSREnemyBase* Enemy = EnemyPtr.Get())
		{
			ToRelease.Add(Enemy);
		}
	}
	for (AFPSREnemyBase* Enemy : ToRelease)
	{
		ReleaseEnemy(Enemy);
	}
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
