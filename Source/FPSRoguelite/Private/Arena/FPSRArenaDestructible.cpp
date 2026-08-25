// Copyright Epic Games, Inc. All Rights Reserved.

#include "Arena/FPSRArenaDestructible.h"
#include "Arena/FPSRArenaActor.h"
#include "Arena/FPSRArenaCells.h"
#include "Arena/FPSRArenaTypes.h"
#include "Enemy/FPSRFlowFieldSubsystem.h"
#include "Core/FPSRLogChannels.h"
#include "FPSRCollisionChannels.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

AFPSRArenaDestructible::AFPSRArenaDestructible()
{
	// Same collision recipe as AFPSRDoor's DoorMesh (see FPSRCollisionChannels.h / this class's header comment):
	// ECC_FPSRDestructible is gathered by every weapon damage query (auto damage via HealthComponent, zero new code),
	// blocks both players and enemies, AND blocks an in-flight projectile — the last part is why this is its own
	// channel and not the player's: a projectile only OVERLAPS pawns, so a prop wearing the player channel was
	// physically transparent to gunfire. QueryOnly is enough — nothing here needs physics simulation.
	DestructibleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DestructibleMesh"));
	DestructibleMesh->SetupAttachment(RootComponent); // RootComponent = AFPSRDestructible's "Root" scene component
	DestructibleMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DestructibleMesh->SetCollisionObjectType(ECC_FPSRDestructible);
	DestructibleMesh->SetCollisionResponseToAllChannels(ECR_Block);
	DestructibleMesh->SetGenerateOverlapEvents(false);
	// No mesh asset assigned here — Game.MD §2 forbids hardcoding asset paths; the designer assigns SM in BP.
}

FBox AFPSRArenaDestructible::GetVoxelBounds() const
{
	// The MESH's own render bounds — see the header comment for why this is neither GetActorBounds(false) (inflated
	// by non-colliding components) nor a collision query (already disabled by break-time). Render bounds track the
	// assigned SM asset and survive collision teardown, so stamp (adoption) and clear (break) read the same box.
	if (DestructibleMesh && DestructibleMesh->GetStaticMesh())
	{
		return DestructibleMesh->Bounds.GetBox();
	}
	return FBox(ForceInit); // no mesh authored — invalid box, both voxel ops no-op on it
}

void AFPSRArenaDestructible::ComputeGridCells(const FVector& ArenaOrigin, float CellSize, const FIntPoint& GridDims,
	TArray<int32>& OutCells) const
{
	if (FootprintCells.X > 0 && FootprintCells.Y > 0)
	{
		// Authored override: an anchor + explicit footprint, growing +X/+Y from GetActorLocation() (see the
		// FootprintCells UPROPERTY comment for why that drifts from the mesh unless the actor sits at the mesh's
		// corner, not its center).
		FFPSRArenaAuthoredDestructible Self;
		Self.Location = GetActorLocation();
		Self.FootprintCells = FootprintCells;
		FFPSRArenaCells::ComputeDestructibleCells(Self, ArenaOrigin, CellSize, GridDims, OutCells);
	}
	else
	{
		// Auto (default, FootprintCells == 0): derive the cells straight from the mesh's own world bounds — the
		// SAME source GetVoxelBounds() hands the 3D voxel stamp, so 2D and 3D can never disagree about where this
		// prop actually sits.
		FFPSRArenaCells::ComputeCellsFromBoundsXY(GetVoxelBounds(), ArenaOrigin, CellSize, GridDims, OutCells);
	}
}

void AFPSRArenaDestructible::HandleBrokenAuthority(AActor* Breaker)
{
	bool bOpened = false;

	if (UWorld* World = GetWorld())
	{
		if (AFPSRArenaActor* Arena = AFPSRArenaActor::FindActiveInWorld(World))
		{
			FFPSRArenaGenParams Params;
			FVector Origin;
			if (Arena->ContainsWorldLocation(GetActorLocation()) && Arena->GetGenParams(Params, Origin))
			{
				// Recomputed, not read back from a cached layout. The pair to this is
				// UFPSRFlowFieldSubsystem::AdoptArenaFieldInternal's adoption-time stamp — both call ONLY
				// ComputeGridCells (never ComputeDestructibleCells/ComputeCellsFromBoundsXY directly), so opening
				// here can never disagree with what adoption blocked. (ADR 0012 retired Generate(), which used to
				// be the other half of this guarantee — "the SAME function Generate() called to block these cells
				// while intact" is no longer true; ComputeGridCells is Generate()'s replacement in that guarantee.)
				TArray<int32> Cells;
				ComputeGridCells(Origin, Params.CellSize, Params.ArenaSizeCells, Cells);

				if (Cells.Num() > 0)
				{
					if (UFPSRFlowFieldSubsystem* Flow = World->GetSubsystem<UFPSRFlowFieldSubsystem>())
					{
						Flow->NotifyArenaCellsOpened(Cells);

						// S2 (ADR 0009 P1): also clear this prop's 3D voxel occupancy at the SAME break event — a
						// destructible has height, so its voxel footprint is not just the 2D cells extruded. The
						// box comes from GetVoxelBounds() — the SAME source the adoption-time stamp used
						// (merge-gate P2: the ECC_WorldStatic bake probe never saw this ECC_FPSRDestructible mesh,
						// so the prop was stamped INTO the adopted field at adoption; stamp and clear must mirror
						// each other exactly or phantom occupancy is left behind).
						Flow->NotifyArenaVolumeOpened(GetVoxelBounds());

						bOpened = true;
					}
				}
			}
		}
	}

	if (!bOpened)
	{
		UE_LOG(LogFPSR, Warning,
			TEXT("[Arena] %s broke but opened no cells (no active arena contains it, or its footprint is entirely off-grid)."),
			*GetName());
	}

	Super::HandleBrokenAuthority(Breaker); // still pay out this prop's Rewards (if any authored), same as AFPSRDoor
}
