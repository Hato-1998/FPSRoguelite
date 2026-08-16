// Copyright Epic Games, Inc. All Rights Reserved.

#include "Arena/FPSRArenaActor.h"
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

bool AFPSRArenaActor::BuildLocalLayout()
{
	if (!ArenaParams)
	{
		UE_LOG(LogFPSR, Error, TEXT("[Arena] %s has no ArenaParams — no layout, no flow field. Assign a UFPSRArenaParamsDataAsset."),
			*GetName());
		Layout = FFPSRArenaLayout();
		return false;
	}

	const FFPSRArenaGenParams GenParams = ArenaParams->ToGenParams();

	// Gather what the designer placed. Note this reads ACTORS, not collision — the arena is told what blocks it
	// rather than discovering it, which is what keeps the mask free of world queries (ADR 0011 E3).
	FFPSRArenaAuthoredInput Authored;
	if (const UWorld* World = GetWorld())
	{
		for (TActorIterator<AFPSRArenaBlocker> It(const_cast<UWorld*>(World)); It; ++It)
		{
			const AFPSRArenaBlocker* Blocker = *It;
			if (!IsValid(Blocker)) { continue; }
			FFPSRArenaAuthoredBox BoxEntry;
			BoxEntry.Center = Blocker->GetActorLocation();
			BoxEntry.HalfExtentXY = Blocker->GetHalfExtentXY();
			BoxEntry.YawDegrees = Blocker->GetYawDegrees();
			Authored.Blockers.Add(BoxEntry);
		}
		for (TActorIterator<AFPSRArenaLandmark> It(const_cast<UWorld*>(World)); It; ++It)
		{
			const AFPSRArenaLandmark* Landmark = *It;
			if (!IsValid(Landmark)) { continue; }
			FFPSRArenaAuthoredLandmark LandmarkEntry;
			LandmarkEntry.Location = Landmark->GetActorLocation();
			LandmarkEntry.ReserveRadiusCells = Landmark->GetReserveRadiusCells();
			LandmarkEntry.bBlocking = Landmark->IsBlocking();
			Authored.Landmarks.Add(LandmarkEntry);
		}
	}

	if (!FFPSRArenaGenerator::Generate(ActiveSeed, GenParams, ComputeGridOrigin(GenParams), Authored, Layout))
	{
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

	UE_LOG(LogFPSR, Log, TEXT("[Arena] Representation rebuilt: %d clusters + 4 walls + floor (seed %d)."),
		Layout.Clusters.Num(), Layout.Seed);
}

AFPSRArenaActor* AFPSRArenaActor::FindInWorld(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AFPSRArenaActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (IsValid(*It))
		{
			return *It;
		}
	}
	return nullptr;
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
	AFPSRArenaActor* Arena = AFPSRArenaActor::FindInWorld(World);
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
