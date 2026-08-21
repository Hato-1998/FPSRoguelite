// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Tasks/Task.h"
#include "Enemy/FPSRHoverWindowCore.h"
#include "FPSRHoverWindowSubsystem.generated.h"

class APawn;

/**
 * One player-centred 3D hover window (ADR 0009 P1 S3, Docs/Architecture/0009-hover-swarm-local-3d-flow-window.md).
 * A double-buffered wavefront field (FPSRHoverWindowCore.h) re-propagated on its own round-robin cadence, owned by
 * one live player pawn at a time. Plain member struct (no UPROPERTY/USTRUCT) — the same "POD arrays, no UObject
 * refs -> no GC concern" rationale UFPSRFlowFieldSubsystem::AdoptedVoxels documents; OwnerPawn is a TWeakObjectPtr
 * for the same reason UFPSREnemySpawnSubsystem::LastGroundedZByPlayer's key is (a weak ref needs no UPROPERTY to
 * stay GC-safe — it just goes stale, which IsValid()/Get() surfaces instead of dangling).
 */
struct FFPSRHoverWindowSlot
{
	/** The player pawn this slot is currently propagating toward. Null/stale = the slot has no owner this cycle
	 *  (fewer live players than slots) — TickWindows reassigns it fresh every launch decision. */
	TWeakObjectPtr<APawn> OwnerPawn;

	/** This window's min-corner, in the ADOPTED VOXEL FIELD's own cell coordinates (UFPSRFlowFieldSubsystem::
	 *  GetAdoptedVoxels()'s axis system) — the argument CopyWindow re-centres against. */
	FIntVector WindowMinCell = FIntVector::ZeroValue;

	/** This slot's window dimensions, snapshotted from UFPSREnemySwarmSettings at LAUNCH time (a live edit takes
	 *  effect on this slot's NEXT launch, not mid-flight — the in-flight task captured its own copy of Dims by
	 *  value at launch, so it keeps running against what it was actually launched with). */
	FFPSRHoverWindowDims Dims;

	/** World-space position of WindowMinCell's own min corner (WindowMinCell*VoxelSize + the adopted field's
	 *  VoxelOrigin) — QueryWindow's world<->local cell conversion anchor. */
	FVector WindowWorldOrigin = FVector::ZeroVector;

	/** GAME-THREAD-WRITTEN, WORKER-READ: this launch's row-wise occupancy snapshot (FFPSRArenaVoxelData::CopyWindow)
	 *  out of the global adopted voxel field. Written before Task is launched; the worker only reads it; the game
	 *  thread never touches it again until Task.IsCompleted() (enforced by TickWindows' publish-before-relaunch
	 *  gate) — see this class's threading contract below. */
	TArray<uint64> OccupancySnapshot;

	/** Double buffer: Fields[FrontIndex] is published (safe for QueryWindow to read on the game thread); the OTHER
	 *  index is the current/next in-flight task's private backbuffer. Reused across launches (Propagate's own
	 *  SetNumUninitialized contract) so a 250ms-cadence slot doesn't realloc every cycle. */
	FFPSRHoverWindowField Fields[2];

	/** Which of Fields[] is currently published/front. Swapped by TickWindows the instant Task.IsCompleted(). */
	int32 FrontIndex = 0;

	/** True once Fields[FrontIndex] holds a USABLE published field (>=1 source seeded) — QueryWindow's first gate.
	 *  False before this slot's first-ever completed launch, and whenever a launch seeded 0 sources (an owner
	 *  whose whole XY column is occupied — S3 plan item (4)'s "전부 점유면 SeededCount=0"). */
	bool bFrontValid = false;

	/** Sources actually seeded by the LAST completed propagation (written by the worker, read by TickWindows only
	 *  after confirming Task.IsCompleted() — never read while a task may still be running). */
	int32 SeededCount = 0;

	/** This slot's currently in-flight (or last-completed, until relaunched) propagation job. */
	UE::Tasks::FTask Task;
};

