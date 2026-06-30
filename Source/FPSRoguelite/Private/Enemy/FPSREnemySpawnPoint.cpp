// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemySpawnPoint.h"
#include "Components/SceneComponent.h"

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
