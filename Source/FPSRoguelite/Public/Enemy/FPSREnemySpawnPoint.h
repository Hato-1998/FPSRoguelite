// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "FPSREnemySpawnPoint.generated.h"

class UArrowComponent;
class USceneComponent;

/** Designer-placed enemy spawn anchor. The enemy spawn subsystem selects UNIFORMLY at random among eligible
 *  points (enabled + min distance + its spawn zone active). Server-only selection; this actor is not replicated
 *  (the spawned enemy actor it anchors is the replicated object). Lightweight: no tick, no collision. A point's
 *  ZoneTag is normally auto-applied by the enclosing AFPSRSpawnRoom at BeginPlay. The enemy spawns at SpawnAnchor's
 *  world location (a child component, default at the actor origin) — move it to spawn INSIDE a structured-spawner's
 *  mesh cavity rather than at the placement gizmo. */
UCLASS()
class FPSROGUELITE_API AFPSREnemySpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AFPSREnemySpawnPoint();

	/** Spawn-zone (room) tag. A point with NO tag is always eligible; a tagged point is eligible only while its
	 *  zone is active in the spawn subsystem (a room opens -> its zone activates -> its points go live, and stay
	 *  live as more rooms open — accumulating spawn locations). Usually auto-applied by the enclosing
	 *  AFPSRSpawnRoom; a manually set tag is respected (override). (Room spawn system, Enemy.md §2-6.) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn")
	FGameplayTag ZoneTag;

	/** If > 0, this point is only eligible when the nearest player is at least this far away (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn", meta = (ClampMin = "0.0"))
	float MinPlayerDistance = 0.0f;

	/** Disable to exclude this point from selection without deleting it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn")
	bool bEnabled = true;

	FGameplayTag GetZoneTag() const { return ZoneTag; }
	float GetMinPlayerDistance() const { return MinPlayerDistance; }
	bool IsEnabled() const { return bEnabled; }

	/** Server/setup: assign this point's spawn zone (used by AFPSRSpawnRoom to auto-tag its interior points). */
	void SetZoneTag(const FGameplayTag& InZoneTag) { ZoneTag = InZoneTag; }

	/** Append this point's authored exit-path waypoints (world space) to Out, in attach order. The waypoints are the
	 *  child scene components of ExitPathRoot: in a structured-spawner BP (a pipe/box mesh enemies spawn INSIDE), add
	 *  Scene components under ExitPathRoot and place them along the route OUT to the mouth — the last is the hand-off
	 *  point to flow-field player-chase. No children = no path (the enemy chases immediately). (C1) */
	void GetExitPathWorldPoints(TArray<FVector>& Out) const;

	/** World location where the enemy actually spawns = SpawnAnchor's world location (falls back to the actor origin
	 *  if SpawnAnchor is somehow null). Lets a structured-spawner BP place the spawn point INSIDE its mesh cavity
	 *  while the actor origin stays the placement/orientation gizmo. */
	FVector GetSpawnLocation() const;

private:
	/** The enemy spawn position (a child Scene component, default at the actor origin). Move it in a structured-spawner
	 *  BP so enemies appear inside the pipe/box cavity instead of at the placement gizmo. Distinct from ExitPathRoot's
	 *  waypoints, which are the route OUT after spawning. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy Spawn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SpawnAnchor;

	/** Container for the authored exit-path waypoints — its direct child scene components are the waypoints (attach
	 *  order = order). Separate from the actor root so a structured-spawner BP can also add a pipe/box mesh under the
	 *  root without those components being mistaken for waypoints. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy Spawn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> ExitPathRoot;

#if WITH_EDITORONLY_DATA
	/** Editor-only direction arrow so designers can see placement + facing (enemy spawn rotation). */
	UPROPERTY()
	TObjectPtr<UArrowComponent> EditorArrow;
#endif
};
