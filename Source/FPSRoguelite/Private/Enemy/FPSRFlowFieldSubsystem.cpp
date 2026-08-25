// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSRFlowFieldSubsystem.h"
#include "Enemy/FPSRFlowFieldBoundsVolume.h"
#include "Enemy/FPSRHoverWindowSubsystem.h" // S3 (ADR 0009 P1): QueryFlow's Window3D routing
#include "Arena/FPSRArenaActor.h"
#include "Arena/FPSRArenaDestructible.h" // S2/merge-gate P2: intact-prop voxel stamp at adoption (GetVoxelBounds/IsBroken)
#include "Core/FPSRGameState.h"
#include "Core/FPSRLogChannels.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/HitResult.h"
#include "TimerManager.h"
#include "EngineUtils.h"

#if !UE_BUILD_SHIPPING
#include "HAL/IConsoleManager.h"
static TAutoConsoleVariable<int32> CVarFlowFieldDebug(
	TEXT("FPSR.FlowField.Debug"), 0,
	TEXT("Draw the swarm flow field near players (1 = flow arrows + blocked cells, per surface at each layer's floor height; rank>=1 arrows tinted cyan; per map). Dev only."),
	ECVF_Cheat);
#endif

static constexpr float GFlowUpdateInterval = 0.2f; // seconds between recomputes

bool UFPSRFlowFieldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
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

bool UFPSRFlowFieldSubsystem::HasServerAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

float UFPSRFlowFieldSubsystem::DetectFloorZ(UWorld& InWorld) const
{
	// Default grid Z anchor: the floor under the first PlayerStart (trace down); fall back to the start's Z / origin.
	for (TActorIterator<APlayerStart> It(&InWorld); It; ++It)
	{
		if (const APlayerStart* Start = *It)
		{
			const FVector StartLoc = Start->GetActorLocation();
			FHitResult Hit;
			return InWorld.LineTraceSingleByChannel(Hit, StartLoc, StartLoc - FVector(0.0f, 0.0f, 5000.0f), ECC_WorldStatic)
				? Hit.ImpactPoint.Z : StartLoc.Z;
		}
	}
	return 0.0f;
}

float UFPSRFlowFieldSubsystem::DetectFloorZForVolume(UWorld& InWorld, const AFPSRFlowFieldBoundsVolume& Volume) const
{
	// Per-map Z anchor: a streamed sublevel need not contain its own PlayerStart, so anchor from the volume's OWN box —
	// trace down from the box top through its center; fall back to the box's world-min Z (Codex consult BLOCKER fix).
	const FBox WB = Volume.GetWorldBounds();
	if (!WB.IsValid)
	{
		return Volume.GetActorLocation().Z;
	}
	const FVector Center = WB.GetCenter();
	const FVector Start(Center.X, Center.Y, WB.Max.Z + 100.0f);
	const FVector End(Center.X, Center.Y, WB.Min.Z - 1000.0f);
	FHitResult Hit;
	return InWorld.LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic) ? Hit.ImpactPoint.Z : WB.Min.Z;
}

void UFPSRFlowFieldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!HasServerAuthority())
	{
		return; // clients never build or recompute the field
	}

	// P-G: build ONE continuous field per world. Scan for the bUnifiedExtent volume (multimap) + a single untagged volume
	// (single-map extent). MapId'd slot volumes are baked into the unified grid by BuildUnifiedField (their world AABBs go
	// into SlotBounds for occupancy / MapId resolution); there is no longer a per-map flow registry.
	bool bAnyMapIdVolume = false;
	const AFPSRFlowFieldBoundsVolume* UnifiedVolume = nullptr;
	const AFPSRFlowFieldBoundsVolume* UntaggedVolume = nullptr;
	for (TActorIterator<AFPSRFlowFieldBoundsVolume> It(&InWorld); It; ++It)
	{
		const AFPSRFlowFieldBoundsVolume* Volume = *It;
		if (!Volume)
		{
			continue;
		}
		if (Volume->IsUnifiedExtent())
		{
			UnifiedVolume = Volume; // U extent — the multimap unified grid (built below)
			continue;
		}
		if (!Volume->GetMapId().IsValid())
		{
			if (!UntaggedVolume) { UntaggedVolume = Volume; } // single-map extent -> the degenerate unified grid below
			continue;
		}
		bAnyMapIdVolume = true; // a MapId'd slot -> baked into the unified grid by BuildUnifiedField (needs a bUnifiedExtent)
	}

	// ADR 0010: an arena actor OWNS the obstacle mask. We PULL it here rather than letting the actor push during its
	// BeginPlay, because actor BeginPlay runs AFTER world subsystems' OnWorldBeginPlay (UWorld::BeginPlay dispatches
	// SubsystemCollection.ForEachSubsystem(OnWorldBeginPlay) FIRST, then GameMode->StartPlay -> ... -> every actor's
	// own BeginPlay — verified against UE 5.7's World.cpp/GameModeBase.cpp/GameStateBase.cpp/WorldSettings.cpp; an
	// earlier version of this comment had the order backwards). A push from actor BeginPlay would therefore always
	// arrive too LATE for this pull, not "be overwritten a moment later". That is exactly why the seed this pull
	// depends on (AFPSRArenaActor::ActiveSeed) is finalized in PostInitializeComponents rather than BeginPlay — the
	// only actor callback the engine guarantees to run before this OnWorldBeginPlay pull (see FPSRArenaActor.h and
	// FPSRArenaActor::PostInitializeComponents for the full chain).
	if (AFPSRArenaActor* Arena = AFPSRArenaActor::FindActiveInWorld(&InWorld))
	{
		// ADR 0012 invariant 1: the BAKED mask is the only source. It is produced in the editor from the level's
		// own collision, so what the designer sees IS what the swarm believes. There is deliberately no second
		// branch here — the procedural fallback that used to sit below was removed with the runtime generator,
		// and a world-trace fallback is forbidden outright: the trace anchors its grid Z from the PlayerStart
		// downtrace, so the day it fired next to a parked reserve arena it would anchor the field to that
		// arena's roof.
		//
		// bBumpGenerationAndRecompute=false: this world-begin pull happens BEFORE the immediate RecomputeAllFields()
		// call a few lines down (which every path below — this one, BuildUnifiedField, BuildFromWorldTrace — shares),
		// so bumping the generation here too would push a fresh world off TopologyGeneration 0 (see
		// AdoptArenaFieldInternal's header comment for why that regresses the late-join ack).
		if (AdoptArenaFieldInternal(*Arena, /*bBumpGenerationAndRecompute=*/false))
		{
			UE_LOG(LogFPSR, Log,
				TEXT("[FlowField] Arena field adopted from %s BAKE (%dx%d cell=%.0f) — no world trace, no generation."),
				*Arena->GetName(), BakedBaseline.GridDimX, BakedBaseline.GridDimY, BakedBaseline.CellSize);
		}
		else
		{
			// Fail-fast, NOT fall-through. An arena reaching this has no bake asset assigned (or an empty one),
			// which means the level a designer is looking at and the mask the swarm would use came from different
			// places — the exact condition ADR 0012 exists to remove. Building NO field makes that loud; quietly
			// substituting one makes it a bug someone finds in playtest instead.
			UE_LOG(LogFPSR, Error,
				TEXT("[FlowField] %s has NO baked mask — building NO flow field. Assign a UFPSRArenaBakeDataAsset and bake it (Tools > FPSR > 아레나 베이크); the world-trace bake is deliberately NOT used as a fallback."),
				*Arena->GetName());
		}
	}
	else if (UnifiedVolume)
	{
		// Multimap: the pre-sized bUnifiedExtent grid with every MapId'd slot baked in (sets UnifiedComputer + SlotBounds +
		// bUnifiedMultiSlot + the BakedBaseline snapshot).
		BuildUnifiedField(InWorld, *UnifiedVolume);
	}
	else
	{
		// P-G auto-degenerate single-slot unified: the ONE world-trace grid IS the continuous field for a plain single-map
		// (an untagged bounds volume, or the origin-centered fallback). Serves flow + connectivity; SlotBounds stays empty (no
		// per-slot MapId partition) and bUnifiedMultiSlot stays false, so the multimap-only gates (ack / combat connectivity /
		// front / trickle) are inert here — single-map is a strict no-op, exactly as before P-G.
		// Z anchor = the PlayerStart floor (DetectFloorZ) whether or not an untagged bounds volume is present — matching the
		// pre-P-G Default field ("S1a parity"). The untagged volume only SIZES the grid; anchoring it at its box-center trace
		// (DetectFloorZForVolume) would regress single-map GridOrigin.Z + re-introduce the volume-center mis-anchor gotcha.
		const float FloorZ = DetectFloorZ(InWorld);
		UnifiedComputer = NewObject<UFPSRFlowFieldComputer>(this);
		UnifiedComputer->BuildFromWorldTrace(&InWorld, UntaggedVolume, FloorZ);
		UnifiedComputer->ExtractSurfaceData(BakedBaseline);
		bHasBaseline = true;
		if (bAnyMapIdVolume)
		{
			UE_LOG(LogFPSR, Warning,
				TEXT("[FlowField] U P-G: MapId'd slot volume(s) present without a bUnifiedExtent volume — built a single degenerate grid (multimap flow needs a bUnifiedExtent). Author one for multimap content."));
		}
	}

	// U (P-F): subscribe to the GameState freeze/run-state so a door broken DURING a card-select freeze is recomputed the
	// instant the freeze lifts (the 0.2s recompute no-ops while paused). Lazily re-bound in RecomputeAllFields if the
	// GameState isn't up yet at world begin.
	TryBindRunStateHandler();

	InWorld.GetTimerManager().SetTimer(
		RecomputeTimerHandle, this, &UFPSRFlowFieldSubsystem::RecomputeAllFields,
		GFlowUpdateInterval, true);

	// Recompute ONCE immediately (not only on the first 0.2s tick) so the connectivity labels are ready from t=0. Without
	// this, the unified combat gate (AreWorldLocationsConnected) fails closed for every pawn until the first scheduled
	// recompute, blocking all player damage during the world-begin window (Codex R16). Source-less is fine — it still
	// rebuilds the labels; player flow sources are added by the timer once pawns are possessed.
	RecomputeAllFields();
}

