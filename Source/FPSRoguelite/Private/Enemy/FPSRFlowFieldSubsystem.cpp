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
	for (TActorIterator<AFPSRFlowFieldBoundsVolume> It(&InWorld); It; ++It)
	{
		const AFPSRFlowFieldBoundsVolume* Volume = *It;
		if (!Volume)
		{
			continue;
		}
		bAnyVolume = true;
		const FGameplayTag Map = Volume->GetMapId();
		const float FloorZ = Map.IsValid() ? DetectFloorZForVolume(InWorld, *Volume) : DetectFloorZ(InWorld);
		BakeMap(Map, Volume, FloorZ);
	}
	if (!bAnyVolume)
	{
		BakeMap(FGameplayTag(), nullptr, DetectFloorZ(InWorld)); // origin-centered fallback Default grid (existing maps)
	}

	InWorld.GetTimerManager().SetTimer(
		RecomputeTimerHandle, this, &UFPSRFlowFieldSubsystem::RecomputeAllFields,
		GFlowUpdateInterval, true);
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
