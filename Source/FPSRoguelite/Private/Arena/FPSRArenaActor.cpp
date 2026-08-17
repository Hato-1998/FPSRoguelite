// Copyright Epic Games, Inc. All Rights Reserved.

#include "Arena/FPSRArenaActor.h"
#include "Arena/FPSRArenaDestructible.h"
#include "Arena/FPSRArenaGenerator.h"
#include "Arena/FPSRArenaMarkers.h"
#include "Arena/FPSRArenaParamsDataAsset.h"
#include "Arena/FPSRArenaValidator.h"
#include "Enemy/FPSRFlowFieldSubsystem.h"
#include "Core/FPSRLogChannels.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
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
	/** Boundary wall thickness (cm). Cosmetic — the flow field already has no edge pointing out of the grid, so
	 *  this only stops the PLAYER from walking off. */
	constexpr double BoundaryWallThicknessCm = 100.0;
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

	BlockingMeshes = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("BlockingMeshes"));
	BlockingMeshes->SetupAttachment(SceneRoot);
	BlockingMeshes->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BlockingMeshes->SetCollisionProfileName(TEXT("BlockAll"));
	BlockingMeshes->SetMobility(EComponentMobility::Static);

	FloorMeshes = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("FloorMeshes"));
	FloorMeshes->SetupAttachment(SceneRoot);
	FloorMeshes->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	FloorMeshes->SetCollisionProfileName(TEXT("BlockAll"));
	FloorMeshes->SetMobility(EComponentMobility::Static);
}

void AFPSRArenaActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRArenaActor, ActiveSeed, Params);
}

void AFPSRArenaActor::BeginPlay()
{
	Super::BeginPlay();

	// Authority-independent and FIRST: bStartsActive is a level-authored constant every machine reads off the same
	// placed actor (not replicated — it does not need to be), so every client agrees with the server on which
	// arena starts active without a network round trip. Everything below (layout build, representation) must see
	// the correct active/inactive state from the start rather than toggling it in after the fact.
	SetArenaActive(bStartsActive);

	if (HasAuthority())
	{
		ActiveSeed = InitialSeed;
		MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRArenaActor, ActiveSeed, this);
	}

	// Both sides build locally. On a client the seed may already have arrived (OnRep can fire before BeginPlay
	// for an actor that exists at level load), in which case OnRep_ActiveSeed's build was skipped because the
	// components were not ready yet — so building here unconditionally is the simple, correct move.
	BuildLocalLayout();
	RebuildRepresentation();
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

bool AFPSRArenaActor::BuildLayoutForSeed(int32 Seed, FFPSRArenaLayout& OutLayout) const
{
	FFPSRArenaGenParams GenParams;
	FVector Origin;
	if (!GetGenParams(GenParams, Origin))
	{
		UE_LOG(LogFPSR, Error, TEXT("[Arena] %s has no ArenaParams — no layout, no flow field. Assign a UFPSRArenaParamsDataAsset."),
			*GetName());
		OutLayout = FFPSRArenaLayout();
		return false;
	}

	// Gather what the designer placed. Note this reads ACTORS, not collision — the arena is told what blocks it
	// rather than discovering it, which is what keeps the mask free of world queries (ADR 0011 E3).
	//
	// Ownership is SPATIAL (ContainsWorldLocation), not an authored per-actor link: with several arenas now able
	// to live in one level (ADR 0010 D6, same-level parking), a marker actor carries no "which arena am I in"
	// property to keep in sync by hand — being physically inside an arena's own cell grid IS the membership test,
	// so it cannot drift as markers are added, moved, or a whole new arena is dropped into the level.
	FFPSRArenaAuthoredInput Authored;
	if (const UWorld* World = GetWorld())
	{
		for (TActorIterator<AFPSRArenaBlocker> It(const_cast<UWorld*>(World)); It; ++It)
		{
			const AFPSRArenaBlocker* Blocker = *It;
			if (!IsValid(Blocker) || !ContainsWorldLocation(Blocker->GetActorLocation())) { continue; }
			FFPSRArenaAuthoredBox BoxEntry;
			BoxEntry.Center = Blocker->GetActorLocation();
			BoxEntry.HalfExtentXY = Blocker->GetHalfExtentXY();
			BoxEntry.YawDegrees = Blocker->GetYawDegrees();
			Authored.Blockers.Add(BoxEntry);
		}
		for (TActorIterator<AFPSRArenaLandmark> It(const_cast<UWorld*>(World)); It; ++It)
		{
			const AFPSRArenaLandmark* Landmark = *It;
			if (!IsValid(Landmark) || !ContainsWorldLocation(Landmark->GetActorLocation())) { continue; }
			FFPSRArenaAuthoredLandmark LandmarkEntry;
			LandmarkEntry.Location = Landmark->GetActorLocation();
			LandmarkEntry.ReserveRadiusCells = Landmark->GetReserveRadiusCells();
			LandmarkEntry.bBlocking = Landmark->IsBlocking();
			Authored.Landmarks.Add(LandmarkEntry);
		}
		for (TActorIterator<AFPSRArenaDestructible> It(const_cast<UWorld*>(World)); It; ++It)
		{
			const AFPSRArenaDestructible* Destructible = *It;
			if (!IsValid(Destructible) || !ContainsWorldLocation(Destructible->GetActorLocation())) { continue; }
			FFPSRArenaAuthoredDestructible DestructibleEntry;
			DestructibleEntry.Location = Destructible->GetActorLocation();
			DestructibleEntry.FootprintCells = Destructible->GetFootprintCells();
			Authored.Destructibles.Add(DestructibleEntry);
		}
	}

	return FFPSRArenaGenerator::Generate(Seed, GenParams, Origin, Authored, OutLayout);
}

