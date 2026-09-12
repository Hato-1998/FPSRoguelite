// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyBase.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h" // GASM1 측정 시임 (Docs/Specs/GASM1_SwarmASCCostMeasurement.md)
#include "AbilitySystem/Attributes/FPSRMeasureAttributeSet.h"
#include "AbilitySystem/Abilities/FPSRMeasureDummyAbility.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h" // FGameplayEffectQuery (측정 티어다운, §6-C)
#include "GameplayAbilitySpec.h" // FGameplayAbilitySpec (측정 어빌리티 부여, §6-A)
#include "Enemy/FPSREnemyAnimProfile.h"
#include "Enemy/FPSREnemyMetricsSubsystem.h" // S4 readability metrics registry (CSV-gated, see below)
#include "Enemy/FPSREnemyCosmeticLODSubsystem.h" // per-viewer cosmetic LOD band (see BeginPlay/EndPlay)
#include "Enemy/FPSRFlowFieldSubsystem.h" // v2 hover height sampler (CachedFlowField, see BeginPlay/ApplyGravity)
#include "Hero/FPSRCharacter.h"
#include "Pickup/FPSRPickupSubsystem.h"
#include "Core/FPSRLogChannels.h"
#include "Core/FPSRPlayerController.h" // ranged attack (promoted from AFPSRRangedEnemyBase, ADR 0013 C1)
#include "Weapon/FPSRProjectile.h"
#include "Weapon/FPSRProjectileSubsystem.h"
#include "Weapon/FPSRProjectileTypes.h"
#include "FPSRCollisionChannels.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "UI/FPSREnemyHealthBarWidget.h" // HB1: BindHealthComponent (InitHealthBarWidget's bind step)
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h" // TActorIterator (debug ForceAnimState command at the end of this file)
#include "GameFramework/PlayerController.h"
#include "ProfilingDebugging/CsvProfiler.h" // CSV_PROFILER_STATS gate for the metrics registry calls below
#include "Settings/FPSRPlaceholderVisualSettings.h"
#include "Settings/FPSREnemyRenderSettings.h" // HB1: HealthBarWidgetClass soft path (InitHealthBarWidget)
#include "Settings/FPSRStatusEffectSettings.h" // STAT1 §8 D단계 debug command: ResolveCatalog (FPSR.Status.Apply)
#include "HAL/IConsoleManager.h"
#include "CollisionQueryParams.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

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

#if !UE_BUILD_SHIPPING
// ---- GASM1 측정 CVar 4종 (Docs/Specs/GASM1_SwarmASCCostMeasurement.md §5-A) — 전부 기본값 off/1.0, 프로덕션
//      경로 diff 0. ForceSpawnClass 는 FPSREnemySpawnSubsystem.cpp 소유(AcquireEnemy 가 소비 — 그쪽엔 이
//      가드가 없다. 그 CVar 의 유일한 소비처는 로스터 UPROPERTY 데이터를 읽을 뿐 이 파일의 #if !UE_BUILD_
//      SHIPPING 멤버를 건드리지 않는다). 이 4개는 #if 로 가드한다 — 유일한 소비처(Activate/ServerTickAttack
//      의 측정 분기)가 #if !UE_BUILD_SHIPPING 안의 비-UPROPERTY 멤버를 읽으므로 CVar 자체도 그 가드 밖에서는
//      의미가 없다(작업 지시 §1 "가드는 사용처(CVar·부착·부여·구동)에만"의 CVar 항목). ----
static TAutoConsoleVariable<int32> CVarAttachASC(
	TEXT("FPSR.Debug.AttachASC"), 0,
	TEXT("일반 적에 UFPSRAbilitySystemComponent 를 런타임 부착한다(액터 실수명당 1회)."), ECVF_Cheat);

static TAutoConsoleVariable<int32> CVarMeasureLoadout(
	TEXT("FPSR.Debug.MeasureLoadout"), 0,
	TEXT("측정용 AttributeSet 부착 + 더미 어빌리티 부여·구동. AttachASC 를 함의한다(코드에서 강제)."), ECVF_Cheat);

static TAutoConsoleVariable<float> CVarMeasureCadence(
	TEXT("FPSR.Debug.MeasureCadence"), 1.0f,
	TEXT("더미 어빌리티 발동 주기(초). 허용값 = 1.0 / 0.2 뿐이며, 그 밖의 값은 경고 후 가까운 쪽으로 스냅한다."),
	ECVF_Cheat);

// 🔁 신설 (G2 P2-1, 구성 ③ᵀ) — 측정 AttributeSet 이 ITickableAttributeSetInterface::ShouldTick() 에
// true 를 돌려주게 해서, 엔진이 정한 틱 조건(§10 자기해제 체인의 ③번)을 합법적으로 만족시킨다. 이것 없이는
// ASC 틱이 첫 프레임 뒤 스스로 꺼져 틱 축을 아예 못 잰다. MeasureLoadout 을 함의한다(세트가 있어야 Tickable
// 을 걸 곳이 있다) — 코드에서 강제(아래 Activate 의 bEffectiveLoadout 계산).
static TAutoConsoleVariable<int32> CVarMeasureTickable(
	TEXT("FPSR.Debug.MeasureTickable"), 0,
	TEXT("구성 ③ᵀ: 측정 AttributeSet 을 Tickable 로 만들어 ASC 틱이 켜진 채 유지되게 한다. "
	     "MeasureLoadout 을 함의한다."), ECVF_Cheat);

namespace
{
	// GASM1 §5-A — 허용값은 1.0 / 0.2 뿐. ServerTickAttack 은 측정이 켜진 액터마다 매 패스 이 함수를 부를 수
	// 있으므로(최대 300+), 경고 로그는 반드시 "값이 실제로 바뀔 때만"(엣지 트리거) — 아니면 스팸이 된다.
	// 전역 static 하나로 충분한 이유 = 이건 액터별 상태가 아니라 CVar(전역) 하나의 사실이다.
	float GetEffectiveMeasureCadenceSeconds()
	{
		const float Raw = CVarMeasureCadence.GetValueOnGameThread();
		if (FMath::IsNearlyEqual(Raw, 1.0f, 0.001f))
		{
			return 1.0f;
		}
		if (FMath::IsNearlyEqual(Raw, 0.2f, 0.001f))
		{
			return 0.2f;
		}

		static float GLastWarnedRawCadence = -1.0f; // sentinel — 허용/실사용 범위 밖(캐던스는 항상 양수)
		const float Snapped = (FMath::Abs(Raw - 1.0f) <= FMath::Abs(Raw - 0.2f)) ? 1.0f : 0.2f;
		if (!FMath::IsNearlyEqual(Raw, GLastWarnedRawCadence, 0.001f))
		{
			GLastWarnedRawCadence = Raw;
			UE_LOG(LogFPSR, Warning,
				TEXT("[GASM1] FPSR.Debug.MeasureCadence=%.3f is not an allowed value (1.0 or 0.2) — snapping to %.1f."),
				Raw, Snapped);
		}
		return Snapped;
	}
}
#endif // !UE_BUILD_SHIPPING

float AFPSREnemyBase::GetEffectiveMoveSpeed() const
{
	float Speed = CurrentMoveSpeed;
#if !UE_BUILD_SHIPPING
	Speed *= FMath::Max(0.0f, GFPSREnemySpeedScale);
#endif
	// STAT1 §6 둔화/속박: the SOLE choke point both TickServerMovement call sites read (the steer-distance calc and
	// the walk/idle bMoved heuristic) — the status axis composes here once instead of twice. bDisableMovement
	// (속박) hard-zeroes movement rather than merely scaling toward 0, so the bMoved heuristic also reads a rooted
	// enemy as fully stopped, not just slow. Knockback is a SEPARATE suppression — TickServerMovement's own
	// knockback block reads bDisableMovement directly, because knockback moves the actor with a direct
	// AddActorWorldOffset that never calls this function.
	if (const UFPSREnemyHealthComponent* Health = GetHealthComponent())
	{
		const FFPSRResolvedStatus& Status = Health->GetResolvedStatus();
		if (Status.bDisableMovement)
		{
			return 0.0f;
		}
		Speed *= Status.MoveSpeedMultiplier;
	}
	return Speed;
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

	// HB1: native health-bar widget component — every AFPSREnemyBase-derived archetype gets one unconditionally
	// (net mode is unknown at ctor time; InitHealthBarWidget is what skips a dedicated server). Values below are
	// BP_EnemyMeleeBase's own MEASURED authoring (HB1 §6-1) copied verbatim, so this is a no-regression migration
	// for that archetype and a net-new (previously zero-cost, silently-missing) bar for the other two. The widget
	// CLASS itself is resolved later from config (InitHealthBarWidget), never here — no asset path in C++.
	HealthBarWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("HealthBarWidgetComponent"));
	HealthBarWidgetComponent->SetupAttachment(Capsule);
	HealthBarWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	HealthBarWidgetComponent->SetDrawSize(FVector2D(160.0f, 120.0f));
	HealthBarWidgetComponent->SetPivot(FVector2D(0.5f, 0.5f));
	HealthBarWidgetComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 120.0f));
	HealthBarWidgetComponent->SetDrawAtDesiredSize(false);
	HealthBarWidgetComponent->SetTickWhenOffscreen(false);
	// Engine default TickMode is Enabled, which never self-gates on SetHiddenInGame — Automatic does
	// (WidgetComponent.cpp:1262's TickMode != Enabled guard), so a hidden-by-LOD bar's component tick actually
	// stops instead of ticking every frame regardless (HB1 §6-1/§10). Not dead code — see that section.
	HealthBarWidgetComponent->SetTickMode(ETickMode::Automatic);
	HealthBarWidgetComponent->SetHiddenInGame(false); // matches the 2-axis visibility contract's true/true initial state
}

void AFPSREnemyBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSREnemyBase, bCharging, Params);
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
		// Hit-flash cosmetic (U20 CPD slot 4): fires on BOTH server (ApplyDamage) and clients (OnRep_Health), unlike
		// OnDeathCosmetic above — see HandleHealthChangedForHitFlash's own comment for the pool-reuse guard this needs.
		HealthComponent->OnHealthChanged.AddDynamic(this, &AFPSREnemyBase::HandleHealthChangedForHitFlash);

		// Seed the edge tracker from the health that is ALREADY there. Leaving it at its -1 default makes the first
		// observation structurally unable to be a "decrease", so a client would swallow the first hit of an actor's
		// first life: the initial replicated Health equals the archetype default, so no initial OnRep_Health fires to
		// prime the tracker, and the first real damage OnRep then compares against -1. On the server the component's
		// own BeginPlay has already set Health = MaxHealth by the time this actor BeginPlay body runs; on a client the
		// initial replicated value is applied before BeginPlay. A late-relevancy joiner seeds from the already-damaged
		// value, which is also correct — it just means no flash for damage it never witnessed.
		LastHealthForHitFlash = HealthComponent->GetHealth();
	}

	// Per-actor animation phase (0..1) derived from the actor id so pooled enemies don't animate in lockstep (U20).
	AnimPhase = static_cast<float>(GetUniqueID() % 1000) / 1000.0f;

	// Publish the phase ONCE to the CPD contract slot so procedural materials de-lockstep from a STABLE per-actor
	// value. (A position-hash phase inside the material rotates the mesh as the actor MOVES — measured regression.)
	// Written here (BeginPlay runs on every machine with rendering) and never cleared: CPD survives pooling reuse.
	if (Mesh)
	{
		Mesh->SetCustomPrimitiveDataFloat(FPSRAnimCPD::CPDSlot_Phase, AnimPhase);
	}

	// Resolve + bind the native (HB1), screen-space health bar / floating-damage widget to the health component once
	// (server + clients). Pooling-safe: the actor + widget persist across dormancy, so this single bind survives
	// every reuse.
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

	// Cosmetic LOD registry — same shape and lifetime as the metrics registration above, and for the same reason: the
	// decision is made from each LOCAL viewer's POV, so this runs on every net mode, not just the server. Absent ONLY
	// on a dedicated server, where GetSubsystem returns null and this is a no-op. (LOD1: creation is no longer gated
	// on any individual consumer's config switch — the pass carries three consumers now, one of which has no switch
	// at all, so gating creation on one would silently kill the others. See ShouldCreateSubsystem.)
	if (UWorld* World = GetWorld())
	{
		if (UFPSREnemyCosmeticLODSubsystem* CosmeticLOD = World->GetSubsystem<UFPSREnemyCosmeticLODSubsystem>())
		{
			CosmeticLOD->RegisterEnemy(this);
		}
	}
}

void AFPSREnemyBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// World teardown / level change: ensure the ranged warning is cleared + the token released (promoted from
	// AFPSRRangedEnemyBase, ADR 0013 C1).
	ReleaseRangedHold();

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

	// Cosmetic LOD registry: symmetric unregister (the pass also compacts stale weak entries, so a missed call during
	// teardown is survivable rather than fatal — this is the tidy path, not the only one).
	if (UWorld* World = GetWorld())
	{
		if (UFPSREnemyCosmeticLODSubsystem* CosmeticLOD = World->GetSubsystem<UFPSREnemyCosmeticLODSubsystem>())
		{
			CosmeticLOD->UnregisterEnemy(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AFPSREnemyBase::SetViewerLOD(FPSREnemyTuning::EFPSRDistanceBand NewBand, float ViewerDistSq,
	float AnimFreezeRadiusSq)
{
	ViewerBand = NewBand;
	bAnimFrozen = (ViewerDistSq > AnimFreezeRadiusSq);

	// 레벨 트리거 + 사망 가드. AnimProfile 미할당(휴면) 아키타입은 무비용으로 빠진다.
	if (bAnimFrozen && AnimProfile && !(HealthComponent && HealthComponent->IsDead()))
	{
		SetAnimState(EFPSRAnimState::Idle, 0.0f);
	}
}

void AFPSREnemyBase::SetShadowCasting(bool bEnabled)
{
	if (Mesh)
	{
		Mesh->SetCastShadow(bEnabled);
	}
}

void AFPSREnemyBase::SetHealthBarInRange(bool bInRange)
{
	if (bHealthBarInRange == bInRange)
	{
		return;
	}
	bHealthBarInRange = bInRange;
	ApplyHealthBarVisibility();
}

void AFPSREnemyBase::InitHealthBarWidget()
{
	// HB1 §6-2 step 1 — a dedicated server renders nothing, so there is no reason to resolve/init a widget class.
	// This project has no dedicated server today, but the check must not lean on that absence. The engine already
	// blocks this path independently: UWidgetComponent::BeginPlay calls InitWidget unconditionally, whose dedicated-
	// server guard sets TickMode=Disabled BEFORE any tick runs — so TickComponent's own dedi guard is a third,
	// normally-unreachable layer. This return is belt-and-suspenders, not the only safety net.
	if (IsRunningDedicatedServer())
	{
		return;
	}

	// HB1 §6-2 step 2 — defensive null guard. NOTE: a content BP canNOT null a native ctor subobject (the details
	// panel only overrides its properties), so this is not the "BP might delete it" case it may look like — it
	// guards construction-order / CDO edge cases and keeps every later step free of repeated null checks.
	if (!HealthBarWidgetComponent)
	{
		return;
	}

	// HB1 §6-2 step 3 — duplicate-widget diagnostic. MUST run before step 4's early return: the diagnostic is most
	// needed exactly when config is empty (a transitional/misconfigured state), so gating it behind that check would
	// silence it precisely when it matters. Catches the transitional state where a content BP (e.g.
	// BP_EnemyMeleeBase) still authors its OWN manual UWidgetComponent alongside this native one — two bars, a state
	// no compile-time check can see.
	TInlineComponentArray<UWidgetComponent*> AllWidgetComponents;
	GetComponents(AllWidgetComponents);
	if (AllWidgetComponents.Num() > 1)
	{
		UE_LOG(LogFPSR, Warning,
			TEXT("[HealthBar] %s carries %d UWidgetComponents (expected 1) — a content BP likely still authors a manual health-bar widget alongside the native HealthBarWidgetComponent (HB1 사용자 작업 2)."),
			*GetName(), AllWidgetComponents.Num());
	}

	// HB1 §6-2 step 4 — widget CLASS resolution. A BP override on the native component's WidgetClass wins outright
	// (content decides per-archetype); only an UNSET class falls through to the config soft path (Game.md §6-2: no
	// hard-coded asset path in C++).
	if (!HealthBarWidgetComponent->GetWidgetClass())
	{
		if (const UFPSREnemyRenderSettings* Settings = GetDefault<UFPSREnemyRenderSettings>())
		{
			if (UClass* WidgetClass = Settings->HealthBarWidgetClass.LoadSynchronous())
			{
				HealthBarWidgetComponent->SetWidgetClass(WidgetClass);
			}
		}

		if (!HealthBarWidgetComponent->GetWidgetClass())
		{
			// Still unset (config never authored, or the soft path failed to load) — the health-bar feature is OFF.
			// This SetTickMode(Disabled) is REQUIRED, not an optimization: a widget-less component's tick-skip path
			// needs bRenderCleared, which the engine sets ONLY on the world-space draw branch
			// (WidgetComponent.cpp:1279-1295) — this component is screen-space — so without this call every one of
			// up to 500 enemies would tick this component forever for nothing (HB1 §6-2 step 4 / §10). Return before
			// InitWidget/bind/OnHealthBarReady below: there is no widget to init or bind.
			HealthBarWidgetComponent->SetTickMode(ETickMode::Disabled);
			return;
		}
	}

	// HB1 §6-2 step 5 — force the widget to exist NOW (it can otherwise be created lazily on first render, after
	// BeginPlay, leaving the bind below on a null widget).
	HealthBarWidgetComponent->InitWidget();

	// HB1 §6-2 step 6 — bind the health component. A WidgetClass override that is not a UFPSREnemyHealthBarWidget
	// (BP authoring choice) fails the cast and is skipped quietly — that is a legitimate authoring choice, not an
	// error.
	if (UFPSREnemyHealthBarWidget* HealthBarWidget = Cast<UFPSREnemyHealthBarWidget>(HealthBarWidgetComponent->GetUserWidgetObject()))
	{
		HealthBarWidget->BindHealthComponent(HealthComponent);
	}

	// HB1 §6-2 step 7 — extension point, fired only once the widget exists and C++ has already bound it. Note this
	// is a CONTRACT CHANGE from the pre-HB1 behavior, which fired unconditionally regardless of widget presence: the
	// config-empty return at step 4 above now also skips this (HB1 §6-2 step 7 / §11 전환기 증상).
	OnHealthBarReady();
}

#if !UE_BUILD_SHIPPING
void AFPSREnemyBase::DebugForceAnimState(EFPSRAnimState State, bool bPin)
{
	// Bypass EVERY gate SetAnimState applies (dedupe, one-shot re-entry guard, the attack hold window) and push the
	// CPD contract straight at the profile. That is the whole point: it isolates "the material does not react" from
	// "the state driver never asked it to". Re-applies on every call, so a repeated Attack must visibly restart.
	if (!AnimProfile || !Mesh)
	{
		return;
	}
	// Set the pin BEFORE applying, so no driver write can slip between the two. Held across pool reuse on purpose:
	// a debug session pins once and expects it to stay pinned until it is explicitly cleared.
	bDebugAnimPinned = bPin;
	const UWorld* World = GetWorld();
	const float Rate = IsOneShotState(State)
		? 1.0f / FMath::Max(KINDA_SMALL_NUMBER, (State == EFPSRAnimState::Death) ? DeathDwellSeconds : AttackAnimHoldSeconds)
		: 1.0f;
	CurrentAnimState = State;
	CurrentSpeedBucket = FMath::Clamp(static_cast<int32>(Rate * FPSRAnimCPD::SpeedBucketCount), 0, FPSRAnimCPD::SpeedBucketCount - 1);
	AnimOneShotEnterTime = IsOneShotState(State) ? (World ? World->GetTimeSeconds() : 0.0f) : -1.0f;
	AnimOneShotCycleSeconds = IsOneShotState(State) ? (1.0f / FMath::Max(KINDA_SMALL_NUMBER, Rate)) : -1.0f;
	AnimProfile->ApplyAnimState(Mesh, State, Rate, AnimPhase);
}
#endif // !UE_BUILD_SHIPPING

void AFPSREnemyBase::SetHealthBarAllowed(bool bAllowed)
{
	if (bHealthBarAllowed == bAllowed)
	{
		return;
	}
	bHealthBarAllowed = bAllowed;
	ApplyHealthBarVisibility();
}

void AFPSREnemyBase::ApplyHealthBarVisibility()
{
	if (!HealthBarWidgetComponent)
	{
		return;
	}
	const bool bVisible = bHealthBarAllowed && bHealthBarInRange;
	HealthBarWidgetComponent->SetHiddenInGame(!bVisible);

	// HB1 §6-3 A안 — 재킥. 숨겨진 위젯은 스크린 레이어에서 빠지며 컴포넌트가 스스로 틱을 끈다(§3-2/§10); 되켜는
	// 유일한 엔진 경로(OnHiddenInGameChanged)는 SetHiddenInGame 이 **값을 바꿀 때만** 발화하므로, 재사용 엣지에서
	// 값-무변화로 그 경로를 못 타는 경우(Activate/클라 언하이드의 직접 대입 호출, 아래 참조) 를 위해 여기서
	// 무조건 재적용한다. Apply 는 멱등이라 안전하고, GetUserWidgetObject() 조건은 위젯이 없는 도달 가능한 상태
	// (config 공백·전용 서버, 둘 다 TickMode=Disabled)에서 잔여 빈 틱 1회조차 남기지 않기 위함이다.
	if (bVisible && HealthBarWidgetComponent->GetUserWidgetObject())
	{
		HealthBarWidgetComponent->SetComponentTickEnabled(true); // 멱등 · SetComponentTickEnabled 자체가 값-가드를 갖는다
	}
}

void AFPSREnemyBase::HandleDeath(AActor* DeadActor, AActor* Killer)
{
	// Death cosmetics for the listen-server host / standalone: OnDeathCosmetic only fires from OnRep_bDead, which
	// never runs on authority, so without this the host is the one machine that never plays the death state (remote
	// clients do). Mirrors AFPSRBossBase::HandleDeath. Now actually VISIBLE on both sides (previously invisible: the
	// old immediate ReleaseEnemy below hid the actor the same frame) — BeginDying keeps the actor visible/replicating
	// for GetDeathDwellSeconds() before its LATER Deactivate(), see BeginDying/EnterDyingState.
	HandleDeathCosmetic();

	if (UWorld* World = GetWorld())
	{
		if (UFPSRPickupSubsystem* Pickups = World->GetSubsystem<UFPSRPickupSubsystem>())
		{
			Pickups->SpawnXPPickup(GetActorLocation(), XPReward);
		}

		if (UFPSREnemySpawnSubsystem* Sub = World->GetSubsystem<UFPSREnemySpawnSubsystem>())
		{
			// Death is the ONE teardown path that is NOT an immediate ReleaseEnemy: BeginDying pulls this actor out
			// of ActiveEnemies right now (so it can't move/attack/shield the swarm) but defers the hide+pool-return
			// to its death-dwell deadline, so the Death cosmetic just triggered above actually gets seen. Every
			// OTHER teardown (pool release / rear-drain / kill-Z / stage-carry overflow) stays an immediate
			// ReleaseEnemy — see the spawn subsystem's BeginDying doc comment for the full call-site reasoning.
			Sub->BeginDying(this);
			return;
		}
	}
	Destroy();
}

void AFPSREnemyBase::EnterDyingState()
{
	// Promoted from AFPSRRangedEnemyBase (ADR 0013 C1). Same reason as Deactivate() (see that function's comment),
	// but earlier: whichever teardown reaches this enemy first, the held ranged state must close HERE, not wait for
	// the LATER Deactivate() call — a ranged corpse can now dwell for GetDeathDwellSeconds() before that runs, and
	// the target's warning indicator (+ this enemy's concurrency token) must not stay held for the whole dwell
	// window. Must run BEFORE collision goes off below (P1 lifecycle contract — see ReleaseRangedHold).
	ReleaseRangedHold();
	ResetRangedCycle();

	// STAT1 §7-6 폐쇄 지점 1/4 (rev4가 처음 지목한 것 — B단계는 ResetForReuse 에만 걸어 두었다): without this, a
	// corpse dwells for GetDeathDwellSeconds() with StatusBits still ON and still replicating — a remote client
	// sees a status icon on a body that already stopped taking further hits/progression. The driver itself turns
	// off a moment earlier, in UFPSREnemySpawnSubsystem::BeginDying's own ActiveEnemies.Remove (the caller of this
	// function) — this call closes the status VALUES, that one closes the ADVANCE privilege.
	if (HealthComponent)
	{
		HealthComponent->ClearStatusForReuse();
	}

#if !UE_BUILD_SHIPPING
	// GASM1 측정 티어다운(§6-C) — 엘리트와 같은 쌍(AFPSREnemyEliteBase::EnterDyingState 의
	// AbilitySystem->CancelAbilities() 와 같은 자리, collision-off 보다 앞). ASC 는 파괴하지 않는다(§8,
	// 실수명당 1회 원칙) — CancelAbilities/RemoveActiveEffects 만. Instant GE 뿐이라 RemoveActiveEffects 의
	// 컨테이너는 사실상 항상 비어 있지만(빈 컨테이너=비용 미미), 실제 엘리트가 매 티어다운에 지불하는 경로를
	// 그대로 밟아 구성 ②③ 의 대표성을 높인다.
	if (bMeasureLoadoutCached && MeasureASC)
	{
		MeasureASC->CancelAbilities();
		MeasureASC->RemoveActiveEffects(FGameplayEffectQuery());
	}
#endif

	// Gameplay ends NOW; presentation does not — see this function's header doc for the full EnterDyingState vs.
	// Deactivate role split. No hide / no SetNetDormancy here (unlike Deactivate): bDead already replicated before
	// HandleDeath ever ran (the health component's ApplyDamage->OnDeath fires first), so a remote client's own
	// OnRep_bDead -> HandleDeathCosmetic needs this actor to keep rendering/replicating for the dwell window to be
	// seen. Collision off so a dying enemy can never deal another contact hit, and — the concrete swarm-scale
	// motivation — a front-row corpse can no longer shield the enemies behind it from LineTraceMulti (which stops at
	// the first blocking hit) or from the movement/attack pass's stop-distance queries.
	SetActorEnableCollision(false);
}

#if !UE_BUILD_SHIPPING
UAbilitySystemComponent* AFPSREnemyBase::GetMeasureAbilitySystemComponent() const
{
	// 기저 타입으로 돌려준다 — 헤더의 선언 주석 참조(DumpEliteState 가 쓰는 것과 같은 형태). 본체는 .cpp
	// 에서만 정의 가능하다: 헤더는 UFPSRAbilitySystemComponent 를 전방선언만 했으므로, 그 타입의 완전한
	// 정의(상속 관계)가 안 보이는 상태에서는 UFPSRAbilitySystemComponent* -> UAbilitySystemComponent* 업캐스트
	// 자체가 인라인으로 컴파일되지 않는다.
	return MeasureASC;
}
#endif

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
	LastAttackTime = -1000.0f; // Stamped by NotifyAttacked on every successful ranged shot. ADR 0013 C0 removed its
	                           // last LIVE reader with the melee CanAttack cooldown gate, so it now feeds only the
	                           // pursuit stall detector — which ADR 0009 retired but kept armed (see the dormant
	                           // pursuit fields above). Still reset here: a reused actor must not hand a prior life's
	                           // attack clock to the stall detector if that path is ever re-armed.
	AttackAnimHoldUntil = -1.0f; // same reasoning as LastAttackTime just above — a reused actor must not inherit a
	                             // prior life's walk/idle-suppression window (a short-lived corpse reused shortly
	                             // after dwelling could otherwise spawn already "holding" for its remaining span)
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
	AnimOneShotEnterTime = -1.0f;
	AnimOneShotCycleSeconds = -1.0f;
	LastRecvTime = -1.0f;
	// LOD1: viewer-band + anim-freeze tracking reset for the reused actor — the cosmetic LOD pass re-derives both
	// authoritatively within its next pass (<=0.2s), but a stale prior-life S3/frozen reading must not survive until
	// then (e.g. a corpse that died far away, reused right next to a player). bHealthBarInRange is deliberately NOT
	// reset here — see that field's own comment.
	ViewerBand = FPSREnemyTuning::EFPSRDistanceBand::S0;
	bAnimFrozen = false;
	// HB1 §6-3 A안: direct assign + unconditional Apply, NOT the setter — reverses HandleDeathCosmetic's hide (the
	// widget survives pooling, the bind doesn't rerun). A non-death reuse (rear-drain etc.) reaches this with
	// bHealthBarAllowed ALREADY true, so the setter's value-guard would skip ApplyHealthBarVisibility entirely and
	// leave a near-player reused enemy's bar hidden for its whole life (the "재표시 홀" §6-3 closes). Apply is
	// idempotent, so calling it unconditionally here is safe.
	bHealthBarAllowed = true;
	ApplyHealthBarVisibility();
	// CPD survives pooling reuse (see the phase write in BeginPlay), so a prior life's hit stamp would otherwise ride
	// into this one and flash an enemy the instant it respawns — CPDSlot_LastHitTime's contract is "this life".
	if (AnimProfile && Mesh)
	{
		Mesh->SetCustomPrimitiveDataFloat(FPSRAnimCPD::CPDSlot_LastHitTime, 0.0f);
	}
	// HealthComponent->ResetForReuse() above already resynced this via its OnHealthChanged broadcast (when
	// AnimProfile is set — see HandleHealthChangedForHitFlash). This explicit resync is belt-and-suspenders for the
	// dormant (no AnimProfile yet) case, so the tracker can never carry a stale prior-life value forward. GetHealth()
	// reads MaxHealth here (ResetForReuse already ran above), so this life's own first real hit is never swallowed.
	LastHealthForHitFlash = HealthComponent ? HealthComponent->GetHealth() : 0.0f;
	SetAnimState(EFPSRAnimState::Idle);

	// Promoted from AFPSRRangedEnemyBase (ADR 0013 C1). Fresh reuse: any prior hold was released by the matching
	// Deactivate; clear defensively so no stale warning/token leaks into the new life, then reset the cycle.
	bHoldingToken = false;
	HeldTargetPC = nullptr;
	ResetRangedCycle();

	// Defensive reset: bCharging should already be false via ReleaseRangedHold on every teardown path (Deactivate /
	// EnterDyingState / EndPlay all route through it), but Activate is the ONE pool-reuse entry point every archetype
	// life passes through — belt-and-suspenders so a reused actor can never render the non-targeted charge telegraph
	// before its first real charge this life.
	if (bCharging)
	{
		bCharging = false;
		MARK_PROPERTY_DIRTY_FROM_NAME(AFPSREnemyBase, bCharging, this);
	}

#if !UE_BUILD_SHIPPING
	// GASM1 측정 시임(Docs/Specs/GASM1_SwarmASCCostMeasurement.md §6-A) — Super 의(위) SetNetDormancy(DORM_
	// Awake) 뒤에서 실행: 부착의 복제가 깨어난 뒤에 실리도록. CVar 둘 다 off 면 즉시 스킵 — 프로덕션 경로
	// diff 0(§10).
	const bool bMeasureAttachASC = CVarAttachASC.GetValueOnGameThread() != 0;
	const bool bMeasureLoadout = CVarMeasureLoadout.GetValueOnGameThread() != 0; // AttachASC 를 함의(아래 OR 로 강제)
	// 🔁 신설(G2 P2-1, 구성 ③ᵀ) — MeasureTickable 은 MeasureLoadout 을 함의한다(§5-A: "세트가 있어야
	// Tickable 을 걸 곳이 있다"). CVarMeasureLoadout->CVarAttachASC 함의가 이미 쓰는 것과 같은 방식으로
	// OR 에 실어 코드에서 강제한다 — bEffectiveLoadout 이 true 면 아래 로드아웃 부착 분기가 돈다.
	const bool bMeasureTickableRequested = CVarMeasureTickable.GetValueOnGameThread() != 0;
	const bool bEffectiveLoadout = bMeasureLoadout || bMeasureTickableRequested;
	if (bMeasureAttachASC || bEffectiveLoadout)
	{
		// 🔴 이중 부착 가드 — 이 액터가 **자기 것이 아닌** ASC 를 이미 갖고 있으면(엘리트의 진짜
		// AbilitySystem) 측정 시임을 얹지 않는다. AFPSREnemyEliteBase::Activate 는 Super::Activate(this)를
		// 먼저 호출하므로(FPSREnemyEliteBase.cpp:79-81) 엘리트에 CVar 를 켜도 여기서 걸려 MeasureASC 가
		// 안 생긴다. AFPSRBossBase 는 애초에 AFPSREnemyBase 를 상속하지 않아(ACharacter 직계) 이 함수
		// 자체를 타지 않는다.
		//
		// 🔁 **자기 자신의 MeasureASC 는 "이중"이 아니다.** 단순히 `!FindComponentByClass<...>()` 로 쓰면
		// 두 번째+ 삶에서 이 체크가 지난 삶에 붙인 MeasureASC 를 찾아내 아래 블록 전체를 스킵한다. 그러면
		//   · ResetForMeasure() 가 안 돌아 어트리뷰트 값이 삶을 넘어 이어지고(§6-A 가 🔴 필수로 못박은 것)
		//   · MeasureActivationCount 가 삶별이 아니라 실수명 누적치가 되어 N 사후 검증(§12-A)이 무의미해지며
		//   · bMeasureLoadoutCached 가 갱신되지 않는다 — 러너가 CVar 를 세우기 **전에** 한 번이라도 Activate
		//     된 액터는 캐시가 false 로 굳어 그 캡처 내내 측정에서 조용히 빠진다(= 무음 오염).
		// 풀은 액터를 파괴하지 않으므로(불변식 5) 재활성화는 예외가 아니라 상시다 — §5 실측의 "요청 300 →
		// 정착 254"(Performance.md:47)가 그 증거다.
		UAbilitySystemComponent* const ExistingASC = FindComponentByClass<UAbilitySystemComponent>();
		if (!ExistingASC || ExistingASC == MeasureASC)
		{
			if (!MeasureASC) // 🔴 실수명당 1회
			{
				MeasureASC = NewObject<UFPSRAbilitySystemComponent>(this, TEXT("MeasureASC"));
				MeasureASC->SetIsReplicated(true); // RegisterComponent 앞 — NewObject 직후엔 아직
				                                    // RF_NeedInitialization 라 ensure 없이 안전
				                                    // (엔진 ActorComponent.cpp:3415-3418, NeedsInitialization()).
				MeasureASC->RegisterComponent();
				MeasureASC->SetReplicationMode(EGameplayEffectReplicationMode::Minimal); // 엘리트와 동일
				MeasureASC->InitAbilityActorInfo(this, this);
				MeasureASC->EnableTimeAxisGuard();
			}
			if (bEffectiveLoadout)
			{
				if (!MeasureSet)
				{
					// 🔴 명세 §6-A pseudocode 는 "...GetOrCreateAttributeSubobject(...)"로 수신 표현식을
					// 생략해 뒀는데(리터럴 코드가 아니라는 뜻), 실제로 그 메서드는 엔진에서 protected 다
					// (AbilitySystemComponent.h:1893, 그 앞의 마지막 접근지정자는 :1681 의 protected: —
					// UAbilitySystemComponent 와 무관한 AFPSREnemyBase 에서 직접 부르면 컴파일이 안 된다).
					// 엔진이 제공하는 공개 템플릿 래퍼 AddSet<T>() 를 대신 쓴다 — 그 본체가 정확히
					// `return (T*)GetOrCreateAttributeSubobject(T::StaticClass());`(AbilitySystemComponent.h:
					// 153-157)라 완전히 같은 호출이고, 제약이 요구하는 "GetOrCreateAttributeSubobject 를
					// 쓰고 AddSpawnedAttribute 는 쓰지 마라"의 IsA 클래스-중복 필터링 동작도 그대로다.
					// const 를 돌려주는 이유도 같은 원문 설계(Set-파이프라인 경유 값쓰기 유도) —
					// ResetForMeasure() 는 우리가 소유한 타입의 의도된 직접-변경 API라 const_cast 한다
					// (같은 리포의 UFPSREnemySpawnSubsystem::GetLastGroundedZ 키-생성 const_cast 와 같은 선례).
					MeasureSet = const_cast<UFPSRMeasureAttributeSet*>(MeasureASC->AddSet<UFPSRMeasureAttributeSet>());
				}
				if (MeasureSet)
				{
					MeasureSet->ResetForMeasure(); // 🔴 값은 삶을 넘어 이어진다 — 매 삶 리셋 필수
				}
				if (!MeasureAbilityHandle.IsValid()) // 🔴 실수명당 1회 — 삶마다면 스펙이 누적된다
				{
					MeasureAbilityHandle = MeasureASC->GiveAbility(
						FGameplayAbilitySpec(UFPSRMeasureDummyAbility::StaticClass(), 1, INDEX_NONE, this));
				}
			}
			if (MeasureSet) // 🔁 신설(G2 P2-1) — bEffectiveLoadout 이 이번 삶엔 false 라도(예: AttachASC 만
			                 // 켜진 구성 ②) 이전 삶에 로드아웃을 켰던 MeasureSet 이 남아있을 수 있다(§8: ASC·
			                 // 세트는 해제하지 않는다) — 그 삶엔 Tickable 요청도 없으므로 무조건 최신값(false)
			                 // 으로 덮어써야 한다. bEffectiveLoadout 이 true 인 삶엔 방금 위에서 만든/리셋한
			                 // MeasureSet 에 CVar 값을 그대로 반영한다.
			{
				MeasureSet->bMeasureTickable = bMeasureTickableRequested;
			}
			MeasureClockSeconds = 0.0f;
			MeasureActivationCount = 0;
			bMeasureLoadoutCached = bEffectiveLoadout;
			// 캐던스도 여기서 1회만 확정한다 — 틱에서 CVar 를 다시 조회하면 그 비용이 구성 ③ 에만 붙어
			// ②↔③ 델타로 새어든다(멤버 주석 참조). 경고·스냅은 이 호출 안에서 엣지 트리거로 처리된다.
			MeasureCadenceCached = GetEffectiveMeasureCadenceSeconds();
		}
	}
	else
	{
		// 🔁 G2 P3-1 — CVar 가 전부 off 인 삶에서는 캐시를 블록 밖에서 무조건 되돌린다. 이 else 가 없으면
		// (수정 전 코드) 한 세션에서 MeasureLoadout/MeasureTickable 을 켰다가 CVar 를 전부 0 으로 내렸을 때,
		// 그 뒤 재활성화되는 액터가 위 if 블록 전체를 건너뛰어 이전 삶의 true 캐시가 영구 잔존했다 —
		// bMeasureLoadoutCached 가 그대로면 ServerTickAttack 이 어빌리티를 계속 쏘고, MeasureSet 이 있는데
		// bMeasureTickable 이 그대로면 ShouldTick() 이 계속 true 를 돌려줘 ASC 가 그 삶 내내 계속 틱한다
		// (§10). 둘 다 이번 삶엔 측정이 꺼졌다는 뜻이므로 false 로 되돌린다 — MeasureASC/MeasureSet 자체는
		// 파괴하지 않는다(§8, 실수명당 1회 원칙).
		bMeasureLoadoutCached = false;
		if (MeasureSet)
		{
			MeasureSet->bMeasureTickable = false;
		}
	}
#endif
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
	// Promoted from AFPSRRangedEnemyBase (ADR 0013 C1). Pool release / death-dwell completion / kill-Z recycle all
	// route here — close the warning + release the token on EVERY teardown path (not just an explicit abort) so a
	// Reliable 'off' is never dropped and the concurrency count never leaks.
	ReleaseRangedHold();
	ResetRangedCycle();

	// STAT1 §7-6 폐쇄 지점 2/4: EnterDyingState already closes the DEATH path's status state; this covers every
	// OTHER teardown that reaches Deactivate WITHOUT going through EnterDyingState first (pool release / rear-drain
	// / kill-Z recycle / stage-carry overflow — this function's own header doc) — none of those call
	// EnterDyingState, so without this a rear-drained infected enemy would return to the dormant pool still
	// carrying live StatusBits into its NEXT life. Idempotent on an already-clear state (the ordinary death path,
	// where EnterDyingState got there first).
	if (HealthComponent)
	{
		HealthComponent->ClearStatusForReuse();
	}

#if !UE_BUILD_SHIPPING
	// GASM1 측정 티어다운(§6-C) — 엘리트와 같은 쌍(AFPSREnemyEliteBase::Deactivate 참조). 🔴 반드시 아래
	// SetNetDormancy(DORM_DormantAll) 이전에 실행 — GE 제거의 복제가 awake->dormant 플러시에 실리려면 아직
	// DORM_Awake 인 동안 실행돼야 한다(엘리트 Deactivate 헤더 주석의 "must run BEFORE Super::Deactivate()
	// flips this actor to DORM_DormantAll" 과 동일 근거). ASC 는 파괴하지 않는다(§8, 실수명당 1회 원칙).
	if (bMeasureLoadoutCached && MeasureASC)
	{
		MeasureASC->CancelAbilities();
		MeasureASC->RemoveActiveEffects(FGameplayEffectQuery());
	}
#endif

	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetNetDormancy(DORM_DormantAll);
}

bool AFPSREnemyBase::IsOneShotState(EFPSRAnimState InState)
{
	return InState == EFPSRAnimState::Attack || InState == EFPSRAnimState::Death;
}

void AFPSREnemyBase::SetAnimState(EFPSRAnimState NewState, float PlayRate)
{
	// Dormant unless an archetype opted into animation; no local rendering (so no cosmetics) on a dedicated server.
	if (!AnimProfile || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	// Canary pin (FPSR.Enemy.ForceAnimState <state> 1): the whole driver is muted so the forced state actually
	// holds. Without this the canary is only a poke and cannot tell a driver-caused restart from a material one.
	if (bDebugAnimPinned)
	{
		return;
	}
#endif

	// PlayRate < 0 is the "caller didn't pass one" sentinel (see the header doc) — a plain 1.0f default couldn't be
	// told apart from a caller explicitly requesting normal speed, and a one-shot state needs a DURATION-derived rate
	// (never a guessed 1.0) so its material progress (Time-EnterTime)*Rate reaches 1.0 exactly at the authored
	// hold/dwell length — so the default itself has to carry that information instead of the argument's value.
	if (PlayRate < 0.0f)
	{
		PlayRate = IsOneShotState(NewState)
			? 1.0f / FMath::Max(KINDA_SMALL_NUMBER, (NewState == EFPSRAnimState::Death) ? DeathDwellSeconds : AttackAnimHoldSeconds)
			: 1.0f;
	}

	// Quantize the playrate so the scalar is re-written only when it crosses a bucket boundary (write-on-change). A
	// frozen clip (playrate 0) lands in bucket 0 and a playing clip in a higher bucket, so freeze<->play transitions
	// still trigger exactly one write.
	const int32 NewBucket = FMath::Clamp(static_cast<int32>(PlayRate * FPSRAnimCPD::SpeedBucketCount), 0, FPSRAnimCPD::SpeedBucketCount - 1);

	// A one-shot RE-ENTERED from itself needs its own rule — the plain dedupe below would freeze it on its final pose
	// forever (a melee attacker re-entering Attack every cooldown sits in the same state AND the same playrate
	// bucket), but bypassing the dedupe unconditionally is just as wrong: the CLIENT attack tell fires from
	// PostNetReceiveLocationAndRotation on EVERY net update while the enemy is in range, which would restamp
	// EnterTime before the clip ever finished and rewind it to frame 0 forever. So: restart a one-shot only once its
	// previous cycle has actually elapsed.
	if (IsOneShotState(NewState) && NewState == CurrentAnimState)
	{
		// Death is TERMINAL — the actor is on its way out, and PostNetReceiveLocationAndRotation re-asserts Death for
		// as long as IsDead(), so a completion-based restart would loop the death animation until the corpse hides.
		if (NewState == EFPSRAnimState::Death)
		{
			return;
		}
		const UWorld* World = GetWorld();
		const float Now = World ? World->GetTimeSeconds() : 0.0f;
		// The RUNNING cycle's length, not one derived from this call's rate — see AnimOneShotCycleSeconds' comment.
		if (AnimOneShotEnterTime >= 0.0f && (Now - AnimOneShotEnterTime) < AnimOneShotCycleSeconds)
		{
			return; // still playing — do not rewind it
		}
	}
	else if (NewState == CurrentAnimState && NewBucket == CurrentSpeedBucket)
	{
		return; // event-driven: state + playrate bucket unchanged, nothing to write
	}
	CurrentAnimState = NewState;
	CurrentSpeedBucket = NewBucket;
	if (IsOneShotState(NewState))
	{
		const UWorld* World = GetWorld();
		AnimOneShotEnterTime = World ? World->GetTimeSeconds() : 0.0f;
		AnimOneShotCycleSeconds = 1.0f / FMath::Max(KINDA_SMALL_NUMBER, PlayRate);
	}
	else
	{
		AnimOneShotEnterTime = -1.0f;
		AnimOneShotCycleSeconds = -1.0f;
	}

	if (Mesh)
	{
		AnimProfile->ApplyAnimState(Mesh, NewState, PlayRate, AnimPhase);
	}
}

void AFPSREnemyBase::HandleHealthChangedForHitFlash(float NewHealth, float MaxHealth)
{
	(void)MaxHealth;

	// Same dormant/dedicated-server gate as SetAnimState above — zero cost until an archetype opts in, and a
	// dedicated server never renders so it never needs the write.
	if (!AnimProfile || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// Decrease-edge gate: ResetForReuse() (FPSREnemyHealthComponent.cpp) broadcasts this SAME delegate on pool reuse
	// (Health snaps 0 -> MaxHealth there), which is an INCREASE, not a hit — without this check every pooled enemy
	// would flash the instant it (re)spawns. LastHealthForHitFlash is resynced in Activate() too (see its own
	// comment), so a reused actor's first real hit this life is judged against that life's own starting Health,
	// never a stale prior-life value.
	const bool bDecreased = NewHealth < LastHealthForHitFlash;
	LastHealthForHitFlash = NewHealth;
	if (!bDecreased || !Mesh)
	{
		return;
	}

	const UWorld* World = GetWorld();
	Mesh->SetCustomPrimitiveDataFloat(FPSRAnimCPD::CPDSlot_LastHitTime, World ? World->GetTimeSeconds() : 0.0f);
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

	// Distance LOD: bAnimFrozen is set by the cosmetic LOD pass (UFPSREnemyCosmeticLODSubsystem -> SetViewerLOD),
	// which re-issues SetAnimState(Idle, 0) as a level trigger every pass while frozen — this driver only needs to
	// step aside so it doesn't fight that write with its own Walk/Idle/Attack one. Runs on every rendering machine,
	// including a listen-server host (LOD1 — this used to be a client-only DistSq recompute here).
	if (bAnimFrozen)
	{
		return;
	}

	// Melee attack tell (cosmetic heuristic): stationary AND within melee range of the local player. Damage stays
	// server-authoritative regardless of this tell. Refined with the real attack clip in Stage 3.
	if (!bMoving && LocalPawn && DistSqToLocal <= (AttackRange * AttackRange))
	{
		SetAnimState(EFPSRAnimState::Attack);
		AttackAnimHoldUntil = Now + AttackAnimHoldSeconds; // mirrors the authority stamp in ServerTickAttack
		return;
	}

	// The same hold the AUTHORITY driver has always applied (TickServerMovement's walk/idle branch), which this
	// client-side driver was missing entirely — an Attack one-shot owns the cosmetic until its window elapses.
	// Without it: (a) a swarm enemy whose bMoving flickers across the 10 cm/s threshold leaves Attack for Walk and
	// comes straight back, and a re-entry from a DIFFERENT state skips SetAnimState's one-shot re-entry guard, so
	// CPD EnterTime is restamped and the material's (Time-EnterTime)*Rate progress rewinds to 0 mid-play; (b) the
	// ranged charge tell was worse still — a ranged enemy sits far outside AttackRange, so this branch erased
	// OnRep_Charging's telegraph on the very next net update, ~33 ms into a charge lasting RangedChargeTime.
	// Placed AFTER the melee tell so a fresh attack can always re-stamp its own window, and after the distance-LOD
	// freeze above so the perf gate still wins at range.
	if (Now < AttackAnimHoldUntil)
	{
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
		AnimOneShotEnterTime = -1.0f;
		AnimOneShotCycleSeconds = -1.0f;
		AttackAnimHoldUntil = -1.0f; // client mirror of Activate()'s authority-side reset — a reused actor must not
		                             // spawn already suppressing walk/idle for a prior life's remaining hold span
		LastRecvTime = -1.0f;
		// LOD1: client mirror of Activate()'s ViewerBand/bAnimFrozen reset — see that block's own comment.
		ViewerBand = FPSREnemyTuning::EFPSRDistanceBand::S0;
		bAnimFrozen = false;
		// HB1 §6-3 A안: client mirror of Activate()'s direct-assign + unconditional Apply (a remote client never
		// runs Activate) — see that block's comment for why the setter's value-guard must be bypassed here too.
		bHealthBarAllowed = true;
		ApplyHealthBarVisibility();
		if (AnimProfile && Mesh) // clear the prior life's hit stamp — see the authority-side reset for why
		{
			Mesh->SetCustomPrimitiveDataFloat(FPSRAnimCPD::CPDSlot_LastHitTime, 0.0f);
		}
		SetAnimState(EFPSRAnimState::Idle, 1.0f);
	}
}

void AFPSREnemyBase::HandleDeathCosmetic()
{
	// Client death edge (from the health component's OnRep_bDead). Enter the Death animation state. No-op when dormant.
	// For a SWARM enemy, HandleDeath -> UFPSREnemySpawnSubsystem::BeginDying keeps this actor visible/replicating
	// (EnterDyingState only disables collision) for GetDeathDwellSeconds() before the LATER Deactivate() actually
	// hides it and returns it to the pool — so the Death state entered here now has a real window to be seen, instead
	// of being applied to an actor hidden the same frame. The BOSS (AFPSRBossBase) persists after death entirely, so
	// its death montage was always visible regardless.
	SetAnimState(EFPSRAnimState::Death);

	// Drop the health bar the instant death is known, not when the corpse finally hides. The dwell that makes the
	// death motion visible would otherwise leave a full-looking bar floating over a shrinking corpse for its whole
	// length, which reads as "it isn't dead yet" (PIE 2026-08-25). This runs on clients (OnRep_bDead) and on the
	// host (HandleDeath calls it directly), which is exactly the pair that renders the bar.
	SetHealthBarAllowed(false);
}

bool AFPSREnemyBase::ServerCancelRangedForStageTransition()
{
	if (!HasAuthority())
	{
		return false;
	}

	// Read BEFORE releasing — ReleaseRangedHold clears exactly these. "Was mid-cycle" covers all three because a
	// Cooldown-state enemy holds no token and shows no warning, yet still needs the reset so it can re-engage
	// immediately in the new arena instead of burning the rest of an old arena's cooldown.
	const bool bWasActive = bCharging || bHoldingToken || (ChargeState != EFPSRRangedChargeState::Idle);

	// The SAME pair every teardown path uses (Deactivate / EnterDyingState) — see the header for why a transition
	// needs its own entry point despite those existing: the enemy is CARRIED OVER, not torn down, so none of them run.
	ReleaseRangedHold();
	ResetRangedCycle();

	return bWasActive;
}

void AFPSREnemyBase::ServerTickAttack(const FFPSRServerAttackContext& Ctx)
{
	// Defensive: A-2 (UFPSREnemySpawnSubsystem::BeginDying) already makes this structurally unreachable — a dying
	// enemy is pulled OUT of ActiveEnemies the instant HandleDeath runs, and the subsystem's per-pass loop only ever
	// calls ServerTickAttack on enemies it iterates FROM that set — but this function had NO IsDead gate of its own
	// before this stage, so a future call path that doesn't route through that same set would silently reopen a
	// dead-enemy-attacks bug. Cheap: one bool check per pass.
	if (HealthComponent && HealthComponent->IsDead())
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	// GASM1 측정 구동(§6-B) — 멤버 bool 1회만 읽는다(CVar 조회 아님, §10 기본 경로 비용: CVar off 면
	// 여기서 즉시 끝난다). 형태는 보스 선례 FPSRBossBase.cpp:960-962 의 TryActivateAbility 호출을 그대로
	// 복제한다. IsDead 가드 바로 다음(타겟 유무 분기·bDisableAttack 게이트보다 앞)에 둔 이유 — 엘리트의
	// EliteCooldownClockSeconds 누산기가 "타겟 있음/없음 두 분기 모두에서 조건 없이" 도는 것
	// (FPSREnemyEliteBase.h:79-85 헤더 주석)과 같은 성격으로, 측정 캐던스가 사냥 로직의 타겟 유무나
	// 상태이상(블라인드 등)에 좌우되지 않게 하기 위함이다. ⚠️ ServerTickAttack 은 tier 별 AttackStride(F1)
	// 로 스킵되므로 이 함수 자체의 호출 빈도가 tier 마다 다르다 — MeasureActivationCount 로 실제 발동 수를
	// 확인할 것(GetMeasureActivationCount, ASCDump).
	if (bMeasureLoadoutCached)
	{
		MeasureClockSeconds += Ctx.DeltaSeconds; // 🔴 §2-2 프리즈 중엔 Ctx.DeltaSeconds 자체가 0 — 자동으로 멈춘다
		if (MeasureClockSeconds >= MeasureCadenceCached) // Activate 에서 1회 확정 — 여기서 CVar 를 조회하지 않는다
		{
			MeasureClockSeconds = 0.0f;
			if (MeasureASC && MeasureASC->TryActivateAbility(MeasureAbilityHandle)) // MeasureASC 는 방어적
			                                                                        // 재확인(bMeasureLoadoutCached
			                                                                        // 가 true 면 §6-A 상 항상
			                                                                        // non-null 이지만 belt-and-suspenders)
			{
				++MeasureActivationCount;
			}
		}
	}
#endif

	// STAT1 §6 실명 (bDisableAttack): true on EVERY pass this holds, release the ranged hold/token and skip the
	// whole cycle below. ReleaseRangedHold/ResetRangedCycle are BOTH documented idempotent (see their own comments),
	// so calling them every pass — not just the onset edge — is behaviorally identical to "onset then early-out"
	// (nothing else can move ChargeState/the token/the warning while we keep returning here first) and needs no new
	// onset-edge-tracking member. 🔴 Releasing the hold is NOT optional here (G1-8): an early-out that only skipped
	// the switch below would leave the token (RangedAttackTokenLimit=3) and the target's directional warning held
	// for the WHOLE blind duration, stalling that player's incoming fire from the REST of the swarm too — this is
	// the same pair Deactivate()/EnterDyingState() use. Covers both paths blind can turn on from (ApplyStatus's
	// immediate combo check, AdvanceStatus's periodic re-check) because this reads the RESOLVED bit, not which call
	// flipped it.
	const FFPSRResolvedStatus AttackStatus = HealthComponent ? HealthComponent->GetResolvedStatus() : FFPSRResolvedStatus();
	if (AttackStatus.bDisableAttack)
	{
		ReleaseRangedHold();
		ResetRangedCycle();
		return;
	}

	// Promoted from AFPSRRangedEnemyBase (ADR 0013 C1). The subsystem already early-returns the whole pass while the
	// run is frozen, so DeltaSeconds only accrues during active gameplay — the charge/cooldown accumulators below
	// are freeze-paused for free.
	const float Dt = Ctx.DeltaSeconds;
	const bool bHaveTarget = (Ctx.TargetChar != nullptr) && (Ctx.TargetController != nullptr);
	const bool bInRange = bHaveTarget
		&& FVector::DistSquared(GetActorLocation(), Ctx.TargetLocation) <= FMath::Square(RangedEngageRange);

	// STAT1 §6 공격속도저하 (AttackIntervalMultiplier, >1 = slower): read AT USE, on the THRESHOLD side of the
	// compare, never baked into RangedChargeTime/RangedFireCooldown themselves — those are ACTOR-INSTANCE
	// UPROPERTYs, so multiplying them in place would leak into this pooled actor's NEXT life the moment
	// ClearStatusForReuse resets the status but leaves the (already-mutated) field behind.
	const float EffectiveChargeTime = RangedChargeTime * AttackStatus.AttackIntervalMultiplier;
	const float EffectiveFireCooldown = RangedFireCooldown * AttackStatus.AttackIntervalMultiplier;

	switch (ChargeState)
	{
	case EFPSRRangedChargeState::Idle:
	{
		UFPSREnemySpawnSubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UFPSREnemySpawnSubsystem>() : nullptr;
		// Cheap gates first (range, then a read-only token peek) so a capped-out idle ranged enemy never pays for the
		// line-of-sight trace every pass at swarm scale (Game.MD §5). Acquire only after LOS confirms a clear shot.
		if (bInRange && Sub && Sub->IsRangedTokenAvailable(Ctx.TargetController) && HasLineOfSight(Ctx.TargetChar, Ctx.TargetLocation))
		{
			if (Sub->TryAcquireRangedToken(Ctx.TargetController))
			{
				ChargeState = EFPSRRangedChargeState::Charging;
				ChargeElapsed = 0.0f;
				bHoldingToken = true;
				HeldTargetPC = Ctx.TargetController;
				LastWarnLocation = GetActorLocation();
				SendRangedWarning(true); // telegraph: the target gets a directional warning to dodge

				// Drive the Attack cosmetic at the EFFECTIVE charge-length rate so the material's
				// (Time-EnterTime)*Rate progress reaches exactly 1.0 the moment the shot fires (not the melee
				// AttackAnimHoldSeconds default), and hold it there for the same span so TickServerMovement's
				// walk/idle branch can't stomp it mid-charge (a stationary/slow-repositioning charger can still read
				// as bMoved on a separation-jitter pass).
				const float ChargeRate = 1.0f / FMath::Max(KINDA_SMALL_NUMBER, EffectiveChargeTime);
				SetAnimState(EFPSRAnimState::Attack, ChargeRate);
				AttackAnimHoldUntil = Ctx.Now + EffectiveChargeTime;

				// Non-targeted client telegraph (user decision, see bCharging's own comment): replicate the charge
				// to EVERY client, not just the Reliable-RPC'd target.
				bCharging = true;
				MARK_PROPERTY_DIRTY_FROM_NAME(AFPSREnemyBase, bCharging, this);
			}
		}
		break;
	}
	case EFPSRRangedChargeState::Charging:
	{
		// Abort if the target left range, became non-engageable (DBNO/dead players are filtered out of the
		// subsystem's PlayerPawns, so the nearest target changes), or we re-targeted a different player. Release the
		// token + clear the warning, then briefly cool down to avoid instant re-charge flicker.
		const bool bSameTarget = HeldTargetPC.IsValid() && (Ctx.TargetController == HeldTargetPC.Get());
		if (!bInRange || !bSameTarget)
		{
			ReleaseRangedHold();
			ChargeState = EFPSRRangedChargeState::Cooldown;
			CooldownElapsed = 0.0f;
			break;
		}

		ChargeElapsed += Dt;

		// STAT1 §6 (G1-9): keep the movement-anim hold tracking the CURRENT effective charge time every pass, not
		// just the value stamped once at the Idle->Charging transition above — if AttackIntervalMultiplier changes
		// MID-CHARGE (a slow debuff lands or expires while already telegraphing), a hold window fixed at entry would
		// expire before the (now longer) real charge completes, and TickServerMovement's walk/idle branch would
		// stomp the Attack pose mid-telegraph. The visual clip's own playback RATE (set once at entry via
		// SetAnimState) deliberately does NOT get re-derived here: SetAnimState's one-shot re-entry guard refuses to
		// rewind an already-playing clip (see its own comment), so re-asserting a new rate here would either no-op
		// through that guard or fight it — the HOLD-WINDOW timestamp has no such guard and is exactly what the
		// walk/idle-stomp bug this note describes keys on. When the multiplier hasn't changed, this recompute is a
		// no-op relative to the entry stamp (Ctx.Now advances by the same Dt ChargeElapsed does, so they cancel).
		AttackAnimHoldUntil = Ctx.Now + FMath::Max(0.0f, EffectiveChargeTime - ChargeElapsed);

		// Track the moving source: re-send the warning location once we've drifted (separation nudges us while we
		// hold), so the indicator points at where we actually are. Throttled by distance (no per-frame Reliable spam).
		if (FVector::DistSquared(GetActorLocation(), LastWarnLocation) > WarnResendDistSq)
		{
			LastWarnLocation = GetActorLocation();
			SendRangedWarning(true);
		}

		if (ChargeElapsed >= EffectiveChargeTime)
		{
			FireProjectile(Ctx);
			NotifyAttacked(Ctx.Now); // ADR 0008: unify the melee/ranged "attack succeeded" signal for stall detection
			ReleaseRangedHold(); // shot away — clear the warning + free the token (no longer "attempting")
			ChargeState = EFPSRRangedChargeState::Cooldown;
			CooldownElapsed = 0.0f;
		}
		break;
	}
	case EFPSRRangedChargeState::Cooldown:
	{
		CooldownElapsed += Dt;
		if (CooldownElapsed >= EffectiveFireCooldown)
		{
			ChargeState = EFPSRRangedChargeState::Idle;
		}
		break;
	}
	}
}

void AFPSREnemyBase::FireProjectile(const FFPSRServerAttackContext& Ctx)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!ProjectileClass)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[RangedEnemy] %s has no ProjectileClass set — shot skipped."), *GetName());
		return;
	}

	UFPSRProjectileSubsystem* ProjSub = World->GetSubsystem<UFPSRProjectileSubsystem>();
	if (!ProjSub)
	{
		return;
	}

	const FVector MuzzleLoc = GetMuzzleLocation();
	FVector Dir = (Ctx.TargetLocation - MuzzleLoc);
	if (Dir.IsNearlyZero())
	{
		Dir = GetActorForwardVector();
	}
	Dir = Dir.GetSafeNormal();

	// Team=Enemy reuses the whole proven projectile/damage bridge: IsHostileTarget hits only players (not other
	// enemies, not the instigator), and damage flows through ApplyContactDamage — no new damage code (Game.MD §2-10).
	FFPSRProjectileParams Params;
	Params.Team = EFPSRProjectileTeam::Enemy;
	Params.InstigatorActor = this;
	Params.Damage = ProjectileDamage;
	// Crit stays default-constructed (Chance 0) — enemy fire never crits (Game.MD §2-10).
	Params.InitialSpeed = ProjectileSpeed;
	Params.Lifetime = ProjectileLifetime;
	Params.GravityScale = ProjectileGravityScale;
	Params.ExplosionRadius = 0.0f;
	Params.Pierce = 0;
	Params.bSelfDamage = false;
	Params.KnockbackStrength = 0.0f;

	ProjSub->AcquireProjectile(ProjectileClass, MuzzleLoc, Dir, Params);
}

bool AFPSREnemyBase::HasLineOfSight(const AActor* TargetActor, const FVector& TargetLocation) const
{
	if (!bRequireLineOfSight)
	{
		return true;
	}
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// Block on STATIC geometry (walls / door frames) AND breakable geometry — CLOSED AFPSRDoor leaves and arena
	// props (ECC_FPSRDestructible, Enemy.md §2-6; these used to ride ECC_FPSRPlayerPawn). Without that channel a
	// ranged enemy would "see" — and shoot — through a closed door to the player behind it. The enemy projectile
	// now BLOCKS on destructibles too, so this is no longer the only thing preventing a through-door hit, but it
	// is still the cheaper gate: it stops the shot from being taken at all instead of eating it on the door.
	// The player channel stays queried so a teammate's body also breaks LOS. Ignore self + the target so neither
	// counts as an occluder. Other ENEMIES (ECC_Pawn) are intentionally NOT queried — an enemy projectile passes
	// through them, so they don't block LOS.
	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjParams.AddObjectTypesToQuery(ECC_FPSRPlayerPawn);
	ObjParams.AddObjectTypesToQuery(ECC_FPSRDestructible);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRRangedLOS), false, this);
	if (TargetActor)
	{
		QueryParams.AddIgnoredActor(TargetActor);
	}
	FHitResult Hit;
	return !World->LineTraceSingleByObjectType(Hit, GetMuzzleLocation(), TargetLocation, ObjParams, QueryParams);
}

