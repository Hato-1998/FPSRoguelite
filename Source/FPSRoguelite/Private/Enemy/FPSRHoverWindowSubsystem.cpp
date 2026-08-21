// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSRHoverWindowSubsystem.h"
#include "Enemy/FPSRFlowFieldSubsystem.h"
#include "Arena/FPSRArenaBakeDataAsset.h"
#include "Settings/FPSREnemySwarmSettings.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"
#include "Hero/FPSRCharacter.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

// ============================================================================
//  FPSRHoverWindowRuntime — pure derivation math (S3 plan: namespace functions, not subsystem members, so
//  FPSRoguelite.FlowField.HoverWindowRuntime exercises them with no UWorld at all).
// ============================================================================

namespace FPSRHoverWindowRuntime
{
	FIntVector ComputeWindowMinCell(const FVector& PlayerWorldPos, const FVector& VoxelOrigin, float VoxelSize, const FFPSRHoverWindowDims& Dims)
	{
		if (VoxelSize <= 0.0f)
		{
			return FIntVector::ZeroValue;
		}
		// FFPSRArenaVoxelData::WorldToVoxel's exact floor-divide, duplicated per this function's own header comment.
		const FVector Rel = PlayerWorldPos - VoxelOrigin;
		const FIntVector PlayerCell(
			FMath::FloorToInt(Rel.X / VoxelSize),
			FMath::FloorToInt(Rel.Y / VoxelSize),
			FMath::FloorToInt(Rel.Z / VoxelSize));
		return PlayerCell - FIntVector(Dims.DimX / 2, Dims.DimY / 2, Dims.DimZ / 2);
	}

	bool ResolveSourceCell(const FFPSRHoverWindowDims& Dims, TConstArrayView<uint64> Occupancy, const FIntVector& PlayerLocalCell, int32& OutSourceCell)
	{
		if (!Dims.IsValid() || PlayerLocalCell.X < 0 || PlayerLocalCell.X >= Dims.DimX
			|| PlayerLocalCell.Y < 0 || PlayerLocalCell.Y >= Dims.DimY)
		{
			// The owner's XY isn't even inside this window. Shouldn't happen (the window is re-centred on this same
			// player every launch), but a stale call against a moved player must fail closed, not index out of range.
			return false;
		}

		// Delta=0 checks the player's own cell; each Delta>0 thereafter checks UP then DOWN at that same distance
		// (ties broken upward) — "가까운 쪽부터" over a search bounded by the window's own DimZ (never unbounded).
		for (int32 Delta = 0; Delta <= Dims.DimZ; ++Delta)
		{
			const int32 ZUp = PlayerLocalCell.Z + Delta;
			if (ZUp >= 0 && ZUp < Dims.DimZ)
			{
				const int32 Cell = Dims.CellIndex(PlayerLocalCell.X, PlayerLocalCell.Y, ZUp);
				if (!FPSRHoverWindow::IsOccupied(Occupancy, Cell))
				{
					OutSourceCell = Cell;
					return true;
				}
			}
			if (Delta > 0)
			{
				const int32 ZDown = PlayerLocalCell.Z - Delta;
				if (ZDown >= 0 && ZDown < Dims.DimZ)
				{
					const int32 Cell = Dims.CellIndex(PlayerLocalCell.X, PlayerLocalCell.Y, ZDown);
					if (!FPSRHoverWindow::IsOccupied(Occupancy, Cell))
					{
						OutSourceCell = Cell;
						return true;
					}
				}
			}
		}
		return false; // whole column occupied — caller launches with zero sources (Propagate reports SeededCount 0)
	}

