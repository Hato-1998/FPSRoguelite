// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Enemy/FPSREnemyBase.h"
#include "Enemy/FPSREnemySpawnPoint.h"
#include "Enemy/FPSRSpawnRoom.h"
#include "Enemy/FPSRFlowFieldSubsystem.h"
#include "Enemy/FPSRFlowFieldComputer.h" // EFPSRFieldQuery (front-chase distance status, U P-D)
#include "Enemy/FPSREnemyAllocator.h"
#include "Enemy/FPSREnemyRosterDataAsset.h"
#include "Hero/FPSRCharacter.h"
#include "Core/FPSRLogChannels.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRPlayerController.h"
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
		CacheSpawnRooms();
		ResetSpawnZones(); // start with only bActiveAtStart rooms live (fresh world)

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

void UFPSREnemySpawnSubsystem::CacheSpawnRooms()
{
	SpawnRooms.Reset();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AFPSRSpawnRoom> It(World); It; ++It)
	{
		if (AFPSRSpawnRoom* Room = *It)
		{
			SpawnRooms.Add(Room);
		}
	}

	UE_LOG(LogFPSR, Log, TEXT("[Spawn] Cached %d spawn room(s)."), SpawnRooms.Num());
}

void UFPSREnemySpawnSubsystem::ActivateSpawnZone(FGameplayTag Zone)
{
	if (!HasServerAuthority() || !Zone.IsValid())
	{
		return;
	}

	// Accumulate: a room, once entered, stays a live spawn region for the rest of the run.
	if (!ActiveSpawnZones.HasTagExact(Zone))
	{
		ActiveSpawnZones.AddTag(Zone);
		UE_LOG(LogFPSR, Log, TEXT("[Spawn] Activated spawn zone %s (%d active)."), *Zone.ToString(), ActiveSpawnZones.Num());
	}
}

void UFPSREnemySpawnSubsystem::DeactivateSpawnZone(FGameplayTag Zone)
{
	if (!HasServerAuthority() || !Zone.IsValid())
	{
		return;
	}

	// Symmetric inverse of ActivateSpawnZone: remove the exact zone tag so its tagged points stop spawning. Rooms use
	// flat unique tags (SpawnZone.Room.*), so exact removal is correct (the eligibility gate uses HasTag, but zones
	// don't nest in practice). Already-spawned enemies are untouched — zones gate spawn LOCATIONS, not live actors.
	if (ActiveSpawnZones.HasTagExact(Zone))
	{
		ActiveSpawnZones.RemoveTag(Zone);
		UE_LOG(LogFPSR, Log, TEXT("[Spawn] Deactivated spawn zone %s (%d active)."), *Zone.ToString(), ActiveSpawnZones.Num());
	}
}

