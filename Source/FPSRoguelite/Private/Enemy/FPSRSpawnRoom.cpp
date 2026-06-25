// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSRSpawnRoom.h"
#include "Enemy/FPSREnemySpawnPoint.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Hero/FPSRCharacter.h"
#include "Core/FPSRLogChannels.h"
#include "FPSRCollisionChannels.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AFPSRSpawnRoom::AFPSRSpawnRoom()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // spawn-zone state lives in the (server-only) spawn subsystem

	EntryTrigger = CreateDefaultSubobject<UBoxComponent>(TEXT("EntryTrigger"));
	SetRootComponent(EntryTrigger);
	EntryTrigger->SetBoxExtent(FVector(500.0f, 500.0f, 250.0f)); // placeholder — designer sizes to the room in BP
	EntryTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	EntryTrigger->SetCollisionObjectType(ECC_WorldDynamic);
	EntryTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	// Overlap ONLY the player object channel: enemies (ECC_Pawn) and weapon traces never trigger a room, and the
	// box never blocks anyone (overlap, not block — players walk through it freely).
	EntryTrigger->SetCollisionResponseToChannel(ECC_FPSRPlayerPawn, ECR_Overlap);
	EntryTrigger->SetGenerateOverlapEvents(true);
}

void AFPSRSpawnRoom::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return; // server-authoritative: clients never select spawns, so room logic runs on the server only
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Auto-tag the enemy spawn points inside this room with its zone (designer convenience — drop points in, no
	// per-point setup). A point that already carries a manual ZoneTag is left alone (explicit override wins).
	// Containment uses the box's oriented bounds (OBB), so a rotated room still tags correctly.
	if (RoomTag.IsValid() && EntryTrigger)
	{
		const FTransform BoxTransform = EntryTrigger->GetComponentTransform();
		const FVector Extent = EntryTrigger->GetScaledBoxExtent();
		for (TActorIterator<AFPSREnemySpawnPoint> It(World); It; ++It)
		{
			AFPSREnemySpawnPoint* Point = *It;
			if (!Point || Point->GetZoneTag().IsValid())
			{
				continue;
			}
			const FVector Local = BoxTransform.InverseTransformPosition(Point->GetActorLocation());
			if (FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z)
			{
				Point->SetZoneTag(RoomTag);
			}
		}
	}

	if (EntryTrigger)
	{
		EntryTrigger->OnComponentBeginOverlap.AddDynamic(this, &AFPSRSpawnRoom::OnEntryBeginOverlap);
	}
}

void AFPSRSpawnRoom::OnEntryBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	if (!HasAuthority() || !RoomTag.IsValid())
	{
		return;
	}

	// Only a player pawn opens a room. The trigger already overlaps only ECC_FPSRPlayerPawn; this is a defensive
	// type check (a future actor on that channel that isn't a player would otherwise activate the zone).
	if (!Cast<AFPSRCharacter>(OtherActor))
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (UFPSREnemySpawnSubsystem* SpawnSub = World->GetSubsystem<UFPSREnemySpawnSubsystem>())
		{
			SpawnSub->ActivateSpawnZone(RoomTag);
		}
	}
}
