// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Destructible/FPSRDestructible.h"
#include "FPSRArenaDestructible.generated.h"

class UStaticMeshComponent;

/**
 * A destructible prop placed INSIDE an arena's cell grid (ADR 0010 D7 — the door's "AFPSRDestructible" generalisation
 * applied to arena props). Breaking it opens the cells it occupies — see ComputeGridCells, which both this class
 * (on break) and UFPSRFlowFieldSubsystem::AdoptArenaFieldInternal (blocking those cells while the prop is intact)
 * call so the two can never compute a different footprint for the same prop.
 *
 * ⚠️ That guarantee used to name FFPSRArenaCells::ComputeDestructibleCells + an AUTHORED FootprintCells, with the
 * runtime generator as the "while intact" half. ADR 0012 retired the generator, and nothing replaced its blocking
 * pass until the adoption-time stamp above — for that window an intact prop blocked NOTHING in the 2D field
 * (ground enemies pathed into it and jammed) while break-time "opened" cells that were never closed. The default
 * cell source is now the prop's own mesh bounds, not a hand-typed footprint; FootprintCells survives as an opt-in
 * override (see its UPROPERTY comment).
 *
 * ## Why "reserve arenas parked beside the live one" matters here
 *
 * ADR 0010 D6 as amended 2026-08-17 (user decision): a future stage transition needs the next arena ready before
 * the current one ends, and that is built as several AFPSRArenaActor instances placed in the SAME level, at XY
 * locations far enough apart that their cell grids never overlap — not a sublevel streamed in on demand. A loaded
 * sublevel cannot be relocated at runtime, and turning its visibility on is not one atomic moment — it resolves
 * over several frames per client as that client's streaming catches up, with no shared deadline — so a transition
 * built on either would stop being one atomic swap and different clients would ack a different-length window —
 * exactly what invariant 8 (a FIXED damage-dealing window) forbids. Parking arenas beside each other in one
 * already-loaded level instead makes a "swap" a transform-free visibility/collision toggle plus a teleport.
 *
 * That is also why THIS class resolves its owning arena SPATIALLY (AFPSRArenaActor::ContainsWorldLocation) rather
 * than through an authored back-reference: with several arenas live in one level, "which arena's cell grid
 * physically contains me" is the one membership test that cannot drift as arenas are added, moved, or authored.
 */
UCLASS()
class FPSROGUELITE_API AFPSRArenaDestructible : public AFPSRDestructible
{
	GENERATED_BODY()

public:
	AFPSRArenaDestructible();

	const FIntPoint& GetFootprintCells() const { return FootprintCells; }

	/** The world AABB both voxel operations use for this prop — the ADOPTION-TIME stamp (SetOccupiedAABB) and the
	 *  BREAK-TIME clear (NotifyArenaVolumeOpened) MUST derive the same box or a stamp/clear mismatch leaves
	 *  phantom occupancy. The prop's own MESH render bounds, deliberately NOT GetActorBounds(false): that sums
	 *  every registered component (particles, sprites), inflating the box into neighbouring wall/floor voxels
	 *  (merge-gate P3), while the mesh's bounds exist regardless of whether collision has already been disabled
	 *  by break-time. Invalid box when the mesh is missing — both callers treat that as a no-op. */
	FBox GetVoxelBounds() const;

	/** The SINGLE decision point for "which grid cells does this prop occupy". Both
	 *  UFPSRFlowFieldSubsystem::AdoptArenaFieldInternal (stamping intact props blocked at arena adoption) and
	 *  HandleBrokenAuthority (opening those same cells on break) call ONLY this — never
	 *  FFPSRArenaCells::ComputeDestructibleCells / ComputeCellsFromBoundsXY directly — so the two can never branch
	 *  differently and disagree about which cells belong to this prop. FootprintCells > 0 on both axes is the
	 *  authored override (ComputeDestructibleCells, anchored at GetActorLocation()); otherwise the cells are
	 *  derived from the mesh's own world bounds (ComputeCellsFromBoundsXY(GetVoxelBounds(), ...)) — the same
	 *  source the 3D voxel stamp already uses, so 2D and 3D can never disagree about where this prop sits. */
	void ComputeGridCells(const FVector& ArenaOrigin, float CellSize, const FIntPoint& GridDims,
		TArray<int32>& OutCells) const;

protected:
	/** Server: open this prop's cells (ComputeGridCells — the same call adoption used to block them) in the owning
	 *  arena's flow field via UFPSRFlowFieldSubsystem::NotifyArenaCellsOpened, THEN (Super) pay out any Rewards
	 *  authored on this prop — same fixed order as AFPSRDoor::HandleBrokenAuthority. If no active arena contains
	 *  this actor, or the footprint computes to zero cells, this is a one-line warning and Super only. */
	virtual void HandleBrokenAuthority(AActor* Breaker) override;

	/** The prop's mesh. Object type ECC_FPSRDestructible / QueryOnly — the exact recipe AFPSRDoor uses for DoorMesh
	 *  (see FPSRCollisionChannels.h): gathered by every weapon damage query (so every weapon type damages it via
	 *  HealthComponent, zero new damage code), blocks players and enemies, and BLOCKS an in-flight projectile so a
	 *  round terminates here instead of passing through to whatever stands behind. Must NOT be WorldStatic: a
	 *  WorldStatic mesh is exactly what the arena's (now-abandoned) world-trace bake would have picked up on its
	 *  own, and re-probing "which cells does this open" at break time would be a runtime world query — banned by
	 *  the D7 performance contract (ADR 0010 D7 / 0011 E3). Declaring FootprintCells instead of tracing the mesh is
	 *  what keeps this prop off any bake entirely. No mesh asset is assigned here — Game.MD §2 forbids hardcoded
	 *  asset paths; the designer assigns the SM in BP. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPSR|Arena")
	TObjectPtr<UStaticMeshComponent> DestructibleMesh;

	/** 셀 발자국(가로×세로). 기본값 0 이면 메시 바운드에서 자동으로 계산한다(ComputeGridCells →
	 *  FFPSRArenaCells::ComputeCellsFromBoundsXY) — 3D 복셀 스탬프와 같은 출처(GetVoxelBounds)라 2D/3D 판정이
	 *  어긋나지 않는다. 0 보다 크게 저작하면 오버라이드로 바뀌는데, 이때는 FFPSRArenaCells::ComputeDestructibleCells 가
	 *  "액터가 선 셀"을 앵커로 삼아 +X/+Y 로만 이 크기만큼 자란다 — 즉 액터 위치가 메시 중심이면 발자국이 메시와
	 *  어긋난다(메시 절반이 앵커 반대쪽으로 벗어난다). 그래서 기본은 자동(0)이다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나", meta = (DisplayName = "셀 발자국(가로×세로)", ClampMin = "0"))
	FIntPoint FootprintCells = FIntPoint(0, 0);
};
