// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "FPSRSpawnRoom.generated.h"

class UBoxComponent;
class UPrimitiveComponent;

/** Designer-placed room volume for the progressive room-spawn system (Enemy.md §2-6). A room owns a unique spawn
 *  zone tag: at BeginPlay it auto-tags the enemy spawn points inside its box with that tag, and when a player first
 *  enters the box it activates the zone in the enemy spawn subsystem (so the room's spawn points go live — and stay
 *  live, accumulating, as further rooms open). The starting room (bActiveAtStart) is live from run start.
 *
 *  Server-only logic (spawn-zone state is server-authoritative); not replicated. Content: subclass in BP, size the
 *  EntryTrigger box to the room, set a unique RoomTag, and flag exactly the start room bActiveAtStart. */
UCLASS()
class FPSROGUELITE_API AFPSRSpawnRoom : public AActor
{
	GENERATED_BODY()

public:
	AFPSRSpawnRoom();

	FGameplayTag GetRoomTag() const { return RoomTag; }
	bool IsActiveAtStart() const { return bActiveAtStart; }

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnEntryBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	/** Room volume — a player entering it activates this room's spawn zone. Overlaps only the player object channel
	 *  (enemies never trigger a room). Designer sizes this to the whole room in BP. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPSR|Spawn Room")
	TObjectPtr<UBoxComponent> EntryTrigger;

	/** Unique spawn-zone tag for this room (declare under SpawnZone.Room.* in DefaultGameplayTags.ini). Auto-applied
	 *  to interior spawn points at BeginPlay and activated on player entry. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPSR|Spawn Room")
	FGameplayTag RoomTag;

	/** The starting room: its zone is active from run start (the player spawns inside it, so relying on the entry
	 *  overlap firing would be timing-fragile — an explicit flag is safe). Normally exactly one room is flagged. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPSR|Spawn Room")
	bool bActiveAtStart = false;
};
