// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemySpawnPoint.h"
#include "Components/SceneComponent.h"
#include "Components/ChildActorComponent.h"

#if WITH_EDITORONLY_DATA
#include "Components/ArrowComponent.h"
#endif

AFPSREnemySpawnPoint::AFPSREnemySpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Actual spawn position (default at the actor origin = no behavior change for simple points). A structured-spawner
	// BP moves this INSIDE its mesh cavity so enemies appear inside the pipe/box, not at the placement gizmo.
	SpawnAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("SpawnAnchor"));
	SpawnAnchor->SetupAttachment(Root);

	// Waypoint container — designers add Scene children to this in a structured-spawner BP (kept separate from the
	// root so a pipe/box mesh added under the root isn't mistaken for a waypoint). (C1)
	ExitPathRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ExitPathRoot"));
	ExitPathRoot->SetupAttachment(Root);

#if WITH_EDITORONLY_DATA
	EditorArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("EditorArrow"));
	if (EditorArrow)
	{
		EditorArrow->SetupAttachment(Root);
		EditorArrow->ArrowColor = FColor(80, 200, 255);
		EditorArrow->ArrowSize = 1.5f;
		EditorArrow->bIsEditorOnly = true;
		EditorArrow->bIsScreenSizeScaled = true;
	}
#endif
}

FVector AFPSREnemySpawnPoint::GetSpawnLocation() const
{
	return SpawnAnchor ? SpawnAnchor->GetComponentLocation() : GetActorLocation();
}

void AFPSREnemySpawnPoint::GetExitPathWorldPoints(TArray<FVector>& Out) const
{
	// (1) PREFERRED when this point is a child actor of a structured-spawner BP: waypoints attached to the
	//     UChildActorComponent that spawned us. Those components live in the SPAWNER's Blueprint, so each hole in
	//     one mesh can have its OWN route, dragged in that BP's viewport.
	//
	//     Without this, every hole would have to share the single path authored inside the spawn-point BP — which
	//     is unusable the moment a mesh's holes exit in different directions, and you cannot work around it by
	//     adding components to a ChildActorComponent's template (the editor does not allow it). Rotating the
	//     ChildActorComponent only covers the case where every route has the SAME shape relative to its hole.
	//
	//     GetAttachChildren() on that component also contains OUR OWN root (the engine attaches a child actor's
	//     root to its ChildActorComponent — ChildActorComponent.cpp:860), so anything owned by this actor is
	//     skipped: it is not a waypoint, it is us.
	if (const UChildActorComponent* SpawnedBy = GetParentComponent())
	{
		const TArray<TObjectPtr<USceneComponent>>& Attached = SpawnedBy->GetAttachChildren();
		for (const USceneComponent* Child : Attached)
		{
			if (Child && Child->GetOwner() != this)
			{
				Out.Add(Child->GetComponentLocation());
			}
		}
		if (Out.Num() > 0)
		{
			return;
		}
		// None authored on the component — fall through so the spawn-point BP's own path can act as the default.
	}

	// (2) The point's own ExitPathRoot children. This is the path for a standalone (directly placed) spawn point,
	//     and the per-hole default a structured spawner inherits when its ChildActorComponent has no waypoints.
	if (!ExitPathRoot)
	{
		return;
	}

	// Each direct child scene component is a waypoint; attach order = order (same idiom as AFPSRMissionPointSet).
	const TArray<TObjectPtr<USceneComponent>>& Waypoints = ExitPathRoot->GetAttachChildren();
	Out.Reserve(Out.Num() + Waypoints.Num());
	for (const USceneComponent* Child : Waypoints)
	{
		if (Child)
		{
			Out.Add(Child->GetComponentLocation());
		}
	}
}