void UFPSRFlowFieldSubsystem::BuildUnifiedField(UWorld& InWorld, const AFPSRFlowFieldBoundsVolume& UnifiedVolume)
{
	if (!HasServerAuthority())
	{
		return;
	}
	const FBox Extent = UnifiedVolume.GetWorldBounds();
	if (!Extent.IsValid)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[FlowField] U: bUnifiedExtent volume has no valid bounds; unified field not built."));
		return;
	}
	const float CellSize = (UnifiedVolume.GetCellSizeOverride() > 0.0f) ? UnifiedVolume.GetCellSizeOverride() : 200.0f;
	const float Step = UnifiedVolume.GetClimbableStepHeightOverride(); // 0 -> BuildEmptyGrid uses the bake default (45)
	const float FloorZ = DetectFloorZForVolume(InWorld, UnifiedVolume);
	const FVector Origin(Extent.Min.X, Extent.Min.Y, FloorZ);
	const int32 DimX = FMath::Max(1, FMath::CeilToInt(Extent.GetSize().X / CellSize));
	const int32 DimY = FMath::Max(1, FMath::CeilToInt(Extent.GetSize().Y / CellSize));

	// Enforce the P-0 cell budget BEFORE allocating (Codex R10): unlike BuildFromWorldTrace, BuildEmptyGrid does NOT coarsen,
	// so an oversized bUnifiedExtent must fail here rather than allocate an over-budget grid with runaway recompute / memory.
	// This is the runtime P-0 gate; the authored extent should already pass CheckUnifiedGridBudget at author time. P-G: there
	// is NO per-map registry fallback anymore — a fail here leaves the multimap with no flow field (enemies only beeline,
	// combat allow-all), so this is a hard content error. Fix the extent; do not ship an over-budget bUnifiedExtent.
	if (DimX > UFPSRFlowFieldComputer::GetMaxGridDimPerAxis() || DimY > UFPSRFlowFieldComputer::GetMaxGridDimPerAxis() ||
		static_cast<int64>(DimX) * DimY > UFPSRFlowFieldComputer::GetMaxTotalCells())
	{
		UE_LOG(LogFPSR, Error,
			TEXT("[FlowField] U: unified extent %dx%d cells exceeds the budget (axis<=%d, total<=%d); NOT building the unified grid (multimap will have NO flow field — content error). Shrink the extent or raise the volume's CellSizeOverride (P-0 gate)."),
			DimX, DimY, UFPSRFlowFieldComputer::GetMaxGridDimPerAxis(), UFPSRFlowFieldComputer::GetMaxTotalCells());
		return;
	}

	UnifiedComputer = NewObject<UFPSRFlowFieldComputer>(this);
	UnifiedComputer->BuildEmptyGrid(DimX, DimY, Origin, CellSize, Step);
	bUnifiedMultiSlot = true; // P-G: a real bUnifiedExtent multimap grid -> the multimap-only gates apply
	UE_LOG(LogFPSR, Log, TEXT("[FlowField] U unified grid %dx%d cell=%.0f origin=%s built from bUnifiedExtent volume."),
		DimX, DimY, CellSize, *Origin.ToString());

	// Bake every currently-loaded MapId'd slot volume into the unified grid (streamed slots bake in later via BakeDiscoveredMap).
	int32 Baked = 0;
	for (TActorIterator<AFPSRFlowFieldBoundsVolume> It(&InWorld); It; ++It)
	{
		const AFPSRFlowFieldBoundsVolume* Slot = *It;
		if (!Slot || Slot->IsUnifiedExtent())
		{
			continue;
		}
		// P-G: record the slot's world AABB for occupancy / MapId resolution (FindMapIdForLocation), INDEPENDENT of the flow
		// bake succeeding — a player standing in a sealed slot still resolves to it. Keyed by MapId (an untagged slot -> unset).
		const FBox SlotBox = Slot->GetWorldBounds();
		SlotBounds.Add(Slot->GetMapId(), SlotBox);
		MaxSlotFootprintDiagonalCm = FMath::Max(MaxSlotFootprintDiagonalCm, SlotFootprintDiagonalXY(SlotBox)); // P-H: net-cull footprint cap
		if (BakeSlotIntoUnified(InWorld, *Slot))
		{
			++Baked;
		}
	}
	UE_LOG(LogFPSR, Log, TEXT("[FlowField] U: %d slot(s) baked into the unified grid at world begin."), Baked);

	// U (P-F): snapshot the just-baked unified surface graph (all seam doors still closed) as the new-run reset baseline.
	// Captured AFTER every world-begin slot bakes but BEFORE any door opens, so ResetDoorTopologyToBaseline restores exactly
	// this closed-seam topology. Server-only; runs before GameMode::BeginPlay/StartRun (WorldSubsystem ordering, confirmed).
	UnifiedComputer->ExtractSurfaceData(BakedBaseline);
	bHasBaseline = true;
}

float UFPSRFlowFieldSubsystem::SlotFootprintDiagonalXY(const FBox& SlotBox)
{
	// U P-H: XY diagonal of a slot's world AABB — the footprint cap the swarm net-cull radius is bounded by. Invalid box -> 0
	// (contributes nothing to the max), so a malformed slot can't inflate the cap.
	if (!SlotBox.IsValid)
	{
		return 0.0f;
	}
	const FVector Size = SlotBox.GetSize();
	return FMath::Sqrt(Size.X * Size.X + Size.Y * Size.Y);
}

bool UFPSRFlowFieldSubsystem::BakeSlotIntoUnified(UWorld& InWorld, const AFPSRFlowFieldBoundsVolume& Slot)
{
	if (!UnifiedComputer)
	{
		return false;
	}
	const FBox SlotBox = Slot.GetWorldBounds();
	if (!SlotBox.IsValid)
	{
		return false;
	}
	// Integer-owned alignment (Codex R1 Q4): CellOffset is the source of truth; CommitSubregion rejects a slot whose box
	// doesn't snap to Origin + CellOffset*CellSize (author bounds drift) -> that subregion stays sealed (fail-closed).
	const FVector Origin = UnifiedComputer->GetGridOrigin();
	const float CellSize = UnifiedComputer->GetCellSize();
	const FIntPoint CellOffset(
		FMath::RoundToInt((SlotBox.Min.X - Origin.X) / CellSize),
		FMath::RoundToInt((SlotBox.Min.Y - Origin.Y) / CellSize));
	const float SlotFloorZ = DetectFloorZForVolume(InWorld, Slot);
	const bool bOk = UnifiedComputer->BakeSlotIntoUnifiedGrid(&InWorld, &Slot, SlotFloorZ, CellOffset);
	if (!bOk)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[FlowField] U: slot '%s' at offset (%d,%d) failed to bake (misaligned / no floor / step mismatch) — subregion sealed."),
			*Slot.GetMapId().ToString(), CellOffset.X, CellOffset.Y);
	}
	return bOk;
}

