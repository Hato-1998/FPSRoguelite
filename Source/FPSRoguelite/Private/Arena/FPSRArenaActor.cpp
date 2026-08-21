// Copyright Epic Games, Inc. All Rights Reserved.

#include "Arena/FPSRArenaActor.h"
#include "Arena/FPSRArenaBakeDataAsset.h"
#include "Arena/FPSRArenaBakeHash.h"
#include "Arena/FPSRArenaDestructible.h"
#include "Arena/FPSRArenaCells.h"
#include "Arena/FPSRArenaMarkers.h"
#include "Arena/FPSRArenaParamsDataAsset.h"
#include "Arena/FPSRArenaValidator.h"
#include "Core/FPSRGameState.h"
#include "Enemy/FPSRFlowFieldSubsystem.h"
#include "Core/FPSRLogChannels.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

#if !UE_BUILD_SHIPPING
#include "DrawDebugHelpers.h"
#endif

namespace
{
	/** P3 redteam fix: how far GetPlayerEntryTransforms searches outward (in cells) for an open cell to snap a
	 *  candidate entry point onto before giving up and leaving it exactly where it was authored/computed. */
	constexpr int32 PlayerEntrySnapMaxRadiusCells = 32;

	/** Deterministic outward ring search for the open cell nearest AnchorCell (Chebyshev distance), scanned in a
	 *  fixed row-major order within each ring — same idiom as UFPSRFlowFieldComputer::FindNearestOpenSurface — so
	 *  every machine picks the identical cell when a ring has more than one open candidate (no float distance
	 *  compare, no FMath::Rand). AnchorCell itself counts as a hit at radius 0. Returns false (OutCell untouched)
	 *  if nothing opens within MaxRadiusCells. */
	bool FindNearestOpenCell(const FFPSRArenaLayout& Layout, const FIntPoint& AnchorCell, int32 MaxRadiusCells, FIntPoint& OutCell)
	{
		if (FFPSRArenaCells::IsCellOpen(Layout, AnchorCell.X, AnchorCell.Y))
		{
			OutCell = AnchorCell;
			return true;
		}
		for (int32 R = 1; R <= MaxRadiusCells; ++R)
		{
			for (int32 DY = -R; DY <= R; ++DY)
			{
				for (int32 DX = -R; DX <= R; ++DX)
				{
					if (FMath::Max(FMath::Abs(DX), FMath::Abs(DY)) != R) { continue; } // ring perimeter only
					const int32 CX = AnchorCell.X + DX;
					const int32 CY = AnchorCell.Y + DY;
					if (FFPSRArenaCells::IsCellOpen(Layout, CX, CY))
					{
						OutCell = FIntPoint(CX, CY);
						return true;
					}
				}
			}
		}
		return false;
	}
}

#if !UE_BUILD_SHIPPING
static int32 GArenaDebugGrid = 0;
static FAutoConsoleVariableRef CVarArenaDebugGrid(
	TEXT("FPSR.Arena.DebugGrid"),
	GArenaDebugGrid,
	TEXT("Draw the arena's blocked cells and cluster bounds around the viewer. 0 = off."),
	ECVF_Cheat);

static float GArenaDebugGridRadius = 3000.0f;
static FAutoConsoleVariableRef CVarArenaDebugGridRadius(
	TEXT("FPSR.Arena.DebugGridRadius"),
	GArenaDebugGridRadius,
	TEXT("Radius (cm) around the viewer within which FPSR.Arena.DebugGrid draws cells."),
	ECVF_Cheat);

static int32 GArenaDebugTraces = 0;
static FAutoConsoleVariableRef CVarArenaDebugTraces(
	TEXT("FPSR.Arena.DebugTraces"),
	GArenaDebugTraces,
	TEXT("Draw the arena's L2 floor wiring traces and junctions around the viewer (ADR 0010 D8). Junctions draw in "
	     "a different colour. 0 = off. Shares FPSR.Arena.DebugGridRadius for the draw distance."),
	ECVF_Cheat);
#endif

AFPSRArenaActor::AFPSRArenaActor()
{
	// Ticks only to draw the debug grid; the arena itself is static between regenerations.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = !UE_BUILD_SHIPPING;

	bReplicates = true;
	bAlwaysRelevant = true; // one actor per world carrying the seed every client needs

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

void AFPSRArenaActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRArenaActor, ActiveSeed, Params);
}

void AFPSRArenaActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (!HasAuthority())
	{
		return;
	}

	// F1: ActiveSeed MUST be final before UFPSRFlowFieldSubsystem::OnWorldBeginPlay PULLs this arena's layout (see
	// this class's header comment for the engine-verified call order). PostInitializeComponents is the only actor
	// callback guaranteed to run before that pull — the engine calls it from ULevel::RouteActorInitialize during
	// UWorld::InitializeActorsForPlay, which always completes before UWorld::BeginPlay() is even invoked. Doing
	// this in BeginPlay (as before) left ActiveSeed at its 0 default for the pull, because actor BeginPlay is
	// dispatched from INSIDE UWorld::BeginPlay(), AFTER world subsystems' OnWorldBeginPlay already ran — the
	// reverse of what an earlier version of this code assumed.
	ActiveSeed = InitialSeed;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRArenaActor, ActiveSeed, this);

	// Seed the replicated "which arena is live" pointer at level start. The stage director only ever sets it on a
	// SWAP, so without this it stays null until the first transition — and everything that reads it as the cheap
	// O(1) answer (the enemy spawn subsystem's arena-bounds gate) would silently not gate for the whole first
	// stage, spawning the swarm into reserve arenas nobody is standing in.
	//
	// GameState can legitimately not exist yet this early in some bootstrap orders — BeginPlay (below) retries the
	// same call once World::BeginPlay guarantees GameState is up; SetActiveArena no-ops once ActiveArena already
	// equals this actor, so the retry is idempotent rather than a double-set.
	if (bStartsActive)
	{
		if (UWorld* World = GetWorld())
		{
			if (AFPSRGameState* GS = World->GetGameState<AFPSRGameState>())
			{
				GS->SetActiveArena(this);
			}
		}
	}
}

void AFPSRArenaActor::BeginPlay()
{
	Super::BeginPlay();

	// Authority-independent and FIRST: bStartsActive is a level-authored constant every machine reads off the same
	// placed actor (not replicated — it does not need to be), so every client agrees with the server on which
	// arena starts active without a network round trip. Everything below (layout build, representation) must see
	// the correct active/inactive state from the start rather than toggling it in after the fact.
	SetArenaActive(bStartsActive);

	// Fallback ONLY: PostInitializeComponents (above) already set ActiveSeed and attempted this same
	// GS->SetActiveArena(this) call, but GetGameState<>() can legitimately be null that early in some bootstrap
	// orders. Retry now that BeginPlay guarantees GameState exists — harmless if PostInitializeComponents already
	// succeeded (SetActiveArena no-ops once ActiveArena already equals this actor).
	if (HasAuthority() && bStartsActive)
	{
		if (UWorld* World = GetWorld())
		{
			if (AFPSRGameState* GS = World->GetGameState<AFPSRGameState>())
			{
				GS->SetActiveArena(this);
			}
		}
	}

	// F1: on the server the layout is usually already in place — UFPSRFlowFieldSubsystem::OnWorldBeginPlay PULLs
	// it before any actor's BeginPlay runs (see this class's header for the engine-verified order). A client has
	// no such pull (the flow field subsystem is server-only), so this IS its first adopt, UNLESS OnRep_ActiveSeed
	// already ran first (OnRep can fire before BeginPlay for an actor that exists at level load). Either way,
	// HasLayout() is the correct guard.
	//
	// A PARKED arena reaches this the same way, when its sublevel is made visible mid-play: AddToWorld routes
	// actor initialisation and BeginPlay for the newly added level. Adopting here is what gives the reserve arena
	// a Layout of its own before the swap, so PerformSwap's activation is a publish and not a build.
	//
	// ADR 0012: there is no procedural fallback below this any more. No bake = no layout, and that is a content
	// error the log names rather than something the runtime papers over (invariant 1).
	if (!HasLayout() && !AdoptBakedLayout())
	{
		UE_LOG(LogFPSR, Error,
			TEXT("[Arena] %s has NO usable baked mask — this arena has no layout at all. Assign a UFPSRArenaBakeDataAsset and bake it (Tools > FPSR > 아레나 베이크)."),
			*GetName());
	}
}

bool AFPSRArenaActor::GetGenParams(FFPSRArenaGenParams& OutParams, FVector& OutOrigin) const
{
	if (!ArenaParams)
	{
		return false;
	}
	OutParams = ArenaParams->ToGenParams();
	OutOrigin = ComputeGridOrigin(OutParams);
	return true;
}

