// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/Mission/FPSRMissionPointSet.h"
#include "Components/SceneComponent.h"

AFPSRMissionPointSet::AFPSRMissionPointSet()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AFPSRMissionPointSet::GetWorldPoints(TArray<FVector>& Out) const
{
	const USceneComponent* Root = GetRootComponent();
	if (!Root)
	{
		return;
	}

	// Each direct child scene component is a point; attach order = order.
	const TArray<TObjectPtr<USceneComponent>>& PointChildren = Root->GetAttachChildren();
	Out.Reserve(Out.Num() + PointChildren.Num());
	for (const USceneComponent* Child : PointChildren)
	{
		if (Child)
		{
			Out.Add(Child->GetComponentLocation());
		}
	}
}

FTransform AFPSRMissionPointSet::GetFirstPointTransform() const
{
	if (const USceneComponent* Root = GetRootComponent())
	{
		for (const USceneComponent* Child : Root->GetAttachChildren())
		{
			if (Child)
			{
				return FTransform(Child->GetComponentLocation());
			}
		}
	}
	return GetActorTransform();
}
