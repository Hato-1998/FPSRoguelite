// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyBase.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Enemy/FPSREnemyAnimProfile.h"
#include "Enemy/FPSREnemyMetricsSubsystem.h" // S4 readability metrics registry (CSV-gated, see below)
#include "Enemy/FPSREnemyShadowLODSubsystem.h" // per-viewer dynamic-shadow band (see BeginPlay/EndPlay)
#include "Enemy/FPSRFlowFieldSubsystem.h" // v2 hover height sampler (CachedFlowField, see BeginPlay/ApplyGravity)
#include "Hero/FPSRCharacter.h"
#include "Pickup/FPSRPickupSubsystem.h"
#include "Core/FPSRLogChannels.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "ProfilingDebugging/CsvProfiler.h" // CSV_PROFILER_STATS gate for the metrics registry calls below
#include "Settings/FPSRPlaceholderVisualSettings.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING
static float GFPSREnemySpeedScale = 1.0f;
static FAutoConsoleVariableRef CVarFPSREnemySpeedScale(
	TEXT("FPSR.Debug.EnemySpeedScale"),
	GFPSREnemySpeedScale,
	TEXT("Playtest multiplier on swarm move speed. Applied at USE, so it affects enemies already on the field.\n"
	     "Pair with FPSR.Debug.PlayerSpeedScale: scaling BOTH by the same factor leaves the chase dynamics\n"
	     "identical and only changes how fast the arena is crossed — which is the point when the arena grows.\n"
	     "Server-authoritative movement, so this only does anything on the host. 1 = off."),
	ECVF_Cheat);
#endif

float AFPSREnemyBase::GetEffectiveMoveSpeed() const
{
#if !UE_BUILD_SHIPPING
	return CurrentMoveSpeed * FMath::Max(0.0f, GFPSREnemySpeedScale);
#else
	return CurrentMoveSpeed;
#endif
}

AFPSREnemyBase::AFPSREnemyBase()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	// Distance net-cull is the sole relevancy lever for the swarm (enemies live in the PERSISTENT, always-level-relevant
	// level, so level visibility never culls them; RepGraph is the separate production fix). This ctor value is the
	// single-map / archetype fallback — in the U unified multimap field the spawn subsystem overrides it per-acquire with a
	// footprint-derived radius (UFPSREnemySpawnSubsystem::ComputeUnifiedNetCullRadius). Boss is separately bAlwaysRelevant.
	// Set via the accessor (raw NetCullDistanceSquared access is UE_DEPRECATED(5.5)).
	SetNetCullDistanceSquared(FMath::Square(NetCullRadius));

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(40.0f, DefaultCapsuleHalfHeight);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Capsule->SetCollisionObjectType(ECC_Pawn);
	Capsule->SetCollisionResponseToAllChannels(ECR_Block);
	// Ignore OTHER enemies (also ECC_Pawn): the swarm overlaps and spreads via soft separation steering instead
	// of mutual physics blocking, which would gridlock a dense crowd and stack co-spawned enemies (Game.MD §1/§5).
	// Walls (WorldStatic), the rifle trace (Visibility) and the player (ECC_FPSRPlayerPawn) stay blocked.
	Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	SetRootComponent(Capsule);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Capsule);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));
	Mesh->SetRelativeScale3D(FVector(0.8f, 0.8f, 1.8f));
	// Placeholder mesh is resolved in BeginPlay from config (Game.md §6-2: no hard-coded asset path in C++). Normal
	// enemy BPs assign their own (VAT) mesh, so the fallback only fires for the raw-C++ spawn (unconfigured roster).

	HealthComponent = CreateDefaultSubobject<UFPSREnemyHealthComponent>(TEXT("HealthComponent"));
}

void AFPSREnemyBase::ApplyNetCullRadius(float RadiusCm)
{
	// U P-H: clamp ONLY against a degenerate (0 / negative / NaN) caller — the gameplay floor (>= weapon range) is applied by
	// the caller's ComputeUnifiedNetCullRadius (single owner), so it's not re-imported here. FMath::Max(NaN, Min) returns Min.
	// Set via the accessor (raw NetCullDistanceSquared access is UE_DEPRECATED(5.5)); the net driver reads it live per pass.
	SetNetCullDistanceSquared(FMath::Square(FMath::Max(RadiusCm, MinNetCullRadiusCm)));
}

void AFPSREnemyBase::BeginPlay()
{
	Super::BeginPlay();

	// U P-H: re-derive the net-cull radius from NetCullRadius now that BP-applied class defaults are live (the ctor ran with
	// the C++ NSDMI default only, so a BP per-archetype override wouldn't otherwise reach NetCullDistanceSquared). Single-map /
	// fallback path. In the unified multi-slot field the spawn subsystem overrides this per-acquire (ApplyNetCullRadius), which
	// runs on every Activate (after BeginPlay) and still wins. No-op vs the ctor when NetCullRadius is unchanged (no regression).
	SetNetCullDistanceSquared(FMath::Square(NetCullRadius));

	// v2 hover height sampler (ApplyGravity): cache the world's flow-field subsystem ONCE — it outlives every pooled
	// enemy (world subsystem lifetime = world lifetime), so this avoids a GetSubsystem<>() lookup per enemy per
	// movement update at swarm scale. Null on a world with no flow field (pre-content) — ApplyGravity falls back to
	// the v1 scene-query path unconditionally in that case.
	if (UWorld* World = GetWorld())
	{
		CachedFlowField = World->GetSubsystem<UFPSRFlowFieldSubsystem>();
	}

	// Fallback placeholder mesh from config only when no content BP mesh was assigned (Game.md §6-2 — no hard-coded
	// path in C++). Normal enemy BPs carry a VAT mesh, so the guard skips the load; only the raw-C++ spawn hits it.
	if (Mesh && Mesh->GetStaticMesh() == nullptr)
	{
		if (const UFPSRPlaceholderVisualSettings* Settings = GetDefault<UFPSRPlaceholderVisualSettings>())
		{
			if (UStaticMesh* PlaceholderMesh = Settings->EnemyPlaceholderMesh.LoadSynchronous())
			{
				Mesh->SetStaticMesh(PlaceholderMesh);
			}
		}
	}

	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddDynamic(this, &AFPSREnemyBase::HandleDeath);
		// Client-side death cosmetic (U20): OnDeathCosmetic fires from OnRep_bDead on clients (the authority drives
		// death cosmetics from its own path — HandleDeath calls HandleDeathCosmetic directly, since OnRep does not
		// run on the server). Enters the Death animation state. Harmless when no AnimProfile is set.
		HealthComponent->OnDeathCosmetic.AddDynamic(this, &AFPSREnemyBase::HandleDeathCosmetic);
	}

	// Per-actor animation phase (0..1) derived from the actor id so pooled enemies don't animate in lockstep (U20).
	AnimPhase = static_cast<float>(GetUniqueID() % 1000) / 1000.0f;

	// Publish the phase ONCE to the CPD contract slot so procedural materials de-lockstep from a STABLE per-actor
	// value. (A position-hash phase inside the material rotates the mesh as the actor MOVES — measured regression.)
	// Written here (BeginPlay runs on every machine with rendering) and never cleared: CPD survives pooling reuse.
	if (Mesh)
	{
		Mesh->SetCustomPrimitiveDataFloat(FPSRVATAnim::CPDSlot_Phase, AnimPhase);
	}

	// Bind the world-space health bar / floating-damage widget to the health component once (server + clients).
	// Pooling-safe: the actor + widget persist across dormancy, so this single bind survives every reuse.
	InitHealthBarWidget();

	// S4 readability metrics: register with the per-client registry (all net modes — the metrics subsystem reads
	// from each LOCAL client's own POV, not just the server). Once per actor lifetime, like the widget bind above
	// (pooled reuse never re-enters BeginPlay — see Deactivate). Compiled out entirely in Shipping (CSV_PROFILER_STATS).