bool AFPSRArenaActor::ServerRegenerate(int32 NewSeed)
{
	if (!HasAuthority())
	{
		return false;
	}

	ActiveSeed = NewSeed;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRArenaActor, ActiveSeed, this);

	// ADR 0012: a BAKED arena has nothing to regenerate — its mask is authored geometry, not a function of the
	// seed. Falling through to the generator here would REPLACE the baked mask with procedural output, and the
	// swarm would start pathing against walls that are not in the level while the players see the level's real
	// ones. The stage director calls this on every swap (FPSRStageDirectorSubsystem), so this branch is the
	// difference between a working transition and a silently broken one — it is not defensive padding.
	if (HasBakedSurface())
	{
		if (!AdoptBakedLayout())
		{
			UE_LOG(LogFPSR, Error, TEXT("[Arena] %s has a bake but could not adopt it — flow field left untouched."), *GetName());
			return false;
		}

		// Same publish gate as the procedural path below: only the LIVE arena's mask may reach the flow field.
		if (!bIsArenaActive)
		{
			UE_LOG(LogFPSR, Verbose, TEXT("[Arena] %s re-adopted its bake but is not the active arena — flow field untouched."), *GetName());
			return true;
		}
		if (UWorld* World = GetWorld())
		{
			if (UFPSRFlowFieldSubsystem* Flow = World->GetSubsystem<UFPSRFlowFieldSubsystem>())
			{
				// AdoptArenaField (2026-08-21, ADR 0009 P1) pulls both the 2D surface AND the voxel occupancy
				// straight from this arena's bake asset + transform, rather than being handed Layout.Surface —
				// the two can no longer come from mismatched sources by caller mistake.
				Flow->AdoptArenaField(*this);
			}
		}
		return true;
	}

	// No bake = no mask. Deliberately no fallback: ADR 0012 invariant 1 makes the baked data the ONLY source, and
	// generating or world-tracing one here would quietly re-introduce exactly the mismatch this ADR removed —
	// the players would see the level's walls while the swarm pathed against something else.
	UE_LOG(LogFPSR, Error,
		TEXT("[Arena] %s has no baked mask — regenerate for seed %d did nothing and the flow field is untouched. Assign a UFPSRArenaBakeDataAsset and bake it (Tools > FPSR > 아레나 베이크)."),
		*GetName(), NewSeed);
	return false;
}

void AFPSRArenaActor::OnRep_ActiveSeed()
{
	// ADR 0012: a baked arena's geometry is in the .umap, so client and server load the SAME file and there is
	// nothing to reproduce from the seed — the whole class of "client regenerated different geometry from the
	// server's mask, so enemies walk through walls" (ADR 0010's ⚠️ on the data-ownership table) cannot happen.
	//
	// So the seed no longer selects geometry at all. It is kept (and still replicated) because ADR 0012 「이번 런의
	// 랜덤 선택 결과」 — picking which traversal-neutral / breakable props this run uses — is the next thing that
	// needs a per-stage seed, and that selection is server-decided and replicated rather than re-derived. Adopting
	// the bake here is what keeps a client's Layout in step with the server's after a stage swap.
	if (!AdoptBakedLayout())
	{
		UE_LOG(LogFPSR, Warning,
			TEXT("[Arena] %s replicated seed %d but has no usable bake — this client has NO arena layout. Bake it in the editor (Tools > FPSR > 아레나 베이크)."),
			*GetName(), ActiveSeed);
	}
}

FVector AFPSRArenaActor::ComputeGridOrigin(const FFPSRArenaGenParams& Params) const
{
	// The actor sits at the arena CENTRE — placing it then reads as "the arena is here" rather than "the arena
	// starts here and extends off in some direction you have to remember".
	const FVector Centre = GetActorLocation();
	return FVector(
		Centre.X - 0.5 * Params.ArenaSizeCells.X * Params.CellSize,
		Centre.Y - 0.5 * Params.ArenaSizeCells.Y * Params.CellSize,
		Centre.Z);
}