	bool ResolveQuery(const FFPSRHoverWindowDims& Dims, const FVector& WindowWorldOrigin, float VoxelSize,
		const FFPSRHoverWindowField& Field, int32 EdgeMarginCells, const FVector& WorldPos,
		FVector& OutDirection, float& OutSeekZ, bool& OutSeekValid)
	{
		OutSeekValid = false;
		if (!Dims.IsValid() || VoxelSize <= 0.0f || Field.Dist.Num() != Dims.NumCells() || Field.StepDir.Num() != Dims.NumCells())
		{
			return false;
		}

		const FVector Rel = WorldPos - WindowWorldOrigin;
		const FIntVector Cell(
			FMath::FloorToInt(Rel.X / VoxelSize),
			FMath::FloorToInt(Rel.Y / VoxelSize),
			FMath::FloorToInt(Rel.Z / VoxelSize));

		const int32 Margin = FMath::Max(0, EdgeMarginCells);
		if (Cell.X < Margin || Cell.X >= Dims.DimX - Margin
			|| Cell.Y < Margin || Cell.Y >= Dims.DimY - Margin
			|| Cell.Z < Margin || Cell.Z >= Dims.DimZ - Margin)
		{
			// Too close to (or past) the window's own edge — a fail-closed occupancy pad may sit here rather than
			// real data (CopyWindow), and the window will re-centre before the agent ever reaches this far anyway.
			return false;
		}

		const int32 CellIdx = Dims.CellIndex(Cell.X, Cell.Y, Cell.Z);
		const uint16 Dist = Field.Dist[CellIdx];
		const uint8 StepDirIdx = Field.StepDir[CellIdx];
		if (Dist == FPSRHoverWindow::UnreachedDist || StepDirIdx == 0)
		{
			return false; // unreached (walled off from the source), or the source cell itself (nowhere to step)
		}

		// StepDir[N] already names the direction an agent AT N should move (FPSRHoverWindowCore.h's Propagate doc —
		// the reverse-offset derivation), so this needs no further inversion.
		const FIntVector Offset = FPSRHoverWindow::GetNeighborOffsets6()[StepDirIdx - 1];
		OutDirection = FVector(static_cast<float>(Offset.X), static_cast<float>(Offset.Y), static_cast<float>(Offset.Z));

		if (Offset.Z != 0)
		{
			const int32 NextZ = Cell.Z + Offset.Z;
			OutSeekZ = WindowWorldOrigin.Z + (static_cast<float>(NextZ) + 0.5f) * VoxelSize;
			OutSeekValid = true;
		}
		// else: the step stays on the same Z layer — leave OutSeekValid false so the caller's 2D terrain-follow
		// keeps driving Z (S3 plan item B: "평지 -> bSeekValid=false").
		return true;
	}
}

// ============================================================================
//  UFPSRHoverWindowSubsystem
// ============================================================================

bool UFPSRHoverWindowSubsystem::ShouldCreateSubsystem(UObject* Outer) const
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

bool UFPSRHoverWindowSubsystem::HasServerAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

void UFPSRHoverWindowSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!HasServerAuthority())
	{
		return; // clients never build or sample hover windows (same authority gate as every sibling subsystem)
	}

	const UFPSREnemySwarmSettings* Settings = GetDefault<UFPSREnemySwarmSettings>();
	ArmedIntervalSec = FMath::Max(0.01f, Settings->WindowUpdateIntervalSec);
	InWorld.GetTimerManager().SetTimer(TickTimerHandle, this, &UFPSRHoverWindowSubsystem::TickWindows, ArmedIntervalSec, true);
}

void UFPSRHoverWindowSubsystem::InvalidateAllWindows()
{
	++AdoptionSerial;
	for (FFPSRHoverWindowSlot& Slot : Slots)
	{
		Slot.bFrontValid = false; // wrong-ARENA stale from this instant on — see the header comment
	}
}

void UFPSRHoverWindowSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TickTimerHandle);
	}

	// A slot's in-flight worker reads its OccupancySnapshot and writes its back Fields[] buffer by reference — both
	// live inside this subsystem (Slots), which is about to be destroyed. Wait()ing here (not just clearing the
	// timer, which only stops NEW launches) is the only thing that makes that safe; see this class's threading
	// contract in the header.
	for (FFPSRHoverWindowSlot& Slot : Slots)
	{
		if (Slot.Task.IsValid())
		{
			Slot.Task.Wait();
		}
	}
	Super::Deinitialize();
}