void UFPSREnemySpawnSubsystem::ResetSpawnZones()
{
	if (!HasServerAuthority())
	{
		return;
	}

	// Clear accumulated zones, then re-arm the start room(s) so a re-run begins from only the start region.
	ActiveSpawnZones.Reset();
	for (const TObjectPtr<AFPSRSpawnRoom>& RoomPtr : SpawnRooms)
	{
		const AFPSRSpawnRoom* Room = RoomPtr;
		if (Room && Room->GetTriggerMode() == ESpawnRoomTriggerMode::Activate
			&& Room->IsActiveAtStart() && Room->GetRoomTag().IsValid())
		{
			ActiveSpawnZones.AddTag(Room->GetRoomTag());
		}
	}

	UE_LOG(LogFPSR, Log, TEXT("[Spawn] Reset spawn zones — %d start zone(s) active."), ActiveSpawnZones.Num());
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

// --- P-E pure helpers (unit-testable; no world). Single source of truth: the director calls these, and
//     FPSRoguelite.Allocator regressions them headless (the exact formulas the Codex/Opus P-E gate hardened). ---

int32 UFPSREnemySpawnSubsystem::ComputeFrontReserved(int32 FrontActiveSlots)
{
	if (FrontActiveSlots <= 0)
	{
		return 0;
	}
	return FMath::Min(FrontBudgetCeiling, PerFrontSlotBudget * FrontActiveSlots);
}

int32 UFPSREnemySpawnSubsystem::ComputePhysicalSteady(int32 TargetAliveCount, int32 FrontReserved)
{
	return FMath::Max(0, FMath::Min(TargetAliveCount, GlobalAliveCap - SeedReserve - FrontReserved));
}

bool UFPSREnemySpawnSubsystem::IsRearStatus(EFPSRFieldQuery Status, int32 Dist)
{
	if (Status == EFPSRFieldQuery::OK)
	{
		return Dist > ChaseExitCells; // a genuinely far OK reading (past the front-chase hysteresis band) is rear
	}
	return Status == EFPSRFieldQuery::Unreachable; // fully disconnected from every source; SourceLess/OffGrid/NoGrid = HOLD
}

float UFPSREnemySpawnSubsystem::ClampDrainDt(float RawElapsed, float SpawnIntervalSeconds)
{
	return FMath::Clamp(RawElapsed, 0.0f, SpawnIntervalSeconds * DrainDtClampTicks);
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

	const float Now = World->GetTimeSeconds();

	// U P-D/P-F: the MULTI-SLOT unified field drives front-chase targeting AND the topology late-join ack gate — both are
	// active ONLY for a real multimap grid (P-G: a single-map degenerate grid keeps the exact same-map behavior, no regression).
	const UFPSRFlowFieldSubsystem* FlowField = World->GetSubsystem<UFPSRFlowFieldSubsystem>();
	const bool bUnified = FlowField && FlowField->GetMultiSlotUnifiedComputer() != nullptr; // P-G: multimap only (single-map degenerate grid = false)

	// Cache alive player pawn locations, pawns, and committed MapIds once for this pass.
	TArray<APawn*, TInlineAllocator<4>> PlayerPawns;
	TArray<FVector, TInlineAllocator<4>> PlayerLocations;
	TArray<FGameplayTag, TInlineAllocator<4>> PlayerMapIds; // multimap Tier 0: enemies target only same-map players
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (APawn* PlayerPawn = PC->GetPawn())
			{
				// B17 (U9): enemies don't target non-alive players (DBNO downed or Dead) — a downed teammate stops
				// drawing aggro and the swarm re-targets the living. (Downed players also take no contact damage.)
				const AFPSRPlayerState* PS = PC->GetPlayerState<AFPSRPlayerState>();
				if (PS && !PS->IsAlive())
				{
					continue;
				}
				// U (P-F): a late joiner that hasn't acked the current topology is excluded from the WHOLE movement+attack
				// pass (targeting + contact/ranged damage in one choke) until its ack lands (or the fail-open timeout). Only
				// with a unified field (multimap) — single-map has no topology to confirm, so it's a strict no-op there (no
				// sub-RTT exclusion for a mid-combat single-map joiner). Host = local authority -> instantly satisfied.
				// DBNO/Dead already excluded above; a revived player is already acked (marked long before), so it re-participates.
				if (bUnified && PS && !PS->HasAckedJoinTopology(Now))
				{
					continue;
				}
				PlayerPawns.Add(PlayerPawn);
				PlayerLocations.Add(PlayerPawn->GetActorLocation());
				// Committed occupancy (unset = Default single-map). Grace is a server-only allocator notion, NOT used
				// here for targeting/attack (Codex R5: combat uses committed MapId strictly, flow-continuity uses grace).
				PlayerMapIds.Add(PS ? PS->GetCurrentMapId() : FGameplayTag());
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

		// Multimap Tier 0: keep the enemy's MapId synced to the grid it is physically in — fast-skip (hysteresis margin)
		// while it is still in its own map, re-resolve only the few that crossed a boundary (Codex R3). Single-map: the
		// enemy's Default (unset) map contains everything -> no re-resolve, zero behaviour change.
		FGameplayTag EnemyMap = Enemy->GetMapId();
		if (FlowField && !FlowField->IsLocationInMap(EnemyMap, EnemyLocation))
		{
			const FGameplayTag NewMap = FlowField->FindMapIdForLocation(EnemyLocation);
			if (NewMap != EnemyMap)
			{
				Enemy->SetMapId(NewMap);
				EnemyMap = NewMap;
			}
		}

		// Nearest SAME-MAP player (committed occupancy, 2D) — attack-eligible. The enemy can ALSO chase a player in a
		// DIFFERENT slot that an opened door connects (front-chase, U P-D) — MOVE-ONLY, so a closed door / wall still blocks
		// contact (the attack below stays gated to a same-map, connected target). MoveTarget = the chosen player (same-map or
		// front); flow / facing / stop / LOD all reference it, consistent with the unified field's flow (Codex R2 #7).
		bool bTargetSameMap = false;
		float BestDistSq = TNumericLimits<float>::Max();
		int32 BestPlayerIndex = INDEX_NONE;
		for (int32 p = 0; p < PlayerLocations.Num(); ++p)
		{
			if (PlayerMapIds[p] != EnemyMap)
			{
				continue; // different map (committed) -> not a SAME-MAP (attack) target (front-chase handled below)
			}
			const float DistSq = FVector::DistSquaredXY(PlayerLocations[p], EnemyLocation);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestPlayerIndex = p;
			}
		}
		if (BestPlayerIndex != INDEX_NONE)
		{
			bTargetSameMap = true;
			Enemy->ClearFrontChasing(); // a same-map target supersedes any front-chase (handoff -> no double-state, Codex R2)
		}

		// Front-chase (U P-D, UNIFIED only): with NO same-map player, chase the nearest player the unified field connects to
		// (through an opened door) IF this enemy is within the front path-distance range (Schmitt via prior state). The unified
		// field already flows toward the nearest player through open doors, so movement just follows it. MOVE-ONLY.
		if (BestPlayerIndex == INDEX_NONE && bUnified)
		{
			const bool bWasChasing = Enemy->IsFrontChasing(Now);
			EFPSRFieldQuery St = EFPSRFieldQuery::NoGrid;
			const int32 EnemyFrontDist = FlowField->GetFrontDistanceCells(EnemyLocation, St);
			bool bFrontEligible = false;
			bool bRenew = false;
			if (St == EFPSRFieldQuery::OK)
			{
				bFrontEligible = (EnemyFrontDist <= (bWasChasing ? ChaseExitCells : ChaseEnterCells));
				bRenew = bFrontEligible; // a fresh in-range reading renews the hold
			}
			else if ((St == EFPSRFieldQuery::SourceLess || St == EFPSRFieldQuery::Unreachable) && bWasChasing)
			{
				// Source-less / transiently-stale field: HOLD an in-flight chaser (don't flip to idle) but do NOT renew, so a
				// persistently source-less / departed field lets the tag expire (ChaseHoldSeconds) and the enemy drains (#5).
				bFrontEligible = true;
			}
			if (bFrontEligible)
			{
				float FrontDistSq = TNumericLimits<float>::Max();
				int32 FrontIndex = INDEX_NONE;
				for (int32 p = 0; p < PlayerLocations.Num(); ++p)
				{
					if (!FlowField->AreLocationsConnected(EnemyLocation, PlayerLocations[p]))
					{
						continue; // a closed door / wall separates them -> not a front target
					}
					const float DistSq = FVector::DistSquaredXY(PlayerLocations[p], EnemyLocation);
					if (DistSq < FrontDistSq)
					{
						FrontDistSq = DistSq;
						FrontIndex = p;
					}
				}
				if (FrontIndex != INDEX_NONE)
				{
					BestPlayerIndex = FrontIndex;
					BestDistSq = FrontDistSq;
					if (bRenew)
					{
						Enemy->SetFrontChasing(Now + ChaseHoldSeconds);
					}
				}
			}
		}
		const bool bHasTarget = (BestPlayerIndex != INDEX_NONE);

		// Strict SAME-MAP + open-grid-CONNECTED target -> may deal contact damage. A front-chase (cross-slot, move-only)
		// never attacks (bTargetSameMap false). Even a same-map target is gated on connectivity when a unified field exists,
		// so a same-MapId target behind an internal closed wall / reclosed seam (a DIFFERENT component) can't be hit through
		// the wall — contact damage bypasses FPSRCombat::CanAffectTarget, so this is the melee guard (Codex R2 #6). No unified
		// grid -> keep the exact same-map behavior (no regression).
		const bool bAttackEligible = bHasTarget && bTargetSameMap &&
			(!bUnified || FlowField->AreLocationsConnected(EnemyLocation, PlayerLocations[BestPlayerIndex]));

		// No target at all (an unoccupied map before the empty-map drain culls it, S2b) -> cheapest LOD, no attack, no
		// player-directed movement (separation only).
		const FVector BestPlayerLocation = bHasTarget ? PlayerLocations[BestPlayerIndex] : EnemyLocation;

		// Distance LOD tier -> update stride + net update frequency (Game.MD §5).
		int32 UpdateStride;
		float NetFreq;
		if (BestDistSq <= TierS0RadiusSq)      { UpdateStride = 1; NetFreq = 30.0f; }
		else if (BestDistSq <= TierS1RadiusSq) { UpdateStride = 2; NetFreq = 10.0f; }
		else if (BestDistSq <= TierS2RadiusSq) { UpdateStride = 4; NetFreq = 5.0f;  }
		else                                   { UpdateStride = 8; NetFreq = 2.0f;  }

		// Only push a net-update-frequency change when the LOD tier actually changed. AActor::SetNetUpdateFrequency
		// (UE5.7) unconditionally broadcasts NetDriver->OnNetUpdateFrequencyChanged even when the value is unchanged,
		// so calling it every movement pass for every enemy is a 500-enemy hot-path regression (W1 P2).
		if (Enemy->GetNetUpdateFrequency() != NetFreq)
		{
			Enemy->SetNetUpdateFrequency(NetFreq);
		}

		// Per-archetype attack decision: the subsystem owns the batch context (nearest ALIVE player, the freeze gate
		// above, and the attack-token budgets); the enemy archetype owns the DECISION — melee contact for the base,
		// ranged charge->fire for AFPSRRangedEnemyBase (it manages its own held token). The XY nearest test ignores
		// Z, so compute the vertical gap (no through-floor melee) and pass it as a gate.
		const float AttackVertGap = FMath::Abs(EnemyLocation.Z - BestPlayerLocation.Z);
		if (bAttackEligible)
		{
			// Strict same-map target: full attack decision (melee contact for the base, ranged charge->fire override).
			if (AFPSRCharacter* TargetChar = Cast<AFPSRCharacter>(PlayerPawns[BestPlayerIndex]))
			{
				FFPSRServerAttackContext AttackCtx;
				AttackCtx.Now = Now;
				AttackCtx.DeltaSeconds = DeltaTime;
				AttackCtx.TargetChar = TargetChar;
				AttackCtx.TargetController = Cast<AFPSRPlayerController>(TargetChar->GetController());
				AttackCtx.TargetLocation = BestPlayerLocation;
				AttackCtx.DistSqToTarget = BestDistSq;
				AttackCtx.bVerticalInRange = (AttackVertGap <= AttackVerticalRange);
				AttackCtx.ContactDamage = ContactDamage;
				AttackCtx.bMeleeTokenAvailable = (AttackersThisPass[BestPlayerIndex] < AttackTokenLimit);
				if (Enemy->ServerTickAttack(AttackCtx) == EFPSRServerAttackResult::MeleeAttacked)
				{
					++AttackersThisPass[BestPlayerIndex];
				}
			}
		}
		else
		{
			// No attack-eligible target — NO same-map player (this enemy's map emptied: the player left/died) or a cross-slot
			// front-chaser (MOVEMENT only, U P-D). Tick the archetype with an EMPTY-target context so its attack FSM still
			// advances: a ranged enemy mid-charge whose target just crossed a boundary / died ABORTS + releases its charge
			// token + clears its client warning instead of freezing mid-charge (its whole charge FSM lives in ServerTickAttack).
			// The base melee no-ops on the null target, and no damage can land (null target), so this never lands a hit across
			// a boundary wall. Single-map: with players present every enemy has a same-map target, so this branch never runs
			// there — zero regression. Cheap: one no-op virtual call per targetless enemy.
			FFPSRServerAttackContext AttackCtx;
			AttackCtx.Now = Now;
			AttackCtx.DeltaSeconds = DeltaTime;
			Enemy->ServerTickAttack(AttackCtx);
		}

		// Spread throttled updates across frames by the enemy's stable id.
		if (((MovementFrameCounter + static_cast<int32>(Enemy->GetUniqueID())) % UpdateStride) != 0)
		{
			continue;
		}

		const float ScaledDelta = DeltaTime * UpdateStride;

		// Authored exit path (C1): an enemy spawned INSIDE a structure (pipe/box) files OUT along its waypoints first,
		// ignoring the flow-field and separation (the route is narrow — separation would shove it into the walls). At
		// the final waypoint ConsumeExitPathSteering returns false and the enemy hands off to flow-field chase below.
		FVector ExitDir;
		if (Enemy->ConsumeExitPathSteering(EnemyLocation, ScaledDelta, ExitDir))
		{
			ExitDir.Z = 0.0f;
			Enemy->TickServerMovement(ExitDir, ExitDir, ScaledDelta); // follow the exit path; face the way we're going
		}
		else
		{
			// Flow-field direction toward SAME-MAP players (fall back to direct-to-nearest same-map player if the field
			// isn't ready). Sampled from the enemy's own map so a mid-transition enemy near the door still gets its map's
			// flow (the subsystem retries by containing-grid on a stale MapId). No same-map player -> no beeline (never
			// chase cross-map): FlowDir stays zero and the enemy just separates.
			FVector FlowDir = FlowField ? FlowField->SampleFlowDirection(EnemyMap, EnemyLocation) : FVector::ZeroVector;
			if (FlowDir.IsNearlyZero() && bHasTarget)
			{
				// Field not ready in this enemy's map yet (no source) but we have a target — beeline straight at the nearest
				// player so the enemy still advances instead of only separating.
				FlowDir = (BestPlayerLocation - EnemyLocation);
				FlowDir.Z = 0.0f;
				FlowDir = FlowDir.GetSafeNormal();
			}

			// Stop advancing only when within StopDistance in FULL 3D. The nearest-player test above is XY-only
			// (DistSquaredXY), so a player on an overlapping upper deck (U7 multi-layer) reads as XY-close while a storey
			// up: a 2D stop (or a loose vertical band) freezes the enemy on the connecting stair ~one storey below the
			// platform — it bunches with its neighbours at the stair top and never crests (separation jitter). Folding
			// the vertical gap into the distance keeps it following the flow UP the stair until it is genuinely close in
			// 3D (i.e. actually on the player's surface), then stops. Flat map: AttackVertGap ~= 0, so this reduces to
			// the original XY stop (no regression); ranged (StopDistance 1500) is essentially unchanged by a 450cm gap.
			// Also keep advancing while still meaningfully BELOW the player (climbing a stair toward a platform-standing
			// player): with the player at the stair top (a chokepoint), the 3D stop would otherwise trigger a step below
			// the platform edge and the swarm bunches on the stair instead of cresting onto the platform. Once the enemy
			// reaches ~the player's height (crested) the stop applies. Flat map: gap ~= 0 < StopClimbBelowPlayer (no regression).
			const float StopDistSq = FMath::Square(Enemy->GetStopDistance());
			const float BestDist3DSq = BestDistSq + AttackVertGap * AttackVertGap;
			const bool bClimbingToPlayer = bHasTarget && (BestPlayerLocation.Z - EnemyLocation.Z) > StopClimbBelowPlayer;
			// No target -> no advance (separation only, below); with a target, stop within 3D StopDistance unless climbing.
			const FVector Desired = !bHasTarget ? FVector::ZeroVector
				: ((!bClimbingToPlayer && BestDist3DSq <= StopDistSq) ? FVector::ZeroVector : FlowDir);

			// Combine flow + separation; TickServerMovement normalizes and moves at CurrentMoveSpeed. Face the player
			// (FlowDir points toward them, direct near them) — NOT MoveDir, whose separation jitter would spin the enemy.
			FVector MoveDir = Desired + ComputeSeparation(i, Locations, SpatialHash) * SeparationStrength;
			MoveDir.Z = 0.0f;

			Enemy->TickServerMovement(MoveDir, FlowDir, ScaledDelta);
		}

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

void UFPSREnemySpawnSubsystem::ComputeOccupancy(TArray<FGameplayTag>& OutOccupiedMaps, TArray<int32>& OutPlayerCounts, float Now)
{
	OutOccupiedMaps.Reset();
	OutPlayerCounts.Reset();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const UFPSRFlowFieldSubsystem* Flow = World->GetSubsystem<UFPSRFlowFieldSubsystem>();
	// U (P-F): the topology late-join ack gate is meaningful ONLY when a unified continuous field exists (multimap) — a
	// single-map run has no door topology to confirm, so the gate is a strict no-op there (avoids even a sub-RTT exclusion
	// for a mid-combat single-map joiner: the "단일맵 무회귀" invariant).
	const bool bUnified = Flow && Flow->GetMultiSlotUnifiedComputer() != nullptr; // P-G: multimap only (single-map degenerate grid = false)
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		const APawn* Pawn = PC->GetPawn();
		if (!Pawn)
		{
			continue; // a player with no pawn doesn't occupy a map
		}
		// Committed occupancy = the map whose grid physically contains the pawn (unset = Default single-map). S2b commits
		// it directly; the settle-delay/grace 2-channel refinement is S3.
		const FGameplayTag Map = Flow ? Flow->FindMapIdForLocation(Pawn->GetActorLocation()) : FGameplayTag();
		AFPSRPlayerState* PS = PC->GetPlayerState<AFPSRPlayerState>();
		if (PS)
		{
			// U (P-F): stamp the topology generation this player entered the current topology at (first sighting; idempotent
			// — MarkTopologyJoin no-ops once set). Starts the ack fail-open clock. Only with a unified field (multimap): a
			// single-map run has no topology to confirm, so marking + gating are both inert there (strict no-op). The
			// HasAckedJoinTopology gate below seals a late joiner out until it confirms this generation.
			if (bUnified)
			{
				PS->MarkTopologyJoin(Flow->GetTopologyGeneration(), Now);
			}

			// Set CurrentMapId for ALL players with a pawn (a downed player is still physically in a map) so the combat
			// cross-map gate + UI stay correct. Idempotent (low-churn: only dirties on a real map change). Left ungated by
			// the topology ack (P-F): committing the physical map is harmless and keeps the combat gate honest.
			PS->SetCurrentMapId(Map);
		}
		// Allocation occupancy counts only LIVE participants — consistent with the movement pass, which excludes DBNO/dead
		// from targeting. A map with only downed players isn't "occupied" for budget: it drains and the budget flows to
		// living teammates elsewhere; it re-occupies when a living player (a reviver) arrives (Codex merge-gate P2).
		if (PS && !PS->IsAlive())
		{
			continue;
		}
		// U (P-F): a not-yet-acked late joiner doesn't count toward occupancy — no spawn budget is apportioned to its map
		// until it confirms the current topology (leading-edge seal). Cleared the instant its ack lands (or the fail-open
		// timeout). Unified-field only: single-map has no topology to confirm -> strict no-op (host = local authority too).
		if (bUnified && PS && !PS->HasAckedJoinTopology(Now))
		{
			continue;
		}
		const int32 Idx = OutOccupiedMaps.IndexOfByKey(Map);
		if (Idx == INDEX_NONE)
		{
			OutOccupiedMaps.Add(Map);
			OutPlayerCounts.Add(1);
		}
		else
		{
			++OutPlayerCounts[Idx];
		}
	}
}

void UFPSREnemySpawnSubsystem::ComputeAliveByMap(TMap<FGameplayTag, int32>& OutAliveByMap) const
{
	OutAliveByMap.Reset();
	for (const TObjectPtr<AFPSREnemyBase>& EnemyPtr : ActiveEnemies)
	{
		AFPSREnemyBase* Enemy = EnemyPtr.Get();
		if (IsValid(Enemy))
		{
			++OutAliveByMap.FindOrAdd(Enemy->GetMapId());
		}
	}
}

int32 UFPSREnemySpawnSubsystem::DrainMapEnemies(const FGameplayTag& MapId, int32 MaxToRelease, float Now)
{
	if (MaxToRelease <= 0)
	{
		return 0;
	}
	// Collect first (ReleaseEnemy mutates ActiveEnemies — never mutate while iterating it).
	TArray<AFPSREnemyBase*, TInlineAllocator<16>> ToRelease;
	for (const TObjectPtr<AFPSREnemyBase>& EnemyPtr : ActiveEnemies)
	{
		AFPSREnemyBase* Enemy = EnemyPtr.Get();
		// U P-D: skip a live FRONT-CHASER — a door-near cohort chasing a player in a connected slot is a live front, not idle.
		// Out-of-range enemies (no front tag) still drain; the tag is expiry-bounded so a departed cohort drains later.
		if (IsValid(Enemy) && Enemy->GetMapId() == MapId && !Enemy->IsFrontChasing(Now))
		{
			ToRelease.Add(Enemy);
			if (ToRelease.Num() >= MaxToRelease)
			{
				break;
			}
		}
	}
	for (AFPSREnemyBase* Enemy : ToRelease)
	{
		ReleaseEnemy(Enemy);
	}
	return ToRelease.Num();
}

bool UFPSREnemySpawnSubsystem::PassesCommonSpawnGates(const AFPSREnemySpawnPoint* Point, TConstArrayView<FVector> PlayerViewLocations) const
{
	if (Point == nullptr || !Point->IsEnabled())
	{
		return false;
	}

	// Zone (room) gate: an untagged point is always eligible; a tagged point only while its room is active. HasTag (not
	// exact) so activating a parent zone would enable its child rooms (hierarchical, optional).
	const FGameplayTag PointZone = Point->GetZoneTag();
	if (PointZone.IsValid() && !ActiveSpawnZones.HasTag(PointZone))
	{
		return false;
	}

	// MinPlayerDistance gate (XY): keep spawns at least this far from the nearest player VIEW (no FOV test anymore).
	if (Point->GetMinPlayerDistance() > 0.0f)
	{
		const FVector PointLocation = Point->GetSpawnLocation();
		float NearestDistSq = TNumericLimits<float>::Max();
		for (const FVector& PL : PlayerViewLocations)
		{
			NearestDistSq = FMath::Min(NearestDistSq, FVector::DistSquaredXY(PL, PointLocation));
		}
		if (NearestDistSq < FMath::Square(Point->GetMinPlayerDistance()))
		{
			return false;
		}
	}

	return true;
}

void UFPSREnemySpawnSubsystem::ComputeFrontState(const TArray<FGameplayTag>& OccupiedMaps,
	TMap<FGameplayTag, TArray<const AFPSREnemySpawnPoint*>>& OutFrontPointsByMap) const
{
	OutFrontPointsByMap.Reset();

	const UWorld* World = GetWorld();
	if (!World || SpawnPoints.Num() == 0)
	{
		return;
	}
	const UFPSRFlowFieldSubsystem* Flow = World->GetSubsystem<UFPSRFlowFieldSubsystem>();
	if (!Flow || Flow->GetMultiSlotUnifiedComputer() == nullptr)
	{
		return; // P-G: front spawning is multimap only (single-map degenerate grid / pre-content: no regression)
	}

	// Player VIEW locations for the shared MinPlayerDistance gate (same source as TrySelectSpawnPoint).
	TArray<FVector, TInlineAllocator<4>> PlayerViewLocations;
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
		PlayerViewLocations.Add(CamLocation);
	}
	if (PlayerViewLocations.Num() == 0)
	{
		return; // no players present -> no front
	}

	for (const TObjectPtr<AFPSREnemySpawnPoint>& PointPtr : SpawnPoints)
	{
		const AFPSREnemySpawnPoint* Point = PointPtr;
		if (Point == nullptr)
		{
			continue;
		}
		// Only NON-occupied slots get front spawning (the physical apportionment already fills occupied slots).
		const FGameplayTag SlotMap = Point->GetMapId();
		if (OccupiedMaps.Contains(SlotMap))
		{
			continue;
		}
		if (!PassesCommonSpawnGates(Point, PlayerViewLocations))
		{
			continue;
		}
		// Front gate: the point's unified path-distance to the nearest player must be OK (=> open-door-connected + reachable,
		// FPSRFlowFieldComputer::GetPathDistanceCells) AND within ChaseEnterCells (near-door). Bounding spawn range to the
		// front-chase ENTER threshold means every front-spawned enemy immediately qualifies to front-chase (P-D) toward the
		// player, so it starts moving through the door at once rather than sitting idle. SourceLess / Unreachable / OffGrid
		// are NOT front-eligible (fail-closed — no spawning across a closed door or in a source-less window).
		EFPSRFieldQuery St = EFPSRFieldQuery::NoGrid;
		const int32 Dist = Flow->GetFrontDistanceCells(Point->GetSpawnLocation(), St);
		if (St != EFPSRFieldQuery::OK || Dist > ChaseEnterCells)
		{
			continue;
		}
		OutFrontPointsByMap.FindOrAdd(SlotMap).Add(Point);
	}
}