void AFPSRArenaActor::SetArenaActive(bool bInActive)
{
	bIsArenaActive = bInActive;

	// ADR 0012: the arena's geometry is LEVEL AUTHORING, not something this actor builds — so "activate" means
	// showing the sublevel this actor lives in, and "deactivate" means hiding it. The old body toggled four HISM
	// components; against a baked arena those are empty and that path silently did nothing.
	//
	// SCOPE = this actor's own ULevel. Invariant 4 (one arena = one sublevel = one mask) is what makes that exact,
	// and it is the same scope FFPSRArenaBakeHash::Compute already uses to decide what the mask was baked from —
	// so the set of things that get hidden and the set of things that are IN the mask are the same set by
	// construction, not by two lists someone has to keep in agreement.
	//
	// This deliberately does NOT replace ContainsWorldLocation, which stays the spatial-membership test for spawn
	// gating, marker ownership and the bake hash. Only the geometry toggle moved to level scope.
	//
	// A parked (visible-but-dormant) arena is hidden rather than left to distance culling: ADR 0012 axis 5 pays
	// for AddToWorld one stage early, so the level IS in the world and would otherwise render. Hiding keeps its
	// components registered, which is what makes re-showing it O(1) at swap time.
	//
	// Never touches a transform (everything here is Static mobility, and UE forbids moving a Static primitive at
	// runtime). SetActorHiddenInGame/SetActorEnableCollision are idempotent, so calling this twice with the same
	// bInActive is a harmless no-op.
	ULevel* MyLevel = GetLevel();
	if (!MyLevel)
	{
		return;
	}

	for (AActor* Actor : MyLevel->Actors)
	{
		if (!IsValid(Actor) || Actor == this)
		{
			// The arena actor carries no geometry (a bare SceneComponent since the HISMs went), so there is
			// nothing to hide — and skipping it keeps the actor performing this operation out of its own
			// authored-state snapshot, where an entry would only ever be dead weight.
			continue;
		}

		// F2: on ACTIVATION only, and only the authority — reset any destructible this arena owns back to intact.
		// Nothing else in the codebase ever clears bBroken, so without this a destructible broken on an earlier
		// visit stayed broken (and unbreakable — 0 health) forever, and a revisited arena could never offer its
		// terrain-changing reward again (ADR 0010 D7 "지형을 바꾼다" means each visit, not once ever). Deactivation
		// does NOT reset — a dormant arena's destructibles should still read as however the player last left them
		// if that arena becomes active again without an intervening reset pass. The explicit HasAuthority() here
		// (on top of ServerReset()'s own guard) avoids the call entirely on clients, who reach this same loop via
		// OnRep_StageTransition -> ApplyStageTransitionLocal.
		if (bInActive && HasAuthority())
		{
			if (AFPSRArenaDestructible* Destructible = Cast<AFPSRArenaDestructible>(Actor))
			{
				Destructible->ServerReset();
			}
		}

		// Restore the AUTHORED flags on activation instead of forcing "visible + colliding". Both bHidden and
		// bActorEnableCollision are per-actor authorable, so a prop deliberately hidden in the level would
		// otherwise be revealed the first time this arena went live — a bug that only ever shows up in play,
		// which is the exact shape of problem ADR 0012 exists to remove.
		const FFPSRAuthoredActorState* Authored = AuthoredActorStates.Find(Actor);
		if (!Authored)
		{
			FFPSRAuthoredActorState Captured;
			Captured.bHidden = Actor->IsHidden();
			Captured.bCollisionEnabled = Actor->GetActorEnableCollision();
			Authored = &AuthoredActorStates.Add(Actor, Captured);
		}

		Actor->SetActorHiddenInGame(bInActive ? Authored->bHidden : true);
		Actor->SetActorEnableCollision(bInActive ? Authored->bCollisionEnabled : false);
	}
}

bool AFPSRArenaActor::HasBakedSurface() const
{
	// "참조가 있다"로 판단하지 않는다. 굽지 않은 빈 에셋을 물려 두는 것은 저작 중 흔한 중간 상태이고,
	// 그것을 마스크로 채택하면 격자는 0셀인데 필드는 만들어졌다고 믿게 된다 — 증상은 적이 아무 데나
	// 걸어가는 것이고, 로그엔 아무것도 안 남는다. IsBaked() 가 배열 길이까지 확인한다.
	return BakeData != nullptr && BakeData->IsBaked();
}

bool AFPSRArenaActor::GetBakedWorldSurface(FFPSRFlowFieldSurfaceData& OutWorld) const
{
	if (!HasBakedSurface())
	{
		return false;
	}
	return BakeData->BuildWorldSurface(GetActorTransform(), OutWorld);
}