bool UFPSRHoverWindowSubsystem::QueryWindow(const APawn* Pawn, const FVector& WorldPos, FVector& OutDirection, float& OutSeekZ, bool& OutSeekValid) const
{
	OutSeekValid = false;
	if (!Pawn)
	{
		return false;
	}
	for (const FFPSRHoverWindowSlot& Slot : Slots)
	{
		if (Slot.OwnerPawn.Get() != Pawn)
		{
			continue;
		}
		if (!Slot.bFrontValid)
		{
			return false; // matched a slot, but it has no usable published field yet (or seeded 0 last launch)
		}
		// The FRONT buffer's OWN placement — the origin/dims/cell-size it was actually propagated against, never
		// the slot-level values of the next launch (merge-gate P1: placement is double-buffered in lockstep with
		// Fields, see FFPSRHoverWindowSlot::FPlacement). VoxelSize likewise comes from the placement, not the
		// compile-time constant (merge-gate P3: a legacy/other-generation asset must not silently skew here).
		const FFPSRHoverWindowSlot::FPlacement& Placement = Slot.Placements[Slot.FrontIndex];
		const UFPSREnemySwarmSettings* Settings = GetDefault<UFPSREnemySwarmSettings>();
		return FPSRHoverWindowRuntime::ResolveQuery(Placement.Dims, Placement.WorldOrigin, Placement.VoxelSize,
			Slot.Fields[Slot.FrontIndex], Settings->WindowEdgeMarginCells, WorldPos, OutDirection, OutSeekZ, OutSeekValid);
	}
	return false; // Pawn doesn't currently own any slot (5th+ co-op player, or not yet assigned this cycle)
}