bool AFPSRArenaActor::BuildLocalLayout()
{
	FFPSRArenaGenParams GenParams;
	FVector Origin;
	if (!GetGenParams(GenParams, Origin) || !BuildLayoutForSeed(ActiveSeed, Layout))
	{
		Layout = FFPSRArenaLayout();
		return false;
	}

	// Alarm, not a gate (ADR 0011 실패 흐름 1). A broken arena still loads: refusing to build it at runtime would
	// only mean the map vanishes for whoever is playing, with nobody present to fix the authoring. The verdict that
	// matters is the editor's — this is here so a regression cannot pass unnoticed.
	const FFPSRArenaValidationResult Validation = FFPSRArenaValidator::Validate(Layout, GenParams);
	if (!Validation.Passed())
	{
		UE_LOG(LogFPSR, Error, TEXT("[Arena] %s FAILED validation — %s"), *GetName(), *FFPSRArenaValidator::Summarize(Validation));
		for (const FString& Err : Validation.Errors) { UE_LOG(LogFPSR, Error, TEXT("[Arena]   %s"), *Err); }
	}
	else
	{
		UE_LOG(LogFPSR, Log, TEXT("[Arena] %s validation %s"), *GetName(), *FFPSRArenaValidator::Summarize(Validation));
	}
	for (const FString& Warn : Validation.Warnings) { UE_LOG(LogFPSR, Warning, TEXT("[Arena]   %s"), *Warn); }

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

	if (!BuildLocalLayout())
	{
		// Deliberately no fallback: ADR 0010 invariant 5 says the generator owns the mask, and a world-trace
		// bake here would quietly re-introduce exactly the failure mode that invariant exists to prevent.
		UE_LOG(LogFPSR, Error, TEXT("[Arena] Regenerate failed for seed %d — flow field left untouched."), NewSeed);
		return false;
	}

	RebuildRepresentation();

	// Only the LIVE arena's mask may reach the flow field. With spare arenas parked in the same level (ADR 0010 D6
	// as amended 2026-08-17), a regenerate on a dormant one — a console command aimed at the wrong actor, a BP
	// call, a future transition bug — would otherwise hand the swarm the geometry of an arena nobody is standing
	// in, and enemies would path against walls 300 m away. Regenerating a dormant arena's own layout is fine and
	// still happens above; publishing it is what is gated.
	if (!bIsArenaActive)
	{
		UE_LOG(LogFPSR, Verbose, TEXT("[Arena] %s regenerated seed %d locally but is not the active arena — flow field untouched."),
			*GetName(), NewSeed);
		return true;
	}

	if (UWorld* World = GetWorld())
	{
		if (UFPSRFlowFieldSubsystem* Flow = World->GetSubsystem<UFPSRFlowFieldSubsystem>())
		{
			Flow->AdoptArenaSurface(Layout.Surface);
		}
	}
	return true;
}