void UFPSREnemySpawnSubsystem::ComputeAliveAndFrontState(const TArray<FGameplayTag>& OccupiedMaps,
	const TMap<FGameplayTag, TArray<const AFPSREnemySpawnPoint*>>& FrontPointsByMap, float Now,
	TMap<FGameplayTag, int32>& OutAliveByMap, TMap<FGameplayTag, int32>& OutFrontAliveBySlot, int32& OutFrontCountedGlobal)
{
	OutAliveByMap.Reset();
	OutFrontAliveBySlot.Reset();
	OutFrontCountedGlobal = 0;

	for (const TObjectPtr<AFPSREnemyBase>& EnemyPtr : ActiveEnemies)
	{
		AFPSREnemyBase* Enemy = EnemyPtr.Get();
		if (!IsValid(Enemy))
		{
			continue;
		}
		const FGameplayTag M = Enemy->GetMapId();
		++OutAliveByMap.FindOrAdd(M);

		if (OccupiedMaps.Contains(M))
		{
			// A front-spawned enemy that has crossed into an occupied slot: run its ONE-SHOT crossing credit so the front
			// keeps counting it for a bounded window (conveyor rate-limit, Codex P-E #4) without inflating that slot's own
			// fill — it is already counted in OutAliveByMap above, which throttles the slot's native spawns. The credit is
			// NEVER renewed, and grants NO drain immunity, so a player round-tripping a door can't leak a drain-immune cohort.
			if (Enemy->IsFrontSpawned())
			{
				if (!Enemy->HasFrontCreditStamp())
				{
					Enemy->StampFrontCredit(Now + CrossingCreditSeconds); // first crossing -> start the single countdown
				}
				if (Enemy->IsFrontCreditLive(Now))
				{
					++OutFrontCountedGlobal;
				}
				else
				{
					Enemy->ClearFrontSpawn(); // credit consumed -> a normal occupied-slot enemy from now on
				}
			}
		}
		else if (FrontPointsByMap.Contains(M))
		{
			// Physically in a front-active (non-occupied) slot: counts toward that slot's front budget regardless of how it
			// got there (a leftover of a just-vacated slot is legitimately part of the front now, so the front doesn't
			// over-spawn on top of it).
			++OutFrontAliveBySlot.FindOrAdd(M);
			++OutFrontCountedGlobal;
		}
		// else: rear / non-front non-occupied -> only in OutAliveByMap (a candidate for the trickle drain).
	}
}