bool AFPSRArenaActor::BuildValidationLayoutFromBake(FFPSRArenaLayout& Out) const
{
	Out = FFPSRArenaLayout();
	if (!GetBakedWorldSurface(Out.Surface))
	{
		return false;
	}

	Out.bFromBake = true;
	Out.GridDims = FIntPoint(Out.Surface.GridDimX, Out.Surface.GridDimY);
	Out.CellSize = Out.Surface.CellSize;
	Out.GridOrigin = Out.Surface.GridOrigin;

	// 랜드마크는 베이크에 들어가지 않는다 — 콜리전이 없으니(FPSRArenaMarkers.h) 장애물 프로브가 통과한다.
	// 그런데 매몰 검사(0011 E4 ⑤)는 여전히 필요하므로 레벨 액터에서 직접 모은다. 소속 판정은 다른 마커와
	// 같은 공간 기준(ContainsWorldLocation)이라 "어느 아레나 것인가"가 어긋날 수 없다.
	if (const UWorld* World = GetWorld())
	{
		for (TActorIterator<AFPSRArenaLandmark> It(const_cast<UWorld*>(World)); It; ++It)
		{
			const AFPSRArenaLandmark* Landmark = *It;
			if (!IsValid(Landmark) || !ContainsWorldLocation(Landmark->GetActorLocation()))
			{
				continue;
			}
			FFPSRArenaAuthoredLandmark Entry;
			Entry.Location = Landmark->GetActorLocation();
			Entry.ReserveRadiusCells = Landmark->GetReserveRadiusCells();
			Out.Landmarks.Add(Entry);
		}
	}

	// Clusters / Props / Traces / TraceJunctions 는 비운 채로 둔다. 전부 생성기 산출물이고 베이크엔 대응물이
	// 없다 — 억지로 채우면 검증기가 있지도 않은 절차 출력을 검사하게 된다.
	return true;
}

bool AFPSRArenaActor::AdoptBakedLayout()
{
	FFPSRArenaLayout BakeLayout;
	if (!BuildValidationLayoutFromBake(BakeLayout))
	{
		return false;
	}

	FFPSRArenaGenParams Params;
	FVector UnusedOrigin;
	if (!GetGenParams(Params, UnusedOrigin))
	{
		UE_LOG(LogFPSR, Error,
			TEXT("[Arena] %s 는 베이크가 있지만 아레나 파라미터가 없어 검증할 수 없다 — '아레나 파라미터' 를 설정할 것."),
			*GetName());
		return false;
	}

	Layout = MoveTemp(BakeLayout);

	UE_LOG(LogFPSR, Log, TEXT("[Arena] %s adopted BAKED layout: grid=%dx%d cell=%.0f origin=%s landmarks=%d"),
		*GetName(), Layout.GridDims.X, Layout.GridDims.Y, Layout.CellSize,
		*Layout.GridOrigin.ToString(), Layout.Landmarks.Num());

#if !UE_BUILD_SHIPPING
	// ADR 0012 5겹 검사 ⑤ — 월드 시작 스테일 알람. 게이트가 아니라 경보다(0011 E4): 여기서 막아도 고칠
	// 사람이 그 자리에 없고, 판정 시점은 에디터다. 그래도 남기는 이유는 앞의 네 겹을 전부 통과해 버린
	// 베이크가 존재할 수 있고, 그때 이 줄이 "적이 왜 이러지"를 몇 시간에서 몇 초로 줄이기 때문이다.
	//
	// 셰이핑 빌드에서는 뺀다 — 레벨의 전 액터를 훑어 해시하는 비용을 출시 빌드가 낼 이유가 없고,
	// 출시 시점에는 이미 커밋 게이트(③④)를 통과한 베이크만 존재한다.
	const FFPSRArenaBakeCheck Freshness = FFPSRArenaBakeHash::CheckFreshness(*this);
	if (Freshness.IsProblem())
	{
		UE_LOG(LogFPSR, Error, TEXT("[Arena] %s"), *Freshness.Message);
	}
#endif

	// 검사는 경보이지 게이트가 아니다(0011 E4) — 여기서 막아도 고칠 사람이 그 자리에 없다. 판정 시점은
	// 에디터이고, 이 줄은 "에디터에서 통과시킨 것이 런타임에도 그대로인가"를 보는 회귀망이다.
	const FFPSRArenaValidationResult Validation = FFPSRArenaValidator::Validate(Layout, Params);
	if (!Validation.Passed())
	{
		UE_LOG(LogFPSR, Error, TEXT("[Arena] %s BAKE validation %s"), *GetName(), *FFPSRArenaValidator::Summarize(Validation));
		for (const FString& Err : Validation.Errors) { UE_LOG(LogFPSR, Error, TEXT("[Arena]   %s"), *Err); }
	}
	else
	{
		UE_LOG(LogFPSR, Log, TEXT("[Arena] %s BAKE validation %s"), *GetName(), *FFPSRArenaValidator::Summarize(Validation));
	}
	for (const FString& Warn : Validation.Warnings) { UE_LOG(LogFPSR, Warning, TEXT("[Arena]   %s"), *Warn); }

	return true;
}

