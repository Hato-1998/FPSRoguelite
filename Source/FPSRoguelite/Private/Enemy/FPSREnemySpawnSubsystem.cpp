// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Enemy/FPSREnemyBase.h" // CancelRangedChargesForTransition -> ServerCancelRangedForStageTransition (ADR 0013 C1: promoted here from the retired AFPSRRangedEnemyBase)
#include "Enemy/FPSREnemyEliteBase.h" // CancelRangedChargesForTransition -> ServerResetEliteForStageCarry (ADR 0013 후속 행 3 실행 1 — 같은 루프에 얹는다)
#include "Enemy/FPSREnemySpawnPoint.h"
#include "Enemy/FPSRSpawnRoom.h"
#include "Enemy/FPSRFlowFieldSubsystem.h"
#include "Enemy/FPSRFlowFieldComputer.h" // EFPSRFieldQuery (front-chase distance status, U P-D)
#include "Enemy/FPSREnemyAllocator.h"
#include "Enemy/FPSREnemyRosterDataAsset.h"
#include "Run/FPSRRunScheduleDataAsset.h" // C3: EvalStageAt(...).MaxEliteAlive — AcquireEnemy's elite-cap gate
#include "Hero/FPSRCharacter.h"
#include "Core/FPSRLogChannels.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"
#include "Core/FPSRPlayerController.h"
#include "Settings/FPSREnemySwarmSettings.h" // separation tuning (designer knob, read once per movement pass)
#include "Arena/FPSRArenaActor.h" // ADR 0010 D6: arena-bounds spawn gate (PassesCommonSpawnGates)
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h" // IsMovingOnGround() (ADR 0009 prep: LastGroundedZByPlayer)
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

float UFPSREnemySpawnSubsystem::ComputeUnifiedNetCullRadius(float MaxSlotDiagonalCm, float WeaponRangeCm, float SeamMarginCm)
{
	// U P-H (Option A — engagement/weapon-range bubble, capped to the slot footprint). NetCull is a SYMMETRIC player<->enemy
	// distance cull, so it cannot do per-slot "seam-only" relevancy (covering your own slot from any position => R >= the full
	// slot diagonal => that same R bleeds a full slot into every neighbor). RepGraph (spatial grid relevancy) is the real fix
	// (deferred). Tier-0: replicate the ENGAGEMENT/WEAPON bubble (what you can shoot / what's mechanically relevant) plus a
	// cross-seam lookahead, capped to the slot footprint so a center player never pulls the whole 3x3 grid; the weapon range
	// is ALSO the floor so an in-range enemy is never culled (alive-but-unshootable). Uniform across enemies (a cross-slot
	// migrant is never undersized). Far same-slot / cross-seam enemies pop in as they approach (accepted D3 Tier-0 limit).
	const float Bubble = WeaponRangeCm + SeamMarginCm;                // what must replicate to be shootable / seen
	const float FootprintCap = MaxSlotDiagonalCm + SeamMarginCm;      // never span more than one slot (+lookahead)
	return FMath::Max(WeaponRangeCm, FMath::Min(Bubble, FootprintCap)); // floor (weapon range) wins even for a tiny slot
}

