// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSRFlowFieldBoundsVolume.h"
#include "Components/BoxComponent.h"

#if WITH_EDITORONLY_DATA
#include "Components/BillboardComponent.h"
#endif

AFPSRFlowFieldBoundsVolume::AFPSRFlowFieldBoundsVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // the server reads the bounds once at world begin; no runtime/replicated state

	BoundsBox = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsBox"));
	SetRootComponent(BoundsBox);
	BoundsBox->SetBoxExtent(FVector(14000.0f, 14000.0f, 1000.0f)); // placeholder — designer sizes to the map in BP
	BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision); // pure gizmo: never blocks/overlaps anything
	BoundsBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	BoundsBox->SetGenerateOverlapEvents(false);
	BoundsBox->ShapeColor = FColor(40, 220, 120); // green wireframe, distinct from spawn rooms/points

#if WITH_EDITORONLY_DATA
	EditorBillboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("EditorBillboard"));
	if (EditorBillboard)
	{
		EditorBillboard->SetupAttachment(BoundsBox);
		EditorBillboard->bIsEditorOnly = true;
		EditorBillboard->bIsScreenSizeScaled = true;
	}
#endif
}

FBox AFPSRFlowFieldBoundsVolume::GetWorldBounds() const
{
	if (BoundsBox)
	{
		// Component's cached world bounds -> axis-aligned box. Valid at OnWorldBeginPlay (components are registered
		// before UWorld::BeginPlay), and rotation-robust (a rotated box yields its enclosing world AABB).
		return BoundsBox->Bounds.GetBox();
	}
	return FBox(ForceInit);
}