bool AFPSRArenaActor::GetGridBoundsXY(FVector2D& OutMin, FVector2D& OutMax) const
{
	// Reads the two dimension fields straight off the asset instead of going through GetGenParams/ToGenParams:
	// that path copies the whole generator contract INCLUDING the flattened prop-set entry arrays, and this runs
	// once per actor in SetArenaActive's sweep and once per component in the bake hash — hundreds of throwaway
	// array copies for a bounds test that needs two numbers.
	if (!ArenaParams)
	{
		return false;
	}

	const double Cell = ArenaParams->CellSize;
	const double SpanX = static_cast<double>(ArenaParams->ArenaSizeCells.X) * Cell;
	const double SpanY = static_cast<double>(ArenaParams->ArenaSizeCells.Y) * Cell;
	// Same centre-anchored origin ComputeGridOrigin derives (the actor sits at the arena CENTRE), restated from the
	// same two fields so the two can only disagree if both are edited.
	const FVector Centre = GetActorLocation();
	OutMin = FVector2D(Centre.X - 0.5 * SpanX, Centre.Y - 0.5 * SpanY);
	OutMax = FVector2D(OutMin.X + SpanX, OutMin.Y + SpanY);
	return true;
}

bool AFPSRArenaActor::OverlapsWorldBoundsXY(const FBoxSphereBounds& Bounds) const
{
	FVector2D GridMin, GridMax;
	if (!GetGridBoundsXY(GridMin, GridMax))
	{
		return false;
	}

	// Closed on both sides here, unlike ContainsWorldLocation's half-open point test: a wall that merely TOUCHES
	// the grid edge is still something the obstacle probe can hit, and excluding it is exactly the bug this
	// function exists to remove. Z is absent for the same reason it is absent there — the arena is one Z-plane
	// and reserve arenas are parked beside the live one, so XY alone separates them.
	const double MinX = Bounds.Origin.X - Bounds.BoxExtent.X;
	const double MaxX = Bounds.Origin.X + Bounds.BoxExtent.X;
	const double MinY = Bounds.Origin.Y - Bounds.BoxExtent.Y;
	const double MaxY = Bounds.Origin.Y + Bounds.BoxExtent.Y;
	return MaxX >= GridMin.X && MinX <= GridMax.X
		&& MaxY >= GridMin.Y && MinY <= GridMax.Y;
}

bool AFPSRArenaActor::ContainsWorldLocation(const FVector& World) const
{
	FVector2D GridMin, GridMax;
	if (!GetGridBoundsXY(GridMin, GridMax))
	{
		return false;
	}

	// Half-open on the upper edge so two arenas laid out edge to edge can never both claim the same point.
	// Z is deliberately absent from this test — see the header comment.
	return World.X >= GridMin.X && World.X < GridMax.X
		&& World.Y >= GridMin.Y && World.Y < GridMax.Y;
}