void UFPSREnemySpawnSubsystem::TickEnemyMovement(float DeltaTime)
{
	if (!HasServerAuthority())
	{
		return; // movement is server-authoritative; clients receive replicated transforms.
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	// Death-dwell sweep: hoisted here, ABOVE the ActiveEnemies==0 early-return just below, so a corpse's dwell window
	// is still honored when it's the LAST enemy standing (BeginDying removes it from ActiveEnemies the instant it
	// dies — a dying corpse is never counted here) or between spawns; this Tick()-driven pass runs every frame
	// regardless of ActiveEnemies.Num(). Gated on the SAME freeze/transition check the movement+attack pass below
	// uses (computed once here as bFrozen, reused at that check further down — see it for why NOT a new
	// FTimerHandle): piggybacking on this already-gated pass gets freeze consistency for free (a dwelling corpse
	// holds its pose for the whole freeze instead of quietly finishing mid-freeze).
	const AFPSRGameState* GameState = World->GetGameState<AFPSRGameState>();
	const bool bFrozen = GameState && (GameState->IsRunPaused() || GameState->IsStageTransitionActive());
	if (!bFrozen)
	{
		SweepDyingEnemies(Now);
	}

	if (ActiveEnemies.Num() == 0)
	{
		return;
	}

	++MovementFrameCounter;

	// U P-D/P-F: the MULTI-SLOT unified field drives front-chase targeting AND the topology late-join ack gate — both are
	// active ONLY for a real multimap grid (P-G: a single-map degenerate grid keeps the exact same-map behavior, no regression).
	const UFPSRFlowFieldSubsystem* FlowField = World->GetSubsystem<UFPSRFlowFieldSubsystem>();
	const bool bUnified = FlowField && FlowField->GetMultiSlotUnifiedComputer() != nullptr; // P-G: multimap only (single-map degenerate grid = false)

	// ADR 0009 prep: prune stale grounded-Z cache entries (a destroyed/departed player's weak key resolves to
	// nullptr) once per pass here, rather than letting the map grow unbounded across a long run.
	for (auto CacheIt = LastGroundedZByPlayer.CreateIterator(); CacheIt; ++CacheIt)
	{
		if (!CacheIt->Key.IsValid())
		{
			CacheIt.RemoveCurrent();
		}
	}

	// Cache alive player pawn locations, pawns, committed MapIds, and grounded Z once for this pass.
	TArray<APawn*, TInlineAllocator<4>> PlayerPawns;
	TArray<FVector, TInlineAllocator<4>> PlayerLocations;
	TArray<FGameplayTag, TInlineAllocator<4>> PlayerMapIds; // multimap Tier 0: enemies target only same-map players
	// ADR 0009 prep (§3 invariant): each player's Z the last time they were GROUNDED, parallel to PlayerLocations
	// (index-aligned) — see LastGroundedZByPlayer's comment. Piped into the move context even while Seek3D stays
	// gated off, so the redesign lands on correct wiring without a second plumbing pass.
	TArray<float, TInlineAllocator<4>> PlayerGroundedZ;
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
				const FVector PlayerLoc = PlayerPawn->GetActorLocation();
				PlayerLocations.Add(PlayerLoc);
				// Committed occupancy (unset = Default single-map). Grace is a server-only allocator notion, NOT used
				// here for targeting/attack (Codex R5: combat uses committed MapId strictly, flow-continuity uses grace).
				PlayerMapIds.Add(PS ? PS->GetCurrentMapId() : FGameplayTag());

				// ADR 0009 prep: refresh the grounded-Z cache while standing (a jump/fall must NOT move the cached
				// value); while airborne, read back the last-grounded value (or the current Z if this player has
				// never been cached yet — e.g. spawned mid-air).
				float GroundedZ = PlayerLoc.Z;
				if (AFPSRCharacter* Character = Cast<AFPSRCharacter>(PlayerPawn))
				{
					const TWeakObjectPtr<AFPSRCharacter> WeakChar(Character);
					const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
					if (Movement && Movement->IsMovingOnGround())
					{
						LastGroundedZByPlayer.Add(WeakChar, GroundedZ);
					}
					else if (const float* Cached = LastGroundedZByPlayer.Find(WeakChar))
					{
						GroundedZ = *Cached;
					}
				}
				PlayerGroundedZ.Add(GroundedZ);
			}
		}
	}
	if (PlayerPawns.Num() == 0)
	{
		return;
	}

	// Global freeze (card selection) OR an active stage transition (ADR 0010 D6): enemies are frozen in place — skip
	// the whole movement+attack pass. During a transition the frozen swarm IS the grace-window reward (안 G) — the
	// player grinds down enemies that cannot move or fight back, so freezing them here (not just their damage output)
	// is what makes that hold. (Enemies move/attack during both Combat and Boss phases outside a transition. bFrozen
	// + GameState were already computed above, alongside the death-dwell sweep gate — a single freeze determination
	// per pass.)
	if (bFrozen)
	{
		return;
	}

	// Separation tuning, snapshotted ONCE per pass (designer knob, read at use so PIE edits hit enemies already on
	// the field). The snapshot is what keeps the hash build and the 3x3 neighbour query on the SAME cell size even
	// if the setting is edited mid-session; the ClampMin(50) on the property keeps the hash from degenerating.
	const UFPSREnemySwarmSettings* SwarmSettings = GetDefault<UFPSREnemySwarmSettings>();
	const float SeparationRadius = FMath::Max(50.0f, SwarmSettings->SeparationRadius);
	const float SeparationStrength = FMath::Max(0.0f, SwarmSettings->SeparationStrength);

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

		// Strict SAME-MAP + open-grid-CONNECTED target -> may run the attack cycle against it. A front-chase (cross-slot, move-only)
		// never attacks (bTargetSameMap false). Even a same-map target is gated on connectivity when a unified field exists,
		// so a same-MapId target behind an internal closed wall / reclosed seam (a DIFFERENT component) is never charged, warned or fired at through
		// the wall (Codex R2 #6 — originally the melee-contact guard, since that axis bypassed FPSRCombat::CanAffectTarget;
		// ADR 0013 C0 removed the axis and this gate now fronts the ranged cycle, whose own LOS trace is a SECOND line
		// of defence, not a replacement for this one). No unified
		// grid -> keep the exact same-map behavior (no regression).
		const bool bAttackEligible = bHasTarget && bTargetSameMap &&
			(!bUnified || FlowField->AreLocationsConnected(EnemyLocation, PlayerLocations[BestPlayerIndex]));

		// No target at all (an unoccupied map before the empty-map drain culls it, S2b) -> cheapest LOD, no attack, no
		// player-directed movement (separation only).
		const FVector BestPlayerLocation = bHasTarget ? PlayerLocations[BestPlayerIndex] : EnemyLocation;
		// ADR 0009 prep: index-aligned with PlayerLocations (both the same-map search and the front-chase override
		// above write BestPlayerIndex into the SAME player arrays) — see LastGroundedZByPlayer's comment.
		const float BestPlayerGroundedZ = bHasTarget ? PlayerGroundedZ[BestPlayerIndex] : EnemyLocation.Z;

		// Distance LOD tier -> movement stride + attack stride + net update frequency (Game.MD §5).
		// AttackStride throttles the per-enemy attack DECISION for distant tiers (F1) so the swarm's attack cost scales
		// with the number of NEAR enemies, not the total active count. It is always <= UpdateStride (attack latency is
		// more sensitive than movement) and the un-throttled band spans S0+S1: any enemy actually in combat is never
		// throttled. INVARIANT: this holds only while every archetype's engage range stays within the S1 radius (sqrt
		// TierS1RadiusSq = 3500) — RangedEngageRange (1400) sits well inside it (as does AttackRange (150), which since
		// ADR 0013 C0 is the client attack-tell radius, not a melee reach),
		// so charging always happens at AttackStride 1. If a future BP tunes an engage range past 3500, its
		// charge/cooldown timing stays correct (DeltaSeconds is stride-scaled below) but its abort/warning cadence would
		// lag by up to AttackStride frames — re-validate this band then (natural home: the F8 significance-radius SSOT).
		int32 UpdateStride;
		int32 AttackStride;
		float NetFreq;
		if (BestDistSq <= TierS0RadiusSq)      { UpdateStride = 1; AttackStride = 1; NetFreq = 30.0f; }
		else if (BestDistSq <= TierS1RadiusSq) { UpdateStride = 2; AttackStride = 1; NetFreq = 10.0f; }
		else if (BestDistSq <= TierS2RadiusSq) { UpdateStride = 4; AttackStride = 2; NetFreq = 5.0f;  }
		else                                   { UpdateStride = 8; AttackStride = 4; NetFreq = 2.0f;  }

		// Only push a net-update-frequency change when the LOD tier actually changed. AActor::SetNetUpdateFrequency
		// (UE5.7) unconditionally broadcasts NetDriver->OnNetUpdateFrequencyChanged even when the value is unchanged,
		// so calling it every movement pass for every enemy is a 500-enemy hot-path regression (W1 P2).
		if (Enemy->GetNetUpdateFrequency() != NetFreq)
		{
			Enemy->SetNetUpdateFrequency(NetFreq);
		}

		// Vertical (Z) gap to the nearest player — feeds the movement stop-gate below (folded into the 3D stop
		// distance for overlapping decks, U7); stays at loop-body scope for that reason (the melee attack gate that
		// used to also read this was removed as dead code, ADR 0013 C0 — see AFPSREnemyBase::ServerTickAttack).
		const float AttackVertGap = FMath::Abs(EnemyLocation.Z - BestPlayerLocation.Z);

		// Attack decision, gated FIRST by front-connectivity eligibility (U P-D) then throttled by AttackStride (perf, merged
		// with the cosmetic-tick/attack-throttle track): an eligible target (same-map, or open-door-connected in the unified
		// field) gets the full attack FSM on the throttled cadence; an INELIGIBLE enemy (its map emptied, or a cross-slot
		// front-chaser = MOVEMENT only) instead gets an every-pass EMPTY-target tick so a ranged enemy mid-charge ABORTS +
		// releases its token / clears its client warning instead of freezing. Single-map: every enemy has a same-map target,
		// so the else never runs there = zero regression.
		if (bAttackEligible)
		{
			// Throttled by AttackStride (spread across frames by the enemy's stable id — same phase basis as the movement
			// stride below). A skipped pass costs nothing (a distant idle enemy stops paying its per-pass range check + token
			// peek + conditional LOS trace every frame; cost scales with NEAR enemies, not total count, F1). When it runs,
			// DeltaSeconds carries the elapsed real time since this enemy's last decision (DeltaTime * AttackStride) so the
			// ranged charge/cooldown accumulators stay wall-clock-correct. Freeze preserved: the whole pass early-returns while paused.
			if (((MovementFrameCounter + static_cast<int32>(Enemy->GetUniqueID())) % AttackStride) == 0)
			{
				// Attack decision: AFPSREnemyBase::ServerTickAttack's ranged charge->fire cycle (promoted from the
				// retired AFPSRRangedEnemyBase, ADR 0013 C1 — every enemy has been ranged since f5b0a78d, so this is
				// no longer a per-subclass override). Neither BestDistSq nor the vertical gap (both computed above)
				// feeds an attack gate any more — they are XY-only / stop-gate inputs, and the ranged cycle measures
				// its own 3D distance to TargetLocation against RangedEngageRange.
				if (AFPSRCharacter* TargetChar = Cast<AFPSRCharacter>(PlayerPawns[BestPlayerIndex]))
				{
					FFPSRServerAttackContext AttackCtx;
					AttackCtx.Now = Now;
					AttackCtx.DeltaSeconds = DeltaTime * AttackStride;
					AttackCtx.TargetChar = TargetChar;
					AttackCtx.TargetController = Cast<AFPSRPlayerController>(TargetChar->GetController());
					AttackCtx.TargetLocation = BestPlayerLocation;
					Enemy->ServerTickAttack(AttackCtx);
				}
			}
		}
		else
		{
			// No attack-eligible target — NO same-map player (this enemy's map emptied) or a cross-slot front-chaser (MOVEMENT
			// only, U P-D). Tick the archetype with an EMPTY-target context so its attack FSM still advances: a ranged enemy
			// mid-charge whose target crossed a boundary / died ABORTS + releases its charge token + clears its client warning
			// instead of freezing. The base itself has no attack of its own (dead melee axis removed, ADR 0013 C0) and simply
			// no-ops here. Cheap: one no-op virtual call per targetless enemy.
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
			// ADR 0008: bHasTarget=false so the pursuit judgment (PursuitState.Tick) never opens Seek3D on an
			// authored exit route — a designer-placed escape path must never be interrupted by a reachability escape.
			FFPSRServerMoveContext ExitCtx;
			ExitCtx.MoveDir = ExitDir;
			ExitCtx.FaceDir = ExitDir; // face the way we're going
			ExitCtx.ScaledDelta = ScaledDelta;
			ExitCtx.Now = Now;
			ExitCtx.bHasTarget = false;
			Enemy->TickServerMovement(ExitCtx);
		}
		else
		{
			// Flow-field direction toward SAME-MAP players (fall back to direct-to-nearest same-map player if the field
			// isn't ready). ADR 0009 결정 5: routed through QueryFlow, the single movement-consumer seam. S3 (P1): naming
			// TargetPlayerPawn lets QueryFlow try the player-centred 3D window FIRST (a real 3D Direction, occasionally
			// SeekZ too), falling back to the unchanged 2D surface sample on any miss — see QueryFlow's own comment for
			// the exact fallback conditions. No same-map player -> no beeline (never chase cross-map) and no window
			// target either (TargetPlayerPawn null): FlowDir stays zero and the enemy just separates.
			FFPSRFlowQuery FlowQuery;
			FlowQuery.WorldPos = EnemyLocation;
			FlowQuery.bWantDirection = true;
			FlowQuery.TargetPlayerPawn = bHasTarget ? PlayerPawns[BestPlayerIndex] : nullptr;
			// Window3D is hover-only (merge-gate P1): a ground archetype handed the window's free-air gradient
			// would zero its XY on a vertical step with no means to climb and both fallbacks suppressed.
			FlowQuery.bHoverCapable = Enemy->GetCurrentHoverHeight() > 0.0f;
			FFPSRFlowResult FlowRes;
			if (FlowField)
			{
				FlowField->QueryFlow(FlowQuery, FlowRes);
			}
			// S3: FlowDir may now carry a nonzero Z when the window answered. That's fine as-is here — MoveDir.Z is
			// zeroed below regardless (horizontal steering stays 2D) and FaceDir only ever reads XY (TickServerMovement
			// zeroes FaceXY.Z) — the vertical component reaches the enemy separately via MoveCtx.SeekZ/bSeekValid below.
			FVector FlowDir = FlowRes.Direction;
			// The beeline fallback's own zero-check (bFlowZero, used immediately below) — ALSO still carried into
			// MoveCtx.bFlowZero (unread by TickServerMovement now that ADR 0008's Seek3D was retired; kept for the
			// same "dormant reactivation" reason as the pursuit fields it used to feed — see FFPSRServerMoveContext's
			// class comment). No added flow-field query either way (still just this one QueryFlow call).
			const bool bFlowZero = !FlowRes.bDirectionValid;
			if (bFlowZero && bHasTarget)
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
			FVector MoveDir = Desired + ComputeSeparation(i, Locations, SpatialHash, SeparationRadius) * SeparationStrength;
			MoveDir.Z = 0.0f;

			FFPSRServerMoveContext MoveCtx;
			MoveCtx.MoveDir = MoveDir;
			MoveCtx.FaceDir = FlowDir;
			MoveCtx.ScaledDelta = ScaledDelta;
			MoveCtx.Now = Now;
			MoveCtx.bHasTarget = bHasTarget;
			MoveCtx.bFlowZero = bFlowZero;
			MoveCtx.TargetLocation = BestPlayerLocation;
			MoveCtx.TargetGroundedZ = BestPlayerGroundedZ;
			// S3 (ADR 0009 P1): forwarded straight from QueryFlow's answer into AFPSREnemyBase::SeekTargetZ/
			// bSeekTargetZValid — see TickServerMovement.
			MoveCtx.SeekZ = FlowRes.SeekZ;
			MoveCtx.bSeekValid = FlowRes.bSeekValid;
			Enemy->TickServerMovement(MoveCtx);
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


FVector UFPSREnemySpawnSubsystem::ComputeSeparation(int32 AgentIndex, const TArray<FVector>& Locations, const TMap<FIntPoint, TArray<int32>>& SpatialHash, float SeparationRadius) const
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

	// Arena-bounds gate (ADR 0010 D6 stage transition): only spawn points inside the CURRENTLY ACTIVE arena are
	// eligible. Reserve arenas are parked 100+ m away in the same level (ADR 0010 D6 as amended 2026-08-17) — without
	// this filter, a spawn point that happens to sit in a currently-INACTIVE arena would still pass every gate above,
	// and the swarm would spawn into an empty arena nobody is standing in.
	//
	// Read off the replicated GameState, NOT AFPSRArenaActor::FindActiveInWorld: this runs per spawn CANDIDATE on the
	// director tick, and FindActiveInWorld sweeps the whole actor list (TActorIterator) — its cost scales with every
	// actor in the level, not with the handful of arenas. The GameState pointer is the same answer in O(1), and it is
	// the authoritative one (the stage director sets it on every swap). Null = no arena seeded (legacy level, or
	// pre-BeginPlay) -> nothing to gate against, pass through unconditionally.
	const UWorld* GateWorld = GetWorld();
	const AFPSRGameState* GateGameState = GateWorld ? GateWorld->GetGameState<AFPSRGameState>() : nullptr;
	if (const AFPSRArenaActor* ActiveArena = GateGameState ? GateGameState->GetActiveArena() : nullptr)
	{
		if (!ActiveArena->ContainsWorldLocation(Point->GetSpawnLocation()))
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
	if (GameState && (GameState->IsRunPaused() || GameState->IsStageTransitionActive()
		|| (!GameState->IsCombatPhase() && GameState->GetRunPhase() != ERunPhase::Boss)))
	{
		// Spawn during Combat AND Boss (the swarm persists + keeps ramping through the boss fight); never while
		// frozen for card selection, during an active stage transition (the swarm is frozen/being ground down, not
		// growing — see TickEnemyMovement), and not in pre-combat/menu phases (Game.MD §2-2). The drain does NOT run
		// here (no draining while frozen); LastDirectorTime is already stamped so the next live tick's DrainDt is one
		// interval.
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

	// Per-map alive counts (+ front pressure). P-G: the single pass. Single-map (no front): FrontPointsByMap is empty and no
	// enemy is front-spawned, so this degrades to a plain alive-by-map bucketing (byte-identical to the old per-map counter).
	TMap<FGameplayTag, int32> AliveByMap;
	TMap<FGameplayTag, int32> FrontAliveBySlot;
	int32 FrontCountedGlobal = 0;
	ComputeAliveAndFrontState(OccupiedMaps, FrontPointsByMap, Now, AliveByMap, FrontAliveBySlot, FrontCountedGlobal);

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
		// U P-E trickle drain (P-G: the only drain path; the old hard empty-map pop is gone): a time-based token bucket drains REAR enemies at
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
	// P-G: single-map (bUnified false) runs no drain — its one map is occupied whenever any alive player is present, so the
	// old hard empty-map drain only ever fired at wipe/run-end (handled by ReleaseAllEnemies + the phase early-return above)
	// and pit-fall recycle covers fallen enemies. (The multimap trickle drain above is the only drain path now.)

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

	// Elite cap gate (ADR 0013 불변식 6 + C3 「구현 사양 B」) — only evaluated when the roster actually picked an
	// elite class (plain tier is untouched by this axis, no early-return cost). Checked BEFORE either a dormant-pool
	// reuse or a fresh spawn: a pool hit and a fresh spawn both result in "one more active elite", and this pool has
	// no "push back into the bucket" path (AcquireOfClass only ever removes), so the gate must run first, not after
	// a pool hit that then has to be undone. 판별 = IsChildOf (티어 판별, 풀 버킷 키의 정확일치와 혼동 금지 — 그
	// 쪽은 FFPSREnemyDormantPool 이 EXACT match 로 별도 관리한다). Effective cap = min(schedule curve, hard cap);
	// a block returns nullptr — both TickDirector fill loops (physical + front) already treat a null AcquireEnemy
	// return as "skip this attempt, try again next pass" (no dedicated handling needed here). No "downgrade to a
	// normal enemy" substitute — that would need a roster re-roll API (bigger surface, 사용자·G1 판정).
	const bool bIsEliteClass = ClassToSpawn->IsChildOf(AFPSREnemyEliteBase::StaticClass());
	if (bIsEliteClass)
	{
		int32 CurrentStageIndex = 0;
		if (const AFPSRGameState* GameState = World->GetGameState<AFPSRGameState>())
		{
			CurrentStageIndex = GameState->GetStageIndex();
		}
		// ActiveSchedule null, or StageDifficulty unauthored, both resolve to 0 here (EvalStageAt's own identity
		// fallback) — "elite 없음" is the correct no-regression default (MaxEliteAlive's own field comment).
		const int32 CurveCap = ActiveSchedule
			? UFPSRRunScheduleDataAsset::EvalStageAt(ActiveSchedule->StageDifficulty, CurrentStageIndex).MaxEliteAlive
			: 0;
		const int32 EffectiveEliteCap = FMath::Min(CurveCap, EliteHardCap);
		if (ActiveEliteCount >= EffectiveEliteCap)
		{
			return nullptr;
		}
	}

	AFPSREnemyBase* Enemy = nullptr;

	// Reuse a dormant actor of the SAME class as picked — a later request must never get a different archetype's
	// mesh/behaviour. O(1) in the requested class's bucket size (ADR 0013 불변식 7 — 풀 취득 비용은 클래스 수와
	// 무관하다); stale nulls are dropped along the way, scoped to just that bucket (FFPSREnemyDormantPool).
	Enemy = DormantPool.AcquireOfClass(ClassToSpawn);

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

	// Multimap U P-H: in the unified multi-slot field, size the enemy's net-cull radius to a footprint-derived engagement/
	// weapon-range bubble (ComputeUnifiedNetCullRadius) — applied UNIFORMLY (MapId-independent) on EVERY acquire, so a pooled
	// reuse never carries a prior life's radius and a cross-slot chaser is never undersized. A single-map run never enters here
	// (GetMultiSlotUnifiedComputer() == null), so its enemies keep the ctor default (byte no-regression). Set after Activate
	// woke net dormancy; the default net driver reads NetCullDistanceSquared live each relevancy pass.
	if (const UFPSRFlowFieldSubsystem* FlowField = World->GetSubsystem<UFPSRFlowFieldSubsystem>();
		FlowField && FlowField->GetMultiSlotUnifiedComputer())
	{
		Enemy->ApplyNetCullRadius(ComputeUnifiedNetCullRadius(
			FlowField->GetMaxSlotFootprintDiagonal(), NetCullWeaponRangeCm, NetCullSeamMarginCm));
	}

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
			Enemy->SetExitPath(ExitWaypoints, SpawnPoint->ShouldPhaseThroughWorldWhileExiting());
		}
	}

	ActiveEnemies.Add(Enemy);
	if (bIsEliteClass)
	{
		++ActiveEliteCount; // paired decrement: BeginDying (death) / ReleaseEnemy (every other teardown) — see their own comments
	}
	return Enemy;
}