int32 UFPSREnemySpawnSubsystem::DrainRearEnemies(const TArray<FGameplayTag>& OccupiedMaps,
	const TMap<FGameplayTag, TArray<const AFPSREnemySpawnPoint*>>& FrontPointsByMap, int32 MaxToRelease, float Now)
{
	if (MaxToRelease <= 0)
	{
		return 0;
	}
	const UWorld* World = GetWorld();
	const UFPSRFlowFieldSubsystem* Flow = World ? World->GetSubsystem<UFPSRFlowFieldSubsystem>() : nullptr;
	if (!Flow)
	{
		return 0;
	}

	// Collect rear candidates (key = path-distance so the stalest drain first), then release — never mutate ActiveEnemies
	// while iterating it.
	TArray<TPair<int32, AFPSREnemyBase*>, TInlineAllocator<32>> Rear;
	for (const TObjectPtr<AFPSREnemyBase>& EnemyPtr : ActiveEnemies)
	{
		AFPSREnemyBase* Enemy = EnemyPtr.Get();
		if (!IsValid(Enemy))
		{
			continue;
		}
		const FGameplayTag M = Enemy->GetMapId();
		if (OccupiedMaps.Contains(M) || FrontPointsByMap.Contains(M))
		{
			continue; // an occupied or front-active slot is never rear-drained (live crowd / live front)
		}
		if (Enemy->IsFrontChasing(Now))
		{
			continue; // a live front-chaser is a live cohort (P-D), exempt from rear drain
		}
		// Drain grace: a recently-vacated slot keeps its crowd for MapDrainGraceSeconds (no door-cross thrash).
		const float* LastOcc = MapLastOccupiedTime.Find(M);
		if (LastOcc && (Now - *LastOcc) < MapDrainGraceSeconds)
		{
			continue;
		}
		// Rear status: only a genuinely FAR OK distance (past the chase hysteresis band) or an Unreachable (a different
		// open-grid component) is rear. A SourceLess / OffGrid reading is HOLD — the source-less window (players
		// airborne/unsnapped) mustn't drain the near-door front (Codex P-E #6 / Opus P0-2). Same rule the unit test regresses.
		EFPSRFieldQuery St = EFPSRFieldQuery::NoGrid;
		const int32 Dist = Flow->GetFrontDistanceCells(Enemy->GetActorLocation(), St);
		if (!IsRearStatus(St, Dist))
		{
			continue;
		}
		const int32 SortKey = (St == EFPSRFieldQuery::OK) ? Dist : MAX_int32; // Unreachable sorts as the stalest
		Rear.Add(TPair<int32, AFPSREnemyBase*>(SortKey, Enemy));
	}

	if (Rear.Num() == 0)
	{
		return 0;
	}
	// Farthest-first (stalest rear drains first, ties -> arbitrary/stable).
	Rear.Sort([](const TPair<int32, AFPSREnemyBase*>& A, const TPair<int32, AFPSREnemyBase*>& B) { return A.Key > B.Key; });

	const int32 NumToRelease = FMath::Min(MaxToRelease, Rear.Num());
	for (int32 i = 0; i < NumToRelease; ++i)
	{
		ReleaseEnemy(Rear[i].Value);
	}
	return NumToRelease;
}

