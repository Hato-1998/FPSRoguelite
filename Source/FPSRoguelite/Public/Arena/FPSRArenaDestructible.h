// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Destructible/FPSRDestructible.h"
#include "FPSRArenaDestructible.generated.h"

class UStaticMeshComponent;

/**
 * A destructible prop placed INSIDE an arena's cell grid (ADR 0010 D7 — the door's "AFPSRDestructible" generalisation
 * applied to arena props). Breaking it opens the cells it was AUTHORED to occupy — see FootprintCells and
 * FFPSRArenaCells::ComputeDestructibleCells, which both this class (on break) and the generator (while intact)
 * call so the two can never compute a different footprint for the same prop.
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

protected:
	/** Server: open this prop's authored cells (FFPSRArenaCells::ComputeDestructibleCells) in the owning
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

	/** 셀 발자국(가로×세로). 부서지면 여는 셀 수 — FFPSRArenaCells::ComputeDestructibleCells 가 이 액터의
	 *  위치를 앵커 셀로 삼아 +X/+Y 로 이 크기만큼 계산한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "아레나", meta = (DisplayName = "셀 발자국(가로×세로)", ClampMin = "1"))
	FIntPoint FootprintCells = FIntPoint(1, 1);
};
