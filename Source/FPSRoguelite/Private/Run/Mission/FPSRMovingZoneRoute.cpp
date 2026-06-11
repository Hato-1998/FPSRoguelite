// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMovingZoneRoute.h"
#include "Components/SplineComponent.h"

AFPSRMovingZoneRoute::AFPSRMovingZoneRoute()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	RouteSpline = CreateDefaultSubobject<USplineComponent>(TEXT("RouteSpline"));
	SetRootComponent(RouteSpline);
	RouteSpline->SetClosedLoop(false);
}

void AFPSRMovingZoneRoute::GetWorldPoints(TArray<FVector>& Out) const
{
	if (!RouteSpline)
	{
		return;
	}
	const int32 Num = RouteSpline->GetNumberOfSplinePoints();
	Out.Reserve(Out.Num() + Num);
	for (int32 i = 0; i < Num; ++i)
	{
		Out.Add(RouteSpline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World));
	}
}

FTransform AFPSRMovingZoneRoute::GetFirstPointTransform() const
{
	if (RouteSpline && RouteSpline->GetNumberOfSplinePoints() > 0)
	{
		return FTransform(RouteSpline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World));
	}
	return GetActorTransform();
}