bool UFPSRFlowFieldSubsystem::AdoptArenaFieldInternal(const AFPSRArenaActor& Arena, bool bBumpGenerationAndRecompute)
{
	if (!HasServerAuthority())
	{
		return false;
	}

	FFPSRFlowFieldSurfaceData WorldSurface;
	if (!Arena.GetBakedWorldSurface(WorldSurface) || WorldSurface.GridDimX <= 0 || WorldSurface.GridDimY <= 0)
	{
		// Callers log their own contextual message (world-begin and a stage swap want different wording) — this
		// function only decides whether the adoption itself succeeded.
		return false;
	}

	if (!UnifiedComputer)
	{
		UnifiedComputer = NewObject<UFPSRFlowFieldComputer>(this);
	}
	UnifiedComputer->BuildFromSurfaceData(WorldSurface);

	// 2D counterpart of the 3D voxel stamp further down: the editor bake probes ObjParams={ECC_WorldStatic} only
	// (FPSRFlowFieldComputer's world-trace bake), and this prop's mesh is ECC_FPSRDestructible (see
	// AFPSRArenaDestructible's constructor comment) — so every cell an intact destructible stands on baked
	// "walkable", and HandleBrokenAuthority's NotifyArenaCellsOpened would go on to "open" a cell that was never
	// actually closed. MUST run BEFORE ExtractSurfaceData(BakedBaseline) below, not after: the baseline is what
	// ResetDoorTopologyToBaseline replays on a revisit, so it has to already contain these blocked cells for a
	// revisited arena to restore its intact destructibles CLOSED — the exact same "stamps included — a baseline
	// reset restores props CLOSED" contract VoxelBaseline documents a few lines down for the 3D field. Stamping
	// after the baseline snapshot would let a reset resurrect a broken prop's cells as open.
	FFPSRArenaGenParams Params;
	FVector Origin;
	if (Arena.GetGenParams(Params, Origin))
	{
		if (UWorld* StampWorld = GetWorld())
		{
			int32 StampedProps = 0;
			int32 StampedCells = 0;
			TArray<int32> Cells; // hoisted: ComputeGridCells Resets it, so one allocation serves every prop
			for (TActorIterator<AFPSRArenaDestructible> It(StampWorld); It; ++It)
			{
				const AFPSRArenaDestructible* Prop = *It;
				if (Prop && !Prop->IsBroken() && Arena.ContainsWorldLocation(Prop->GetActorLocation()))
				{
					Prop->ComputeGridCells(Origin, Params.CellSize, Params.ArenaSizeCells, Cells);
					for (int32 Cell : Cells)
					{
						UnifiedComputer->StampCellBlocked(Cell, /*Rank=*/0, /*bBlocked=*/true);
					}
					++StampedProps;
					StampedCells += Cells.Num();
				}
			}
			UE_LOG(LogFPSR, Log,
				TEXT("[FlowField] Arena surface adopted from %s: %d intact destructible(s) stamped blocked (%d cell(s))."),
				*Arena.GetName(), StampedProps, StampedCells);
		}
	}
	// GetGenParams failing here is not logged separately — it means this arena has no grid params to stamp
	// against, and the surface adoption above already either succeeded on its own terms or the caller will log
	// the overall adoption failure; a second warning here would just be noise on top of that.

	// The adopted arena IS the baseline now. Keeping the old snapshot would let a later
	// ResetDoorTopologyToBaseline restore the PREVIOUS arena's walls into the current one.
	UnifiedComputer->ExtractSurfaceData(BakedBaseline);
	bHasBaseline = true;

	// S2 (ADR 0009 P1): pull this SAME arena's WORLD-space voxel occupancy from the SAME bake asset + the SAME
	// actor transform as the surface above, so flow-surface and voxel-occupancy can never describe different
	// arenas/poses (ADR 0009 결정 3's whole reason for riding the existing bake pipeline instead of a second one).
	// No voxels baked yet (an asset baked before this P1) -> soft downgrade: AdoptedVoxels/VoxelBaseline go empty
	// and S3's window simply has no field here until re-baked — logged once, not a hard failure, because the 2D
	// flow field everything else already depends on is fine.
	FFPSRArenaVoxelData WorldVoxels;
	const UFPSRArenaBakeDataAsset* BakeData = Arena.GetBakeData();
	if (BakeData && BakeData->BuildWorldVoxels(Arena.GetActorTransform(), WorldVoxels))
	{
		AdoptedVoxels = MoveTemp(WorldVoxels);

		// Stamp INTACT destructible props into the adopted field (merge-gate P2): their meshes are
		// ECC_FPSRDestructible, so the editor bake's ECC_WorldStatic probe never saw them — without this stamp the
		// break-time NotifyArenaVolumeOpened clears bits that were never set (its whole purpose a no-op) while the
		// hover window happily floods straight through intact props. Spatial membership (ContainsWorldLocation) is
		// this class of actor's own arena-resolution rule; props are intact at every adoption path (SetArenaActive
		// resets them before ServerRegenerate; a fresh world starts intact) but IsBroken is checked anyway so an
		// unforeseen adoption order can only under-stamp, never resurrect a broken prop's occupancy.
		if (UWorld* StampWorld = GetWorld())
		{
			int32 StampedProps = 0;
			for (TActorIterator<AFPSRArenaDestructible> It(StampWorld); It; ++It)
			{
				const AFPSRArenaDestructible* Prop = *It;
				if (Prop && !Prop->IsBroken() && Arena.ContainsWorldLocation(Prop->GetActorLocation()))
				{
					FFPSRArenaVoxelData::SetOccupiedAABB(AdoptedVoxels, Prop->GetVoxelBounds());
					++StampedProps;
				}
			}
			UE_LOG(LogFPSR, Log, TEXT("[FlowField] Voxel field adopted from %s: %d intact destructible(s) stamped occupied."),
				*Arena.GetName(), StampedProps);
		}
	}
	else
	{
		AdoptedVoxels = FFPSRArenaVoxelData();
		UE_LOG(LogFPSR, Warning,
			TEXT("[FlowField] %s has no baked voxel occupancy — S3 hover window will have no field here until this arena is re-baked (ADR 0009 S2)."),
			*Arena.GetName());
	}
	VoxelBaseline = AdoptedVoxels; // same adopt-time-snapshot contract as BakedBaseline (stamps included — a
	                               // baseline reset restores props CLOSED, matching their intact reset on re-run)

	// Every published hover window was propagated over the PREVIOUS field's occupancy/placement — wrong-ARENA stale
	// from this instant (merge-gate P1). Null-safe: at world begin the window subsystem may not exist yet, and its
	// slots are all empty then anyway.
	if (UWorld* InvalidateWorld = GetWorld())
	{
		if (UFPSRHoverWindowSubsystem* HoverWindows = InvalidateWorld->GetSubsystem<UFPSRHoverWindowSubsystem>())
		{
			HoverWindows->InvalidateAllWindows();
		}
	}

	if (bBumpGenerationAndRecompute)
	{
		bTopologyMutatedSinceBaseline = false;

		// Connectivity changed wholesale, so clients' late-join ack and the freeze pre-unfreeze recompute must both
		// see a new generation; then recompute immediately rather than leaving up to 0.2s of stale flow after a
		// swap. AdvanceTopologyGeneration() — NOT a raw ++ — because it also mirrors the count to the replicated
		// GameState; a bare ++TopologyGeneration here left that mirror stale, so a remote client's late-join ack
		// could never see the post-swap generation (bug found during arena-topology multi-arena work; GameState
		// never OnRep'd).
		AdvanceTopologyGeneration();
		RecomputeAllFields();

		UE_LOG(LogFPSR, Log, TEXT("[FlowField] Adopted arena field %s (%dx%d cell=%.0f, topology gen %d)."),
			*Arena.GetName(), WorldSurface.GridDimX, WorldSurface.GridDimY, WorldSurface.CellSize, TopologyGeneration);
	}
	return true;
}

