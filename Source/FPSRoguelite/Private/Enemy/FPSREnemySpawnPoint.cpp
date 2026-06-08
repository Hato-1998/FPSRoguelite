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