void UFPSREnemySpawnSubsystem::ReleaseEnemy(AFPSREnemyBase* Enemy)
{
	if (Enemy == nullptr)
	{
		return;
	}

	ActiveEnemies.Remove(Enemy);
	// Elite cap accounting (C3): every teardown path EXCEPT death routes through here (pool release / rear-drain /
	// kill-Z recycle / stage-carry overflow / ReleaseAllEnemies) — the death path decrements in BeginDying instead
	// (it never reaches this function), so the two decrement points never double-count the same enemy.
	if (Enemy->IsA(AFPSREnemyEliteBase::StaticClass()))
	{
		--ActiveEliteCount;
	}
	Enemy->Deactivate();
	DormantPool.Add(Enemy);
}

void UFPSREnemySpawnSubsystem::FinishDyingEnemy(AFPSREnemyBase* Enemy)
{
	// The shared "corpse's dwell is over" recovery point — same Deactivate+DormantPool.Add pair ReleaseEnemy uses,
	// just without the ActiveEnemies.Remove (BeginDying already did that the moment the corpse started dwelling).
	if (!IsValid(Enemy))
	{
		return;
	}
	Enemy->Deactivate();
	DormantPool.Add(Enemy);
}

void UFPSREnemySpawnSubsystem::BeginDying(AFPSREnemyBase* Enemy)
{
	if (Enemy == nullptr)
	{
		return;
	}

	// Remove from ActiveEnemies IMMEDIATELY — this is the crux of the death-dwell split. TickEnemyMovement's per-pass
	// loop (and its ComputeAliveAndFrontState / DrainRearEnemies siblings) all iterate ActiveEnemies, so the instant
	// this enemy drops out of it, it can no longer move, can no longer attack, and its collision-off corpse
	// (EnterDyingState) can never front-line-shield the enemies behind it. It also frees its GlobalAliveCap /
	// MaxActiveEnemies slot at once, so a corpse dwelling does NOT starve the spawner.
	ActiveEnemies.Remove(Enemy);
	// Elite cap accounting (C3): the death path's decrement point (paired with ReleaseEnemy's — see that function's
	// comment for why the two never double-count). Decremented HERE, at the same instant the enemy leaves
	// ActiveEnemies, rather than later at FinishDyingEnemy/Deactivate — for the SAME reason ActiveEnemies itself
	// drops the enemy immediately (comment above): a dying elite must free its cap slot at once, or a dwelling
	// elite corpse (death-dwell can run several seconds) would keep blocking a fresh elite from spawning even
	// though the old one is already gameplay-over.
	if (Enemy->IsA(AFPSREnemyEliteBase::StaticClass()))
	{
		--ActiveEliteCount;
	}
	Enemy->EnterDyingState();

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	DyingEnemies.Add(FFPSRDyingEnemy{ Enemy, Now + Enemy->GetDeathDwellSeconds() });

	// Bound the dwell list: a burst of deaths beyond MaxDyingEnemies finishes the corpse CLOSEST TO EXPIRING
	// immediately (the same FinishDyingEnemy path the deadline sweep uses) rather than growing this list unbounded.
	// Adding exactly one entry above can push the count at most one over the cap, so a single eviction always
	// suffices. The victim is found by scanning for the earliest deadline rather than taking index 0: SweepDyingEnemies
	// uses RemoveAtSwap, so this array's ORDER carries no age information — cutting index 0 short would evict an
	// arbitrary corpse (possibly one that just died) while the one about to vanish anyway kept dwelling. The scan is
	// bounded by MaxDyingEnemies and only runs on overflow.
	if (DyingEnemies.Num() > MaxDyingEnemies)
	{
		int32 EarliestIndex = 0;
		for (int32 i = 1; i < DyingEnemies.Num(); ++i)
		{
			if (DyingEnemies[i].DeadlineWorldSeconds < DyingEnemies[EarliestIndex].DeadlineWorldSeconds)
			{
				EarliestIndex = i;
			}
		}
		FinishDyingEnemy(DyingEnemies[EarliestIndex].Enemy.Get());
		DyingEnemies.RemoveAtSwap(EarliestIndex);
	}
}