bool UFPSRFlowFieldSubsystem::AdoptArenaField(const AFPSRArenaActor& Arena)
{
	if (AdoptArenaFieldInternal(Arena, /*bBumpGenerationAndRecompute=*/true))
	{
		return true;
	}
	UE_LOG(LogFPSR, Error, TEXT("[FlowField] AdoptArenaField(%s) rejected: no usable baked surface."), *Arena.GetName());
	return false;
}

bool UFPSRFlowFieldSubsystem::BakeDiscoveredMap(const FGameplayTag& MapId)
{
	if (!HasServerAuthority() || !MapId.IsValid())
	{
		return false;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	for (TActorIterator<AFPSRFlowFieldBoundsVolume> It(World); It; ++It)
	{
		const AFPSRFlowFieldBoundsVolume* Volume = *It;
		if (Volume && Volume->GetMapId() == MapId)
		{
			// P-G: record the streamed slot's world AABB for occupancy / MapId resolution (independent of the flow bake).
			const FBox SlotBox = Volume->GetWorldBounds();
			SlotBounds.Add(MapId, SlotBox);
			MaxSlotFootprintDiagonalCm = FMath::Max(MaxSlotFootprintDiagonalCm, SlotFootprintDiagonalXY(SlotBox)); // P-H: net-cull footprint cap
			if (UnifiedComputer)
			{
				// U: swarm flow samples ONLY the unified field, so readiness = the unified slot bake succeeded. If it fails
				// (misaligned / no floor / step mismatch) report NOT ready, so the stream subsystem keeps the boundary
				// blockers up rather than letting players into a slot the swarm can't route through (Codex R10).
				const bool bBaked = BakeSlotIntoUnified(*World, *Volume);
				if (bBaked)
				{
					// U (P-F): a slot streaming in grows the unified topology — bump the generation (mirrored to the
					// GameState in Stage 2 so late joiners re-ack) and mark the field mutated so a new-run reset restores
					// the pre-stream baseline.
					bTopologyMutatedSinceBaseline = true;
					AdvanceTopologyGeneration();
					// Recompute NOW so the newly-streamed slot's connectivity labels are ready immediately, not on the next
					// 0.2s tick — otherwise the combat gate fails closed for every pawn in the meantime (Codex R16).
					RecomputeAllFields();
				}
				return bBaked;
			}
			return true;
		}
	}
	UE_LOG(LogFPSR, Warning, TEXT("[FlowField] BakeDiscoveredMap: no bounds volume with MapId '%s' is loaded."), *MapId.ToString());
	return false;
}

void UFPSRFlowFieldSubsystem::NotifyDoorBroken(const AActor* Door)
{
	// Multimap (a real bUnifiedExtent grid with seams) only / off-authority -> no-op. A single-map degenerate grid has no
	// seams, so a plain room-gate door there opens no cross-slot edge — gate on bUnifiedMultiSlot to keep it a strict no-op
	// (P-G: UnifiedComputer is now always set, so the null-check no longer distinguishes single-map).
	if (!HasServerAuthority() || !Door || !bUnifiedMultiSlot || !UnifiedComputer)
	{
		return;
	}

	// Door AABB from ALL components (not colliding-only): HandleBroken calls this BEFORE ApplyBrokenState, but be robust to
	// the leaf's collision already being off.
	FVector BoundsOrigin, BoundsExtent;
	Door->GetActorBounds(/*bOnlyCollidingComponents*/ false, BoundsOrigin, BoundsExtent);
	const FBox DoorAABB(BoundsOrigin - BoundsExtent, BoundsOrigin + BoundsExtent);

	TArray<TPair<int32, int32>> Pairs;
	UnifiedComputer->MapDoorSeamCellPairs(DoorAABB, Pairs);
	if (Pairs.Num() == 0)
	{
		UE_LOG(LogFPSR, Warning,
			TEXT("[FlowField] NotifyDoorBroken: door '%s' mapped to 0 seam cell-pairs (not on a seam / off-grid / degenerate bounds); grid unchanged."),
			*Door->GetName());
		return;
	}

	int32 OpenedEdges = 0;
	for (const TPair<int32, int32>& Pair : Pairs)
	{
		OpenedEdges += UnifiedComputer->StampDoorEdgesOpen(Pair.Key, Pair.Value);
	}

	// NO voxel clear here, deliberately (merge-gate P3): this door path is gated on bUnifiedMultiSlot (multimap
	// mode), and AdoptedVoxels only exists in ARENA mode — the two are mutually exclusive branches of
	// OnWorldBeginPlay, so a "voxel-adopted world with doors" cannot exist. If multimap content ever returns AND
	// grows a voxel field, doors will also need stamping INTO that field first (their leaf mesh is
	// ECC_FPSRDestructible, invisible to the ECC_WorldStatic bake probe — same situation as the arena
	// destructibles, solved for them in AdoptArenaFieldInternal's stamp pass).

	// U (P-F): the seam topology changed — bump the generation (mirrored to the replicated GameState in Stage 2 so late
	// joiners re-ack) and mark the field mutated so a new-run ResetDoorTopologyToBaseline restores the closed seam. Done
	// BEFORE the recompute so RecomputeAllFields stamps LastRecomputedGeneration to the NEW generation (or, if a freeze is
	// up, RecomputeAllFields no-ops and the pre-unfreeze handler catches up on the unpause edge).
	bTopologyMutatedSinceBaseline = true;
	AdvanceTopologyGeneration();
	// Recompute NOW (not on the next 0.2s tick) so the opened seam's connectivity is live immediately for both swarm flow
	// and the origin-aware combat gate — same immediacy contract as the streamed-slot bake (Codex R16).
	RecomputeAllFields();
	UE_LOG(LogFPSR, Log, TEXT("[FlowField] NotifyDoorBroken: door '%s' opened %d seam edge(s) across %d cell-pair(s); field recomputed (topology generation now %d)."),
		*Door->GetName(), OpenedEdges, Pairs.Num(), TopologyGeneration);
}

void UFPSRFlowFieldSubsystem::NotifyArenaCellsOpened(TConstArrayView<int32> Cells)
{
	// The arena is always a single grid (never a multi-slot seam), so — unlike NotifyDoorBroken — there is no
	// bUnifiedMultiSlot gate here: only authority + "a grid actually exists yet" matter.
	if (!HasServerAuthority() || !UnifiedComputer)
	{
		return;
	}

	for (const int32 Cell : Cells)
	{
		UnifiedComputer->StampCellBlocked(Cell, /*Rank=*/0, /*bBlocked=*/false);
	}
	bTopologyMutatedSinceBaseline = true;

	// Coalesce same-frame breaks into ONE recompute: several destructibles can go down in one frame (an
	// explosion), and ADR 0010 D7 is explicit that what must be bounded is simultaneous-break FREQUENCY, not prop
	// count — "여러 개가 한 프레임에 터지면 BFS를 1회로 합친다". Unlike NotifyDoorBroken (a single door, recomputed
	// immediately), this defers to next tick so a burst of N breaks this frame costs one generation bump + one BFS,
	// not N of each.
	if (!bArenaOpenRecomputePending)
	{
		bArenaOpenRecomputePending = true;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				bArenaOpenRecomputePending = false;
				AdvanceTopologyGeneration();
				RecomputeAllFields();
				UE_LOG(LogFPSR, Log, TEXT("[FlowField] NotifyArenaCellsOpened: coalesced recompute done (topology generation now %d)."),
					TopologyGeneration);
			}));
		}
	}
}

