// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSRFlowFieldSubsystem.h"
#include "Enemy/FPSRFlowFieldComputer.h"
#include "Enemy/FPSRFlowFieldBoundsVolume.h"
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

	// Bake every bounds volume present in the persistent world at begin play. Untagged volume (or none) -> Default field
	// (PlayerStart floor anchor, S1a parity); a MapId'd volume -> its own per-map computer (own-box floor anchor). Streamed
	// sublevels that arrive later are baked by the MapStreamSubsystem on collision-ready (S3).
	bool bAnyVolume = false;
	const AFPSRFlowFieldBoundsVolume* UnifiedVolume = nullptr;
	for (TActorIterator<AFPSRFlowFieldBoundsVolume> It(&InWorld); It; ++It)
	{
		const AFPSRFlowFieldBoundsVolume* Volume = *It;
		if (!Volume)
		{
			continue;
		}
		if (Volume->IsUnifiedExtent())
		{
			UnifiedVolume = Volume; // U extent — built below (not a per-map slot)
			continue;
		}
		bAnyVolume = true;
		const FGameplayTag Map = Volume->GetMapId();
		const float FloorZ = Map.IsValid() ? DetectFloorZForVolume(InWorld, *Volume) : DetectFloorZ(InWorld);
		BakeMap(Map, Volume, FloorZ);
	}
	if (!bAnyVolume && !UnifiedVolume)
	{
		BakeMap(FGameplayTag(), nullptr, DetectFloorZ(InWorld)); // origin-centered fallback Default grid (existing maps)
	}

	// U (2026-07-07): if a bUnifiedExtent volume is present, build the single continuous grid over it and bake each loaded
	// slot into it. The per-map registry above still exists (allocator / IsMapFieldReady coexistence) until P-G removes it.
	if (UnifiedVolume)
	{
		BuildUnifiedField(InWorld, *UnifiedVolume);
	}

	InWorld.GetTimerManager().SetTimer(
		RecomputeTimerHandle, this, &UFPSRFlowFieldSubsystem::RecomputeAllFields,
		GFlowUpdateInterval, true);

	// Recompute ONCE immediately (not only on the first 0.2s tick) so the connectivity labels are ready from t=0. Without
	// this, the unified combat gate (AreWorldLocationsConnected) fails closed for every pawn until the first scheduled
	// recompute, blocking all player damage during the world-begin window (Codex R16). Source-less is fine — it still
	// rebuilds the labels; player flow sources are added by the timer once pawns are possessed.
	RecomputeAllFields();
}