void AFPSREnemyBase::SendRangedWarning(bool bActive)
{
	if (AFPSRPlayerController* PC = HeldTargetPC.Get())
	{
		// Existing Client+Reliable RPC -> UFPSRPlayerFeedbackComponent::ReceiveRangedTarget. SourceId = our unique id
		// (stable across the charge window; distinct per enemy so concurrent shooters track independently).
		PC->ClientNotifyRangedTarget(static_cast<int32>(GetUniqueID()), GetActorLocation(), bActive);
	}
}

void AFPSREnemyBase::ReleaseRangedHold()
{
	// bCharging is cleared UNCONDITIONALLY here, ahead of the bHoldingToken early-return below, so the non-targeted-
	// client telegraph never sticks true across a teardown that races the charge state — this function is already
	// the single "idempotent, safe on every teardown path" recovery point (Deactivate / EnterDyingState / EndPlay /
	// both ServerTickAttack exits all route through it), exactly mirroring why the warning RPC below always fires.
	if (bCharging)
	{
		bCharging = false;
		MARK_PROPERTY_DIRTY_FROM_NAME(AFPSREnemyBase, bCharging, this);
		// Also release the movement-anim hold immediately: an ABORTED charge (target left range / re-targeted) must
		// not leave the cosmetic Attack pose stuck through the remainder of the original RangedChargeTime window
		// while the enemy is actually free to move (a successful FIRE reaches this at ChargeElapsed>=RangedChargeTime,
		// i.e. Ctx.Now is already ~AttackAnimHoldUntil, so clearing it here early is a no-op harm-wise on that path).
		AttackAnimHoldUntil = -1.0f;
	}

	if (!bHoldingToken)
	{
		return;
	}
	SendRangedWarning(false); // Reliable 'off' — must always fire or the warning indicator sticks forever
	if (UWorld* World = GetWorld())
	{
		if (UFPSREnemySpawnSubsystem* Sub = World->GetSubsystem<UFPSREnemySpawnSubsystem>())
		{
			Sub->ReleaseRangedToken(HeldTargetPC);
		}
	}
	bHoldingToken = false;
	HeldTargetPC = nullptr;
}