void UFPSREnemySpawnSubsystem::RefreshSpawnPointCache()
{
	if (!HasServerAuthority())
	{
		return;
	}
	// Re-scan the world (all loaded sublevels) so a newly-streamed map's spawn points + rooms become selectable. Cheap,
	// fires only on a stream-in event (not per tick). Full re-cache is simplest and idempotent (points are cached refs).
	CacheSpawnPoints();
	CacheSpawnRooms();
}

void UFPSREnemySpawnSubsystem::TickDirector()
{
	if (!HasServerAuthority())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	// Trickle-drain clock (U P-E): advance every tick INCLUDING the early return below, and clamp the elapsed to a couple of
	// director intervals so a long freeze / pause can't accrue a burst of drain tokens that pops the whole rear on the first
	// unfrozen tick (Codex P-E gate #4 / Opus P0-1). LastDirectorTime is stamped here unconditionally.
	const float RawElapsed = (LastDirectorTime < 0.0f) ? 0.0f : (Now - LastDirectorTime);
	const float DrainDt = ClampDrainDt(RawElapsed, SpawnInterval);
	LastDirectorTime = Now;

	const AFPSRGameState* GameState = World->GetGameState<AFPSRGameState>();
	if (GameState && (GameState->IsRunPaused()
		|| (!GameState->IsCombatPhase() && GameState->GetRunPhase() != ERunPhase::Boss)))
	{
		// Spawn during Combat AND Boss (the swarm persists + keeps ramping through the boss fight); never while
		// frozen for card selection, and not in pre-combat/menu phases (Game.MD §2-2). The drain does NOT run here (no
		// draining while frozen); LastDirectorTime is already stamped so the next live tick's DrainDt is one interval.
		return;
	}

	// Map-aware allocator (multimap Tier 0). Occupancy (also commits each player's CurrentMapId + records boundary
	// crossings for the Tier 1 transition tracker).
	TArray<FGameplayTag> OccupiedMaps;
	TArray<int32> PlayerCounts;
	ComputeOccupancy(OccupiedMaps, PlayerCounts, Now);

	// U P-E: front detection (unified continuous field ONLY). Front-active adjacent slots + their near-door eligible spawn
	// points. Empty (and every P-E branch below dormant) when there is no unified field -> byte-identical to pre-P-E.
	const UFPSRFlowFieldSubsystem* FlowField = World->GetSubsystem<UFPSRFlowFieldSubsystem>();
	const bool bUnified = FlowField && FlowField->GetMultiSlotUnifiedComputer() != nullptr; // P-G: multimap only (single-map degenerate grid = false)
	TMap<FGameplayTag, TArray<const AFPSREnemySpawnPoint*>> FrontPointsByMap;
	if (bUnified)
	{
		ComputeFrontState(OccupiedMaps, FrontPointsByMap);
	}
	const int32 FrontActiveSlots = FrontPointsByMap.Num();

	// U P-E: EXPLICIT front reserve carved out of the steady budget so the physical apportionment target stays honest —
	// PhysicalSteady = Cap - SeedReserve - FrontReserved — the front never inflates / "steals" the physical target (Codex
	// P-E gate #1). No unified field / no active front => FrontReserved 0 => PhysicalSteady == the pre-P-E steady (no regression).
	const int32 FrontReserved = ComputeFrontReserved(FrontActiveSlots);

	// Per-map alive counts (+ front pressure when unified). The unified pass also advances each front-spawned enemy's
	// one-shot crossing credit.
	TMap<FGameplayTag, int32> AliveByMap;
	TMap<FGameplayTag, int32> FrontAliveBySlot;
	int32 FrontCountedGlobal = 0;
	if (bUnified)
	{
		ComputeAliveAndFrontState(OccupiedMaps, FrontPointsByMap, Now, AliveByMap, FrontAliveBySlot, FrontCountedGlobal);
	}
	else
	{
		ComputeAliveByMap(AliveByMap);
	}

	// Grace: stamp each occupied map's last-seen time so a just-vacated map isn't drained for MapDrainGraceSeconds (a
	// player dipping across a boundary and back finds the crowd intact). Server-only.
	for (const FGameplayTag& Map : OccupiedMaps)
	{
		MapLastOccupiedTime.FindOrAdd(Map) = Now;
	}

	// Split the global target across occupied maps (pure math, no side effects — computed here so the trickle drain below
	// can read the per-map deficit for its burst gate). PhysicalSteady already reserves the front's share (above).
	const int32 PhysicalSteady = ComputePhysicalSteady(TargetAliveCount, FrontReserved);
	TArray<int32> PerMapTarget;
	FPSREnemyAllocator::Apportion(PlayerCounts, PhysicalSteady, MapGroupBonus, PerMapTarget);

	if (bUnified)
	{
		// U P-E trickle drain (replaces the hard EmptyMapDrainPerTick pop): a time-based token bucket drains REAR enemies at
		// an ambient rate, accelerating to a burst rate ONLY when the swarm is cap-bound AND a physical/front deficit exists
		// (rear is eating the cap the live front needs). Rear = far / disconnected-from-front, past grace, not chasing; a
		// source-less window never drains the front (DrainRearEnemies HOLDs SourceLess/OffGrid).
		int32 PhysicalDeficit = 0;
		for (int32 m = 0; m < OccupiedMaps.Num(); ++m)
		{
			PhysicalDeficit += FMath::Max(0, PerMapTarget[m] - AliveByMap.FindRef(OccupiedMaps[m]));
		}
		const bool bFrontDeficit = (FrontActiveSlots > 0) && (FrontCountedGlobal < FrontReserved);
		const bool bCapBound = ActiveEnemies.Num() >= (GlobalAliveCap - CapBoundMargin);
		const bool bDeficit = (PhysicalDeficit > 0) || bFrontDeficit;
		const float DrainRate = (bCapBound && bDeficit) ? BurstDrainRatePerSec : BaseDrainRatePerSec;

		DrainTokenBucket += DrainRate * DrainDt;
		const int32 DrainRequested = FMath::FloorToInt(DrainTokenBucket);
		if (DrainRequested > 0)
		{
			const int32 Released = DrainRearEnemies(OccupiedMaps, FrontPointsByMap, DrainRequested, Now);
			DrainTokenBucket -= Released;
			if (Released < DrainRequested)
			{
				DrainTokenBucket = 0.0f; // rear pool exhausted this tick -> don't carry drain debt into the next
			}
		}
	}
	else
	{
		// No unified field: the exact pre-P-E hard empty-map drain (byte-identical, no regression).
		for (const TPair<FGameplayTag, int32>& Pair : AliveByMap)
		{
			if (Pair.Value <= 0 || OccupiedMaps.Contains(Pair.Key))
			{
				continue;
			}
			const float* LastOcc = MapLastOccupiedTime.Find(Pair.Key);
			if (LastOcc && (Now - *LastOcc) < MapDrainGraceSeconds)
			{
				continue; // grace: recently vacated -> keep the crowd for a few seconds (no door-cross thrash)
			}
			DrainMapEnemies(Pair.Key, EmptyMapDrainPerTick, Now);
		}
	}

	if (OccupiedMaps.Num() == 0)
	{
		return; // no players anywhere -> nothing to fill (the rear drain above still ran)
	}

	// Physical round-robin fill: at most one spawn per map per outer pass so a big map doesn't consume the whole per-tick
	// budget before a smaller / newly-seeded map gets a turn. Every spawn is hard-gated on the GLOBAL cap.
	int32 SpawnedThisTick = 0;
	bool bSpawnedAny = true;
	while (bSpawnedAny && SpawnedThisTick < MaxSpawnPerTick && ActiveEnemies.Num() < GlobalAliveCap)
	{
		bSpawnedAny = false;
		for (int32 m = 0; m < OccupiedMaps.Num(); ++m)
		{
			if (SpawnedThisTick >= MaxSpawnPerTick || ActiveEnemies.Num() >= GlobalAliveCap)
			{
				break;
			}
			const FGameplayTag& Map = OccupiedMaps[m];
			int32& Alive = AliveByMap.FindOrAdd(Map);
			if (Alive >= PerMapTarget[m])
			{
				continue; // this map is at (or over, after apportionment shrank) its target
			}
			FVector SpawnAt;
			bool bSnapToGround = true;
			const AFPSREnemySpawnPoint* SpawnPoint = nullptr;
			if (!ComputeSpawnLocation(Map, SpawnAt, bSnapToGround, SpawnPoint))
			{
				continue; // no eligible spawn point in this map this tick (all too close / wrong zone / none placed)
			}
			if (AcquireEnemy(SpawnAt, bSnapToGround, SpawnPoint) == nullptr)
			{
				continue;
			}
			++Alive;
			++SpawnedThisTick;
			bSpawnedAny = true;
		}
	}

	// U P-E: front round-robin fill (after physical). Per-front-slot cap (fair across open fronts) + the global FrontReserved
	// + its OWN MaxFrontSpawnPerTick (so it never starves the physical fill's per-tick throughput, Codex P-E gate #B) + the
	// shared hard cap. Front enemies are TAGGED (bFrontSpawned) so their one-shot crossing credit rate-limits the front's
	// refill once they cross into the player's slot (no conveyor, #4).
	if (bUnified && FrontActiveSlots > 0 && FrontReserved > 0)
	{
		TArray<FGameplayTag, TInlineAllocator<8>> FrontMaps;
		for (const TPair<FGameplayTag, TArray<const AFPSREnemySpawnPoint*>>& Pair : FrontPointsByMap)
		{
			FrontMaps.Add(Pair.Key);
		}
		int32 FrontSpawnedThisTick = 0;
		bool bFrontSpawnedAny = true;
		while (bFrontSpawnedAny && FrontSpawnedThisTick < MaxFrontSpawnPerTick
			&& FrontCountedGlobal < FrontReserved && ActiveEnemies.Num() < GlobalAliveCap)
		{
			bFrontSpawnedAny = false;
			for (const FGameplayTag& FM : FrontMaps)
			{
				if (FrontSpawnedThisTick >= MaxFrontSpawnPerTick || FrontCountedGlobal >= FrontReserved
					|| ActiveEnemies.Num() >= GlobalAliveCap)
				{
					break;
				}
				int32& SlotCount = FrontAliveBySlot.FindOrAdd(FM);
				if (SlotCount >= PerFrontSlotBudget)
				{
					continue; // this front slot is at its per-front cap
				}
				const TArray<const AFPSREnemySpawnPoint*>& Pts = FrontPointsByMap[FM];
				if (Pts.Num() == 0)
				{
					continue;
				}
				// Uniform pick among this front slot's near-door eligible points; keep the authored Z (no ground re-snap).
				const AFPSREnemySpawnPoint* Chosen = Pts[FMath::RandRange(0, Pts.Num() - 1)];
				if (AcquireEnemy(Chosen->GetSpawnLocation(), /*bSnapToGround*/false, Chosen, /*bFrontSpawned*/true) == nullptr)
				{
					continue;
				}
				++SlotCount;
				++FrontCountedGlobal;
				++FrontSpawnedThisTick;
				bFrontSpawnedAny = true;
			}
		}
	}
}