bool AFPSRArenaActor::GetPlayerEntryTransforms(TArray<FTransform>& Out) const
{
	Out.Reset();

	// P3 redteam fix: neither an authored APlayerStart nor the centre-offset fallback below checks the mask — a
	// start sitting inside a cell L1 (or the skeleton-avoidance fix) ended up blocking, or a fallback offset that
	// happens to land on a blocked cell near a busy centre, both used to teleport straight into geometry (the
	// caller's teleport uses bSweep=false, which does not resolve overlaps — see PerformSwap). Snaps every
	// candidate's XY onto the nearest OPEN cell in place; Z and rotation are left untouched. Skipped when this
	// machine has no layout yet (e.g. inspected in the editor before AdoptBakedLayout ever ran) since IsCellOpen
	// has nothing to answer with. Called at BOTH exits below (authored starts and the no-starts fallback) so
	// neither path can hand back a point sitting inside a wall.
	auto SnapToOpenCells = [this](TArray<FTransform>& Transforms)
	{
		if (!Layout.IsValid())
		{
			return;
		}
		for (FTransform& Xf : Transforms)
		{
			const FVector Loc = Xf.GetLocation();
			const FIntPoint Cell = FFPSRArenaCells::WorldToCell(Layout, Loc);

			FIntPoint OpenCell;
			if (!FindNearestOpenCell(Layout, Cell, PlayerEntrySnapMaxRadiusCells, OpenCell))
			{
				UE_LOG(LogFPSR, Warning,
					TEXT("[Arena] %s player entry at %s sits in blocked cell (%d,%d) with no open cell within %d — leaving it unsnapped."),
					*GetName(), *Loc.ToString(), Cell.X, Cell.Y, PlayerEntrySnapMaxRadiusCells);
				continue;
			}
			if (OpenCell != Cell)
			{
				const FVector SnapCentre = Layout.CellCenterWorld(OpenCell.X, OpenCell.Y);
				Xf.SetLocation(FVector(SnapCentre.X, SnapCentre.Y, Loc.Z));
			}
		}
	};

	TArray<APlayerStart*> Starts;
	if (const UWorld* World = GetWorld())
	{
		for (TActorIterator<APlayerStart> It(const_cast<UWorld*>(World)); It; ++It)
		{
			APlayerStart* Start = *It;
			if (IsValid(Start) && ContainsWorldLocation(Start->GetActorLocation()))
			{
				Starts.Add(Start);
			}
		}
	}

	if (Starts.Num() == 0)
	{
		// No authored entry points — fall back to something usable (spread around the arena centre) rather than
		// handing the caller an empty array, but report false so the gap can be logged instead of silently
		// teleporting every player onto the same point.
		const FVector Centre = GetActorLocation();
		constexpr double FallbackOffset = 200.0;
		const FVector Offsets[4] = {
			FVector(FallbackOffset, 0.0, 0.0), FVector(-FallbackOffset, 0.0, 0.0),
			FVector(0.0, FallbackOffset, 0.0), FVector(0.0, -FallbackOffset, 0.0)
		};
		Out.Reserve(4);
		for (const FVector& Offset : Offsets)
		{
			Out.Add(FTransform(FRotator::ZeroRotator, Centre + Offset));
		}
		SnapToOpenCells(Out);
		return false;
	}

	// Sorted by NAME, not TActorIterator discovery order — that order is not guaranteed identical across machines
	// or even across runs, and every client must derive the same entry-point order from the same authored actors
	// with nothing replicated to settle it.
	Starts.Sort([](const APlayerStart& A, const APlayerStart& B) { return A.GetName() < B.GetName(); });

	Out.Reserve(Starts.Num());
	for (const APlayerStart* Start : Starts)
	{
		Out.Add(Start->GetActorTransform());
	}
	SnapToOpenCells(Out);
	return true;
}

AFPSRArenaActor* AFPSRArenaActor::FindActiveInWorld(const UWorld* World)
{
	TArray<AFPSRArenaActor*> All;
	FindAllInWorld(World, All); // already sorted: StageOrder ascending, ties by name — every tier below wants that order

	// Tier 1: whichever arena actually toggled itself active (normal play — SetArenaActive ran in BeginPlay).
	for (AFPSRArenaActor* Arena : All)
	{
		if (Arena->IsArenaActive()) { return Arena; }
	}
	// Tier 2: nobody has gone through BeginPlay yet (editor / not in PIE) — fall back to what is AUTHORED to
	// start active, since that is what BeginPlay would pick a moment later.
	for (AFPSRArenaActor* Arena : All)
	{
		if (Arena->bStartsActive) { return Arena; }
	}
	// Tier 3: nothing is marked active or start-active at all — an authoring gap. Still return something
	// deterministic (lowest StageOrder, ties by name) rather than null, so editor tools have an arena to work with.
	return All.Num() > 0 ? All[0] : nullptr;
}

void AFPSRArenaActor::FindAllInWorld(const UWorld* World, TArray<AFPSRArenaActor*>& Out)
{
	Out.Reset();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AFPSRArenaActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (IsValid(*It))
		{
			Out.Add(*It);
		}
	}

	// StageOrder ascending (the transition order); ties broken by name so every machine agrees on the same order
	// even when two arenas are authored with the same StageOrder.
	// Predicate takes REFERENCES, not pointers: TArray<T*>::Sort routes through TDereferenceWrapper<T*, ...>, whose
	// operator() calls Predicate(*A, *B) (Core/Public/Templates/Sorting.h) — a pointer-typed predicate does not compile.
	Out.Sort([](const AFPSRArenaActor& A, const AFPSRArenaActor& B)
	{
		return (A.StageOrder != B.StageOrder) ? (A.StageOrder < B.StageOrder) : (A.GetName() < B.GetName());
	});
}

void AFPSRArenaActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