void AFPSREnemyBase::OnRep_Charging()
{
	// Non-targeted client telegraph (see bCharging's header comment). True -> enter the Attack cosmetic at the
	// charge-length rate, mirroring the server's own SetAnimState call in ServerTickAttack's Idle->Charging
	// transition. False -> release the hold so the next PostNetReceiveLocationAndRotation re-derives Walk/Idle from
	// the replicated transform, same as any other attack tell falling out of range.
	if (bCharging)
	{
		SetAnimState(EFPSRAnimState::Attack, 1.0f / FMath::Max(KINDA_SMALL_NUMBER, RangedChargeTime));
		// Hold the cosmetic for the charge length on THIS client, mirroring the authority-side hold that
		// ServerTickAttack stamps alongside its own SetAnimState. A ranged enemy fires from far outside
		// AttackRange, so PostNetReceiveLocationAndRotation's melee tell never claims it and its walk/idle branch
		// would otherwise erase this telegraph on the very next net update — a charge the user decided to
		// replicate specifically so it could be READ would be visible for one frame out of RangedChargeTime.
		if (const UWorld* World = GetWorld())
		{
			AttackAnimHoldUntil = World->GetTimeSeconds() + RangedChargeTime;
		}
	}
	else
	{
		// Aborted or completed charge: drop the hold immediately, exactly as ClearRangedReservation does on the
		// authority side, so the enemy does not sit in the charge pose while it is already free to move.
		AttackAnimHoldUntil = -1.0f;
	}
}

