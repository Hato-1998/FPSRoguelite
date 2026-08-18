// Copyright Epic Games, Inc. All Rights Reserved.

#include "Door/FPSRDoor.h"
#include "Enemy/FPSRFlowFieldSubsystem.h"
#include "Map/FPSRMapStreamSubsystem.h"
#include "FPSRCollisionChannels.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

AFPSRDoor::AFPSRDoor()
{
	// Durability (150) and DamageStageThresholds (75/50/25/5%) are inherited from AFPSRDestructible, whose in-class
	// defaults ARE the pre-refactor door's values — the base was extracted from this class, so restating them here
	// would only duplicate the numbers. A BP_Door instance override still wins over the inherited default as before.

	// Breakable leaves — the weapon target + barrier. ECC_FPSRPlayerPawn (see header): gathered by every weapon
	// object-query (-> auto damage via HealthComponent), blocks players + enemies, and is immune to the ECC_Pawn
	// pass-through windows (grace / downed). QueryOnly is enough
	// (movement sweeps + weapon queries are all query-based; no physics simulation needed).
	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(RootComponent); // RootComponent = AFPSRDestructible's "Root" scene component
	DoorMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DoorMesh->SetCollisionObjectType(ECC_FPSRPlayerPawn);
	DoorMesh->SetCollisionResponseToAllChannels(ECR_Block);
	DoorMesh->SetGenerateOverlapEvents(false);
	// No mesh asset assigned here — Game.MD §2 forbids hardcoding asset paths; the designer assigns SM in BP.

	// Frame (문틀) — inert wall. WorldStatic object type is NEVER gathered by the weapon object-queries
	// (ECC_Pawn / ECC_FPSRPlayerPawn / weakpoint), so shots stop on it as cover with no damage; it still blocks
	// movement and the weapon Visibility wall-trace like normal static geometry. Empty by default (frameless door).
	FrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrameMesh"));
	FrameMesh->SetupAttachment(RootComponent);
	FrameMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	FrameMesh->SetCollisionObjectType(ECC_WorldStatic);
	FrameMesh->SetCollisionResponseToAllChannels(ECR_Block);
	FrameMesh->SetGenerateOverlapEvents(false);
}

void AFPSRDoor::HandleBrokenAuthority(AActor* Breaker)
{
	// U (P-B): open the swarm flow field's seam this door was blocking so enemies cross + the origin-aware combat gate
	// allows across immediately. BEFORE ApplyBrokenState (see AFPSRDestructible::HandleBroken's fixed order) so the
	// leaf collision (door bounds) is still valid; no unified field (single-map) or off-authority makes this a no-op.
	if (UWorld* World = GetWorld())
	{
		if (UFPSRFlowFieldSubsystem* Flow = World->GetSubsystem<UFPSRFlowFieldSubsystem>())
		{
			Flow->NotifyDoorBroken(this);
		}
	}

	// Multimap Tier 0: a giant boundary door streams in the adjacent map when broken. The MapStreamSubsystem bakes the
	// field / re-caches spawn points / drops the boundary blocker once the sublevel's collision is verified ready (S3).
	// A plain (non-streaming) room gate leaves TargetMapId unset and this is a no-op. Client visibility is engine-replicated.
	// (Multimap CONTENT was dropped by ADR 0010 — every current door leaves TargetMapId unset, so this path stays wired
	// but unexercised until multimap content returns; it is not dead code, just currently unused.)
	if (TargetMapId.IsValid() && !TargetLevelName.IsNone())
	{
		if (UWorld* World = GetWorld())
		{
			if (UFPSRMapStreamSubsystem* Stream = World->GetSubsystem<UFPSRMapStreamSubsystem>())
			{
				Stream->RequestStreamIn(TargetMapId, TargetLevelName);
			}
		}
	}

	Super::HandleBrokenAuthority(Breaker); // pay out this door's Rewards (if any authored), same as any destructible
}

void AFPSRDoor::ApplyBrokenState()
{
	// Disable collision + hide the LEAVES only (propagate to their sub-meshes); the frame is a sibling and stays
	// solid/visible. Not Super::ApplyBrokenState() (which hides the WHOLE actor) — that would also take down the
	// frame, and not SetActorHiddenInGame/SetActorEnableCollision directly for the same reason.
	if (DoorMesh)
	{
		DoorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DoorMesh->SetVisibility(false, /*bPropagateToChildren*/ true);
	}
}

void AFPSRDoor::ClearBrokenState()
{
	// Reverse of ApplyBrokenState above: QueryOnly is DoorMesh's ORIGINAL collision setting from the constructor,
	// not QueryAndPhysics — restoring anything else would silently change what the leaf collides as post-reset.
	if (DoorMesh)
	{
		DoorMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		DoorMesh->SetVisibility(true, /*bPropagateToChildren*/ true);
	}
}

void AFPSRDoor::FireBrokenPresentation()
{
	OnDoorBroken();
}

void AFPSRDoor::FireDamageStagePresentation(int32 StageIndex, float HealthPct, float Threshold)
{
	OnDoorDamageStage(StageIndex, HealthPct, Threshold);
}