void AFPSRArenaActor::OnRep_ActiveSeed()
{
	// Client side: same seed, same generator, same arena. Nothing about the layout crosses the wire.
	BuildLocalLayout();
	RebuildRepresentation();
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

void AFPSRArenaActor::RebuildRepresentation()
{
	if (!BlockingMeshes || !FloorMeshes)
	{
		return;
	}

	BlockingMeshes->ClearInstances();
	FloorMeshes->ClearInstances();

	if (!WhiteboxCubeMesh)
	{
		// Not fatal: the debug grid view still shows the topology, which is what invariant 12 actually needs.
		// No engine asset path is hardcoded here on purpose (asset paths in C++ are banned, and /Engine/ content
		// is a known cook hazard for this project).
		UE_LOG(LogFPSR, Warning, TEXT("[Arena] %s has no WhiteboxCubeMesh — geometry not built. Assign any box mesh (size/pivot are read from its bounds), or inspect the topology with FPSR.Arena.DebugGrid 1."),
			*GetName());
		return;
	}

	if (!Layout.IsValid())
	{
		return;
	}

	BlockingMeshes->SetStaticMesh(WhiteboxCubeMesh);
	FloorMeshes->SetStaticMesh(WhiteboxCubeMesh);

	const double Cell = Layout.CellSize;
	const double SpanX = Layout.GridDims.X * Cell;
	const double SpanY = Layout.GridDims.Y * Cell;
	const FVector Origin = Layout.GridOrigin;

	// Derive the mesh's real size instead of assuming a 100 cm cube: assigning a differently-sized (or off-centre)
	// box would otherwise scale the whole arena wrong with nothing on screen saying so, and "the arena is subtly
	// the wrong size" is exactly the kind of bug that survives a playtest and poisons the verdict.
	const FBoxSphereBounds MeshBounds = WhiteboxCubeMesh->GetBounds();
	const FVector MeshSize = MeshBounds.BoxExtent * 2.0;
	if (MeshSize.X <= UE_KINDA_SMALL_NUMBER || MeshSize.Y <= UE_KINDA_SMALL_NUMBER || MeshSize.Z <= UE_KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogFPSR, Error, TEXT("[Arena] WhiteboxCubeMesh '%s' has degenerate bounds (%s) — geometry not built."),
			*WhiteboxCubeMesh->GetName(), *MeshSize.ToString());
		return;
	}

	auto AddBox = [MeshSize, MeshOffset = MeshBounds.Origin](
		UHierarchicalInstancedStaticMeshComponent* Comp, const FVector& WorldCentre, const FVector& SizeCm)
	{
		const FVector Scale(SizeCm.X / MeshSize.X, SizeCm.Y / MeshSize.Y, SizeCm.Z / MeshSize.Z);
		// Cancel the mesh's own pivot offset so WorldCentre really is the centre of the placed box.
		const FTransform Xf(FRotator::ZeroRotator, WorldCentre - MeshOffset * Scale, Scale);
		Comp->AddInstance(Xf, /*bWorldSpace=*/true);
	};

	// Floor slab, sitting just under the grid's Z so the walkable surface is exactly GridOrigin.Z.
	AddBox(FloorMeshes,
		FVector(Origin.X + SpanX * 0.5, Origin.Y + SpanY * 0.5, Origin.Z - FloorThickness * 0.5),
		FVector(SpanX, SpanY, FloorThickness));

	// Clusters are NOT drawn here any more (ADR 0011 E2): each AFPSRArenaBlocker carries its own box and mesh, so
	// what the designer moves in the viewport is the same object the rasteriser reads. Drawing a second copy from
	// the layout would be a duplicate that agrees with the authored one only until someone nudges a blocker.

	// Boundary walls, placed just OUTSIDE the grid so they never occupy a cell the generator called open.
	const double T = BoundaryWallThicknessCm;
	const double WallZ = Origin.Z + WallHeight * 0.5;
	AddBox(BlockingMeshes, FVector(Origin.X + SpanX * 0.5, Origin.Y - T * 0.5, WallZ), FVector(SpanX + 2 * T, T, WallHeight));
	AddBox(BlockingMeshes, FVector(Origin.X + SpanX * 0.5, Origin.Y + SpanY + T * 0.5, WallZ), FVector(SpanX + 2 * T, T, WallHeight));
	AddBox(BlockingMeshes, FVector(Origin.X - T * 0.5, Origin.Y + SpanY * 0.5, WallZ), FVector(T, SpanY, WallHeight));
	AddBox(BlockingMeshes, FVector(Origin.X + SpanX + T * 0.5, Origin.Y + SpanY * 0.5, WallZ), FVector(T, SpanY, WallHeight));

	// L1 micro props. Both tiers get real collision — that is the point of the height rule rather than a collision
	// flag: a 30 cm box IS steppable at MaxStepHeight 45, and a 100 cm one IS not. The geometry and the swarm's
	// mask agree because both are derived from the same number.
	for (const FFPSRArenaProp& Prop : Layout.Props)
	{
		const bool bBlocking = (Prop.Tier == EFPSRArenaPropTier::Blocking);
		const double PropHeight = bBlocking ? BlockingPropHeight : PassablePropHeight;
		const FVector Centre = Layout.CellCenterWorld(Prop.Cell.X, Prop.Cell.Y);
		AddBox(BlockingMeshes,
			FVector(Centre.X, Centre.Y, Origin.Z + PropHeight * 0.5),
			FVector(Cell * 0.9, Cell * 0.9, PropHeight));
	}

	UE_LOG(LogFPSR, Log, TEXT("[Arena] Representation rebuilt: %d clusters + %d props + 4 walls + floor (seed %d)."),
		Layout.Clusters.Num(), Layout.Props.Num(), Layout.Seed);
}