void UFPSREnemySpawnSubsystem::SweepDyingEnemies(float Now)
{
	// Reverse iteration + RemoveAtSwap: this list's order carries no meaning (unlike DrainRearEnemies' sorted rear
	// candidates), so an O(1) swap-remove while walking backward is the cheap, safe "remove while iterating" idiom
	// this same file already uses for the dormant-pool scan in AcquireEnemy.
	for (int32 i = DyingEnemies.Num() - 1; i >= 0; --i)
	{
		AFPSREnemyBase* Enemy = DyingEnemies[i].Enemy.Get();
		if (!IsValid(Enemy) || Now >= DyingEnemies[i].DeadlineWorldSeconds)
		{
			FinishDyingEnemy(Enemy); // no-op (IsValid guard inside) for an already-invalid entry
			DyingEnemies.RemoveAtSwap(i);
		}
	}
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

	// A bulk release must also flush any corpse still dwelling — BeginDying already pulled it OUT of ActiveEnemies,
	// so the loop above never touches it, and leaving it dwelling would let it survive past this explicit "clear the
	// board now" call and leak into the next run/stage (ResetForNewRun and the CarryEnemiesToNewStage no-player-delta
	// fallback both route through here). FinishDyingEnemy is the SAME Deactivate+DormantPool.Add pair the deadline
	// sweep uses, just invoked immediately instead of waiting for each corpse's own deadline.
	for (const FFPSRDyingEnemy& Dying : DyingEnemies)
	{
		FinishDyingEnemy(Dying.Enemy.Get());
	}
	DyingEnemies.Reset();

	// Every charging enemy released its ranged token via Deactivate; reset the per-player counts as a safety net
	// (e.g. against a stale-controller decrement that couldn't match its key after a player left mid-charge).
	RangedChargeCountByPlayer.Reset();
}