void UFPSRFlowFieldSubsystem::NotifyArenaVolumeOpened(const FBox& WorldBounds)
{
	// S2 (ADR 0009 P1): voxel-only stamp, deliberately separate from NotifyArenaCellsOpened above — a
	// destructible's 2D footprint cells and its full 3D world AABB are different shapes (props have height), so
	// this takes the box directly instead of re-deriving one from the cell list. No generation bump / recompute of
	// its own: the sibling NotifyArenaCellsOpened call at the same call site (AFPSRArenaDestructible::
	// HandleBrokenAuthority) already advances TopologyGeneration for this SAME break event, and a future S3 window
	// is expected to key its re-sample off that one counter, not a second voxel-private one.
	if (!HasServerAuthority() || !AdoptedVoxels.IsBakedVoxels())
	{
		return;
	}
	FFPSRArenaVoxelData::ClearOccupiedAABB(AdoptedVoxels, WorldBounds);
}

bool UFPSRFlowFieldSubsystem::IsLocationInMap(const FGameplayTag& MapId, const FVector& WorldLocation) const
{
	// P-G: resolve against the lightweight SlotBounds table (the per-map flow registry is gone). An EMPTY table means a
	// single-map degenerate grid (one map, no slots) -> every location is "in map", so the enemy MapId never re-resolves
	// (matches the pre-P-G "Default grid contains everything" behavior). A MapId ABSENT from a NON-empty table -> false, so a
	// seam-gap enemy (unset MapId between slots) keeps re-resolving until it re-enters a slot (Plan pressure-test correction 2).
	if (SlotBounds.Num() == 0)
	{
		return true;
	}
	const FBox* B = SlotBounds.Find(MapId);
	if (!B || !B->IsValid)
	{
		return false;
	}
	constexpr float Margin = 200.0f; // hysteresis: stay in-map a cell past the edge so a boundary enemy doesn't flip-flop
	return WorldLocation.X >= B->Min.X - Margin && WorldLocation.X < B->Max.X + Margin &&
		WorldLocation.Y >= B->Min.Y - Margin && WorldLocation.Y < B->Max.Y + Margin;
}

bool UFPSRFlowFieldSubsystem::AreLocationsConnected(const FVector& A, const FVector& B) const
{
	// P-G: the front-sense connectivity is a MULTIMAP notion (cross-slot). A single-map degenerate grid -> not "connected" in
	// the front sense (callers keep same-map behavior, no regression), so gate on bUnifiedMultiSlot (UnifiedComputer is now
	// always set).
	return (bUnifiedMultiSlot && UnifiedComputer) ? UnifiedComputer->AreWorldLocationsConnected(A, B) : false;
}

int32 UFPSRFlowFieldSubsystem::GetFrontDistanceCells(const FVector& Loc, EFPSRFieldQuery& OutStatus) const
{
	if (!bUnifiedMultiSlot || !UnifiedComputer) // P-G: front distance is multimap only (single-map degenerate grid -> NoGrid)
	{
		OutStatus = EFPSRFieldQuery::NoGrid;
		return MAX_int32;
	}
	return UnifiedComputer->GetPathDistanceCells(Loc, OutStatus);
}

FGameplayTag UFPSRFlowFieldSubsystem::FindMapIdForLocation(const FVector& WorldLocation) const
{
	// P-G: the slot whose world AABB (XY) contains the location, from the lightweight SlotBounds table (the per-map flow
	// registry is gone). Slots are spatially separated so at most one contains it. Empty table (single-map degenerate grid)
	// or the inter-slot seam gap -> unset (the Default map), exactly as the per-map registry resolved before P-G.
	for (const TPair<FGameplayTag, FBox>& Pair : SlotBounds)
	{
		const FBox& B = Pair.Value;
		if (B.IsValid && WorldLocation.X >= B.Min.X && WorldLocation.X < B.Max.X &&
			WorldLocation.Y >= B.Min.Y && WorldLocation.Y < B.Max.Y)
		{
			return Pair.Key;
		}
	}
	return FGameplayTag();
}