bool UFPSREnemySpawnSubsystem::ComputeSpawnLocation(const FGameplayTag& TargetMapId, FVector& OutLocation, bool& bOutSnapToGround, const AFPSREnemySpawnPoint*& OutPoint) const
{
	// The swarm spawns ONLY at designer-placed spawn points (Game.MD §2-8, §1 fixed map). The player-proximity/ring
	// fallback was removed (user 2026-06-24) and the out-of-view (FOV) gate was removed (user 2026-06-29): a point is
	// eligible regardless of whether it's in a player's view — designer placement + MinPlayerDistance + room zones
	// control where/when. When no point qualifies this tick (none placed / wrong zone / too close), return false so the
	// director skips spawning and retries next tick. The designer point is authoritative — keep its exact Z (no ground
	// re-snap onto a ceiling/roof for indoor placements, Codex review 2026-06-09).
	if (TrySelectSpawnPoint(TargetMapId, OutLocation, OutPoint))
	{
		bOutSnapToGround = false;
		return true;
	}
	return false;
}

bool UFPSREnemySpawnSubsystem::TrySelectSpawnPoint(const FGameplayTag& TargetMapId, FVector& OutLocation, const AFPSREnemySpawnPoint*& OutPoint) const
{
	OutPoint = nullptr;

	const UWorld* World = GetWorld();
	if (!World || SpawnPoints.Num() == 0)
	{
		return false;
	}

	// Gather each player's location once for the MinPlayerDistance gate. The out-of-view (FOV) gate was REMOVED
	// (user 2026-06-29): a point is eligible regardless of whether it lies in any player's view — designer placement
	// + MinPlayerDistance + room zones now fully control where/when enemies appear, so a single visible point no
	// longer starves spawns.
	TArray<FVector, TInlineAllocator<4>> PlayerLocations;
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
		PlayerLocations.Add(CamLocation);
	}

	if (PlayerLocations.Num() == 0)
	{
		return false; // no players present — nothing to spawn this tick
	}

	// Build the eligible candidate set, then pick UNIFORMLY at random (weight + distance-falloff removed 2026-06-25):
	// designer points are equal-probability, and the room/zone gate decides WHICH points are live this tick.
	TArray<const AFPSREnemySpawnPoint*, TInlineAllocator<32>> Candidates;

	for (const TObjectPtr<AFPSREnemySpawnPoint>& PointPtr : SpawnPoints)
	{
		const AFPSREnemySpawnPoint* Point = PointPtr;
		if (Point == nullptr)
		{
			continue;
		}

		// Map gate (multimap Tier 0): only this map's points spawn this map's allocation. Single-map: both unset -> match.
		if (Point->GetMapId() != TargetMapId)
		{
			continue;
		}

		// Shared eligibility (enabled + active zone + MinPlayerDistance). Same gate the front selector reuses (U P-E).
		if (!PassesCommonSpawnGates(Point, PlayerLocations))
		{
			continue;
		}

		Candidates.Add(Point);
	}

	if (Candidates.Num() == 0)
	{
		return false;
	}

	// Uniform random among eligible points. The exact designer anchor is used (no jitter): it is the validated,
	// authoritative spawn transform (§1 fixed map). If the same point is picked more than once in a tick, the
	// co-located enemies are pushed apart at the source by ComputeSeparation's coincident handling rather than by
	// moving the spawn into possibly-unsafe wall/ledge geometry.
	const AFPSREnemySpawnPoint* Chosen = Candidates[FMath::RandRange(0, Candidates.Num() - 1)];
	OutLocation = Chosen->GetSpawnLocation(); // SpawnAnchor world loc (inside a structured spawner), else actor origin
	OutPoint = Chosen; // carries the authored exit path (C1) to AcquireEnemy
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

