// Copyright Epic Games, Inc. All Rights Reserved.

#include "Arena/FPSRArenaActor.h"
#include "Arena/FPSRArenaBakeDataAsset.h"
#include "Arena/FPSRArenaDestructible.h"
#include "Arena/FPSRArenaGenerator.h"
#include "Arena/FPSRArenaMarkers.h"
#include "Arena/FPSRArenaParamsDataAsset.h"
#include "Arena/FPSRArenaValidator.h"
#include "Core/FPSRGameState.h"
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
		if (FFPSRArenaGenerator::IsCellOpen(Layout, AnchorCell.X, AnchorCell.Y))
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
					if (FFPSRArenaGenerator::IsCellOpen(Layout, CX, CY))
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

	// L2 floor wiring (ADR 0010 D8): NoCollision throughout — 0010 D4 「평면 장식」, these never affect traversal.
	TraceMeshes = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("TraceMeshes"));
	TraceMeshes->SetupAttachment(SceneRoot);
	TraceMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TraceMeshes->SetMobility(EComponentMobility::Static);

	JunctionMeshes = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("JunctionMeshes"));
	JunctionMeshes->SetupAttachment(SceneRoot);
	JunctionMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	JunctionMeshes->SetMobility(EComponentMobility::Static);
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

	// F1: the server's layout was already built by UFPSRFlowFieldSubsystem::OnWorldBeginPlay PULLING it
	// (Arena->BuildLocalLayout(), reading the ActiveSeed PostInitializeComponents already finalized) — calling
	// BuildLocalLayout() again here would silently regenerate the whole grid a second time for nothing. A client
	// has no such pull (the flow field subsystem is server-only — HasServerAuthority() gates its whole
	// OnWorldBeginPlay), so this IS its first build, UNLESS OnRep_ActiveSeed already ran first (OnRep can fire
	// before BeginPlay for an actor that exists at level load). Either way, HasLayout() is the correct guard.
	if (!HasLayout())
	{
		BuildLocalLayout();
	}
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
	if (!BlockingMeshes || !FloorMeshes || !TraceMeshes || !JunctionMeshes)
	{
		return;
	}

	BlockingMeshes->ClearInstances();
	FloorMeshes->ClearInstances();
	TraceMeshes->ClearInstances();
	JunctionMeshes->ClearInstances();

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

	// Same pivot-cancelling idea as AddBox, but (a) takes a YAW so a trace segment can point along its own
	// direction, and (b) takes the mesh's size/offset as PARAMETERS rather than a capture, because traces and
	// junctions are drawn with their OWN meshes (TraceMesh / TraceJunctionMesh), not WhiteboxCubeMesh — one
	// lambda serves both instead of two near-duplicates that could drift apart.
	auto AddOrientedBox = [](UHierarchicalInstancedStaticMeshComponent* Comp, const FVector& MeshSizeIn,
		const FVector& MeshOffsetIn, const FVector& WorldCentre, const FVector& SizeCm, float YawDegrees)
	{
		const FVector Scale(SizeCm.X / MeshSizeIn.X, SizeCm.Y / MeshSizeIn.Y, SizeCm.Z / MeshSizeIn.Z);
		const FRotator Rot(0.0f, YawDegrees, 0.0f);
		// The pivot offset lives in the mesh's LOCAL space, so it has to be scaled AND rotated (in that order —
		// TRS) into world space before it can be subtracted from WorldCentre; AddBox skips the rotation only
		// because its rotation is always identity.
		const FVector PivotWorld = Rot.RotateVector(MeshOffsetIn * Scale);
		const FTransform Xf(Rot, WorldCentre - PivotWorld, Scale);
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

	// L2 floor wiring (ADR 0010 D8): each trace segment is a box oriented along the two cells it connects, each
	// junction a square "via" marker. Both are drawn with their OWN meshes and both are silently skipped (no
	// warning) when unassigned — unlike WhiteboxCubeMesh above, an unset TraceMesh/TraceJunctionMesh is a normal
	// "no art yet" authoring state, not a gap in the arena's whole representation. FPSR.Arena.DebugTraces shows
	// the topology regardless of whether either mesh is assigned.
	const double TraceZ = Origin.Z + TraceHeightCm * 0.5;

	if (TraceMesh)
	{
		const FBoxSphereBounds TraceBounds = TraceMesh->GetBounds();
		const FVector TraceMeshSize = TraceBounds.BoxExtent * 2.0;
		if (TraceMeshSize.X <= UE_KINDA_SMALL_NUMBER || TraceMeshSize.Y <= UE_KINDA_SMALL_NUMBER || TraceMeshSize.Z <= UE_KINDA_SMALL_NUMBER)
		{
			UE_LOG(LogFPSR, Error, TEXT("[Arena] TraceMesh '%s' has degenerate bounds (%s) — trace geometry not built."),
				*TraceMesh->GetName(), *TraceMeshSize.ToString());
		}
		else
		{
			TraceMeshes->SetStaticMesh(TraceMesh);
			for (const FFPSRArenaTraceSegment& Seg : Layout.Traces)
			{
				const FVector FromWorld = Layout.CellCenterWorld(Seg.FromCell.X, Seg.FromCell.Y);
				const FVector ToWorld = Layout.CellCenterWorld(Seg.ToCell.X, Seg.ToCell.Y);
				const FVector Delta = ToWorld - FromWorld;
				// Diagonal segments come out √2·Cell long automatically — no special-casing needed.
				const double SegLength = Delta.Size();
				const float SegYaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)));
				const double SegWidth = TraceWidthCm + Seg.WidthClass * TraceWidthPerClassCm;
				const FVector MidPoint = (FromWorld + ToWorld) * 0.5;

				AddOrientedBox(TraceMeshes, TraceMeshSize, TraceBounds.Origin,
					FVector(MidPoint.X, MidPoint.Y, TraceZ),
					FVector(SegLength, SegWidth, TraceHeightCm), SegYaw);
			}
		}
	}

	if (TraceJunctionMesh)
	{
		const FBoxSphereBounds JuncBounds = TraceJunctionMesh->GetBounds();
		const FVector JuncMeshSize = JuncBounds.BoxExtent * 2.0;
		if (JuncMeshSize.X <= UE_KINDA_SMALL_NUMBER || JuncMeshSize.Y <= UE_KINDA_SMALL_NUMBER || JuncMeshSize.Z <= UE_KINDA_SMALL_NUMBER)
		{
			UE_LOG(LogFPSR, Error, TEXT("[Arena] TraceJunctionMesh '%s' has degenerate bounds (%s) — junction geometry not built."),
				*TraceJunctionMesh->GetName(), *JuncMeshSize.ToString());
		}
		else
		{
			JunctionMeshes->SetStaticMesh(TraceJunctionMesh);
			for (const FIntPoint& JCell : Layout.TraceJunctions)
			{
				const FVector JWorld = Layout.CellCenterWorld(JCell.X, JCell.Y);
				AddOrientedBox(JunctionMeshes, JuncMeshSize, JuncBounds.Origin,
					FVector(JWorld.X, JWorld.Y, TraceZ),
					FVector(TraceWidthCm, TraceWidthCm, TraceHeightCm), 0.0f);
			}
		}
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
	// L2 wiring is NoCollision (0010 D4 「평면 장식」) so only visibility matters here — but it does matter: a dormant
	// arena that hid its walls and floor while still drawing its floor traces would leave a glowing circuit diagram
	// floating in the dark 300 m from the live arena.
	if (TraceMeshes)
	{
		TraceMeshes->SetVisibility(bInActive);
	}
	if (JunctionMeshes)
	{
		JunctionMeshes->SetVisibility(bInActive);
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

			// F2: on ACTIVATION only, and only the authority — reset any destructible this arena grid owns back to
			// intact. Nothing else in the codebase ever clears bBroken, so without this a destructible broken on an
			// earlier visit stayed broken (and unbreakable — 0 health) forever, and a revisited arena could never
			// offer its terrain-changing reward again (ADR 0010 D7 "지형을 바꾼다" means each visit, not once ever).
			// Deactivation does NOT reset — a dormant arena's destructibles should still read as however the
			// player last left them if that arena becomes active again without an intervening reset pass. The
			// explicit HasAuthority() here (on top of ServerReset()'s own guard) avoids the call entirely on
			// clients, who reach this same loop via OnRep_StageTransition -> ApplyStageTransitionLocal.
			if (bInActive && HasAuthority())
			{
				Destructible->ServerReset();
			}

			Destructible->SetActorHiddenInGame(!bInActive);
			Destructible->SetActorEnableCollision(bInActive);
		}
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

	// P3 redteam fix: neither an authored APlayerStart nor the centre-offset fallback below checks the mask — a
	// start sitting inside a cell L1 (or the skeleton-avoidance fix) ended up blocking, or a fallback offset that
	// happens to land on a blocked cell near a busy centre, both used to teleport straight into geometry (the
	// caller's teleport uses bSweep=false, which does not resolve overlaps — see PerformSwap). Snaps every
	// candidate's XY onto the nearest OPEN cell in place; Z and rotation are left untouched. Skipped when this
	// machine has no layout yet (e.g. inspected in the editor before BuildLocalLayout ever ran) since IsCellOpen
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
			const FIntPoint Cell(
				FMath::FloorToInt((Loc.X - Layout.GridOrigin.X) / Layout.CellSize),
				FMath::FloorToInt((Loc.Y - Layout.GridOrigin.Y) / Layout.CellSize));

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
				const bool bOpen = FFPSRArenaGenerator::IsCellOpen(Layout, CX, CY);
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
