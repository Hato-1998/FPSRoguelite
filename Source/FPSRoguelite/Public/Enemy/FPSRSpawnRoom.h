// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "FPSRSpawnRoom.generated.h"

class UBoxComponent;
class UPrimitiveComponent;

/** What a room volume does to its spawn zone when a player enters (Enemy.md §2-6). Activate (default) latches the
 *  zone on (accumulating, as cleared rooms keep spawning); Deactivate turns the target zone off so a designer can
 *  stop an earlier room's spawns once the player moves past it. */
UENUM(BlueprintType)
enum class ESpawnRoomTriggerMode : uint8
{
	/** Player entry activates this room's zone (RoomTag) — it stays live (accumulates). The room auto-tags the enemy
	 *  spawn points inside its box with RoomTag at BeginPlay. */
	Activate,
	/** Player entry DEACTIVATES the target zone (RoomTag) — removes it from the active set so its points stop
	 *  spawning. Does NOT auto-tag interior points (it references an existing zone by tag; 1 volume = 1 zone). */
	Deactivate
};

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
	ESpawnRoomTriggerMode GetTriggerMode() const { return TriggerMode; }

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnEntryBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	/** Room volume — a player entering it activates this room's spawn zone. Overlaps only the player object channel
	 *  (enemies never trigger a room). Designer sizes this to the whole room in BP. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPSR|Spawn Room")
	TObjectPtr<UBoxComponent> EntryTrigger;

	/** Whether entering this volume activates (default) or deactivates its zone. Activate = the standard progressive
	 *  room (latches RoomTag on, auto-tags interior points); Deactivate = turns the target RoomTag off (no auto-tag). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPSR|Spawn Room")
	ESpawnRoomTriggerMode TriggerMode = ESpawnRoomTriggerMode::Activate;

	/** The spawn-zone tag this volume controls (declare under SpawnZone.Room.* in DefaultGameplayTags.ini). Activate
	 *  mode: this room's own zone — auto-applied to interior spawn points at BeginPlay and activated on entry.
	 *  Deactivate mode: the existing zone to turn OFF on entry (must match the Activate room's RoomTag). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPSR|Spawn Room")
	FGameplayTag RoomTag;

	/** The starting room: its zone is active from run start (the player spawns inside it, so relying on the entry
	 *  overlap firing would be timing-fragile — an explicit flag is safe). Normally exactly one room is flagged.
	 *  Activate mode only (a deactivation volume has no start state). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPSR|Spawn Room",
		meta = (EditCondition = "TriggerMode == ESpawnRoomTriggerMode::Activate", EditConditionHides))
	bool bActiveAtStart = false;
};