bool UFPSRFlowFieldSubsystem::FindNearestOpenLocation(const FVector& InWorld, int32 MaxRadiusCells, FVector& OutWorld) const
{
	// UnifiedComputer is the LIVE adopted grid (arena mode: rebuilt whole by AdoptArenaField on every transition,
	// so this always reflects the destination arena's post-regenerate, post-destructible-reset state). Null off
	// authority (clients never build it) or before the first bake/adopt — false, same as every other query here.
	return UnifiedComputer && UnifiedComputer->FindNearestOpenLocation(InWorld, MaxRadiusCells, OutWorld);
}

void UFPSRFlowFieldSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RecomputeTimerHandle);

		// U (P-F): unbind the freeze/run-state handler (symmetric with TryBindRunStateHandler).
		if (bRunStateHandlerBound)
		{
			if (AFPSRGameState* GS = World->GetGameState<AFPSRGameState>())
			{
				GS->OnRunStateChanged.RemoveDynamic(this, &UFPSRFlowFieldSubsystem::HandleRunStateChanged);
			}
			bRunStateHandlerBound = false;
		}
	}
	SlotBounds.Reset();
	MaxSlotFootprintDiagonalCm = 0.0f;
	UnifiedComputer = nullptr;
	bUnifiedMultiSlot = false;
	Super::Deinitialize();
}

void UFPSRFlowFieldSubsystem::RecomputeAllFields()
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

	// U (P-F): lazily bind the freeze/run-state handler if the GameState wasn't up at world begin (idempotent).
	TryBindRunStateHandler();

	// Skip the recompute during the global freeze (§2-2): enemy movement is gated off, so nothing samples the field.
	// LastRecomputedGeneration is deliberately NOT stamped here — so if a door breaks mid-freeze, the field stays a
	// generation behind and the pre-unfreeze handler (HandleRunStateChanged) recomputes on the unpause edge.
	if (const AFPSRGameState* GS = World->GetGameState<AFPSRGameState>())
	{
		if (GS->IsRunPaused())
		{
			return;
		}
	}

	if (UnifiedComputer)
	{
		// P-G: the single continuous field (multimap unified grid OR single-map degenerate grid), seeded by every player
		// physically inside its extent (also rebuilds connectivity for the origin-aware combat gate). This is the door-open-
		// topology recompute the near-cap bench measured (~5ms @39k). No per-computer timers (Codex R2).
		UnifiedComputer->RecomputeFromWorld(World);
	}

	// U (P-F): the field now reflects the current topology — record which generation it was computed for so the freeze
	// pre-unfreeze handler knows whether a mid-freeze door break left it stale. Only reached on a non-frozen recompute.
	LastRecomputedGeneration = TopologyGeneration;

#if !UE_BUILD_SHIPPING
	if (CVarFlowFieldDebug.GetValueOnAnyThread() > 0)
	{
		TArray<FVector> PlayerLocs;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (const APlayerController* PC = It->Get())
			{
				if (const APawn* Pawn = PC->GetPawn())
				{
					PlayerLocs.Add(Pawn->GetActorLocation());
				}
			}
		}
		if (UnifiedComputer)
		{
			UnifiedComputer->DebugDraw(World, PlayerLocs, GFlowUpdateInterval * 1.2f);
		}
	}
#endif
}

void UFPSRFlowFieldSubsystem::TryBindRunStateHandler()
{
	if (bRunStateHandlerBound || !HasServerAuthority())
	{
		return;
	}
	UWorld* World = GetWorld();
	AFPSRGameState* GS = World ? World->GetGameState<AFPSRGameState>() : nullptr;
	if (!GS)
	{
		return; // GameState not up yet — RecomputeAllFields retries lazily.
	}
	GS->OnRunStateChanged.AddDynamic(this, &UFPSRFlowFieldSubsystem::HandleRunStateChanged);
	// Seed the pause state at bind time so a bind that happens while ALREADY paused doesn't miss the first unpause edge.
	bWasPaused = GS->IsRunPaused();
	bRunStateHandlerBound = true;
}

void UFPSRFlowFieldSubsystem::HandleRunStateChanged()
{
	if (!HasServerAuthority())
	{
		return;
	}
	const UWorld* World = GetWorld();
	const AFPSRGameState* GS = World ? World->GetGameState<AFPSRGameState>() : nullptr;
	if (!GS)
	{
		return;
	}
	// OnRunStateChanged fires for many reasons (run clock, mission progress, freeze). Only the unpause edge with a
	// topology change while frozen warrants an immediate recompute — the pure predicate isolates that (headless-tested).
	const bool bNowPaused = GS->IsRunPaused();
	if (ShouldRecomputeOnUnfreeze(bWasPaused, bNowPaused, TopologyGeneration, LastRecomputedGeneration))
	{
		RecomputeAllFields();
	}
	bWasPaused = bNowPaused;
}

bool UFPSRFlowFieldSubsystem::ShouldRecomputeOnUnfreeze(bool bWasPaused, bool bNowPaused, int32 TopologyGen, int32 LastRecomputedGen)
{
	// Unpause edge (was paused, now not) AND the field is a generation behind (a door broke while frozen). Any other
	// transition — still paused, just-paused, or no topology change — is a no-op.
	return bWasPaused && !bNowPaused && TopologyGen != LastRecomputedGen;
}

void UFPSRFlowFieldSubsystem::AdvanceTopologyGeneration()
{
	++TopologyGeneration;
	// Mirror to the replicated GameState so remote clients OnRep_TopologyGeneration and re-ack the new generation (P-F
	// Stage 2). Server-only; SetTopologyGeneration self-guards on authority + change.
	if (UWorld* World = GetWorld())
	{
		if (AFPSRGameState* GS = World->GetGameState<AFPSRGameState>())
		{
			GS->SetTopologyGeneration(TopologyGeneration);
		}
	}
}