#if CSV_PROFILER_STATS
	if (UWorld* World = GetWorld())
	{
		if (UFPSREnemyMetricsSubsystem* Metrics = World->GetSubsystem<UFPSREnemyMetricsSubsystem>())
		{
			Metrics->RegisterEnemy(this);
		}
	}
#endif

	// Shadow LOD registry — same shape and lifetime as the metrics registration above, and for the same reason: the
	// decision is made from each LOCAL viewer's POV, so this runs on every net mode, not just the server. Absent on a
	// dedicated server (and when the feature is off), where GetSubsystem returns null and this is a no-op.
	if (UWorld* World = GetWorld())
	{
		if (UFPSREnemyShadowLODSubsystem* ShadowLOD = World->GetSubsystem<UFPSREnemyShadowLODSubsystem>())
		{
			ShadowLOD->RegisterEnemy(this);
		}
	}
}

void AFPSREnemyBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// S4 readability metrics: symmetric unregister (see the BeginPlay registration above for why this is once-per-
	// actor-lifetime, not once-per-Deactivate).
#if CSV_PROFILER_STATS
	if (UWorld* World = GetWorld())
	{
		if (UFPSREnemyMetricsSubsystem* Metrics = World->GetSubsystem<UFPSREnemyMetricsSubsystem>())
		{
			Metrics->UnregisterEnemy(this);
		}
	}