/**
 * ADR 0009 P1 S3 — the 3D hover-swarm window runtime. UWorldSubsystem (no Tick — driven by its own timer, like
 * UFPSRFlowFieldSubsystem's RecomputeTimerHandle), server-only (HasServerAuthority gate, same pattern as every
 * other flow/spawn subsystem in this module — clients never build or sample this).
 *
 * Owns a FIXED 4-slot array (ADR 0009 decision item 4 — one window per co-op player, up to 4) on a staggered
 * round-robin: one slot's turn comes up every WindowUpdateIntervalSec tick (default 62.5ms), so with 4 slots each
 * individual window re-propagates every ~250ms — the same cadence UFPSRFlowFieldSubsystem's 2D field recomputes at.
 * QueryWindow is the ONLY read seam (called from UFPSRFlowFieldSubsystem::QueryFlow's Window3D branch); nothing
 * else in the codebase should reach into Slots directly.
 *
 * THREADING CONTRACT (read this before touching anything here):
 *   - EVERY read/write of a slot's OwnerPawn, WindowMinCell, Dims, WindowWorldOrigin, OccupancySnapshot, and the
 *     BACK Fields[] buffer happens on the GAME THREAD ONLY, inside TickWindows (launch prep) or QueryWindow (which
 *     only ever reads the FRONT buffer, never the back one).
 *   - The WORKER (the UE::Tasks::Launch body) touches exactly three things, all captured by reference at launch:
 *     the slot's OccupancySnapshot (read-only), its SOURCES array (task-owned, moved in at launch), and its BACK
 *     Fields[] buffer (read-write, via FPSRHoverWindow::Propagate). It never touches a UObject, the adopted voxel
 *     field, or any other slot.
 *   - The handoff is IsCompleted()-gated, not lock/atomic-based: TickWindows only re-touches a slot's snapshot/back
 *     buffer once Slot.Task.IsCompleted() is true (checked at the TOP of that slot's next turn, before anything
 *     else runs for it), and only PUBLISHES (swaps FrontIndex) at that same moment — so the game thread and the
 *     worker are never both live on the same memory at once. This is the SAME reasoning
 *     UFPSRFlowFieldComputer::CommitSubregion's "single game thread + no yield -> no half-applied read" comment
 *     gives for the 2D field; see FPSRFlowFieldComputer.cpp for that precedent (that comment now also points here
 *     for the 3D case, since this class's cross-thread contract is a different mechanism — completion-gated
 *     handoff to an ACTUAL worker thread, not just "nothing yields").
 *   - Deinitialize Wait()s every slot's Task before the subsystem (and its Slots array) is destroyed — a worker
 *     still running against freed memory would be a use-after-free, not just a stale read.
 */
UCLASS()
class FPSROGUELITE_API UFPSRHoverWindowSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** UFPSRFlowFieldSubsystem::QueryFlow's Window3D read seam. True only when: Pawn currently owns a slot AND that
	 *  slot's front buffer is valid (bFrontValid) AND WorldPos falls inside the window with WindowEdgeMarginCells
	 *  of margin from every edge AND the cell there was actually reached by the flood. On a true return,
	 *  OutDirection is the 3D (Z included) step-toward-the-source unit vector; OutSeekValid/OutSeekZ additionally
	 *  report a vertical seek target when the gradient's next step changes Z layer (false = keep 2D terrain-follow).
	 *  Read-only, game-thread-only (reads only the FRONT buffer, per this class's threading contract). */
	bool QueryWindow(const APawn* Pawn, const FVector& WorldPos, FVector& OutDirection, float& OutSeekZ, bool& OutSeekValid) const;

private:
	/** The staggered round-robin driver (FTimerHandle loop at WindowUpdateIntervalSec) — considers exactly ONE
	 *  slot per call: publish its completed task if any, then (freeze/authority/occupancy permitting) prepare and
	 *  launch its next propagation. See this class's threading contract for the publish/relaunch ordering rules. */
	void TickWindows();

	bool HasServerAuthority() const;

	/** Fixed at 4 (ADR 0009 decision item 4 — one window per co-op player, up to 4). Not a designer knob: the
	 *  round-robin cadence math (WindowUpdateIntervalSec x NumSlots = per-window refresh period) is derived from
	 *  this exact count elsewhere (this class's header comment) — changing it is a P2+ decision, not a tuning pass. */
	static constexpr int32 NumSlots = 4;

	FFPSRHoverWindowSlot Slots[NumSlots];

	/** Next slot TickWindows will consider (round-robin cursor, wraps mod NumSlots). */
	int32 NextSlotCursor = 0;

	FTimerHandle TickTimerHandle;

	/** Server-only: each tracked player's Z the last time IsMovingOnGround() was true — the SAME "don't chase a
	 *  jump/fall in real time" cache UFPSREnemySpawnSubsystem::LastGroundedZByPlayer keeps, duplicated here (not
	 *  shared) because that one is keyed on AFPSRCharacter and scoped to the spawn subsystem's own movement pass;
	 *  this is this subsystem's own copy of the same idea, pruned of stale (dead pawn) entries every TickWindows
	 *  pass. Not a UPROPERTY — see FFPSRHoverWindowSlot::OwnerPawn's comment for why a weak-key map needs none. */
	TMap<TWeakObjectPtr<APawn>, float> LastGroundedZByPawn;
};