AFPSREnemyBase* UFPSREnemySpawnSubsystem::AcquireEnemy(const FVector& Location, bool bSnapToGround, const AFPSREnemySpawnPoint* SpawnPoint, bool bFrontSpawned)
{
	UWorld* World = GetWorld();
	if (!World || !HasServerAuthority())
	{
		return nullptr;
	}

	const FVector SpawnLocation = bSnapToGround ? SnapToGround(Location) : Location;

	// Pick the archetype to spawn: weighted-random from the data-driven roster (Game.MD §2-6), falling back to the
	// single configured EnemyClass (then the C++ base) so an unconfigured run still spawns.
	TSubclassOf<AFPSREnemyBase> PickedClass;
	if (EnemyRoster)
	{
		FFPSREnemySpawnContext SpawnCtx;
		if (const AFPSRGameState* GS = World->GetGameState<AFPSRGameState>())
		{
			SpawnCtx.RunClockSeconds = GS->GetRunClockSeconds();
			SpawnCtx.PartyLevel = GS->GetPartyLevel();
		}
		PickedClass = EnemyRoster->PickEnemyClass(SpawnCtx);
	}
	if (!PickedClass)
	{
		PickedClass = EnemyClass;
	}
	UClass* ClassToSpawn = PickedClass ? PickedClass.Get() : AFPSREnemyBase::StaticClass();

	AFPSREnemyBase* Enemy = nullptr;

	// Reuse a dormant actor of the SAME class as picked — a later request must never get a different archetype's
	// mesh/behaviour. Drop any stale nulls; the pool is bounded (<=MaxActiveEnemies) so the linear scan is cheap.
	for (int32 i = DormantPool.Num() - 1; i >= 0; --i)
	{
		AFPSREnemyBase* Candidate = DormantPool[i].Get();
		if (!IsValid(Candidate))
		{
			DormantPool.RemoveAtSwap(i);
			continue;
		}
		if (Candidate->GetClass() == ClassToSpawn)
		{
			Enemy = Candidate;
			DormantPool.RemoveAtSwap(i);
			break;
		}
	}

	if (Enemy == nullptr)
	{
		// Hard cap on total pooled actors (Game.MD §5).
		if (TotalSpawned >= MaxActiveEnemies)
		{
			return nullptr;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Enemy = World->SpawnActor<AFPSREnemyBase>(ClassToSpawn, SpawnLocation, FRotator::ZeroRotator, SpawnParams);
		if (Enemy == nullptr)
		{
			return nullptr;
		}
		++TotalSpawned;
	}

	// Activate and add to active set.
	Enemy->Activate(SpawnLocation);

	// Multimap Tier 0: inherit the spawn point's MapId (unset = Default single-map). Set explicitly on every acquire so a
	// pooled enemy reused in a different map never carries a stale MapId; the movement pass keeps it synced as it moves.
	Enemy->SetMapId(SpawnPoint ? SpawnPoint->GetMapId() : FGameplayTag());

	// Multimap U P-E: tag a front-spawned enemy right after its MapId is set (Activate already cleared any stale tag), so
	// the front pressure budget can keep counting it through its one-shot crossing credit. Marked here (not by the caller
	// after return) so a future call site can't forget it. bFrontSpawned=false (physical / debug spawns) => normal enemy.
	if (bFrontSpawned)
	{
		Enemy->MarkFrontSpawned();
	}

	// Structured spawner (C1): if this point authored an exit path, the enemy follows the waypoints OUT of the spawn
	// structure (pipe/box) before flow-field player-chase takes over — so it never jams inside concave geometry the
	// flow-field can't path out of. Applied after Activate (which clears any leftover path from a prior life).
	if (SpawnPoint)
	{
		TArray<FVector> ExitWaypoints;
		SpawnPoint->GetExitPathWorldPoints(ExitWaypoints);
		if (ExitWaypoints.Num() > 0)
		{
			Enemy->SetExitPath(ExitWaypoints);
		}
	}

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

	// Every charging enemy released its ranged token via Deactivate; reset the per-player counts as a safety net
	// (e.g. against a stale-controller decrement that couldn't match its key after a player left mid-charge).
	RangedChargeCountByPlayer.Reset();
}

void UFPSREnemySpawnSubsystem::ResetForNewRun()
{
	if (!HasServerAuthority())
	{
		return;
	}

	// Director transient state for a same-world re-run: the trickle-drain clock/bucket (so a stale freeze burst can't pop
	// the rear on the first tick) and the per-map grace map. A first run starts with all of these empty, so this is a byte
	// no-op there (no regression).
	DrainTokenBucket = 0.0f;
	LastDirectorTime = -1.0f;
	MapLastOccupiedTime.Reset();

	// Return every active enemy to the pool — this also clears their front attribution / crossing credit (those live on the
	// enemy actor, cleared on Deactivate). A first run has none active (no-op).
	ReleaseAllEnemies();

	// U (P-F): reset each connected PlayerState's topology late-join ack so a same-world re-run re-marks + re-gates every
	// player against the new run's topology. A first run's PlayerStates are already at the -1 default (no-op there), and a
	// cross-world run reaches a fresh field (generation 0) — so on every CURRENTLY reachable path this is a no-op / correct.
	// FUTURE NOTE (same-world re-run only, not yet reachable): this pairs with StartRun's ResetDoorTopologyToBaseline, which
	// bumps the generation + replicates it (OnRep -> clients re-ack) WHEN the prior run opened a door. If a same-world re-run
	// is ever added where the topology was NOT mutated (generation unchanged), there is no OnRep to restore Acked after this
	// wipe, so a remote client would sit gated until the 5s fail-open (a benign but misleading "RPC loss?" log). Handle that
	// case then by pairing the reset with an unconditional generation bump, or resetting only the Join marker (Acked is
	// monotone within a world's generation space, so keeping it is safe).
	if (UWorld* World = GetWorld())
	{
		if (const AGameStateBase* GS = World->GetGameState())
		{
			for (APlayerState* PS : GS->PlayerArray)
			{
				if (AFPSRPlayerState* FPS = Cast<AFPSRPlayerState>(PS))
				{
					FPS->ResetTopologyAck();
				}
			}
		}
	}
}

bool UFPSREnemySpawnSubsystem::IsRangedTokenAvailable(AFPSRPlayerController* TargetPC) const
{
	if (TargetPC == nullptr)
	{
		return false;
	}
	const int32* Count = RangedChargeCountByPlayer.Find(TObjectKey<AFPSRPlayerController>(TargetPC));
	return (Count == nullptr) || (*Count < RangedAttackTokenLimit);
}

bool UFPSREnemySpawnSubsystem::TryAcquireRangedToken(AFPSRPlayerController* TargetPC)
{
	if (!HasServerAuthority() || TargetPC == nullptr)
	{
		return false;
	}
	int32& Count = RangedChargeCountByPlayer.FindOrAdd(TObjectKey<AFPSRPlayerController>(TargetPC));
	if (Count >= RangedAttackTokenLimit)
	{
		return false;
	}
	++Count;
	return true;
}

void UFPSREnemySpawnSubsystem::ReleaseRangedToken(const TWeakObjectPtr<AFPSRPlayerController>& TargetPC)
{
	// Decrement by the controller's object key. If the controller is gone (player left mid-charge), the key won't
	// match and the (now-unconsulted) stale count is left for ReleaseAllEnemies to clear — harmless.
	if (int32* Count = RangedChargeCountByPlayer.Find(TObjectKey<AFPSRPlayerController>(TargetPC.Get())))
	{
		*Count = FMath::Max(0, *Count - 1);
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