UFPSRFlowFieldComputer* UFPSRFlowFieldSubsystem::BakeMap(const FGameplayTag& MapId, const AFPSRFlowFieldBoundsVolume* BoundsVolume, float FloorZ)
{
	if (!HasServerAuthority())
	{
		return nullptr;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	TObjectPtr<UFPSRFlowFieldComputer>& Slot = Computers.FindOrAdd(MapId);
	if (!Slot)
	{
		Slot = NewObject<UFPSRFlowFieldComputer>(this);
	}
	Slot->BuildFromWorldTrace(World, BoundsVolume, FloorZ); // once per map; re-bakeable if a map ever re-streams
	return Slot;
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
	// so an oversized bUnifiedExtent must fail here (leaving the per-map registry as the flow source) rather than allocate an
	// over-budget grid with runaway recompute / memory. This is the P-0 gate applied to the actual authored extent.
	if (DimX > UFPSRFlowFieldComputer::GetMaxGridDimPerAxis() || DimY > UFPSRFlowFieldComputer::GetMaxGridDimPerAxis() ||
		static_cast<int64>(DimX) * DimY > UFPSRFlowFieldComputer::GetMaxTotalCells())
	{
		UE_LOG(LogFPSR, Error,
			TEXT("[FlowField] U: unified extent %dx%d cells exceeds the budget (axis<=%d, total<=%d); NOT building the unified grid (per-map registry remains the flow source). Shrink the extent or raise the volume's CellSizeOverride (P-0 gate)."),
			DimX, DimY, UFPSRFlowFieldComputer::GetMaxGridDimPerAxis(), UFPSRFlowFieldComputer::GetMaxTotalCells());
		return;
	}

	UnifiedComputer = NewObject<UFPSRFlowFieldComputer>(this);
	UnifiedComputer->BuildEmptyGrid(DimX, DimY, Origin, CellSize, Step);
	UE_LOG(LogFPSR, Log, TEXT("[FlowField] U unified grid %dx%d cell=%.0f origin=%s built from bUnifiedExtent volume."),
		DimX, DimY, CellSize, *Origin.ToString());

	// Bake every currently-loaded MapId'd slot volume into the unified grid (streamed slots bake in later via BakeDiscoveredMap).
	int32 Baked = 0;
	for (TActorIterator<AFPSRFlowFieldBoundsVolume> It(&InWorld); It; ++It)
	{
		const AFPSRFlowFieldBoundsVolume* Slot = *It;
		if (Slot && !Slot->IsUnifiedExtent() && BakeSlotIntoUnified(InWorld, *Slot))
		{
			++Baked;
		}
	}
	UE_LOG(LogFPSR, Log, TEXT("[FlowField] U: %d slot(s) baked into the unified grid at world begin."), Baked);
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
			BakeMap(MapId, Volume, DetectFloorZForVolume(*World, *Volume));
			if (UnifiedComputer)
			{
				// U: swarm flow samples ONLY the unified field, so readiness = the unified slot bake succeeded. If it fails
				// (misaligned / no floor / step mismatch) report NOT ready, so the stream subsystem keeps the boundary
				// blockers up rather than letting players into a slot the swarm can't route through (Codex R10).
				const bool bBaked = BakeSlotIntoUnified(*World, *Volume);
				if (bBaked)
				{
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

bool UFPSRFlowFieldSubsystem::IsMapFieldReady(const FGameplayTag& MapId) const
{
	const TObjectPtr<UFPSRFlowFieldComputer>* Slot = Computers.Find(MapId);
	return Slot && *Slot && (*Slot)->IsFieldReady();
}

bool UFPSRFlowFieldSubsystem::IsLocationInMap(const FGameplayTag& MapId, const FVector& WorldLocation) const
{
	const TObjectPtr<UFPSRFlowFieldComputer>* Slot = Computers.Find(MapId);
	if (!Slot || !*Slot)
	{
		return false;
	}
	const FBox B = (*Slot)->GetGridBounds();
	if (!B.IsValid)
	{
		return false;
	}
	constexpr float Margin = 200.0f; // hysteresis: stay in-map a cell past the edge so a boundary enemy doesn't flip-flop
	return WorldLocation.X >= B.Min.X - Margin && WorldLocation.X < B.Max.X + Margin &&
		WorldLocation.Y >= B.Min.Y - Margin && WorldLocation.Y < B.Max.Y + Margin;
}

FGameplayTag UFPSRFlowFieldSubsystem::FindMapIdForLocation(const FVector& WorldLocation) const
{
	for (const TPair<FGameplayTag, TObjectPtr<UFPSRFlowFieldComputer>>& Pair : Computers)
	{
		if (!Pair.Value)
		{
			continue;
		}
		const FBox B = Pair.Value->GetGridBounds();
		if (B.IsValid && WorldLocation.X >= B.Min.X && WorldLocation.X < B.Max.X &&
			WorldLocation.Y >= B.Min.Y && WorldLocation.Y < B.Max.Y)
		{
			return Pair.Key;
		}
	}
	return FGameplayTag();
}

bool UFPSRFlowFieldSubsystem::EvictMap(const FGameplayTag& MapId)
{
	// Tier 0 keeps maps loaded (LOD-cull only); this is an API stub for the S3 stream-out contract. The caller must have
	// drained the map's enemies first (else a cached Computer* dangles — see the S3/allocator "no evict while alive" rule).
	return Computers.Remove(MapId) > 0;
}

void UFPSRFlowFieldSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RecomputeTimerHandle);
	}
	Computers.Reset();
	UnifiedComputer = nullptr;
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

	// Skip the recompute during the global freeze (§2-2): enemy movement is gated off, so nothing samples the field.
	if (const AFPSRGameState* GS = World->GetGameState<AFPSRGameState>())
	{
		if (GS->IsRunPaused())
		{
			return;
		}
	}

	// Single scheduler over the registry (Codex R2: no per-computer timers). Each computer seeds only from players
	// physically inside ITS grid (WorldToCellIndex rejects out-of-grid pawns), so an empty map's recompute early-outs.
	for (const TPair<FGameplayTag, TObjectPtr<UFPSRFlowFieldComputer>>& Pair : Computers)
	{
		if (Pair.Value)
		{
			Pair.Value->RecomputeFromWorld(World);
		}
	}
	if (UnifiedComputer)
	{
		// U: single continuous field, seeded by every player physically inside its extent (also rebuilds connectivity for
		// the origin-aware combat gate). This is the door-open-topology recompute the near-cap bench measured (~5ms @39k).
		UnifiedComputer->RecomputeFromWorld(World);
	}

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
		for (const TPair<FGameplayTag, TObjectPtr<UFPSRFlowFieldComputer>>& Pair : Computers)
		{
			if (Pair.Value)
			{
				Pair.Value->DebugDraw(World, PlayerLocs, GFlowUpdateInterval * 1.2f);
			}
		}
	}
#endif
}