#if !UE_BUILD_SHIPPING
	if ((GArenaDebugGrid == 0 && GArenaDebugTraces == 0) || !Layout.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Anchor the draw window on the local viewer — 6400 cells of DrawDebugBox would cost more than the arena.
	FVector ViewPos = GetActorLocation();
	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		FRotator ViewRot;
		PC->GetPlayerViewPoint(ViewPos, ViewRot);
	}

	const double Cell = Layout.CellSize;
	const FVector Origin = Layout.GridOrigin;
	const double R = GArenaDebugGridRadius;

	if (GArenaDebugGrid != 0)
	{
		const int32 MinCX = FMath::Max(0, FMath::FloorToInt((ViewPos.X - R - Origin.X) / Cell));
		const int32 MaxCX = FMath::Min(Layout.GridDims.X - 1, FMath::CeilToInt((ViewPos.X + R - Origin.X) / Cell));
		const int32 MinCY = FMath::Max(0, FMath::FloorToInt((ViewPos.Y - R - Origin.Y) / Cell));
		const int32 MaxCY = FMath::Min(Layout.GridDims.Y - 1, FMath::CeilToInt((ViewPos.Y + R - Origin.Y) / Cell));

		const FVector HalfCell(Cell * 0.5 * 0.9, Cell * 0.5 * 0.9, 4.0);
		for (int32 CY = MinCY; CY <= MaxCY; ++CY)
		{
			for (int32 CX = MinCX; CX <= MaxCX; ++CX)
			{
				const bool bOpen = FFPSRArenaCells::IsCellOpen(Layout, CX, CY);
				DrawDebugBox(World, Layout.CellCenterWorld(CX, CY) + FVector(0, 0, 10.0), HalfCell,
					bOpen ? FColor(0, 160, 255, 40) : FColor(255, 64, 0, 90), false, -1.0f, 0, 1.0f);
			}
		}
	}

	if (GArenaDebugTraces != 0)
	{
		// Segments draw as lines, junctions as points in a DIFFERENT colour — the crossing points are the whole
		// point of this debug view (ADR 0010 D1: circulation choices happen where the wiring branches), so they
		// need to read as visually distinct from an ordinary run of trace.
		const double RSq = R * R;
		for (const FFPSRArenaTraceSegment& Seg : Layout.Traces)
		{
			const FVector FromWorld = Layout.CellCenterWorld(Seg.FromCell.X, Seg.FromCell.Y) + FVector(0, 0, 12.0);
			const FVector ToWorld = Layout.CellCenterWorld(Seg.ToCell.X, Seg.ToCell.Y) + FVector(0, 0, 12.0);
			if (FVector::DistSquaredXY(FromWorld, ViewPos) > RSq && FVector::DistSquaredXY(ToWorld, ViewPos) > RSq)
			{
				continue;
			}
			DrawDebugLine(World, FromWorld, ToWorld, Seg.bJunction ? FColor(255, 200, 0) : FColor(0, 255, 140),
				false, -1.0f, 0, Seg.bJunction ? 3.0f : 1.5f);
		}
		for (const FIntPoint& JCell : Layout.TraceJunctions)
		{
			const FVector JWorld = Layout.CellCenterWorld(JCell.X, JCell.Y) + FVector(0, 0, 12.0);
			if (FVector::DistSquaredXY(JWorld, ViewPos) > RSq)
			{
				continue;
			}
			DrawDebugPoint(World, JWorld, 12.0f, FColor(255, 40, 220), false, -1.0f, 0);
		}
	}
#endif
}

#if !UE_BUILD_SHIPPING
static void ArenaGenerateCmd(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
{
	AFPSRArenaActor* Arena = AFPSRArenaActor::FindActiveInWorld(World);
	if (!Arena)
	{
		Ar.Logf(TEXT("FPSR.Arena.Generate: no AFPSRArenaActor in this world."));
		return;
	}
	if (!Arena->HasAuthority())
	{
		Ar.Logf(TEXT("FPSR.Arena.Generate: server only (the seed is server-authoritative and replicates)."));
		return;
	}

	const int32 Seed = (Args.Num() > 0) ? FCString::Atoi(*Args[0]) : (Arena->GetActiveSeed() + 1);
	Ar.Logf(TEXT("FPSR.Arena.Generate: seed %d -> %s"), Seed, Arena->ServerRegenerate(Seed) ? TEXT("ok") : TEXT("FAILED"));
}

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GArenaGenerateCommand(
	TEXT("FPSR.Arena.Generate"),
	TEXT("Regenerate the arena (server). Usage: FPSR.Arena.Generate [Seed]  — omit the seed to advance by one."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&ArenaGenerateCmd));
#endif
