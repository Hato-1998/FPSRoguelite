// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Enemy/FPSREnemyBase.h" // CancelRangedChargesForTransition -> ServerCancelRangedForStageTransition (ADR 0013 C1: promoted here from the retired AFPSRRangedEnemyBase)
#include "Enemy/FPSREnemyEliteBase.h" // CancelRangedChargesForTransition -> ServerResetEliteForStageCarry (ADR 0013 후속 행 3 실행 1 — 같은 루프에 얹는다)
#include "AbilitySystemComponent.h" // FPSR.EliteDump: reads the elite ASC's live spec/GE/tag counts (debug only)
#include "AbilitySystem/FPSRAbilitySystemComponent.h" // GASM1 FPSR.Debug.ASCDump (Docs/Specs/GASM1_SwarmASCCostMeasurement.md §12-A)
#include "AbilitySystem/Attributes/FPSRMeasureAttributeSet.h" // GASM1 ASCDump: UClass::GetStructureSize() instance accounting
#include "AbilitySystem/Abilities/FPSRMeasureDummyAbility.h" // GASM1 ASCDump: same
#include "Enemy/FPSREnemySpawnPoint.h"
#include "Enemy/FPSRSpawnRoom.h"
#include "Enemy/FPSRFlowFieldSubsystem.h"
#include "Enemy/FPSRFlowFieldComputer.h" // EFPSRFieldQuery (front-chase distance status, U P-D)
#include "Enemy/FPSREnemyAllocator.h"
#include "Enemy/FPSREnemyRosterDataAsset.h"
#include "Enemy/FPSREnemyHealthComponent.h" // VIT1: InitializeVitals at Acquire time
#include "Combat/FPSRVitalsProfile.h" // VIT1: FFPSRResolvedVitals::Resolve(profile x deck)
#include "Combat/FPSRCombatStatics.h" // STAT1 C2: FPSRCombat::ApplyDamage — the DoT batch pass's damage bridge
#include "Weapon/FPSRWeaponFragment.h" // STAT1 C2: FPSRWeaponHooks::NotifyStatusKill + FFPSRFireContext
#include "Settings/FPSRStatusEffectSettings.h" // STAT1 C2: UFPSRStatusEffectSettings::ResolveCatalog
#include "Run/FPSRRunScheduleDataAsset.h" // C3: EvalStageAt(...).MaxEliteAlive — AcquireEnemy's elite-cap gate
#include "Hero/FPSRCharacter.h"
#include "Core/FPSRLogChannels.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"
#include "Combat/FPSRTargeting.h"
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

	// 이 패스에서 월드 밖으로 떨어져 회수된 적 수 — 루프 끝에서 1줄로 집계 로그를 낸다(KillZRecycleCount 참조).
	int32 KillZRecycledThisPass = 0;

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
		// STAT1 §6 진행·조합 (C2단계): same !bFrozen gate as SweepDyingEnemies right above (프리즈·전환에는 안
		// 돈다), placed AHEAD of the ActiveEnemies==0 / PlayerPawns early-returns below so status still progresses
		// through a full-DBNO window (§10 월드 14) and doesn't need the Agents/Locations movement scratch built
		// further down this pass. Iterates StatusActiveEnemies ONLY — never ActiveEnemies in full — so its cost is
		// O(감염된 적), not O(alive) (제1원리, 액터당 비용 최소화).
		AdvanceStatusEffects();
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
				// The eligibility rule itself now lives in FPSRTargeting::IsEligibleTarget (BOSS1) so the boss's
				// patterns apply exactly the same one — the two checks it folds (alive, topology ack) and their
				// fail-open handling of a missing PlayerState are documented there. Only the RULE moved: this loop
				// still makes a single pass and allocates nothing.
				if (!FPSRTargeting::IsEligibleTarget(PC, Now, bUnified))
				{
					continue;
				}
				const AFPSRPlayerState* PS = PC->GetPlayerState<AFPSRPlayerState>();
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
		if (BestDistSq <= FPSREnemyTuning::SignificanceS0RadiusSq)      { UpdateStride = 1; AttackStride = 1; NetFreq = 30.0f; }
		else if (BestDistSq <= FPSREnemyTuning::SignificanceS1RadiusSq) { UpdateStride = 2; AttackStride = 1; NetFreq = 10.0f; }
		else if (BestDistSq <= FPSREnemyTuning::SignificanceS2RadiusSq) { UpdateStride = 4; AttackStride = 2; NetFreq = 5.0f;  }
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
			++KillZRecycledThisPass;
		}
	}

	// 무음 금지. 이 회수가 로그 없이 돌던 탓에, 스폰 좌표 아래에 바닥이 없어 공중에 태어난 적이 조용히
	// 사라졌고 그 손실이 성능 측정의 적 수를 말없이 깎았다(2026-08-30 M0 베이스라인: 요청 300 → 정상 스폰
	// 300 → 4초 뒤 260). 패스당 1줄이라 상시 낙하 지형에서도 프레임당 1줄을 넘지 않는다.
	if (KillZRecycledThisPass > 0)
	{
		KillZRecycleCount += KillZRecycledThisPass;
		UE_LOG(LogFPSR, Warning,
			TEXT("[Spawn] Kill-Z recycle: %d enemy(ies) fell out of the world this pass (cumulative %d). Their spawn "
			     "location had no static floor beneath it — check the spawn ring radius / spawn point placement."),
			KillZRecycledThisPass, KillZRecycleCount);
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

// GASM1(Docs/Specs/GASM1_SwarmASCCostMeasurement.md §5-A) — 파일 로컬 static. 기본값 off("") = 프로덕션
// 경로 diff 0. #if !UE_BUILD_SHIPPING 로 가드하지 않는다 — AcquireEnemy 의 소비 분기(아래)는 이미 로드된
// EnemyRoster 데이터를 이름으로 훑을 뿐, §5-C 의 #if !UE_BUILD_SHIPPING 멤버/UCLASS 인스턴스화와는 무관하다
// (작업 지시 §1 의 "사용처(CVar·부착·부여·구동)" 가드는 AttachASC/MeasureLoadout 쪽 이야기 — 그 둘만이
// 측정 UCLASS 3종의 실제 인스턴스화를 게이트한다).
static TAutoConsoleVariable<FString> CVarForceSpawnClass(
	TEXT("FPSR.Debug.ForceSpawnClass"), TEXT(""),
	TEXT("AcquireEnemy 의 로스터 가중추첨을 무시하고, 로스터 규칙 중 클래스 이름이 일치하는 것을 쓴다. "
	     "BP 클래스는 _C 접미가 붙는다 (예: BP_EnemyRangedBase_C). 불일치 시 경고 1회 후 로스터 기본 동작."),
	ECVF_Cheat);

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

	// GASM1 §5-A/§6 — CVarForceSpawnClass 가 켜져 있으면 위 가중추첨을 완전히 우회하고, 로스터 "안"에서
	// 이름이 일치하는 규칙을 직접 골라 쓴다(에셋 경로 하드코딩 금지 — 새 로드 없이 이미 로드된 로스터
	// 데이터만 이름으로 훑는다). 불일치/로스터 미설정이면 경고 1회(edge-triggered, 아래 elite-cap 블로킹
	// 로그와 같은 관용구) 후 평소처럼 로스터 기본 동작으로 흘러간다.
	const FString ForceSpawnClassName = CVarForceSpawnClass.GetValueOnGameThread();
	if (!ForceSpawnClassName.IsEmpty() && EnemyRoster)
	{
		for (const TObjectPtr<UFPSREnemySpawnRule>& RulePtr : EnemyRoster->SpawnRules)
		{
			const UFPSREnemySpawnRule* Rule = RulePtr;
			const TSubclassOf<AFPSREnemyBase> RuleClass = Rule ? Rule->GetEnemyClass() : nullptr;
			if (RuleClass && RuleClass->GetName().Equals(ForceSpawnClassName, ESearchCase::IgnoreCase))
			{
				PickedClass = RuleClass;
				break;
			}
		}
		if (PickedClass)
		{
			bForceSpawnClassMismatchWarned = false; // 다음번 진짜 불일치를 위해 래치 해제
		}
		else if (!bForceSpawnClassMismatchWarned)
		{
			bForceSpawnClassMismatchWarned = true;
			UE_LOG(LogFPSR, Warning,
				TEXT("[Spawn] FPSR.Debug.ForceSpawnClass='%s' matched no EnemyRoster rule — falling back to the ")
				TEXT("roster's normal weighted pick. BP classes need the _C suffix (e.g. BP_EnemyRangedBase_C)."),
				*ForceSpawnClassName);
		}
	}
	else
	{
		bForceSpawnClassMismatchWarned = false; // CVar 비었거나 로스터 미설정 — 래치 해제
	}

	if (!PickedClass && EnemyRoster)
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
			// Edge-triggered, NOT per-attempt: the director retries every fill pass, so an unconditional log here
			// would spam a saturated run. Logging only the transition still answers the one question a PIE smoke
			// actually asks ("is the effective cap the number I authored, and is it binding right now?") — without
			// it a blocked elite spawn is completely silent, and a mis-authored MaxEliteAlive is indistinguishable
			// from "elites just haven't been rolled yet" (G2 merge-gate P3).
			if (!bEliteCapBlocking)
			{
				bEliteCapBlocking = true;
				UE_LOG(LogFPSR, Log,
					TEXT("[Spawn] Elite cap BINDING: %d/%d alive (stage %d, curve %d, hard cap %d) — elite spawns deferred."),
					ActiveEliteCount, EffectiveEliteCap, CurrentStageIndex, CurveCap, EliteHardCap);
			}
			return nullptr;
		}
		if (bEliteCapBlocking)
		{
			bEliteCapBlocking = false;
			UE_LOG(LogFPSR, Log, TEXT("[Spawn] Elite cap released: %d/%d alive."), ActiveEliteCount, EffectiveEliteCap);
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
			// C4 「구현 사양 B」 — 풀 기아(starvation) 해소: TotalSpawned 는 증가만 하고 클래스 무관 총량이라
			// (FFPSREnemyDormantPool 의 클래스 주석 참조), 전반 스테이지가 클래스 A 로 캡을 채우면 후반의
			// 클래스 B(엘리트 등) 요청은 버킷 미스 + 캡 도달로 영구 거부됐다 — 적을 잡아도 정원은 안 빈다
			// (죽은 적은 자기 클래스 버킷으로 돌아갈 뿐이다). 수요 기반 축출로 해소: 가장 큰 **다른** 클래스
			// 버킷에서 휴면체 1개를 꺼내 Destroy() 하고 그만큼 TotalSpawned 를 되돌려 정상 스폰 경로로
			// 진행한다. acquire 당 최대 1개(유계) — while 이 아니라 if.
			//
			// 안전 확인(검증됨) — 휴면체는 ActiveEnemies·DyingEnemies 어디에도 없다(ReleaseEnemy/
			// FinishDyingEnemy 모두 DormantPool.Add 전에 그 두 컨테이너에서 먼저 뺀다). AFPSREnemyBase::EndPlay
			// 가 메트릭 레지스트리(UFPSREnemyMetricsSubsystem)와 코스메틱 LOD 레지스트리(UFPSREnemyCosmeticLOD
			// Subsystem) 양쪽에서 스스로 Unregister 하므로 Destroy() 가 그 두 등록을 대신 정리해 줄 필요가
			// 없다. AcquireOfClass 는 이미 무효 슬롯을 관용한다(위 참조). 따라서 Destroy() 대상은 무참조다.
			//
			// ⚠️ 불변식 5("적은 Destroy 하지 않고 풀에 반납한다")의 예외 개정 — 그 불변식은 사망·teardown
			// 경로의 계약이고, 이 축출은 휴면 풀 거주자를 다른 클래스의 acquire 를 위해 재활용하는 것이라
			// 그 문면 밖이다(ADR 0013 은 C6 에서 갱신 예정 — FFPSREnemyDormantPool 의 클래스 주석도 참조).
			if (AFPSREnemyBase* Evicted = DormantPool.EvictOneFromLargestOtherBucket(ClassToSpawn))
			{
				UClass* EvictedClass = Evicted->GetClass();
				Evicted->Destroy();
				--TotalSpawned;
				++EvictionCount;
				// 상시 로그(디버그용 아님) — 교번 클래스 수요에서 축출이 상시화되면 사실상 풀링이 무효화되므로
				// 보이게 해야 한다(사양서 요구).
				UE_LOG(LogFPSR, Warning,
					TEXT("[Spawn] Pool starvation: evicted 1 dormant %s to make room for %s (TotalSpawned %d/%d, EvictionCount %d total)."),
					*EvictedClass->GetName(), *ClassToSpawn->GetName(), TotalSpawned, MaxActiveEnemies, EvictionCount);
			}
			else
			{
				return nullptr;
			}
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

	// VIT1 §8: resolve this archetype's survival spec (its own VitalsProfile x this roster's deck multiplier) and
	// bake it into the health component. MUST run AFTER Activate() — Activate() calls HealthComponent::ResetForReuse()
	// internally, which fills the pool from the STALE MaxHealth (a prior life's regime, or the BP editor default on
	// a fresh spawn); InitializeVitals below overwrites it with the freshly-resolved spec. The spawn subsystem is the
	// one place that knows both the enemy's own profile AND the active deck, so the fold happens here, once, rather
	// than re-resolved on every hit.
	if (UFPSREnemyHealthComponent* EnemyHealthComp = Enemy->GetHealthComponent())
	{
		const FFPSRVitalsDeckModifier Deck = EnemyRoster ? EnemyRoster->VitalsModifier : FFPSRVitalsDeckModifier();
		const FFPSRResolvedVitals Resolved = FFPSRResolvedVitals::Resolve(Enemy->GetVitalsProfile(), Deck, EnemyHealthComp->GetMaxHealth());
		EnemyHealthComp->InitializeVitals(Resolved);
	}

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
	// STAT1 §5-6: the swarm/elite half of the status-progression driver gate — turned ON the INSTANT this enemy
	// joins ActiveEnemies (the set AdvanceStatusEffects/TickEnemyMovement iterate), turned OFF at both
	// ActiveEnemies.Remove sites below (ReleaseEnemy / BeginDying) so a status bit can never land on an actor
	// nothing will ever advance again.
	if (UFPSREnemyHealthComponent* EnemyHealth = Enemy->GetHealthComponent())
	{
		EnemyHealth->SetStatusDriverPresent(true);
	}
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
	// STAT1 §5-6/§6: driver OFF + leave the status compact list — see AcquireEnemy's own comment for the pairing.
	// Both are idempotent (SetStatusDriverPresent is a plain flag write; TSet::Remove on an absent key is a
	// documented safe no-op), so this costs nothing extra for the common case of an enemy that was never infected.
	if (UFPSREnemyHealthComponent* EnemyHealth = Enemy->GetHealthComponent())
	{
		EnemyHealth->SetStatusDriverPresent(false);
	}
	StatusActiveEnemies.Remove(Enemy);
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
	// STAT1 §5-6/§6: driver OFF + leave the status compact list — the DEATH path's own removal site, paired with
	// ReleaseEnemy's (see AcquireEnemy's own comment). Must run BEFORE EnterDyingState below, which separately
	// closes this enemy's status VALUES (§7-6 closure point) — that call needs no driver-flag help from here, but
	// the ORDER matters conceptually: the driver that would have advanced this corpse's status is gone the instant
	// it leaves ActiveEnemies, exactly like every other "may move/attack this pass" privilege BeginDying revokes here.
	if (UFPSREnemyHealthComponent* EnemyHealth = Enemy->GetHealthComponent())
	{
		EnemyHealth->SetStatusDriverPresent(false);
	}
	StatusActiveEnemies.Remove(Enemy);
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

void UFPSREnemySpawnSubsystem::RegisterStatusActive(AFPSREnemyBase* Enemy)
{
	if (!Enemy || !HasServerAuthority())
	{
		return;
	}
	// A TSet, not a TArray: idempotent by construction, which is what lets every successful ApplyStatus call this
	// (not just a tracked "first ever" one) — see this method's own header comment for why that is observationally
	// the same as "register on first apply".
	StatusActiveEnemies.Add(Enemy);
}

void UFPSREnemySpawnSubsystem::AdvanceStatusEffects()
{
	if (StatusActiveEnemies.Num() == 0)
	{
		return; // O(0) — the common case once nothing on the field is infected (§6 압축 리스트의 비용 계약).
	}

	const UFPSRStatusCatalogDataAsset* Catalog = UFPSRStatusEffectSettings::ResolveCatalog();
	UWorld* World = GetWorld();

	// Snapshot before iterating: a DoT kill dealt below can synchronously reach BeginDying (HealthComponent::
	// ApplyDamage -> OnDeath -> ... -> BeginDying), which removes THAT SAME enemy from StatusActiveEnemies mid-walk
	// — ranging over the live TSet while this loop's own body can mutate it is the classic "modify container while
	// iterating" hazard. Uses the MEMBER scratch (Reset keeps capacity) rather than a local inline array: a wide-AoE
	// status build can infect a large fraction of the field, and an inline budget that spills would then heap-
	// allocate EVERY frame — the same reason the movement pass below keeps its own scratch as members (W1 P2-4).
	TArray<AFPSREnemyBase*>& Snapshot = StatusStepScratch;
	Snapshot.Reset(StatusActiveEnemies.Num());
	for (const TObjectPtr<AFPSREnemyBase>& EnemyPtr : StatusActiveEnemies)
	{
		if (AFPSREnemyBase* Enemy = EnemyPtr.Get())
		{
			Snapshot.Add(Enemy);
		}
	}

	for (AFPSREnemyBase* Enemy : Snapshot)
	{
		UFPSREnemyHealthComponent* HealthComp = IsValid(Enemy) ? Enemy->GetHealthComponent() : nullptr;
		if (!HealthComp)
		{
			StatusActiveEnemies.Remove(Enemy); // stale/invalid entry (defensive — see BeginDying/ReleaseEnemy, which
			continue;                          // should already have removed any enemy that reaches this state).
		}

		// STAT1 §6 저항 행: the SAME profile ApplyDamage already mitigates this target's damage against — status
		// resist and damage mitigation can never disagree. Null -> 1.0/1.0, never 0 (a 0 fallback would make every
		// enemy without an authored profile completely status-immune).
		const UFPSRVitalsProfileDataAsset* Profile = HealthComp->GetVitalsProfile();
		const float WeakResist = Profile ? Profile->WeakResistScale : 1.0f;
		const float StrongResist = Profile ? Profile->StrongResistScale : 1.0f;

		float DotDamage = 0.0f;
		AActor* DotInstigator = nullptr;
		UFPSRWeaponInstance* DotSourceWeapon = nullptr;
		TArray<uint8, TInlineAllocator<8>> Expired;
		TArray<uint8, TInlineAllocator<8>> Fired;
		HealthComp->AdvanceStatus(Catalog, WeakResist, StrongResist, DotDamage, DotInstigator, DotSourceWeapon, Expired, Fired);

		if (DotDamage > 0.0f)
		{
			FFPSRDamageSpec Spec;
			// STAT1 §6 도트 행: suppress the lifesteal ability-activation trigger (240 infected enemies x a 0.5s
			// tick would otherwise storm one player's ASC with TryActivateAbility calls every second — see
			// FFPSRDamageSpec::bSuppressDealtDamageEvent's own comment) and BACKDATE the shield-regen time anchor
			// rather than freezing it (§6-2 — see bDotRegenAnchorPolicy's own comment for why "freeze" alone lets a
			// delayed-regen shield compound back to full while still being hit every tick).
			Spec.bSuppressDealtDamageEvent = true;
			Spec.bDotRegenAnchorPolicy = true;
			// Spec.DamageType stays the empty/default tag (무속성) — STAT1 §6/G2-J: the profile validator only
			// blocks tags that do NOT start with "DamageType.", so an authored DamageType.Status was never actually
			// blocked; this unit simply doesn't attribute a damage TYPE to a status DoT (§2 비목표).
			const FPSRCombat::FDamageResult Result = FPSRCombat::ApplyDamage(Enemy, DotDamage, DotInstigator, Spec);
			if (Result.bKilled)
			{
				// STAT1 §6 킬 시임: rebuild a minimal FireContext from the status's own stored weak refs — the SAME
				// "the live context is long gone" precedent FPSRProjectile.cpp's MakeProjectileFireContext uses for
				// a delayed projectile kill (NotifyStatusKill's own header comment). A stale/expired DotSourceWeapon
				// resolves to a null Instance, which NotifyStatusKill's own guard turns into a quiet no-op — the DoT
				// itself still killed the enemy either way (G1-15).
				FFPSRFireContext KillCtx;
				KillCtx.Avatar = Cast<APawn>(DotInstigator);
				KillCtx.Controller = KillCtx.Avatar ? KillCtx.Avatar->GetController() : nullptr;
				KillCtx.World = World;
				KillCtx.Instance = DotSourceWeapon;
				KillCtx.ShotCount = 1;
				KillCtx.bAuthority = true; // this whole pass already runs inside TickEnemyMovement's own HasServerAuthority() gate
				FPSRWeaponHooks::NotifyStatusKill(KillCtx, Enemy);
			}
		}

		// §6 압축 리스트: leave the list once none of the 8 slots are still set. HasStatus(0..7) is used rather than
		// a new aggregate accessor — §7-7 caps UFPSREnemyHealthComponent's public surface at 6 entry points, and
		// HasStatus is already one of them; 8 cheap bit tests cost nothing next to the ApplyDamage call this same
		// iteration may already have paid for.
		bool bStillInfected = false;
		for (uint8 Slot = 0; Slot < 8; ++Slot)
		{
			if (HealthComp->HasStatus(Slot))
			{
				bStillInfected = true;
				break;
			}
		}
		if (!bStillInfected)
		{
			StatusActiveEnemies.Remove(Enemy);
		}
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
	bEliteCapBlocking = false; // diagnostics latch — see its declaration; kept in lockstep with the counter above

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
void UFPSREnemySpawnSubsystem::DumpEliteState() const
{
	// Effective cap, recomputed exactly the way AcquireEnemy's gate does it — printing the two inputs separately
	// (curve vs hard cap) is the point: "cap is 8" alone never says whether the schedule was authored at all.
	int32 CurrentStageIndex = 0;
	if (const UWorld* World = GetWorld())
	{
		if (const AFPSRGameState* GameState = World->GetGameState<AFPSRGameState>())
		{
			CurrentStageIndex = GameState->GetStageIndex();
		}
	}
	const int32 CurveCap = ActiveSchedule
		? UFPSRRunScheduleDataAsset::EvalStageAt(ActiveSchedule->StageDifficulty, CurrentStageIndex).MaxEliteAlive
		: 0;
	const int32 EffectiveEliteCap = FMath::Min(CurveCap, EliteHardCap);

	UE_LOG(LogFPSR, Log, TEXT("===== FPSR.EliteDump ====="));
	UE_LOG(LogFPSR, Log, TEXT("  stage %d | elite cap: effective %d = min(curve %d, hard %d)%s"),
		CurrentStageIndex, EffectiveEliteCap, CurveCap, EliteHardCap,
		ActiveSchedule ? TEXT("") : TEXT("  <-- NO SCHEDULE PUSHED (curve reads 0: no elite can ever spawn)"));

	int32 CountedElites = 0;
	for (const TObjectPtr<AFPSREnemyBase>& EnemyPtr : ActiveEnemies)
	{
		const AFPSREnemyEliteBase* Elite = Cast<AFPSREnemyEliteBase>(EnemyPtr.Get());
		if (!Elite)
		{
			continue;
		}
		++CountedElites;

		const UAbilitySystemComponent* ASC = Elite->GetAbilitySystemComponent();
		if (!ASC)
		{
			UE_LOG(LogFPSR, Warning, TEXT("  [%d] %s — NO ASC (elite without an ability system: construction bug)"),
				CountedElites, *Elite->GetName());
			continue;
		}

		FGameplayTagContainer OwnedTags;
		ASC->GetOwnedGameplayTags(OwnedTags);
		// GrantedAbilities is the authored expectation; the live spec count must equal it on EVERY life. Anything
		// higher means a pool reuse re-granted without clearing (the accumulation this command exists to catch).
		UE_LOG(LogFPSR, Log, TEXT("  [%d] %s | abilities %d (authored %d)%s | active GEs %d | owned tags %d %s"),
			CountedElites, *Elite->GetName(),
			ASC->GetActivatableAbilities().Num(), Elite->GetGrantedAbilityCount(),
			(ASC->GetActivatableAbilities().Num() > Elite->GetGrantedAbilityCount())
				? TEXT("  <-- ACCUMULATING (Activate did not clear the previous life)") : TEXT(""),
			ASC->GetNumActiveGameplayEffects(),
			OwnedTags.Num(), OwnedTags.Num() > 0 ? *OwnedTags.ToStringSimple() : TEXT(""));
	}

	// The accounting cross-check. ActiveEliteCount is incremented in one place and decremented in two; a missed
	// decrement otherwise surfaces only as "elites gradually stop spawning" long after the cause.
	UE_LOG(LogFPSR, Log, TEXT("  ActiveEliteCount %d vs elites actually in ActiveEnemies %d  %s"),
		ActiveEliteCount, CountedElites,
		(ActiveEliteCount == CountedElites) ? TEXT("OK")
			: TEXT("<-- MISMATCH: the counter leaked (see BeginDying / ReleaseEnemy decrements)"));
	UE_LOG(LogFPSR, Log, TEXT("  pool: alive %d | dormant %d | TotalSpawned %d/%d | evictions %d"),
		ActiveEnemies.Num(), DormantPool.Num(), TotalSpawned, MaxActiveEnemies, EvictionCount);
}

static FAutoConsoleCommandWithWorldAndArgs GFPSREliteDumpCmd(
	TEXT("FPSR.EliteDump"),
	TEXT("Log every active elite's ASC state (granted abilities vs authored, active GEs, owned tags) plus the elite "
	     "cap inputs and the ActiveEliteCount cross-check. Kill+respawn an elite a few times and watch the ability "
	     "count stay flat — a growing count means pool reuse is not clearing the previous life."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		if (const UFPSREnemySpawnSubsystem* Sub = World->GetSubsystem<UFPSREnemySpawnSubsystem>())
		{
			Sub->DumpEliteState();
		}
	}));

void UFPSREnemySpawnSubsystem::DumpMeasureASCState() const
{
	UE_LOG(LogFPSR, Log, TEXT("===== FPSR.Debug.ASCDump (GASM1) ====="));

	int32 CountWithASC = 0;
	int32 CountWithLoadout = 0;
	int32 TotalASCInstances = 0; // 🔁 G2 P2-3 — GetComponents<UAbilitySystemComponent> 실측 합계(§12 #3)
	int32 TotalActivations = 0; // 300마리 x 장시간 캡처를 감안해도 int32 범위(~21억)에 전혀 안 닿는다

	for (const TObjectPtr<AFPSREnemyBase>& EnemyPtr : ActiveEnemies)
	{
		const AFPSREnemyBase* Enemy = EnemyPtr.Get();
		const UAbilitySystemComponent* ASC = Enemy ? Enemy->GetMeasureAbilitySystemComponent() : nullptr;
		if (!ASC)
		{
			continue; // 구성 ①(측정 미부착) — 찍을 것이 없다
		}
		++CountWithASC;

		// 🔁 G2 P2-3 — CountWithASC 는 "액터당 1행"이라 이중부착을 실증하지 못한다(Activate 의
		// if (!MeasureASC) 가드를 지워도 이 값은 그대로 통과한다). 액터가 실제로 들고 있는
		// UAbilitySystemComponent 컴포넌트 수를 직접 세어(멱등하면 항상 1), 1 을 넘으면 경고를 찍고,
		// 최종 합계(TotalASCInstances)를 CountWithASC 와 나란히 로그로 남겨 눈으로 대조할 수 있게 한다
		// (§12 검증기준 #3 — "N 이 그 시점 생존 수와 일치해야 한다").
		TArray<UAbilitySystemComponent*> FoundMeasureASCs;
		Enemy->GetComponents<UAbilitySystemComponent>(FoundMeasureASCs);
		TotalASCInstances += FoundMeasureASCs.Num();
		if (FoundMeasureASCs.Num() > 1)
		{
			UE_LOG(LogFPSR, Warning,
				TEXT("[GASM1] %s has %d UAbilitySystemComponent instances attached — measure-ASC idempotency ")
				TEXT("guard may have failed (expected 1, §12 검증기준 #3)."),
				*Enemy->GetName(), FoundMeasureASCs.Num());
		}

		const bool bLoadout = Enemy->HasMeasureLoadout();
		if (bLoadout)
		{
			++CountWithLoadout;
		}
		TotalActivations += Enemy->GetMeasureActivationCount();

		FGameplayTagContainer OwnedTags;
		ASC->GetOwnedGameplayTags(OwnedTags);
		UE_LOG(LogFPSR, Log, TEXT("  [%d] %s | loadout=%d | abilities %d | active GEs %d | activations %d | owned tags %d"),
			CountWithASC, *Enemy->GetName(), bLoadout ? 1 : 0,
			ASC->GetActivatableAbilities().Num(), ASC->GetNumActiveGameplayEffects(),
			Enemy->GetMeasureActivationCount(), OwnedTags.Num());
	}

	// §12-A 메모리 계측 규칙 "1차" — UClass::GetStructureSize() x 인스턴스 수. obj list 의 IncNum/ResExc
	// ("2차")와 obj list 가 세지 않는 객체 본체를 이 값이 메운다(UnrealEngine.cpp:9463-9490 — Serialize 는
	// 카운팅 아카이브에 본체를 더하지 않는다).
	const int32 ASCStructSize = UFPSRAbilitySystemComponent::StaticClass()->GetStructureSize();
	const int32 SetStructSize = UFPSRMeasureAttributeSet::StaticClass()->GetStructureSize();
	const int32 AbilityStructSize = UFPSRMeasureDummyAbility::StaticClass()->GetStructureSize();
	UE_LOG(LogFPSR, Log,
		TEXT("  instances: ASC actors=%d components=%d (%s — GetComponents<UAbilitySystemComponent> 실측, ")
		TEXT("멱등하면 둘이 같아야 한다) x %dB = %dB | AttributeSet %d x %dB = %dB | DummyAbility %d x %dB = %dB ")
		TEXT("(DummyAbility=InstancedPerActor, 로드아웃당 1개)"),
		CountWithASC, TotalASCInstances, (CountWithASC == TotalASCInstances) ? TEXT("일치") : TEXT("불일치!!"),
		ASCStructSize, TotalASCInstances * ASCStructSize,
		CountWithLoadout, SetStructSize, CountWithLoadout * SetStructSize,
		CountWithLoadout, AbilityStructSize, CountWithLoadout * AbilityStructSize);
	UE_LOG(LogFPSR, Log, TEXT("  total measure-ability activations (live, ActiveEnemies only) %d"), TotalActivations);

	// §8/비목표 — "휴면 풀 ASC 상시 틱" 결함은 고치지 않는다(별도 행). ASC 는 한 번 붙으면 해제되지 않으므로
	// (§8) 이 총계는 "출시 코드와 다름"을 기록하는 참고치다: DormantPool 은 클래스별 버킷(private
	// BucketsByClass, FFPSREnemyDormantPool)이라 이 서브시스템도 개별 휴면 액터를 순회해 몇 개가 측정 ASC 를
	// 달고 있는지는 셀 수 없다(그 반복 API 자체가 없다 — GASM1 은 건드리지 않는다) — 휴면 총수만 참고로
	// 남긴다. ActiveEnemies 쪽 총계가 이 캡처 시점의 측정 ASC 실측 하한이다.
	UE_LOG(LogFPSR, Log,
		TEXT("  dormant pool total %d (ASC attachment is never released, GASM1 §8 — per-entry breakdown not ")
		TEXT("available: FFPSREnemyDormantPool exposes no iteration API)"),
		DormantPool.Num());
}

static FAutoConsoleCommandWithWorldAndArgs GFPSRMeasureASCDumpCmd(
	TEXT("FPSR.Debug.ASCDump"),
	TEXT("GASM1: log the swarm's measurement-ASC footprint (per-actor rows + UClass::GetStructureSize() instance "
	     "accounting + dormant pool total). See Docs/Specs/GASM1_SwarmASCCostMeasurement.md §12-A."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		if (const UFPSREnemySpawnSubsystem* Sub = World->GetSubsystem<UFPSREnemySpawnSubsystem>())
		{
			Sub->DumpMeasureASCState();
		}
	}));

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

		// Spawn in a ring pattern. AcquireEnemy REJECTS an attempt (returns null) at three gates, and this loop used
		// to swallow that null and silently under-deliver. The binding one in practice is the ELITE CAP: whenever the
		// current stage authors no MaxEliteAlive, the effective cap is 0, so every elite the roster rolls is refused —
		// and because that gate's log is edge-triggered, the director's own first fill pass trips it one frame before
		// this command runs, leaving every rejection here completely silent. Measured 2026-08-28: `300 6000` delivered
		// 225/226 and `500 6000` delivered 394/398, i.e. short by exactly the roster's elite weight share, with no
		// failure logged anywhere. A frame-budget judgment line defined at "적 300" cannot be measured at 225, so:
		//   1. retry a rejected attempt until the requested count is met, and
		//   2. always report what was actually delivered, with its composition.
		// The retry is BOUNDED because two of the three gates (pool exhausted, SpawnActor failure) are deterministic —
		// they never succeed on retry, and the budget stops the game thread spinning on them. A rejected attempt
		// returns before it touches the pool, so even a fully consumed budget is cheap.
		const int32 MaxAttempts = Count * 32 + 128;
		int32 Spawned = 0;
		int32 Attempts = 0;
		TMap<const UClass*, int32> SpawnedByClass;
		while (Spawned < Count && Attempts < MaxAttempts)
		{
			++Attempts;
			// Angle from the SPAWNED index, not the attempt index, so the delivered ring stays evenly spaced.
			const float Angle = (2.0f * PI * Spawned) / FMath::Max(1, Count);
			const FVector Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 100.0f);
			if (const AFPSREnemyBase* Enemy = Sub->AcquireEnemy(Center + Offset))
			{
				++Spawned;
				++SpawnedByClass.FindOrAdd(Enemy->GetClass());
			}
		}

		// Always report — measurement provenance. A composition line in game.log is what makes a shortfall like the
		// 2026-08-28 one self-evident while the run is still in front of you, instead of an unexplained number later.
		// Sorted by class name so two runs' lines diff cleanly (TMap iteration order is not stable).
		TArray<FString> Parts;
		Parts.Reserve(SpawnedByClass.Num());
		for (const TPair<const UClass*, int32>& Pair : SpawnedByClass)
		{
			Parts.Add(FString::Printf(TEXT("%s x%d"), *Pair.Key->GetName(), Pair.Value));
		}
		Parts.Sort();
		const FString Composition = Parts.Num() > 0 ? FString::Join(Parts, TEXT(", ")) : TEXT("(none)");
		if (Spawned < Count)
		{
			UE_LOG(LogFPSR, Warning,
				TEXT("[Spawn] FPSR.SpawnEnemies: requested %d, delivered %d in %d attempts — SHORT BY %d. Every remaining "
				     "attempt was rejected by AcquireEnemy (elite cap / pool exhausted / spawn failure). Composition: %s"),
				Count, Spawned, Attempts, Count - Spawned, *Composition);
		}
		else
		{
			UE_LOG(LogFPSR, Log, TEXT("[Spawn] FPSR.SpawnEnemies: delivered %d/%d in %d attempts. Composition: %s"),
				Spawned, Count, Attempts, *Composition);
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

bool UFPSREnemySpawnSubsystem::GetLastGroundedZ(const AFPSRCharacter* Player, float& OutZ) const
{
	if (!Player)
	{
		return false;
	}
	// The const_cast is a KEY-CONSTRUCTION requirement, not a mutation: TWeakObjectPtr's pointer constructor is
	// constrained by UE_REQUIRES(std::is_convertible_v<U, T*>) (WeakObjectPtrTemplates.h), and a const pointer does
	// not satisfy it. The lookup itself reads nothing through the pointer.
	const TWeakObjectPtr<AFPSRCharacter> Key(const_cast<AFPSRCharacter*>(Player));
	if (const float* Found = LastGroundedZByPlayer.Find(Key))
	{
		OutZ = *Found;
		return true;
	}
	return false;
}