void UFPSREnemySpawnSubsystem::CancelRangedChargesForTransition()
{
	if (!HasServerAuthority())
	{
		return;
	}

	// No snapshot needed (unlike the carry-over below): ServerCancelRangedForStageTransition only mutates the enemy's
	// OWN ranged state — it never releases the actor or touches ActiveEnemies — so iterating the live array is safe.
	// No Cast<> any more (ADR 0013 C1): the ranged FSM is now AFPSREnemyBase's own, not a AFPSRRangedEnemyBase
	// subclass's, so every active enemy is a direct candidate.
	int32 CancelledCount = 0;
	for (const TObjectPtr<AFPSREnemyBase>& EnemyPtr : ActiveEnemies)
	{
		if (AFPSREnemyBase* Enemy = EnemyPtr.Get())
		{
			if (Enemy->ServerCancelRangedForStageTransition())
			{
				++CancelledCount;
			}

			// ADR 0013 후속 행 3 실행 1: an elite carried over here hits NEITHER Activate() nor Deactivate() (it's
			// relocated, not torn down/reused — see ServerRelocateForStageCarry), so its ASC needs this SAME loop as
			// its one teardown-adjacent entry point. Cast is needed here (unlike the ranged FSM above, which lives on
			// the base): the ASC only exists on the elite tier. See AFPSREnemyEliteBase::ServerResetEliteForStageCarry
			// for why in-progress abilities are cancelled but active (Infinite) GEs are deliberately left alone.
			if (AFPSREnemyEliteBase* Elite = Cast<AFPSREnemyEliteBase>(Enemy))
			{
				Elite->ServerResetEliteForStageCarry();
			}
		}
	}

	UE_LOG(LogFPSR, Log, TEXT("[Spawn] CancelRangedChargesForTransition: cancelled %d in-progress ranged charge(s)."),
		CancelledCount);
}