/** Pure, worldless derivation math for the hover window runtime (S3 plan: "도출 로직은 static 순수 함수로 빼서
 *  테스트... 서브시스템 멤버 아닌 namespace 함수"). Exercised directly by FPSRoguelite.FlowField.HoverWindowRuntime
 *  with no UWorld/UObject anywhere — the same worldless-core precedent FPSRHoverWindowCore.h's own namespace sets. */
namespace FPSRHoverWindowRuntime
{
	/** Window re-centring (S3 plan item ②): floor-divides PlayerWorldPos into VoxelOrigin/VoxelSize's cell grid
	 *  (the EXACT math FFPSRArenaVoxelData::WorldToVoxel uses — duplicated here, not called, so this function stays
	 *  independent of that UPROPERTY-bearing struct and testable with no asset/UObject at all; keep the two in sync
	 *  if either's rounding rule ever changes), then returns that cell minus half of Dims on each axis — the window
	 *  MIN corner that puts the player at its centre. */
	FPSROGUELITE_API FIntVector ComputeWindowMinCell(const FVector& PlayerWorldPos, const FVector& VoxelOrigin, float VoxelSize, const FFPSRHoverWindowDims& Dims);

	/** Source-cell resolution (S3 plan item ④): PlayerLocalCell is the owner's (XY, grounded-Z) column in the
	 *  WINDOW's own local cell coordinates (already clamped to be inside the window by construction — the window is
	 *  re-centred on this same player every launch). If that cell is occupied, searches the SAME XY column
	 *  outward (nearest Z first, ties broken upward) for the closest free cell, bounded by the window's own DimZ —
	 *  never an unbounded search. Returns false (OutSourceCell untouched) only when the ENTIRE column is occupied;
	 *  the caller then launches with zero sources, which Propagate reports as SeededCount 0. */
	FPSROGUELITE_API bool ResolveSourceCell(const FFPSRHoverWindowDims& Dims, TConstArrayView<uint64> Occupancy, const FIntVector& PlayerLocalCell, int32& OutSourceCell);

	/** The QueryWindow answer, given one slot's placement + published field (S3 plan item B/§도출 로직 ③). Converts
	 *  WorldPos into the window's local cell (WindowWorldOrigin/VoxelSize), rejects it if outside [Margin,
	 *  Dim-Margin) on ANY axis or if the cell was never reached (or IS the source itself), then reads StepDir[cell]
	 *  as the (already-reversed, see FPSRHoverWindowCore.h's Propagate doc) 3D unit direction toward the source.
	 *  OutSeekValid is true only when that one step also changes Z layer (OutSeekZ = the stepped-to cell's world
	 *  center Z); a same-layer step leaves OutSeekValid false so the caller's 2D terrain-follow keeps driving Z. */
	FPSROGUELITE_API bool ResolveQuery(const FFPSRHoverWindowDims& Dims, const FVector& WindowWorldOrigin, float VoxelSize,
		const FFPSRHoverWindowField& Field, int32 EdgeMarginCells, const FVector& WorldPos,
		FVector& OutDirection, float& OutSeekZ, bool& OutSeekValid);
}