void AFPSREnemyBase::ResetRangedCycle()
{
	ChargeState = EFPSRRangedChargeState::Idle;
	ChargeElapsed = 0.0f;
	CooldownElapsed = 0.0f;
}

FVector AFPSREnemyBase::GetMuzzleLocation() const
{
	return GetActorTransform().TransformPosition(MuzzleOffset);
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

	}

	// Face the PLAYER (Ctx.FaceDir), not the move direction: at StopDistance the move is separation-only and its
	// direction jitters, which would spin the enemy 360deg in place. Ctx.FaceDir is stable (toward the target).
	// This lives OUTSIDE the steering block on purpose (2026-09-07, voxel drone PIE): with no neighbours the stop-ring
	// steering vector is exactly zero, so a facing update inside that block never ran and an enemy holding at attack
	// range kept its last heading while the player strafed. Every archetype before the drone was rotationally
	// symmetric, which is why this went unnoticed. Knockback still suppresses it (a shoved enemy keeps its heading
	// until the push decays — same as before, the block above is skipped during knockback too).
	if (!bKnockbackActive)
	{
		FVector FaceXY = Ctx.FaceDir;
		FaceXY.Z = 0.0f;
		if (!FaceXY.IsNearlyZero())
		{
			SetActorRotation(FaceXY.GetSafeNormal().Rotation());
		}
	}

	// STAT1 §6 속박 (bDisableMovement): knockback moves the actor with a direct AddActorWorldOffset just below,
	// which never reads GetEffectiveMoveSpeed() — so root needs its OWN gate here, or a rooted enemy still gets
	// launched by an explosion (이동불가가 반쪽이 됨, spec's own wording). Suppressed, not decayed, while rooted: an
	// already-in-flight impulse simply resumes decaying the instant root lifts, rather than being silently discarded.
	const bool bRootedByStatus = HealthComponent && HealthComponent->GetResolvedStatus().bDisableMovement;
	if (bKnockbackActive && !bRootedByStatus)
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

	// (U20) Cosmetic walk/idle from the actual XY displacement this pass. Gated on AttackAnimHoldUntil rather than
	// CurrentAnimState (the pre-this-stage code read `CurrentAnimState != Attack`, which is correct only on a
	// listen-server host — SetAnimState early-returns before ever WRITING CurrentAnimState on a dedicated server, so
	// it is permanently stuck at Idle there; see AttackAnimHoldUntil's own comment). Applies to BOTH branches: a
	// stationary melee attacker / ranged charger can still read as bMoved on a separation-jitter pass, which used to
	// stomp Walk over Attack unconditionally (Idle was the only guarded branch before).
	if (AnimProfile && !bAnimFrozen && Ctx.Now >= AttackAnimHoldUntil)
	{
		const float ExpectedMove = GetEffectiveMoveSpeed() * Ctx.ScaledDelta;
		const float MovedSq = FVector::DistSquaredXY(GetActorLocation(), AnimStartLoc);
		const bool bMoved = ExpectedMove > KINDA_SMALL_NUMBER && MovedSq > FMath::Square(ExpectedMove * 0.25f);
		SetAnimState(bMoved ? EFPSRAnimState::Walk : EFPSRAnimState::Idle);
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

#if !UE_BUILD_SHIPPING
// 애니 상태 카나리아 — "값을 바꿨는데 화면이 안 변한다" 류를 C++ 쪽인지 머티리얼 쪽인지 1분 만에 가른다.
// 상태 구동(공격 쿨다운·홀드 창·재진입 가드)을 통째로 우회해 CPD 를 직접 밀어 넣으므로, 이걸로 형태가
// 매번 반응하면 머티리얼은 무죄이고 상태 구동 쪽을 파면 된다. 반대로 이걸로도 첫 회만 반응하면
// 머티리얼(진행도 계산)이 범인이다.
//   FPSR.Enemy.ForceAnimState 2      → 전 적을 Attack 으로 (재호출할 때마다 진행도 재시작)
//   FPSR.Enemy.ForceAnimState 0      → Idle 로 되돌림
//
// 두 번째 인자 = 핀. **"진행도가 저절로 되감긴다"는 이 핀 없이는 판정이 안 된다** — 핀이 없으면 구동부가
// 다음 네트 업데이트에서 상태를 다시 써 버려서, 되감은 범인이 구동부인지 머티리얼인지 구분이 안 된다.
//   FPSR.Enemy.ForceAnimState 2 1    → Attack 으로 고정(구동부 정지). 원샷이 **한 번 재생되고 멈춰야** 정상.
//                                       그래도 되감기면 그때가 머티리얼(진행도 계산) 범인이다.
//   FPSR.Enemy.ForceAnimState 0 0    → 핀 해제 + Idle 로 복귀
static FAutoConsoleCommandWithWorldAndArgs GFPSRForceAnimStateCmd(
	TEXT("FPSR.Enemy.ForceAnimState"),
	TEXT("Force every active enemy into an animation state, bypassing the state driver (debug). "
	     "Usage: FPSR.Enemy.ForceAnimState [0=Idle 1=Walk 2=Attack 3=Death] [pin 0|1]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		const int32 Raw = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 2;
		const EFPSRAnimState State = static_cast<EFPSRAnimState>(FMath::Clamp(Raw, 0, 3));
		// Default OFF: an unpinned poke stays the cheap "does the material react at all" probe this command started
		// as. Pinning is opt-in because it deliberately stops gameplay from animating the swarm.
		const bool bPin = Args.Num() > 1 && FCString::Atoi(*Args[1]) != 0;
		int32 Count = 0;
		for (TActorIterator<AFPSREnemyBase> It(World); It; ++It)
		{
			AFPSREnemyBase* Enemy = *It;
			if (IsValid(Enemy) && !Enemy->IsHidden())
			{
				Enemy->DebugForceAnimState(State, bPin);
				++Count;
			}
		}
		UE_LOG(LogFPSR, Log, TEXT("[Enemy] ForceAnimState %d (pin=%d) applied to %d enemies."),
			static_cast<int32>(State), bPin ? 1 : 0, Count);
	}));
