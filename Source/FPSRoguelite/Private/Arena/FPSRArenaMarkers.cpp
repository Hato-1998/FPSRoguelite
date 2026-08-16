// Copyright Epic Games, Inc. All Rights Reserved.

#include "Arena/FPSRArenaMarkers.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"

AFPSRArenaBlocker::AFPSRArenaBlocker()
{
	PrimaryActorTick.bCanEverTick = false;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	SetRootComponent(Box);
	// 400 x 400 x 400 cm: four cells square at the shipped 100 cm cell size, so a freshly dropped blocker is
	// already a believable cluster instead of something the designer has to resize before it means anything.
	Box->SetBoxExtent(FVector(200.0f, 200.0f, 200.0f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionProfileName(TEXT("BlockAll"));
	Box->SetMobility(EComponentMobility::Static);
	Box->bDrawOnlyIfSelected = false;
	Box->ShapeColor = FColor(255, 96, 0);

	DisplayMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DisplayMesh"));
	DisplayMesh->SetupAttachment(Box);
	// The box already owns collision. A second colliding primitive here would be a second opinion about where the
	// wall is, and the rasteriser only reads one of them.
	DisplayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DisplayMesh->SetMobility(EComponentMobility::Static);
}

FVector2D AFPSRArenaBlocker::GetHalfExtentXY() const
{
	if (!Box)
	{
		return FVector2D::ZeroVector;
	}
	// Scaled extent, so a designer resizing the actor changes the mask exactly as much as it changes the collision.
	const FVector Scaled = Box->GetScaledBoxExtent();
	return FVector2D(Scaled.X, Scaled.Y);
}

float AFPSRArenaBlocker::GetYawDegrees() const
{
	return GetActorRotation().Yaw;
}

AFPSRArenaLandmark::AFPSRArenaLandmark()
{
	PrimaryActorTick.bCanEverTick = false;

	DisplayMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DisplayMesh"));
	SetRootComponent(DisplayMesh);
	DisplayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DisplayMesh->SetMobility(EComponentMobility::Static);
}
