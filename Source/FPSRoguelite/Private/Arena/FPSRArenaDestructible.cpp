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
				// Recomputed, not read back from a cached layout — ComputeDestructibleCells is the SAME function
				// Generate() called to block these cells while intact, so opening can never disagree with closing.
				FFPSRArenaAuthoredDestructible Self;
				Self.Location = GetActorLocation();
				Self.FootprintCells = FootprintCells;

				TArray<int32> Cells;
				FFPSRArenaCells::ComputeDestructibleCells(Self, Origin, Params.CellSize, Params.ArenaSizeCells, Cells);

				if (Cells.Num() > 0)
				{
					if (UFPSRFlowFieldSubsystem* Flow = World->GetSubsystem<UFPSRFlowFieldSubsystem>())
					{
						Flow->NotifyArenaCellsOpened(Cells);

						// S2 (ADR 0009 P1): also clear this prop's full 3D world AABB out of the voxel occupancy
						// field, at the SAME break event — a destructible has height, so its voxel footprint is not
						// just the 2D cells extruded. GetActorBounds(false): ALL components, matching NotifyDoorBroken's
						// own AABB computation (be robust to collision already being off by the time this runs).
						FVector BoundsOrigin, BoundsExtent;
						GetActorBounds(/*bOnlyCollidingComponents*/ false, BoundsOrigin, BoundsExtent);
						Flow->NotifyArenaVolumeOpened(FBox(BoundsOrigin - BoundsExtent, BoundsOrigin + BoundsExtent));

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