#endif // !UE_BUILD_SHIPPING

#if !UE_BUILD_SHIPPING
namespace
{
	// STAT1 §8/§11-2 검증 전용 폴백 카탈로그 — FPSR.Status.Apply 가 콘텐츠(UFPSRStatusEffectSettings.StatusCatalog)
	// 없이도 배선(권위 브로드캐스트·OnRep 클라 반쪽·GMS 발행·헬스바 게터)을 눈으로 보게 하는 것이 유일한 목적이다.
	// 슬롯 순서·조합 재료쌍은 명세 §1 표 그대로(둔화·도트·방어력감소·공속저하 / 실명=0+1, 속박=1+2)지만 지속시간·
	// 배율 수치는 전부 미확정 플레이스홀더(§1 "이름·설명·수치는 미확정") — 콘텐츠가 나중에 정할 값을 대신 채운
	// 것뿐이므로 출시 코드로 취급하지 말 것. 매 호출마다 새로 만들고 버린다(FPSRStatusWorldTest.cpp 의
	// MakeStatus/MakeCatalog 와 같은 트랜지언트 수명 — 리턴 이후 아무도 포인터를 들고 있지 않아 GC 세이프하다).
	// 프로젝트 카탈로그가 이미 설정돼 있으면 그쪽을 그대로 쓴다(콘텐츠가 채워지는 순간 이 폴백은 자동으로 안 쓰인다).
	const UFPSRStatusCatalogDataAsset* ResolveOrBuildDebugStatusCatalog()
	{
		if (const UFPSRStatusCatalogDataAsset* Configured = UFPSRStatusEffectSettings::ResolveCatalog())
		{
			return Configured;
		}

		UFPSRStatusEffectDataAsset* Slow = NewObject<UFPSRStatusEffectDataAsset>();
		Slow->SlotIndex = 0;
		Slow->Kind = EFPSRStatusKind::Weak;
		Slow->DurationSeconds = 5.0f;
		Slow->MoveSpeedMultiplier = 0.5f;

		UFPSRStatusEffectDataAsset* Dot = NewObject<UFPSRStatusEffectDataAsset>();
		Dot->SlotIndex = 1;
		Dot->Kind = EFPSRStatusKind::Weak;
		Dot->DurationSeconds = 5.0f;
		Dot->DamagePerSecond = 5.0f;
		Dot->DotTickIntervalSeconds = 0.5f;

		UFPSRStatusEffectDataAsset* ArmorBreak = NewObject<UFPSRStatusEffectDataAsset>();
		ArmorBreak->SlotIndex = 2;
		ArmorBreak->Kind = EFPSRStatusKind::Weak;
		ArmorBreak->DurationSeconds = 5.0f;
		ArmorBreak->IncomingDamageMultiplier = 1.5f;

		UFPSRStatusEffectDataAsset* AttackSlow = NewObject<UFPSRStatusEffectDataAsset>();
		AttackSlow->SlotIndex = 3;
		AttackSlow->Kind = EFPSRStatusKind::Weak;
		AttackSlow->DurationSeconds = 5.0f;
		AttackSlow->AttackIntervalMultiplier = 2.0f;

		UFPSRStatusEffectDataAsset* Blind = NewObject<UFPSRStatusEffectDataAsset>();
		Blind->SlotIndex = 4;
		Blind->Kind = EFPSRStatusKind::Strong;
		Blind->DurationSeconds = 5.0f;
		Blind->bDisableAttack = true;
		Blind->RequiredWeakSlots.Add(0);
		Blind->RequiredWeakSlots.Add(1);

		UFPSRStatusEffectDataAsset* Root = NewObject<UFPSRStatusEffectDataAsset>();
		Root->SlotIndex = 5;
		Root->Kind = EFPSRStatusKind::Strong;
		Root->DurationSeconds = 5.0f;
		Root->bDisableMovement = true;
		Root->RequiredWeakSlots.Add(1);
		Root->RequiredWeakSlots.Add(2);

		UFPSRStatusCatalogDataAsset* DebugCatalog = NewObject<UFPSRStatusCatalogDataAsset>();
		DebugCatalog->Statuses.Add(Slow);
		DebugCatalog->Statuses.Add(Dot);
		DebugCatalog->Statuses.Add(ArmorBreak);
		DebugCatalog->Statuses.Add(AttackSlow);
		DebugCatalog->Statuses.Add(Blind);
		DebugCatalog->Statuses.Add(Root);
		return DebugCatalog;
	}
}