void AFPSRArenaActor::SetArenaActive(bool bInActive)
{
	bIsArenaActive = bInActive;

	if (BlockingMeshes)
	{
		BlockingMeshes->SetVisibility(bInActive);
		BlockingMeshes->SetCollisionEnabled(bInActive ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
	if (FloorMeshes)
	{
		FloorMeshes->SetVisibility(bInActive);
		FloorMeshes->SetCollisionEnabled(bInActive ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}

	// Marker actors physically inside THIS arena's grid — same spatial-ownership test BuildLayoutForSeed uses, so
	// "which arena does this marker belong to" can never disagree between generation and activation. Transform is
	// never touched (Static mobility on all three types); SetActorHiddenInGame/SetActorEnableCollision are
	// idempotent, so calling this twice with the same bInActive is a harmless no-op.
	if (const UWorld* World = GetWorld())
	{
		for (TActorIterator<AFPSRArenaBlocker> It(const_cast<UWorld*>(World)); It; ++It)
		{
			AFPSRArenaBlocker* Blocker = *It;
			if (!IsValid(Blocker) || !ContainsWorldLocation(Blocker->GetActorLocation())) { continue; }
			Blocker->SetActorHiddenInGame(!bInActive);
			Blocker->SetActorEnableCollision(bInActive);
		}
		for (TActorIterator<AFPSRArenaLandmark> It(const_cast<UWorld*>(World)); It; ++It)
		{
			AFPSRArenaLandmark* Landmark = *It;
			if (!IsValid(Landmark) || !ContainsWorldLocation(Landmark->GetActorLocation())) { continue; }
			Landmark->SetActorHiddenInGame(!bInActive);
			Landmark->SetActorEnableCollision(bInActive);
		}
		for (TActorIterator<AFPSRArenaDestructible> It(const_cast<UWorld*>(World)); It; ++It)
		{
			AFPSRArenaDestructible* Destructible = *It;
			if (!IsValid(Destructible) || !ContainsWorldLocation(Destructible->GetActorLocation())) { continue; }
			Destructible->SetActorHiddenInGame(!bInActive);
			Destructible->SetActorEnableCollision(bInActive);
		}
	}
}

bool AFPSRArenaActor::ContainsWorldLocation(const FVector& World) const
{
	// Reads the two dimension fields straight off the asset instead of going through GetGenParams/ToGenParams:
	// that path copies the whole generator contract INCLUDING the flattened prop-set entry arrays, and this
	// predicate runs once per marker actor in each of SetArenaActive's three sweeps and again in
	// BuildLayoutForSeed — hundreds of throwaway array copies for a bounds test that needs two numbers.
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
	const double MinX = Centre.X - 0.5 * SpanX;
	const double MinY = Centre.Y - 0.5 * SpanY;

	// Z is deliberately absent from this test — see the header comment. The arena is a single Z-plane and reserve
	// arenas are parked BESIDE the live one (2026-08-17 decision), so XY alone is what tells two arenas apart.
	return World.X >= MinX && World.X < MinX + SpanX
		&& World.Y >= MinY && World.Y < MinY + SpanY;
}

bool AFPSRArenaActor::GetPlayerEntryTransforms(TArray<FTransform>& Out) const
{
	Out.Reset();

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
	if (GArenaDebugGrid == 0 || !Layout.IsValid())
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
	const int32 MinCX = FMath::Max(0, FMath::FloorToInt((ViewPos.X - R - Origin.X) / Cell));
	const int32 MaxCX = FMath::Min(Layout.GridDims.X - 1, FMath::CeilToInt((ViewPos.X + R - Origin.X) / Cell));
	const int32 MinCY = FMath::Max(0, FMath::FloorToInt((ViewPos.Y - R - Origin.Y) / Cell));
	const int32 MaxCY = FMath::Min(Layout.GridDims.Y - 1, FMath::CeilToInt((ViewPos.Y + R - Origin.Y) / Cell));

	const FVector HalfCell(Cell * 0.5 * 0.9, Cell * 0.5 * 0.9, 4.0);
	for (int32 CY = MinCY; CY <= MaxCY; ++CY)
	{
		for (int32 CX = MinCX; CX <= MaxCX; ++CX)
		{
			const bool bOpen = FFPSRArenaGenerator::IsCellOpen(Layout, CX, CY);
			DrawDebugBox(World, Layout.CellCenterWorld(CX, CY) + FVector(0, 0, 10.0), HalfCell,
				bOpen ? FColor(0, 160, 255, 40) : FColor(255, 64, 0, 90), false, -1.0f, 0, 1.0f);
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