void UFPSREnemySpawnSubsystem::CarryEnemiesToNewStage(const TArray<FVector>& OldPlayerLocs, const TArray<FVector>& NewPlayerLocs, float CarryMaxFraction)
{
	if (!HasServerAuthority())
	{
		return;
	}

	// No delta to carry by (no player actually teleported this swap, or a caller bug pairing mismatched arrays) —
	// fall back to the old behavior rather than guess at a delta. Warning: an all-DBNO/no-pawn transition (every
	// controller skipped PerformSwap's teleport loop) is a legitimate, if rare, run state, not a bug on its own.
	if (OldPlayerLocs.Num() == 0 || NewPlayerLocs.Num() != OldPlayerLocs.Num())
	{
		UE_LOG(LogFPSR, Warning,
			TEXT("[Spawn] CarryEnemiesToNewStage: no player delta to carry by (%d old / %d new loc) — releasing the whole swarm instead."),
			OldPlayerLocs.Num(), NewPlayerLocs.Num());
		ReleaseAllEnemies();
		return;
	}

	const UWorld* World = GetWorld();
	const UFPSRFlowFieldSubsystem* FlowField = World ? World->GetSubsystem<UFPSRFlowFieldSubsystem>() : nullptr;

	// Snapshot BEFORE any release/move — ReleaseEnemy (below) mutates ActiveEnemies, so it can't be walked directly,
	// and MaxCarry must be measured against the count as it stood the instant the carry-over started.
	TArray<AFPSREnemyBase*> Snapshot;
	Snapshot.Reserve(ActiveEnemies.Num());
	for (const TObjectPtr<AFPSREnemyBase>& EnemyPtr : ActiveEnemies)
	{
		if (AFPSREnemyBase* Enemy = EnemyPtr.Get())
		{
			Snapshot.Add(Enemy);
		}
	}
	// RoundToInt, not FloorToInt: authored fractions are not exactly representable (0.7f is 0.69999…), so a floor
	// systematically under-carries by one on clean authored values (0.7 × 10 → 6, not 7 — merge-review finding A1).
	// Round keeps the designer's arithmetic; the 0..Num clamp is implicit (fraction is ClampMin/Max 0..1 in the DA).
	const int32 MaxCarry = FMath::RoundToInt(CarryMaxFraction * Snapshot.Num());

	// Per-enemy candidate (post-delta, pre-snap) position + its rank key. A VALUE struct, not AFPSREnemyBase* — so
	// the Sort below compares plain floats, not enemy pointers (TArray<T*>::Sort dereferences its predicate's
	// arguments — a pointer-typed predicate signature trips a hard-to-diagnose C2664 buried in the engine's sort
	// header rather than here).
	struct FCarryCandidate
	{
		AFPSREnemyBase* Enemy = nullptr;
		FVector CandidateLoc = FVector::ZeroVector;
		float RankDistSq = 0.0f; // squared XY distance from CandidateLoc to the nearest NEW player location
	};
	TArray<FCarryCandidate> Candidates;
	Candidates.Reserve(Snapshot.Num());

	for (AFPSREnemyBase* Enemy : Snapshot)
	{
		const FVector CurrentLoc = Enemy->GetActorLocation();

		// Nearest OLD player location (XY — the same "nearest player" metric TickEnemyMovement's DistSquaredXY
		// already uses) decides whose teleport delta this enemy rides.
		int32 NearestOldIdx = 0;
		float BestOldDistSq = FVector::DistSquaredXY(CurrentLoc, OldPlayerLocs[0]);
		for (int32 i = 1; i < OldPlayerLocs.Num(); ++i)
		{
			const float DistSq = FVector::DistSquaredXY(CurrentLoc, OldPlayerLocs[i]);
			if (DistSq < BestOldDistSq)
			{
				BestOldDistSq = DistSq;
				NearestOldIdx = i;
			}
		}

		const FVector Delta = NewPlayerLocs[NearestOldIdx] - OldPlayerLocs[NearestOldIdx];
		const FVector Candidate = CurrentLoc + Delta;

		float BestNewDistSq = FVector::DistSquaredXY(Candidate, NewPlayerLocs[0]);
		for (int32 i = 1; i < NewPlayerLocs.Num(); ++i)
		{
			BestNewDistSq = FMath::Min(BestNewDistSq, FVector::DistSquaredXY(Candidate, NewPlayerLocs[i]));
		}

		Candidates.Add(FCarryCandidate{ Enemy, Candidate, BestNewDistSq });
	}

	// Nearest-to-a-new-player first; the excess (index >= MaxCarry, once sorted) is released farthest-first (A4).
	Candidates.Sort([](const FCarryCandidate& A, const FCarryCandidate& B) { return A.RankDistSq < B.RankDistSq; });

	int32 CarriedCount = 0;
	int32 SnapFailCount = 0;
	for (int32 i = 0; i < Candidates.Num(); ++i)
	{
		AFPSREnemyBase* Enemy = Candidates[i].Enemy;
		if (i >= MaxCarry)
		{
			ReleaseEnemy(Enemy); // over the carry cap — farthest from a new player, released like ReleaseAllEnemies
			continue;
		}

		FVector SnapLoc;
		// merge-gate P3 교정: UFPSRFlowFieldSubsystem::CarrySnapMaxRadiusCells 하나가 이 반경을
		// UFPSRPickupSubsystem::CarryPickupsToNewStage 와 공유한다 — "젬이 자기를 떨군 적과 같은 반경에
		// 스냅된다"는 계약을 한 곳에서 강제(이전엔 각자 로컬 constexpr 16 을 들고 "kept identical" 주석만 믿었다).
		if (!FlowField || !FlowField->FindNearestOpenLocation(Candidates[i].CandidateLoc, UFPSRFlowFieldSubsystem::CarrySnapMaxRadiusCells, SnapLoc))
		{
			UE_LOG(LogFPSR, Verbose,
				TEXT("[Spawn] CarryEnemiesToNewStage: no open cell within %d cells of %s — releasing %s instead of carrying it."),
				UFPSRFlowFieldSubsystem::CarrySnapMaxRadiusCells, *Candidates[i].CandidateLoc.ToString(), *Enemy->GetName());
			ReleaseEnemy(Enemy);
			++SnapFailCount;
			continue;
		}

		Enemy->ServerRelocateForStageCarry(SnapLoc);
		++CarriedCount;
	}

	UE_LOG(LogFPSR, Log, TEXT("[Spawn] CarryEnemiesToNewStage: carried %d / released %d (cap %d, snap-failed %d)."),
		CarriedCount, Snapshot.Num() - CarriedCount, MaxCarry, SnapFailCount);
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

	// Elite cap accounting (C3) — defensive safety net, same precedent as ReleaseAllEnemies's own
	// RangedChargeCountByPlayer.Reset() (called inside it, just above): ReleaseAllEnemies already decremented this
	// back to 0 via ReleaseEnemy for every currently-active elite, so this line is normally redundant. Kept
	// explicit anyway so a future teardown path added without wiring into ActiveEliteCount's accounting can't
	// leave the counter stuck above 0 across a same-world re-run.
	ActiveEliteCount = 0;

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
	TEXT("Burst-spawn N test enemies via the pool in a ring around the local player. Radius (cm) is optional — a far "
	     "ring (e.g. 6000) keeps the converging swarm visible in front of the camera for render measurements, where "
	     "the default close ring collapses onto the player/camera within seconds. Usage: FPSR.SpawnEnemies [count] [radius=600]"),
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

		// Clamp to the pool hard cap — AcquireEnemy stops there anyway, but an absurd count (e.g. a typo'd 2000000000)
		// must not spin this loop through millions of guaranteed-null acquires.
		int32 Count = 5;
		if (Args.Num() > 0)
		{
			Count = FMath::Clamp(FCString::Atoi(*Args[0]), 1, UFPSREnemySpawnSubsystem::MaxActiveEnemies);
		}
		float Radius = 600.0f;
		if (Args.Num() > 1)
		{
			Radius = FMath::Max(100.0f, FCString::Atof(*Args[1]));
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
			const FVector Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 100.0f);
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