void UFPSRHoverWindowSubsystem::TickWindows()
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

	// Live re-arm (merge-gate P3): the settings block advertises the cadence as a designer knob, so a PIE edit must
	// actually take — compare against what the timer is armed with and re-arm on change (next fire at the new rate).
	{
		const float WantedInterval = FMath::Max(0.01f, GetDefault<UFPSREnemySwarmSettings>()->WindowUpdateIntervalSec);
		if (!FMath::IsNearlyEqual(WantedInterval, ArmedIntervalSec))
		{
			ArmedIntervalSec = WantedInterval;
			World->GetTimerManager().SetTimer(TickTimerHandle, this, &UFPSRHoverWindowSubsystem::TickWindows, ArmedIntervalSec, true);
		}
	}

	UFPSRFlowFieldSubsystem* FlowField = World->GetSubsystem<UFPSRFlowFieldSubsystem>();
	if (!FlowField || !FlowField->HasAdoptedVoxels())
	{
		// Soft downgrade (S3 plan): no adopted 3D occupancy (pre-P1 bake, or no arena adopted yet) -> every slot
		// stays unpublished; QueryWindow answers "no match" and UFPSRFlowFieldSubsystem::QueryFlow falls back to 2D.
		for (FFPSRHoverWindowSlot& Slot : Slots)
		{
			Slot.bFrontValid = false;
		}
		return;
	}

	// Round-robin: exactly ONE slot considered per tick (WindowUpdateIntervalSec x NumSlots -> each slot's own turn
	// comes every ~250ms at the default cadence, matching the 2D flow field's own recompute interval).
	const int32 SlotIndex = NextSlotCursor;
	NextSlotCursor = (NextSlotCursor + 1) % NumSlots;
	FFPSRHoverWindowSlot& Slot = Slots[SlotIndex];

	// Publish BEFORE deciding whether to relaunch: a completed task's backbuffer becomes this slot's new front
	// regardless of what happens below, so already-finished work is never thrown away (this class's threading
	// contract: the swap is the ONLY moment the game thread may touch what was the back buffer).
	if (Slot.Task.IsValid())
	{
		if (!Slot.Task.IsCompleted())
		{
			return; // still in flight — this slot's own next turn (one full round-robin cycle) retries
		}
		Slot.FrontIndex = 1 - Slot.FrontIndex;
		// LaunchSerial gate (merge-gate P1): a task launched BEFORE the last voxel adoption propagated over the
		// previous arena's occupancy — its result must never publish as valid against the new arena.
		Slot.bFrontValid = Slot.SeededCount > 0 && Slot.LaunchSerial == AdoptionSerial;
		// Publish EXACTLY once per completed task: without this reset, any turn that skips the relaunch below
		// (freeze gate, no owner) re-enters here with the SAME completed task and swaps again — flip-flopping the
		// front between the latest and the previous field every cycle for as long as launches stay skipped.
		Slot.Task = UE::Tasks::FTask();
	}

	// ---- Everything below decides + prepares the NEXT launch — all game-thread work (this class's threading
	//      contract: the worker only ever sees what is captured into it below). ----

	// (1) Player enumeration (UFPSREnemySpawnSubsystem::TickEnemyMovement's precedent, trimmed to what a window
	//     needs): alive pawns only, with the SAME grounded-Z cache pattern (a jump/fall must not drag the window's
	//     Z seed around in real time).
	for (auto CacheIt = LastGroundedZByPawn.CreateIterator(); CacheIt; ++CacheIt)
	{
		if (!CacheIt->Key.IsValid())
		{
			CacheIt.RemoveCurrent();
		}
	}
	TArray<APawn*, TInlineAllocator<4>> PlayerPawns;
	TArray<float, TInlineAllocator<4>> PlayerGroundedZ;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		APawn* Pawn = PC->GetPawn();
		if (!Pawn)
		{
			continue;
		}
		const AFPSRPlayerState* PS = PC->GetPlayerState<AFPSRPlayerState>();
		if (PS && !PS->IsAlive())
		{
			continue; // DBNO/dead players don't own a window (mirrors the spawn subsystem's targeting exclusion)
		}

		float GroundedZ = Pawn->GetActorLocation().Z;
		if (AFPSRCharacter* Character = Cast<AFPSRCharacter>(Pawn))
		{
			const TWeakObjectPtr<APawn> WeakPawn(Pawn);
			if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
			{
				if (Movement->IsMovingOnGround())
				{
					LastGroundedZByPawn.Add(WeakPawn, GroundedZ);
				}
				else if (const float* Cached = LastGroundedZByPawn.Find(WeakPawn))
				{
					GroundedZ = *Cached;
				}
			}
		}
		PlayerPawns.Add(Pawn);
		PlayerGroundedZ.Add(GroundedZ);
	}

	if (!PlayerPawns.IsValidIndex(SlotIndex))
	{
		// Fewer live players than slots -> this slot has no owner this cycle. Leave it unpublished/un-owned rather
		// than serving a stale window for a player who may have left.
		Slot.OwnerPawn = nullptr;
		Slot.bFrontValid = false;
		return;
	}

	// Freeze gate (§2-2, mirrors UFPSRFlowFieldSubsystem::RecomputeAllFields): only the LAUNCH decision is skipped —
	// the publish above already ran, so a window completed just before the freeze started is still served.
	if (const AFPSRGameState* GS = World->GetGameState<AFPSRGameState>())
	{
		if (GS->IsRunPaused())
		{
			return;
		}
	}

	APawn* OwnerPawn = PlayerPawns[SlotIndex];
	if (Slot.OwnerPawn.Get() != OwnerPawn)
	{
		// Ownership changed (join/leave reshuffled the enumeration): the published front field is centred on the
		// PREVIOUS owner's position, and serving it to the new owner's chasers would steer them toward the old
		// player's window — wrong-PLAYER stale, which is outside what invariant 5 ("낡아도 안전") tolerates, unlike
		// ordinary same-player staleness. Unpublish until this launch's own field publishes on the next turn.
		Slot.bFrontValid = false;
	}
	Slot.OwnerPawn = OwnerPawn;

	// (2) Window re-centre + dims snapshot, written into the BACK buffer's OWN placement (merge-gate P1: the front
	//     buffer's placement — what QueryWindow decodes with — is untouched here; the pair swaps together at the
	//     next publish). Live-tunable — a settings edit takes effect on THIS slot's next launch.
	const int32 BackIndex = 1 - Slot.FrontIndex;
	FFPSRHoverWindowSlot::FPlacement& BackPlacement = Slot.Placements[BackIndex];
	const UFPSREnemySwarmSettings* Settings = GetDefault<UFPSREnemySwarmSettings>();
	FFPSRHoverWindowDims Dims;
	Dims.DimX = FMath::Max(1, Settings->WindowDimXY);
	Dims.DimY = FMath::Max(1, Settings->WindowDimXY);
	Dims.DimZ = FMath::Max(1, Settings->WindowDimZ);
	if (!Dims.IsValid())
	{
		// A config-file value past the UI clamps overflowed the cell product (merge-gate P3) — refuse the launch
		// (and any publish) rather than hand CopyWindow/Propagate a wrapped cell count.
		Slot.bFrontValid = false;
		return;
	}

	const FFPSRArenaVoxelData& Voxels = FlowField->GetAdoptedVoxels();
	const FVector OwnerLocation = OwnerPawn->GetActorLocation();
	const FIntVector MinCell = FPSRHoverWindowRuntime::ComputeWindowMinCell(OwnerLocation, Voxels.VoxelOrigin, Voxels.VoxelSize, Dims);
	BackPlacement.MinCell = MinCell;
	BackPlacement.Dims = Dims;
	BackPlacement.VoxelSize = Voxels.VoxelSize;
	BackPlacement.WorldOrigin = Voxels.VoxelOrigin + FVector(static_cast<float>(MinCell.X), static_cast<float>(MinCell.Y), static_cast<float>(MinCell.Z)) * Voxels.VoxelSize;

	// (3) Occupancy snapshot: row-wise fail-closed copy out of the global adopted voxel field. GAME THREAD ONLY —
	//     the worker launched below never touches Voxels/FlowField itself, only this slot's own snapshot.
	Voxels.CopyWindow(MinCell, Dims, Slot.OccupancySnapshot);

	// (4) Source cell: the owner's (XY, grounded Z) column, snapped to the nearest free voxel if occupied.
	const FVector SourceWorldPos(OwnerLocation.X, OwnerLocation.Y, PlayerGroundedZ[SlotIndex]);
	const FVector SourceRel = SourceWorldPos - BackPlacement.WorldOrigin;
	const FIntVector SourceLocalCell(
		FMath::FloorToInt(SourceRel.X / Voxels.VoxelSize),
		FMath::FloorToInt(SourceRel.Y / Voxels.VoxelSize),
		FMath::FloorToInt(SourceRel.Z / Voxels.VoxelSize));

	TArray<int32> Sources;
	int32 SourceCell = INDEX_NONE;
	if (FPSRHoverWindowRuntime::ResolveSourceCell(Dims, Slot.OccupancySnapshot, SourceLocalCell, SourceCell))
	{
		Sources.Add(SourceCell);
	}
	// else: Sources stays empty -> Propagate seeds 0 (the owner's whole column is occupied); the slot's NEXT publish
	// sets bFrontValid false, so QueryWindow correctly reports no answer until the owner moves off the wall.

	// (5) Launch onto a worker (6-neighbour, per S3 plan — the window's live per-launch propagation, distinct from
	//     the correctness/bench spike's 26-neighbour comparison). Everything captured by reference here is a member
	//     of THIS slot, which the game thread will not touch again until Task.IsCompleted() is confirmed on a later
	//     tick (this class's threading contract) — SourceCell/Sources is moved in as a task-owned copy since it was
	//     only ever a local.
	FFPSRHoverWindowField& BackField = Slot.Fields[BackIndex]; // BackIndex/BackPlacement from step (2) above
	TArray<uint64>& Snapshot = Slot.OccupancySnapshot;
	int32* SeededCountPtr = &Slot.SeededCount;

	Slot.LaunchSerial = AdoptionSerial; // this launch's occupancy generation — publish gates on it (merge-gate P1)
	Slot.Task = UE::Tasks::Launch(TEXT("FPSRHoverWindowPropagate"),
		[Dims, &Snapshot, Sources = MoveTemp(Sources), &BackField, SeededCountPtr]()
		{
			*SeededCountPtr = FPSRHoverWindow::Propagate(Dims, Snapshot, Sources, FPSRHoverWindow::GetNeighborOffsets6(), BackField);
		});
}