void UFPSRFlowFieldSubsystem::ResetDoorTopologyToBaseline()
{
	if (!HasServerAuthority() || !UnifiedComputer || !bHasBaseline)
	{
		return;
	}
	// First (unmutated) run: StartRun runs even then, so the dirty-flag guard makes this a no-op and the generation stays
	// 0 — a fresh client's gen-0 ack is instantly satisfied. Only a same-world re-run after a door opened restores.
	if (!bTopologyMutatedSinceBaseline)
	{
		return;
	}
	// Atomically restore the closed-seam baseline (a plain re-adopt of the baked surface graph — all opened doors close at
	// once). BuildFromSurfaceData invalidates the field; the recompute below rebuilds flow + connectivity.
	UnifiedComputer->BuildFromSurfaceData(BakedBaseline);
	// S2 (ADR 0009 P1): restore the voxel field's adopt-time snapshot too, same reasoning as BakedBaseline — any
	// door/destructible voxel clears since adoption (NotifyDoorBroken / NotifyArenaVolumeOpened) must not survive
	// a same-world re-run any more than their 2D counterparts do.
	AdoptedVoxels = VoxelBaseline;
	// The baseline restore just changed the voxel occupancy under every published window (broken props close
	// again) — invalidate them for the same reason AdoptArenaFieldInternal does (merge-gate P1's serial gate).
	if (UWorld* InvalidateWorld = GetWorld())
	{
		if (UFPSRHoverWindowSubsystem* HoverWindows = InvalidateWorld->GetSubsystem<UFPSRHoverWindowSubsystem>())
		{
			HoverWindows->InvalidateAllWindows();
		}
	}
	bTopologyMutatedSinceBaseline = false;
	AdvanceTopologyGeneration();
	RecomputeAllFields();
	UE_LOG(LogFPSR, Log, TEXT("[FlowField] U P-F: door topology reset to baked baseline (generation now %d)."), TopologyGeneration);
}

FVector UFPSRFlowFieldSubsystem::SampleFlowDirection(const FGameplayTag& MapId, const FVector& WorldLocation) const
{
	// P-G: the enemy's MapId is irrelevant now (one grid). Kept as a distinct overload so the movement-pass call site (which
	// passes the enemy's MapId) is unchanged.
	return UnifiedComputer ? UnifiedComputer->Sample(WorldLocation) : FVector::ZeroVector;
}

bool UFPSRFlowFieldSubsystem::SampleHoverFloorZ(const FVector& Loc, const FVector2D& FlowDirXY, float AnchorFootZ, float MaxSurfaceDeltaCm, float& OutFloorZ, FVector* OutFloorNormal) const
{
	// Same P-G routing as SampleFlowDirection: ONE continuous field, no per-map MapId. False off-authority / pre-build.
	return UnifiedComputer ? UnifiedComputer->SampleHoverFloorZ(Loc, FlowDirXY, AnchorFootZ, MaxSurfaceDeltaCm, OutFloorZ, OutFloorNormal) : false;
}

bool UFPSRFlowFieldSubsystem::QueryFlow(const FFPSRFlowQuery& Query, FFPSRFlowResult& Out) const
{
	// ADR 0009 결정 5 — the single movement-consumer seam. S1 had exactly one source (the 2D surface field via the
	// private routing wrappers below); S3 adds the 3D window here as a local edit to THIS function, not a call-site
	// change — every existing QueryFlow caller (front-chase, exit-path, hover-floor-only) is unaffected unless it
	// also started passing TargetPlayerPawn.
	bool bAnyValid = false;
	bool bWindowAnswered = false;

	if (Query.bWantDirection)
	{
		// S3: try the player-centred 3D window FIRST when the caller named its target player. A live, in-bounds
		// window answer carries a REAL 3D Direction (Z included) and may also set SeekZ — something the 2D surface
		// path never produces. Falls back to the unconditional 2D sample on ANY miss (no TargetPlayerPawn, no
		// window subsystem, no window adopted for this world, this pawn owns no slot, WorldPos outside the window/
		// its edge margin, or the cell itself unreached) — the 2D field is always ready, so there is never a
		// "neither answered" gap (ADR §실패 흐름).
		if (Query.TargetPlayerPawn && Query.bHoverCapable)
		{
			if (!CachedHoverWindowSubsystem.IsValid())
			{
				if (const UWorld* World = GetWorld())
				{
					CachedHoverWindowSubsystem = World->GetSubsystem<UFPSRHoverWindowSubsystem>();
				}
			}
			if (const UFPSRHoverWindowSubsystem* HoverWindow = CachedHoverWindowSubsystem.Get())
			{
				bWindowAnswered = HoverWindow->QueryWindow(Query.TargetPlayerPawn, Query.WorldPos, Out.Direction, Out.SeekZ, Out.bSeekValid);
			}
		}

		if (bWindowAnswered)
		{
			Out.bDirectionValid = true;
		}
		else
		{
			// Identical to the former public SampleFlowDirection(const FVector&) single-arg overload: an unset
			// MapId routes to the Default field via the tag-overload's own MapId-is-irrelevant routing (P-G, one
			// grid). Never sets SeekZ/bSeekValid — S1 parity (only the window supplies a vertical seek target).
			Out.Direction = SampleFlowDirection(FGameplayTag(), Query.WorldPos);
			Out.bDirectionValid = !Out.Direction.IsNearlyZero();
		}
		bAnyValid |= Out.bDirectionValid;
	}

	if (Query.bWantHoverFloor)
	{
		// Unchanged from S1 (ADR 0009 §B — bWantHoverFloor is NOT routed through the window; terrain-follow stays
		// 2D CellFloorZ, the verified path).
		Out.bFloorValid = SampleHoverFloorZ(Query.WorldPos, Query.LookAheadDirXY, Query.HoverAnchorFootZ, Query.MaxSurfaceDeltaCm, Out.FloorZ, &Out.FloorNormal);
		bAnyValid |= Out.bFloorValid;
	}

	if (bAnyValid)
	{
		Out.Source = bWindowAnswered ? EFPSRFlowSource::Window3D : EFPSRFlowSource::Surface2D;
	}

	return bAnyValid;
}