#endif

	// Shadow LOD registry: symmetric unregister (the pass also compacts stale weak entries, so a missed call during
	// teardown is survivable rather than fatal — this is the tidy path, not the only one).
	if (UWorld* World = GetWorld())
	{
		if (UFPSREnemyShadowLODSubsystem* ShadowLOD = World->GetSubsystem<UFPSREnemyShadowLODSubsystem>())
		{
			ShadowLOD->UnregisterEnemy(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AFPSREnemyBase::SetShadowCasting(bool bEnabled)
{
	if (Mesh)
	{
		Mesh->SetCastShadow(bEnabled);
	}
}

void AFPSREnemyBase::InitHealthBarWidget()
{
	// Force the BP-added world-space widget to exist NOW (it can otherwise be created lazily on first render — after
	// BeginPlay — which would leave the BP bind on a null widget). Then let the BP bind it to OnHealthChanged. Runs on
	// clients too (the bar is a client visual; OnHealthChanged is client-fired via OnRep_Health, B12).
	if (UWidgetComponent* WidgetComp = FindComponentByClass<UWidgetComponent>())
	{
		WidgetComp->InitWidget();
	}
	OnHealthBarReady();
}

void AFPSREnemyBase::HandleDeath(AActor* DeadActor, AActor* Killer)
{
	// Death cosmetics for the listen-server host / standalone: OnDeathCosmetic only fires from OnRep_bDead, which
	// never runs on authority, so without this the host is the one machine that never plays the death state (remote
	// clients do). Mirrors AFPSRBossBase::HandleDeath. Currently invisible on BOTH sides because ReleaseEnemy below
	// hides the actor in the same frame (the known Stage-3 death-dwell dependency documented on HandleDeathCosmetic);
	// calling it here means Stage-3 lands with host/client parity instead of regressing the host only.
	HandleDeathCosmetic();

	if (UWorld* World = GetWorld())
	{
		if (UFPSRPickupSubsystem* Pickups = World->GetSubsystem<UFPSRPickupSubsystem>())
		{
			Pickups->SpawnXPPickup(GetActorLocation(), XPReward);
		}

		if (UFPSREnemySpawnSubsystem* Sub = World->GetSubsystem<UFPSREnemySpawnSubsystem>())
		{
			Sub->ReleaseEnemy(this);
			return;
		}
	}
	Destroy();
}

void AFPSREnemyBase::Activate(const FVector& Location)
{
	SetActorLocation(Location);
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	// Wake the pooled actor for its WHOLE active life so its server movement (AddActorWorldOffset each pass)
	// replicates to clients. A pooled reuse returns from DORM_DormantAll, and FlushNetDormancy would force only ONE
	// update — the still-dormant enemy then stops streaming its transform (invisible / frozen on clients while the
	// server enemy keeps moving and dealing damage), and the death hide set in Deactivate never reaches them (zombie).
	// DORM_Awake here + DORM_DormantAll in Deactivate makes the awake->dormant transition flush the final
	// hidden/dead state. Mirrors the projectile pool recipe (FPSRProjectile::Activate/Deactivate).
	SetNetDormancy(DORM_Awake);

	if (HealthComponent)
	{
		HealthComponent->ResetForReuse();
	}

	CurrentMoveSpeed = MoveSpeed * FMath::FRandRange(0.9f, 1.1f);
	// Per-instance hover height (spec item 4): sample from the authored range when one exists (Max > Min), else fall
	// back to the single HoverHeight value (existing archetype BPs with no range authored see no behavior change).
	CurrentHoverHeight = (HoverHeightMax > HoverHeightMin) ? FMath::FRandRange(HoverHeightMin, HoverHeightMax) : HoverHeight;
	HoverSpringRateZ = 0.0f; // fresh spring state for the reused actor (no residual velocity from a prior life)
	// Pool-reuse leak-prevention sweep for the (now dormant, ADR 0008 retired 2026-08-21) pursuit fields: PursuitState
	// back to Flow with every timer/accumulator zeroed even though nothing ticks it any more (Reset stays cheap and
	// keeps a future reactivation from needing its own reset wiring — see PursuitState's field comment), a fresh
	// per-instance altitude sample (HoverHeightMin/Max's pattern exactly, itself currently unconsumed — see
	// CurrentSeekAltitude's comment), and the seek-Z override + forward-blocked flag cleared so a reused actor never
	// inherits a prior life's stale window seek target.
	PursuitState.Reset();
	CurrentSeekAltitude = (SeekAltitudeAbovePlayerMax > SeekAltitudeAbovePlayerMin)
		? FMath::FRandRange(SeekAltitudeAbovePlayerMin, SeekAltitudeAbovePlayerMax)
		: SeekAltitudeAbovePlayerMin;
	SeekTargetZ = 0.0f;
	bSeekTargetZValid = false;
	bLastForwardBlocked = false;
	LastAttackTime = -1000.0f; // CanAttack's own cooldown gate reads this (unrelated to the dormant pursuit fields
	                           // above) — reset so a reused actor doesn't inherit a prior life's cooldown clock
	VerticalVelocity = 0.0f; // reset fall state for the reused actor
	bGrounded = false;       // re-check ground on the first update (may spawn on a rooftop)
	GroundRecheckTimer = 0.0f;
	KnockbackVelocityXY = FVector::ZeroVector; // clear residual knockback from a prior life
	ClearExitPath();                           // drop any leftover path; AcquireEnemy re-assigns it if this spawn point has one
	ClearFrontChasing();                       // drop any stale front-chase tag from a prior life (U P-D)
	ClearFrontSpawn();                         // drop any stale front-spawn attribution / crossing credit from a prior life (U P-E)

	// Reset the cosmetic animation state for the reused actor (U20). No-op when dormant / on a dedicated server. On a
	// client the reused actor self-corrects on its first PostNetReceiveLocationAndRotation (it's alive, so movement
	// state overrides any stale Death), so this authority-side reset covers the standalone / listen-server host.
	CurrentAnimState = EFPSRAnimState::Idle;
	CurrentSpeedBucket = -1;
	LastRecvTime = -1.0f;
	SetAnimState(EFPSRAnimState::Idle);
}

void AFPSREnemyBase::SetExitPath(const TArray<FVector>& InWaypoints, bool bPhaseThroughWorld)
{
	if (!HasAuthority())
	{
		return;
	}
	ExitPath = InWaypoints;
	ExitPathIndex = 0;
	ExitPathElapsed = 0.0f;
	bFollowingExitPath = (ExitPath.Num() > 0);

	// 구조형 스포너(Enemy.md C1)를 위한 통과. 대안이었던 "메시 콜리전에 구멍을 뚫는다"는 두 가지가 나쁘다 —
	// 구멍이 적 캡슐보다 좁으면 조용히 끼고, 무콜리전으로 만들면 **플레이어가 스포너 안으로 빠진다**.
	// 메시를 완전히 막힌 채 두고 적만 잠시 통과시키면 둘 다 없다.
	//
	// 바꾸는 것은 WorldStatic 응답 **하나뿐**이다. 그래서 통과 중에도:
	//   - 소총 트레이스(Visibility)가 그대로 맞는다 — 나오는 중에도 죽일 수 있다
	//   - 플레이어(ECC_FPSRPlayerPawn)를 그대로 막는다
	//   - 바닥을 그대로 밟는다 — ApplyGravity 는 캡슐 응답이 아니라 별도 월드 쿼리
	//     (SweepSingleByObjectType)로 바닥을 찾으므로 이 변경에 영향받지 않는다
	//
	// 경로가 잘못 저작돼 적이 아레나 밖으로 새는 최악은 ExitPathTimeout 이 막는다(그때 ClearExitPath 가
	// 복구한다). 저작 시점 검사는 에디터 「아레나 검증」이 한다 — 마지막 웨이포인트가 열린 셀인지.
	if (bFollowingExitPath && bPhaseThroughWorld && Capsule)
	{
		Capsule->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Ignore);
	}
}

void AFPSREnemyBase::ClearExitPath()
{
	ExitPath.Reset();
	ExitPathIndex = 0;
	ExitPathElapsed = 0.0f;
	bFollowingExitPath = false;

	// 통과를 무조건 되돌린다 — 플래그를 기억해 두고 조건부로 풀지 않는다. 이 함수는 경로 소진 · 스톨
	// 타임아웃 · 풀에서 꺼낼 때(Activate) 전부에서 불리는 단일 복구 지점이고, 한 번이라도 새면 그 적은
	// 남은 수명 내내 벽을 통과한다. 무조건 Block 으로 되돌리는 것이 생성자 설정과 같은 값이라 안전하다.
	if (Capsule)
	{
		Capsule->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	}
}

void AFPSREnemyBase::ServerRelocateForStageCarry(const FVector& NewLocation)
{
	if (!HasAuthority())
	{
		return;
	}

	ClearExitPath(); // unconditional, BEFORE moving (spec: the single recovery point, see ClearExitPath's comment)

	// NewLocation.Z is the flow-field cell's FLOOR SURFACE Z (UFPSRFlowFieldSubsystem::FindNearestOpenLocation
	// hands out baked surface heights, not actor heights). This actor's rest convention is capsule CENTER at
	// floor + HalfHeight + GroundRestClearance (ApplyGravity's TargetZ) — placing the center AT the floor sinks
	// the capsule half-deep, ApplyGravity refuses the > GroundSnapTolerance upward snap it would need ("never
	// teleport up onto a wall"), and the enemy falls out of the world. First live-fire PIE of the carry-over hit
	// exactly that: all 17 carried enemies vanished. Convert to the rest pose HERE, where the capsule is known.
	FVector RestLocation = NewLocation;
	if (Capsule)
	{
		// + CurrentHoverHeight: a hover archetype's rest pose FLOATS. Omitting it planted the carried swarm at
		// ground rest for the whole (frozen) transition, then the spring re-lifted everyone after FadeIn — the
		// observed "sink at swap, rise after transition" (PIE feedback 2026-08-21). Placing at the full hover rest
		// makes the first post-freeze ApplyGravity a no-op: the enemy is already exactly at its target.
		RestLocation.Z += Capsule->GetScaledCapsuleHalfHeight() + GroundRestClearance + CurrentHoverHeight;
	}
	SetActorLocation(RestLocation, false, nullptr, ETeleportType::TeleportPhysics);

	// Physics-contact state reset (see the header comment) — same subset Activate() resets for a pooled reuse.
	VerticalVelocity = 0.0f;
	bGrounded = false;
	GroundRecheckTimer = 0.0f;
	KnockbackVelocityXY = FVector::ZeroVector;
	// Hover spring continuity across the swap: rest target = the pose we just placed (NOT the previous arena's
	// cached height), rate = 0 (any pre-swap glide momentum is meaningless at the new location). Without this the
	// v1 amortized branch could spring toward the OLD arena's HoverRestZ for one recheck window after unfreeze.
	HoverRestZ = static_cast<float>(RestLocation.Z);
	HoverSpringRateZ = 0.0f;
}


bool AFPSREnemyBase::ConsumeExitPathSteering(const FVector& MyLocation, float ScaledDeltaSeconds, FVector& OutDir)
{
	if (!bFollowingExitPath)
	{
		return false;
	}

	// Skip past any waypoints already reached (XY), then steer toward the next one.
	while (ExitPathIndex < ExitPath.Num())
	{
		FVector ToWp = ExitPath[ExitPathIndex] - MyLocation;
		ToWp.Z = 0.0f;
		if (ToWp.SizeSquared() <= FMath::Square(ExitWaypointReachRadius))
		{
			++ExitPathIndex;          // reached: advance
			ExitPathElapsed = 0.0f;   // progress made — reset the stall timer
			continue;
		}

		// Not yet reached: tick the stall safety timer; a misplaced/blocked waypoint hands off to the flow-field.
		ExitPathElapsed += ScaledDeltaSeconds;
		if (ExitPathElapsed >= ExitPathTimeout)
		{
			ClearExitPath();
			return false;
		}

		const FVector Dir = ToWp.GetSafeNormal();
		if (Dir.IsNearlyZero())
		{
			// Degenerate (waypoint directly above/below): treat as reached to avoid spinning in place.
			++ExitPathIndex;
			ExitPathElapsed = 0.0f;
			continue;
		}
		OutDir = Dir;
		return true;
	}

	// Path exhausted — hand off to flow-field player-chase.
	ClearExitPath();
	return false;
}

void AFPSREnemyBase::Deactivate()
{
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetNetDormancy(DORM_DormantAll);
}

void AFPSREnemyBase::SetAnimState(EFPSRAnimState NewState, float PlayRate)
{
	// Dormant unless an archetype opted into animation; no local rendering (so no cosmetics) on a dedicated server.
	if (!AnimProfile || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// Quantize the playrate so the scalar is re-written only when it crosses a bucket boundary (write-on-change). A
	// frozen clip (playrate 0) lands in bucket 0 and a playing clip in a higher bucket, so freeze<->play transitions
	// still trigger exactly one write.
	const int32 NewBucket = FMath::Clamp(static_cast<int32>(PlayRate * FPSRVATAnim::SpeedBucketCount), 0, FPSRVATAnim::SpeedBucketCount - 1);
	if (NewState == CurrentAnimState && NewBucket == CurrentSpeedBucket)
	{
		return; // event-driven: state + playrate bucket unchanged, nothing to write
	}
	CurrentAnimState = NewState;
	CurrentSpeedBucket = NewBucket;

	if (Mesh)
	{
		AnimProfile->ApplyAnimState(Mesh, NewState, PlayRate, AnimPhase);
	}
}

void AFPSREnemyBase::PostNetReceiveLocationAndRotation()
{
	Super::PostNetReceiveLocationAndRotation();

	// Client-side cosmetic animation from the replicated transform (the authority uses its server pass instead). This
	// fires only when new location data arrives, so it is naturally distance-throttled by the server's per-enemy net
	// frequency (S0 30Hz .. S3 2Hz) — a free coarse LOD. Dormant unless an archetype opted in.
	if (!AnimProfile)
	{
		return;
	}
	// Dead enemies hold the Death state (set via OnDeathCosmetic); don't override it with movement.
	if (HealthComponent && HealthComponent->IsDead())
	{
		return;
	}

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	const FVector Loc = GetActorLocation();

	// Nearest LOCAL viewer (one local player per client) for distance LOD + the melee attack tell.
	const AActor* LocalPawn = nullptr;
	if (const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
	{
		LocalPawn = PC->GetPawn();
	}
	const float DistSqToLocal = LocalPawn ? FVector::DistSquaredXY(Loc, LocalPawn->GetActorLocation()) : 0.0f;

	// Speed from the position delta since the last received update.
	bool bMoving = false;
	if (LastRecvTime >= 0.0f && Now > LastRecvTime)
	{
		const float Dt = Now - LastRecvTime;
		const float SpeedXY = FVector::DistXY(Loc, LastRecvLocation) / Dt;
		bMoving = SpeedXY > 10.0f; // cm/s: below this the enemy is effectively stopped
	}
	LastRecvLocation = Loc;
	LastRecvTime = Now;

	// Distance LOD: beyond the freeze radius, FREEZE the clip (playrate 0) — this stops CPU scalar writes (write-on-
	// change settles after one freeze) AND the distant GPU frame advance. Reuses the S1 boundary (Performance §5-1);
	// no new per-enemy world query (arithmetic on data the client already has).
	if (LocalPawn && DistSqToLocal > FPSRVATAnim::AnimFreezeRadiusSq)
	{
		SetAnimState(EFPSRAnimState::Idle, 0.0f);
		return;
	}

	// Melee attack tell (cosmetic heuristic): stationary AND within melee range of the local player. Damage stays
	// server-authoritative regardless of this tell. Refined with the real attack clip in Stage 3.
	if (!bMoving && LocalPawn && DistSqToLocal <= (AttackRange * AttackRange))
	{
		SetAnimState(EFPSRAnimState::Attack);
		return;
	}

	SetAnimState(bMoving ? EFPSRAnimState::Walk : EFPSRAnimState::Idle);
}

void AFPSREnemyBase::SetActorHiddenInGame(bool bNewHidden)
{
	Super::SetActorHiddenInGame(bNewHidden);

	// Client-only pool-reuse reset. AActor::bHidden is UPROPERTY(Replicated) with NO ReplicatedUsing/OnRep (Actor.h)
	// — the engine applies a replicated bHidden change on the receiving end by calling THIS virtual setter instead:
	// AActor::PreNetReceive() saves the pre-update value, PostNetReceive() exchanges the new value back in and calls
	// SetActorHiddenInGame(NewValue) only if it differs (ActorReplication.cpp). So this override is the real
	// client-side "bHidden changed" hook, not an OnRep. Only the true->false (became visible again) edge matters here
	// — mirror Activate()'s authority-side reset (see Activate, cpp above) so a reused enemy's stale CPD/anim state
	// (a prior life's Death clip, or a huge dormant-period dt in PostNetReceiveLocationAndRotation's speed calc)
	// doesn't leak into the new life. Authority doesn't need this: Activate() already resets on that path, and this
	// setter also fires locally on the server's own SetActorHiddenInGame(false) call inside Activate() (HasAuthority
	// guards against double-resetting there).
	if (!bNewHidden && !HasAuthority())
	{
		CurrentAnimState = EFPSRAnimState::Idle;
		CurrentSpeedBucket = -1;
		LastRecvTime = -1.0f;
		SetAnimState(EFPSRAnimState::Idle, 1.0f);
	}
}

void AFPSREnemyBase::HandleDeathCosmetic()
{
	// Client death edge (from the health component's OnRep_bDead). Enter the Death animation state. No-op when dormant.
	// ⚠️ KNOWN Stage-3 dependency (Codex merge-gate P2, accepted+deferred): for a SWARM enemy the authoritative
	// HandleDeath immediately ReleaseEnemy -> Deactivate (hide + dormancy flush) in the same death flow, so this Death
	// state is applied to an actor that is being hidden and won't be visibly seen until Stage 3 adds a server
	// death-dwell (delay the pool release for the death-clip window; the clip LENGTH is content, hence Stage 3). This
	// hook is the foundation for that. The BOSS (AFPSRBossBase) persists after death, so its death montage IS visible.
	SetAnimState(EFPSRAnimState::Death);
}

EFPSRServerAttackResult AFPSREnemyBase::ServerTickAttack(const FFPSRServerAttackContext& Ctx)
{
	// Melee contact attack: in horizontal range + within the vertical gap (no through-floor hits) + cooldown elapsed
	// + the target player's attack-token budget allows. Behaviour-identical refactor of the spawn subsystem's former
	// inline attack block — the subsystem now delegates the decision here so ranged archetypes can override it.
	if (Ctx.TargetChar
		&& Ctx.DistSqToTarget <= (AttackRange * AttackRange)
		&& Ctx.bVerticalInRange
		&& CanAttack(Ctx.Now)
		&& Ctx.bMeleeTokenAvailable)
	{
		Ctx.TargetChar->ApplyContactDamage(Ctx.ContactDamage, this);
		NotifyAttacked(Ctx.Now);
		// Authority-side attack anim tell (U20) — drives the listen-server host / standalone render. Clients derive
		// their own attack tell from proximity in PostNetReceiveLocationAndRotation. Foundational/transient this pass:
		// the next movement pass reverts to Walk/Idle (attack-anim persistence needs the baked clip length, Stage 3).
		SetAnimState(EFPSRAnimState::Attack);
		return EFPSRServerAttackResult::MeleeAttacked;
	}
	return EFPSRServerAttackResult::None;
}

void AFPSREnemyBase::TickServerMovement(const FFPSRServerMoveContext& Ctx)
{
	if (!HasAuthority() || (HealthComponent && HealthComponent->IsDead()))
	{
		return;
	}

	// ADR 0008's mode-switching Seek3D pursuit judgment (PursuitState.Tick + the beeline/step-up-skip branches this
	// block used to gate) was RETIRED 2026-08-21 (ADR 0009 P1) — the S3 hover window supersedes it structurally:
	// SeekZ is now just ANOTHER vertical target ApplyGravity's spring already max()s against, no mode switch or
	// straight-line beeline needed. FFPSRPursuitState is preserved dormant (see its field comment) but no longer
	// ticked here.
	bLastForwardBlocked = false; // this pass's OWN sweep result is recorded fresh below, for the NEXT call

	// S3 (ADR 0009 P1): the hover window's vertical seek target for ApplyGravity's spring, straight from the
	// subsystem's own QueryFlow answer — no per-pass computation here (that was the retired ADR 0008 formula).
	bSeekTargetZValid = Ctx.bSeekValid;
	SeekTargetZ = Ctx.SeekZ;

	// (U20) Pre-move location to classify walk vs idle for the cosmetic anim. Guarded so it is zero-cost when no
	// AnimProfile is assigned (the swarm default until Stage 3). Authority render only (standalone / listen host).
	const FVector AnimStartLoc = AnimProfile ? GetActorLocation() : FVector::ZeroVector;

	// Knockback (explosion push): a decaying horizontal impulse. While it's active, suppress flow steering so the
	// push isn't immediately cancelled by the enemy walking back toward the player.
	const bool bKnockbackActive = !KnockbackVelocityXY.IsNearlyZero(1.0f);

	// Horizontal steering: flow mode only (Ctx.MoveDir already carries flow + separation, combined by the caller) —
	// unified since the ADR 0008 Seek3D beeline branch above was retired.
	FVector Dir = Ctx.MoveDir;
	Dir.Z = 0.0f;
	if (!bKnockbackActive && Dir.SizeSquared() > KINDA_SMALL_NUMBER)
	{
		const FVector Normalized = Dir.GetSafeNormal();
		// Steering-magnitude speed scale (PIE feedback 2026-08-21, stop-ring jitter): normalizing Dir made ANY
		// nonzero steering a full-speed move, so at the stop ring — where the flow term is zeroed and only the
		// separation push remains — neighbours shoved past each other at full speed and flipped push direction
		// every pass (the observed left-right vibration). Scaling by the pre-normalize magnitude, clamped at 1 so
		// a flow-driven chase keeps exactly its speed, lets a separation-only nudge taper to zero toward the
		// radius edge: the crowd relaxes to its spacing instead of oscillating around it. The seek beeline and
		// exit-path steering hand in vectors at/above unit length -> clamp = full speed, unchanged.
		const float SteerScale = FMath::Min(1.0f, static_cast<float>(Dir.Size()));
		const float MoveDist = GetEffectiveMoveSpeed() * Ctx.ScaledDelta * SteerScale;

		// Walk ALONG the ground slope (the swarm equivalent of CharacterMovement's MoveAlongFloor): project the steering
		// onto the last-known ground plane and move at full speed along it, so the enemy ascends/descends ramps and stair
		// inclines SMOOTHLY instead of jamming flat against them each tick (the earlier jam-then-slide was janky). On flat
		// ground GroundNormal is up -> a plain horizontal move. GroundNormal is refreshed by ApplyGravity every tick while
		// on a slope (forced below).
		FVector MoveDir = FVector::VectorPlaneProject(Normalized, GroundNormal);
		MoveDir = MoveDir.IsNearlyZero() ? Normalized : MoveDir.GetSafeNormal();
		FHitResult MoveHit;
		AddActorWorldOffset(MoveDir * MoveDist, true, &MoveHit);

		if (MoveHit.bBlockingHit)
		{
			const FVector Remaining = MoveDir * MoveDist * (1.0f - MoveHit.Time);
			if (MoveHit.ImpactNormal.Z >= WalkableSlopeNormalZ)
			{
				// (a) Hit a WALKABLE SLOPE (stepping from flat ground ONTO a ramp/incline): slide the remainder UP ALONG
				// it so we mount the slope; next tick GroundNormal reflects the slope and the move follows it directly.
				if (!Remaining.IsNearlyZero())
				{
					AddActorWorldOffset(FVector::VectorPlaneProject(Remaining, MoveHit.ImpactNormal), true);
				}
			}
			else if (bGrounded && !Remaining.IsNearlyZero())
			{
				// (b) RISER / LEDGE / ramp-crest LIP (anything not a walkable slope — covers the whole normal-Z range below
				// WalkableSlopeNormalZ, so a face between a ramp and vertical no longer stalls the enemy dead).
				//
				// Step up so it climbs what the flow field routed it toward (unconditional since the ADR 0008 Seek3D
				// skip-the-lift branch that used to gate this was retired — flow mode is now the ONLY mode). A
				// ramp/stair top onto a platform can present a lip taller than one flat GroundSnapTolerance step, so
				// try progressively taller lifts and take the SMALLEST that lets the re-advance make progress (no
				// over-hop on small risers). On a SLOPE (cresting a ramp — GroundNormal tilted) allow up to
				// MaxCrestStepUp; on FLAT ground cap at one step so enemies don't scale walls the field routes
				// around. Each lift is swept (stops under a low ceiling); ApplyGravity settles onto the top.
				// Revert if none clears (taller than the cap = a wall, not a riser) so we don't bob against it.
				//
				// Re-advance along the FLOW (Ctx.FaceDir), NOT the separation-laden move dir: a lifted enemy
				// carrying the lateral separation push of its stair-mates would walk off the side of a narrow
				// flight and fall. Climbing FORWARD (toward the objective) keeps it on the stairs. Magnitude =
				// the blocked remainder of this move.
				FVector StepFwd = Ctx.FaceDir;
				StepFwd.Z = 0.0f;
				StepFwd = StepFwd.IsNearlyZero() ? MoveDir.GetSafeNormal2D() : StepFwd.GetSafeNormal();
				const FVector StepAdvance = StepFwd * (MoveDist * (1.0f - MoveHit.Time));
				const float StepAdvanceLen = static_cast<float>(StepAdvance.Size());
				const FVector PreStepLoc = GetActorLocation();
				const float PreStepFootZ = static_cast<float>(PreStepLoc.Z) - (Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.0f);
				const float MaxLift = (GroundNormal.Z < 0.99f) ? MaxCrestStepUp : GroundSnapTolerance;
				bool bCleared = false;
				for (float Lift = GroundSnapTolerance; Lift <= MaxLift + KINDA_SMALL_NUMBER; Lift += GroundSnapTolerance)
				{
					SetActorLocation(PreStepLoc, false);
					AddActorWorldOffset(FVector(0.0f, 0.0f, Lift), true);
					FHitResult StepFwdHit;
					AddActorWorldOffset(StepAdvance, true, &StepFwdHit);
					// (i) REAL progress, not a hair's slide: the old test (Time < KINDA_SMALL_NUMBER) accepted any
					// nonzero forward movement, so a beveled/tessellated wall face let the lift stand (2026-08-21
					// wall-climb: crowd pressure re-ran this every pass and the kept lifts were the ladder's rungs).
					const float Advanced = StepFwdHit.bBlockingHit ? StepFwdHit.Time * StepAdvanceLen : StepAdvanceLen;
					if (Advanced < FMath::Max(1.0f, StepAdvanceLen * 0.25f))
					{
						continue;
					}
					// (ii) FLOOR PROOF: a lift only stands if the capsule actually arrived OVER a walkable floor
					// HIGHER than where it started — the definition of a step. A slide along a wall face passes (i)
					// but has nothing new underneath, so it reverts here. One line trace, only on a lift that made
					// progress — bounded cost (this branch runs at most once per blocked move).
					const FVector PostLoc = GetActorLocation();
					FHitResult FloorHit;
					FCollisionObjectQueryParams StepObjParams;
					StepObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
					FCollisionQueryParams StepQueryParams(SCENE_QUERY_STAT(FPSREnemyStepProof), false, this);
					const float StepHalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.0f;
					const FVector ProofStart(PostLoc.X, PostLoc.Y, PostLoc.Z - StepHalfHeight + GroundSnapTolerance);
					const FVector ProofEnd(PostLoc.X, PostLoc.Y, PostLoc.Z - StepHalfHeight - Lift - GroundSnapTolerance);
					UWorld* const StepWorld = GetWorld();
					if (StepWorld
						&& StepWorld->LineTraceSingleByObjectType(FloorHit, ProofStart, ProofEnd, StepObjParams, StepQueryParams)
						&& FloorHit.ImpactNormal.Z >= WalkableSlopeNormalZ
						&& FloorHit.ImpactPoint.Z > PreStepFootZ + 1.0f)
					{
						bCleared = true; // stepped onto something real and higher
						break;
					}
				}
				if (!bCleared)
				{
					SetActorLocation(PreStepLoc, false);
				}
				bLastForwardBlocked = !bCleared; // still blocked only if no lift height cleared it
			}
		}

		// On a slope (or right after hitting a rise), re-trace the ground THIS tick so GroundNormal tracks the incline and
		// ApplyGravity re-snaps us onto it — no float/jitter while climbing. Flat movers keep the cheap amortized recheck.
		if (GroundNormal.Z < 0.99f || MoveHit.bBlockingHit)
		{
			GroundRecheckTimer = 0.0f;
		}

		// Face the PLAYER (Ctx.FaceDir), not the move direction: at StopDistance the move is separation-only and its
		// direction jitters, which would spin the enemy 360deg in place. Ctx.FaceDir is stable (toward the target).
		FVector FaceXY = Ctx.FaceDir;
		FaceXY.Z = 0.0f;
		if (!FaceXY.IsNearlyZero())
		{
			SetActorRotation(FaceXY.GetSafeNormal().Rotation());
		}
	}

	if (bKnockbackActive)
	{
		AddActorWorldOffset(KnockbackVelocityXY * Ctx.ScaledDelta, true); // swept: blocks against walls
		const float DecayFactor = FMath::Exp(-Ctx.ScaledDelta / FMath::Max(KnockbackDecayTime, 0.01f));
		KnockbackVelocityXY *= DecayFactor;
		if (KnockbackVelocityXY.IsNearlyZero(1.0f))
		{
			KnockbackVelocityXY = FVector::ZeroVector;
		}
	}

	// Vertical: ground-follow + gravity ALWAYS (even when not steering) so enemies never float and a
	// rooftop-spawned enemy falls before chasing. Ctx.FaceDir feeds the v2 hover sampler's look-ahead (a floater
	// pre-rises before a step in its direction of travel); SeekTargetZ (set above) folds into the SAME spring via
	// ApplyGravity's max() — see ApplyGravity and its ADR 0008 note.
	ApplyGravity(Ctx.ScaledDelta, Ctx.FaceDir);

	// (U20) Cosmetic walk/idle from the actual XY displacement this pass. Skip the override while a fresh melee attack
	// tell is still within its cooldown window so ServerTickAttack's Attack state isn't clobbered the same pass (a
	// stationary attacker reads as Idle otherwise). Attack-anim length/persistence is refined with the clips in Stage 3.
	if (AnimProfile)
	{
		const float ExpectedMove = GetEffectiveMoveSpeed() * Ctx.ScaledDelta;
		const float MovedSq = FVector::DistSquaredXY(GetActorLocation(), AnimStartLoc);
		const bool bMoved = ExpectedMove > KINDA_SMALL_NUMBER && MovedSq > FMath::Square(ExpectedMove * 0.25f);
		if (bMoved)
		{
			SetAnimState(EFPSRAnimState::Walk);
		}
		else if (CurrentAnimState != EFPSRAnimState::Attack)
		{
			SetAnimState(EFPSRAnimState::Idle);
		}
	}
}

void AFPSREnemyBase::ApplyKnockback(const FVector& Velocity)
{
	if (!HasAuthority() || (HealthComponent && HealthComponent->IsDead()))
	{
		return;
	}
	// Additive: stacking blasts compound. Horizontal goes to the decaying member; vertical feeds VerticalVelocity
	// so the existing gravity integrator carries the enemy up and back down (a launched pop).
	KnockbackVelocityXY += FVector(Velocity.X, Velocity.Y, 0.0f);
	VerticalVelocity += Velocity.Z;
	bGrounded = false;        // leave the ground; re-acquire it on landing
	GroundRecheckTimer = 0.0f; // re-check the floor immediately
}

void AFPSREnemyBase::ApplyGravity(float ScaledDeltaSeconds, const FVector& FlowDirXY)
{
	UWorld* World = GetWorld();
	if (!World || !Capsule)
	{
		return;
	}

	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();

	// --- v2 PRIORITY PATH: flow-field CellFloorZ array sampler (spec ①③) — zero scene query, every movement update
	//     (no amortize timer; SampleHoverFloorZ is O(1) array math, cheap enough to run unthrottled at swarm scale).
	//     Gate = CurrentHoverHeight>0 && VerticalVelocity<=0 (NOT bGrounded, 2026-08-14 follow-up — user decision): a
	//     hover archetype descending under gravity, past the apex of a knockback launch, or freshly spawned in the
	//     air ALSO glides down on this path instead of free-falling — a cliff/step-down should look like a smooth
	//     height-correction, not a physics drop. Still ballistic while RISING (VerticalVelocity>0, the initial
	//     upward half of a knockback pop) — the launch arc itself is unchanged.
	//     NOT while following an authored exit path (bFollowingExitPath): the exit route exists precisely because the
	//     baked field does not describe the spawn structure's interior, and the look-ahead (FaceDir = exit direction,
	//     pointing INTO the structure's wall) can anchor a low structure's ROOF surface, springing the enemy up onto
	//     it instead of out through the authored hole (merge-gate P2). The exit leg runs the v1 scene-query path below,
	//     which is the invariant SetExitPath's phase-through was designed against ("바닥을 그대로 밟는다" — the ground
	//     probe is a world query, not a capsule response, so it works while WorldStatic is ignored). ---
	if (CurrentHoverHeight > 0.0f && VerticalVelocity <= 0.0f && CachedFlowField && !bFollowingExitPath)
	{
		const FVector Loc = GetActorLocation();
		// AnchorFootZ = capsule BOTTOM (Z - HalfHeight), NOT ActorZ - EnemyStandOffset(95) — that offset is Sample()'s
		// own convention for the flow-DIRECTION query. SampleFloorZBilinear picks each corner's surface NEAREST this
		// foot Z within MaxLayerPickDrop (200cm, PickRankForFootZ), falling back to the nearest surface BELOW the foot
		// when none is near it (a cliff/ledge glide-descent target) — since a hovering foot sits CurrentHoverHeight
		// above its actual floor, that 200cm budget is still the hard ceiling on how high HoverHeight/HoverHeightMax
		// can go before the primary anchor pick finds the WRONG storey (see the HoverHeight UPROPERTY comment).
		const float AnchorFootZ = static_cast<float>(Loc.Z) - HalfHeight;
		// ADR 0009 결정 5: routed through QueryFlow, the single movement-consumer seam — S1 this is still the 2D
		// surface field's SampleHoverFloorZ, unchanged sampling.
		FFPSRFlowQuery HoverQuery;
		HoverQuery.WorldPos = Loc;
		HoverQuery.bWantHoverFloor = true;
		HoverQuery.HoverAnchorFootZ = AnchorFootZ;
		HoverQuery.LookAheadDirXY = FVector2D(FlowDirXY.X, FlowDirXY.Y);
		HoverQuery.MaxSurfaceDeltaCm = MaxCrestStepUp;
		FFPSRFlowResult HoverRes;
		CachedFlowField->QueryFlow(HoverQuery, HoverRes);
		float FloorZ = HoverRes.FloorZ;
		FVector FloorNormal = HoverRes.FloorNormal;
		const bool bTerrainSampled = HoverRes.bFloorValid;

		// ADR 0008: while Seek3D holds a valid target this pass (bSeekTargetZValid), the spring's TargetZ is
		// max(terrain-relative rest, SeekTargetZ) — ONE integrator regardless of pursuit mode (invariant 2), never a
		// separate Seek3D Z code path. A terrain-sampler MISS with a live seek target (open air past a roof edge, a
		// building the flow field doesn't cover) still resolves via SeekTargetZ ALONE, so a Seek3D enemy stays on
		// this zero-scene-query path instead of dropping to the v1 fallback below (invariant 5 — no added queries).
		if (bTerrainSampled || bSeekTargetZValid)
		{
			// No cliff cutoff here (v1 had one via MaxStepDownHeight) — a sampled target arbitrarily far below is now
			// exactly the case this path exists to handle: the spring GLIDES down to it over time (rate-limited by
			// HoverSpringFrequency/DampingRatio, not a teleport), which is the requested descent curve. The anchor's
			// two-tier pick (SampleFloorZBilinear) plus the corner MaxSurfaceDeltaCm guard are what keep the terrain
			// TARGET itself sane (never a storey the enemy didn't actually fall past); this code only decides how to
			// REACH the (possibly seek-overridden) target.
			const float TerrainRestZ = bTerrainSampled ? (FloorZ + HalfHeight + GroundRestClearance + CurrentHoverHeight) : -MAX_flt;
			float TargetZ = TerrainRestZ;
			if (bSeekTargetZValid)
			{
				TargetZ = FMath::Max(TargetZ, SeekTargetZ);
			}

			// Continuity seed: captured while airborne (falling, or past a knockback's apex) — the spring inherits the
			// current ballistic VerticalVelocity so the hand-off curves smoothly into the glide instead of kinking. An
			// already-grounded tick keeps the spring's own running rate (VerticalVelocity is 0 there -> a no-op).
			if (!bGrounded)
			{
				HoverSpringRateZ = VerticalVelocity;
			}

			float Z = static_cast<float>(Loc.Z);
			FMath::SpringDamper(Z, HoverSpringRateZ, TargetZ, 0.0f, ScaledDeltaSeconds, HoverSpringFrequency, HoverSpringDampingRatio);

			// Deep-glide sweep (merge-gate P2): a terrain anchor MORE than the tier-1 pick window below the foot was
			// served by the tier-2 unlimited-depth fallback — the one case where blocking-but-UNBAKED geometry (an
			// awning, the roof over an enclosed interior) can sit between the pawn and the target, and a blind Z
			// write would carry the capsule straight through it. Sweep ONLY these descending moves (cost ≈ the v1
			// airborne probe this path replaced, paid only while a deep glide is in flight); in-window hover / stair
			// motion keeps the zero-scene-query write.
			bool bGlideBlocked = false;
			const bool bDeepGlide = Z < Loc.Z && bTerrainSampled
				&& (AnchorFootZ - FloorZ) > UFPSRFlowFieldComputer::GetMaxLayerPickDrop();
			if (bDeepGlide)
			{
				FHitResult GlideHit;
				SetActorLocation(FVector(Loc.X, Loc.Y, Z), true, &GlideHit);
				if (GlideHit.bBlockingHit)
				{
					// Came to rest on real (unbaked) geometry short of the sampled target — hold HERE and kill the
					// spring's downward rate so the next pass doesn't immediately re-shove into the surface.
					bGlideBlocked = true;
					HoverSpringRateZ = 0.0f;
				}
			}
			else
			{
				SetActorLocation(FVector(Loc.X, Loc.Y, Z), false);
			}
			if (bTerrainSampled && !bGlideBlocked)
			{
				GroundNormal = FloorNormal; // only a real terrain sample has a meaningful surface normal to lean on
			}
			VerticalVelocity = 0.0f;
			bGrounded = true; // the spring is now carrying vertical motion — no longer a free-falling body
			// Keep the v1 fallback's glide target current in case a later sample fails — but only ever a TERRAIN rest
			// height (or the blocked-landing Z), never a Seek3D altitude: caching a dead seek altitude here left the
			// v1 amortized branch springing toward mid-air with no scene query behind it (merge-gate P3).
			HoverRestZ = bGlideBlocked ? static_cast<float>(GetActorLocation().Z)
				: (bTerrainSampled ? TerrainRestZ : HoverRestZ);
			// And the v1 amortized hold must never resume on a timer that froze while this path carried the motion —
			// force the first v1 pass after a handoff to do a full floor re-check (merge-gate P3).
			GroundRecheckTimer = 0.0f;
			return;
		}
		// Neither the terrain sampler nor a Seek3D target succeeded — fall through to the v1 scene-query path: a
		// genuine void / grid edge with nothing (terrain or seek) to glide onto.
	}

	// --- v1 SCENE-QUERY PATH (fallback for a non-hovering archetype, an airborne/launched enemy, a hover sample
	//     miss, or a world with no CachedFlowField) — unchanged ground-follow + gravity integrator, except the two
	//     glide sites now drive the SAME spring as the v2 path (HoverSpringRateZ) instead of v1's constant-speed
	//     FInterpConstantTo, so a mid-flight handoff between the array sampler and this fallback carries its
	//     velocity instead of resetting it. ---

	// Amortize: a stably grounded enemy re-checks the floor only every GroundRecheckInterval; airborne enemies
	// (falling) run every update so they land promptly (Codex P1 — no per-frame scene query for the whole swarm).
	GroundRecheckTimer -= ScaledDeltaSeconds;
	if (bGrounded && GroundRecheckTimer > 0.0f)
	{
		// Hovering archetypes keep springing toward the cached rest height BETWEEN floor re-checks — only these ticks
		// touch Z; the amortization otherwise quantizes the motion into visible hops (no scene query here).
		if (CurrentHoverHeight > 0.0f)
		{
			const FVector L = GetActorLocation();
			if (!FMath::IsNearlyEqual(L.Z, HoverRestZ, 0.1f))
			{
				float Z = static_cast<float>(L.Z);
				FMath::SpringDamper(Z, HoverSpringRateZ, HoverRestZ, 0.0f, ScaledDeltaSeconds, HoverSpringFrequency, HoverSpringDampingRatio);
				SetActorLocation(FVector(L.X, L.Y, Z), false);
			}
		}
		return;
	}
	GroundRecheckTimer = GroundRecheckInterval;

	const FVector Loc = GetActorLocation();

	// Down-trace against STATIC world ONLY — ignore other pawns/dynamic actors so a falling enemy doesn't 'land'
	// on the swarm and jitter (Codex P2). Short probe; the fall step is clamped below so the floor is always
	// within reach on the next update (no tunneling).
	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSREnemyGround), false, this);
	FHitResult Hit;
	const FVector TraceStart(Loc.X, Loc.Y, Loc.Z + HalfHeight);
	const FVector TraceEnd(Loc.X, Loc.Y, Loc.Z + HalfHeight - GroundProbeDistance);

	// Floor probe. PRIMARY: a footprint-sized SPHERE sweep (not a single center line) so a small gap/seam in a
	// tiled/paneled platform floor isn't fallen through — the sphere bridges it, matching UCharacterMovementComponent::
	// ComputeFloorDist. But a sphere sweeping down NEAR a wall/riser can return the wall's SIDE as its first blocking hit
	// (a sideways normal), which would wrongly ground-snap the enemy to the wall or feed a sideways GroundNormal (Codex).
	// So accept the sphere hit ONLY if it is a WALKABLE up-facing surface; otherwise FALL BACK to a straight-down center
	// LINE trace, which only returns a surface directly under the foot (no side geometry) — the pre-sphere behavior.
	// Amortized (grounded enemies probe every GroundRecheckInterval), so the sweep+fallback cost is bounded at swarm scale.
	const FCollisionShape GroundProbeShape = FCollisionShape::MakeSphere(Capsule->GetScaledCapsuleRadius());
	bool bHitFloor = World->SweepSingleByObjectType(Hit, TraceStart, TraceEnd, FQuat::Identity, ObjParams, GroundProbeShape, QueryParams)
		&& !Hit.bStartPenetrating && Hit.ImpactNormal.Z >= WalkableSlopeNormalZ;
	if (!bHitFloor)
	{
		bHitFloor = World->LineTraceSingleByObjectType(Hit, TraceStart, TraceEnd, ObjParams, QueryParams);
	}
	if (bHitFloor)
	{
		const float TargetZ = Hit.ImpactPoint.Z + HalfHeight + GroundRestClearance + CurrentHoverHeight; // rest just above the floor (+CurrentHoverHeight for floating archetypes)
		const float Diff = Loc.Z - TargetZ;

		// Snap window: DOWN up to MaxStepDownHeight while GROUNDED (a grounded enemy walking off a small ledge / down a
		// stair step-DOWNs deterministically instead of free-falling the storey gap — the descent mirror of the swept
		// step-UP), UP only within GroundSnapTolerance (never teleport up onto a wall; the step-up handles real risers).
		// A drop beyond the down budget is a true cliff -> fall under gravity below (the flow routes to the stair; don't
		// snap across a storey). An AIRBORNE enemy keeps the tight ±GroundSnapTolerance window so it lands cleanly rather
		// than snapping onto a passing ledge. NOT while rising under a knockback impulse (VerticalVelocity > 0).
		// The UP-snap window must include CurrentHoverHeight: a hovering archetype's rest target sits CurrentHoverHeight
		// above the old one, so a freshly spawned (ground-snapped) enemy is legitimately that much below target —
		// without this the spawn state reads as the no-snap fall path and the enemy free-falls through the floor
		// (regression fix). The wall guard's meaning is preserved: relative to the ACTUAL rest height the allowance is
		// still ±Tolerance.
		const float SnapDown = bGrounded ? MaxStepDownHeight : GroundSnapTolerance;
		if (VerticalVelocity <= 0.0f && Diff <= SnapDown && Diff >= -(GroundSnapTolerance + CurrentHoverHeight))
		{
			HoverRestZ = TargetZ; // refresh the glide target on every floor re-check
			if (!FMath::IsNearlyZero(Diff))
			{
				// Floaters spring toward the rest height (stair treads = smooth ramp); walkers snap instantly (no regression).
				float NewZ;
				if (CurrentHoverHeight > 0.0f)
				{
					float Z = static_cast<float>(Loc.Z);
					FMath::SpringDamper(Z, HoverSpringRateZ, TargetZ, 0.0f, ScaledDeltaSeconds, HoverSpringFrequency, HoverSpringDampingRatio);
					NewZ = Z;
				}
				else
				{
					NewZ = TargetZ;
				}
				SetActorLocation(FVector(Loc.X, Loc.Y, NewZ), false); // small slope/step correction
			}
			VerticalVelocity = 0.0f;
			bGrounded = true;
			GroundNormal = Hit.ImpactNormal; // remember the slope so TickServerMovement walks along it
			return;
		}

		if (Diff > 0.0f || VerticalVelocity > 0.0f)
		{
			// Above the floor (or launched upward) — integrate ballistically, clamping to land exactly on the floor
			// only while descending (a rising knockback passes up through TargetZ without snapping).
			VerticalVelocity -= GravityAccel * ScaledDeltaSeconds;
			float NewZ = Loc.Z + VerticalVelocity * ScaledDeltaSeconds;
			if (VerticalVelocity <= 0.0f && NewZ <= TargetZ)
			{
				if (CurrentHoverHeight > 0.0f)
				{
					// Seed the shared spring with the falling velocity BEFORE it's zeroed below, so a hover archetype's
					// landing eases into its rest height carrying its fall momentum instead of restarting from a dead
					// stop (v1 hard-snapped on landing; v2's spring inherits the motion — no rethink-then-rise pop).
					HoverSpringRateZ = VerticalVelocity;
				}
				NewZ = TargetZ;
				VerticalVelocity = 0.0f;
				bGrounded = true;
				GroundNormal = Hit.ImpactNormal; // just landed -> remember the slope
				HoverRestZ = TargetZ;            // landing seeds the floater's glide target
			}
			else
			{
				bGrounded = false;
				GroundNormal = FVector::UpVector; // airborne -> steer horizontally
			}
			SetActorLocation(FVector(Loc.X, Loc.Y, NewZ), false);
			return;
		}
		// Diff < -tolerance: a static surface is far ABOVE the feet (overhang) — fall through to the path below.
	}

	// No reachable floor within the probe — fall, clamping the step so it can't overshoot the probe range and
	// tunnel below the floor before the next update's trace can catch it.
	VerticalVelocity -= GravityAccel * ScaledDeltaSeconds;
	const float MaxFallStep = FMath::Max(GroundProbeDistance - 2.0f * HalfHeight - GroundSnapTolerance, 1.0f);
	const float StepZ = FMath::Max(VerticalVelocity * ScaledDeltaSeconds, -MaxFallStep);
	SetActorLocation(FVector(Loc.X, Loc.Y, Loc.Z + StepZ), false);
	bGrounded = false;
	GroundNormal = FVector::UpVector; // airborne -> steer horizontally
}