// STAT1 §8/§11-2 가시성 배선 검증 명령 — 카탈로그·카드가 아직 저작되지 않은 상태에서도 권위 브로드캐스트·OnRep
// 클라 반쪽·GMS 발행·헬스바 위젯 게터가 실제로 도는지 눈으로 확인하기 위한 것. 무기 히트/카드 부여 경로(§5-5 훅면)
// 를 완전히 우회해 지정한 슬롯을 로컬 플레이어 반경 내 적 전원에게 직접 ApplyStatus 한다.
//   FPSR.Status.Apply 0        → 반경 2000uu 내 적에게 슬롯 0(둔화) 적용
//   FPSR.Status.Apply 1 3000   → 반경 3000uu 내 적에게 슬롯 1(도트) 적용
// 보스(AFPSRBossBase)는 대상이 아니다 — 배치 패스가 아니라 자기 틱으로 진행되는 별도 드라이버라 이 명령의
// RegisterStatusActive 배선이 안 맞는다(§5-6 표); 필요하면 별도 명령으로 다룰 것.
static FAutoConsoleCommandWithWorldAndArgs GFPSRStatusApplyCmd(
	TEXT("FPSR.Status.Apply"),
	TEXT("STAT1 §8/§11-2 visibility-wiring check: force-apply a status slot to every AFPSREnemyBase within radius of "
	     "the local player (bypasses the weapon-hit/card path entirely; server/host-only). Falls back to a built-in "
	     "debug catalog (slots 0-5, §1 표) when the project catalog is unset. Usage: FPSR.Status.Apply <Slot 0-7> [Radius=2000]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() == 0)
		{
			UE_LOG(LogFPSR, Warning, TEXT("[Status] FPSR.Status.Apply requires a slot argument. Usage: FPSR.Status.Apply <Slot 0-7> [Radius=2000]"));
			return;
		}

		APlayerController* PC = World->GetFirstPlayerController();
		if (!PC || !PC->HasAuthority())
		{
			UE_LOG(LogFPSR, Warning, TEXT("[Status] FPSR.Status.Apply is server/host-only (ApplyStatus is authority-gated, §5-6)."));
			return;
		}

		APawn* LocalPawn = PC->GetPawn();
		if (!LocalPawn)
		{
			UE_LOG(LogFPSR, Warning, TEXT("[Status] FPSR.Status.Apply: no local pawn to center the radius search on."));
			return;
		}

		const uint8 Slot = static_cast<uint8>(FMath::Clamp(FCString::Atoi(*Args[0]), 0, 7));
		const float Radius = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 2000.0f;
		const FVector Center = LocalPawn->GetActorLocation();
		const float RadiusSq = FMath::Square(Radius);

		const UFPSRStatusCatalogDataAsset* Catalog = ResolveOrBuildDebugStatusCatalog();
		UFPSREnemySpawnSubsystem* SpawnSub = World->GetSubsystem<UFPSREnemySpawnSubsystem>();

		int32 Count = 0;
		for (TActorIterator<AFPSREnemyBase> It(World); It; ++It)
		{
			AFPSREnemyBase* Enemy = *It;
			UFPSREnemyHealthComponent* Health = (IsValid(Enemy) && !Enemy->IsHidden()) ? Enemy->GetHealthComponent() : nullptr;
			if (!Health || FVector::DistSquared(Enemy->GetActorLocation(), Center) > RadiusSq)
			{
				continue;
			}

			// WeakResist/StrongResist 는 1.0 그대로 우회한다 — §6 저항 행의 프로파일 조회는 실제 부여 경로
			// (UFPSRStatusApplyFragment::OnDamageApplied)의 몫이고, 이 명령의 목적은 배선 확인이지 저항 판정 검증이
			// 아니다.
			TArray<uint8, TInlineAllocator<8>> Fired;
			if (Health->ApplyStatus(Catalog, Slot, 1.0f, 1.0f, LocalPawn, nullptr, Fired))
			{
				// §6 압축 리스트 등록 — 실제 부여 경로의 진짜 호출부는 UFPSRStatusApplyFragment::OnDamageApplied
				// (FPSRWeaponFragment.cpp) 이고, 이 디버그 명령은 그 호출을 대신한다. 안 하면 배치 패스가 이 적을
				// 영원히 못 보고, 슬롯이 켜진 채(카탈로그 미설정이면 애초에 만료도 없지만) StatusActiveEnemies 밖에
				// 남는다.
				if (SpawnSub)
				{
					SpawnSub->RegisterStatusActive(Enemy);
				}
				++Count;
			}
		}

		UE_LOG(LogFPSR, Log, TEXT("[Status] FPSR.Status.Apply slot=%d radius=%.0f: %d enemy(ies) affected."), Slot, Radius, Count);
	}));
#endif // !UE_BUILD_SHIPPING