FVector UFPSRFlowFieldSubsystem::SampleFlowDirection(const FVector& WorldLocation) const
{
	if (UnifiedComputer)
	{
		return UnifiedComputer->Sample(WorldLocation); // U: single continuous field covers all slots
	}
	// By-location routing (S1b bridge): pick the computer whose grid AABB (XY) contains WorldLocation. Maps are spatially
	// separated so at most one contains it. S2a replaces this with a MapId-keyed sample (enemy carries its map).
	for (const TPair<FGameplayTag, TObjectPtr<UFPSRFlowFieldComputer>>& Pair : Computers)
	{
		if (!Pair.Value)
		{
			continue;
		}
		const FBox B = Pair.Value->GetGridBounds();
		if (B.IsValid && WorldLocation.X >= B.Min.X && WorldLocation.X < B.Max.X &&
			WorldLocation.Y >= B.Min.Y && WorldLocation.Y < B.Max.Y)
		{
			return Pair.Value->Sample(WorldLocation);
		}
	}
	return FVector::ZeroVector;
}

FVector UFPSRFlowFieldSubsystem::SampleFlowDirection(const FGameplayTag& MapId, const FVector& WorldLocation) const
{
	if (UnifiedComputer)
	{
		return UnifiedComputer->Sample(WorldLocation); // U: single field — the enemy's MapId is irrelevant (one grid)
	}
	if (const TObjectPtr<UFPSRFlowFieldComputer>* Slot = Computers.Find(MapId))
	{
		if (*Slot)
		{
			// If the location is inside the passed map's grid, sample it (even a zero at a source is correct).
			const FBox B = (*Slot)->GetGridBounds();
			if (B.IsValid && WorldLocation.X >= B.Min.X && WorldLocation.X < B.Max.X &&
				WorldLocation.Y >= B.Min.Y && WorldLocation.Y < B.Max.Y)
			{
				return (*Slot)->Sample(WorldLocation);
			}
		}
	}
	// Mid-transition (enemy physically crossed into another map's region while still carrying its old MapId): retry
	// against whichever map's grid actually contains the location so flow stays continuous at the door (Codex R2).
	return SampleFlowDirection(WorldLocation);
}
