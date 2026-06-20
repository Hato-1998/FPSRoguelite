// Copyright Epic Games, Inc. All Rights Reserved.

#include "Boss/FPSRBossSpawnPoint.h"
#include "Components/SceneComponent.h"

#if WITH_EDITORONLY_DATA
#include "Components/ArrowComponent.h"
#endif

AFPSRBossSpawnPoint::AFPSRBossSpawnPoint()
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
		EditorArrow->ArrowColor = FColor(255, 80, 80); // red — distinct from the cyan enemy spawn arrow
		EditorArrow->ArrowSize = 2.5f;
		EditorArrow->bIsEditorOnly = true;
		EditorArrow->bIsScreenSizeScaled = true;
	}
#endif
}
