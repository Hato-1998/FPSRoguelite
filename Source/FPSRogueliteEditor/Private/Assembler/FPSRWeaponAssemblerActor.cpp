// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assembler/FPSRWeaponAssemblerActor.h"

#include "Weapon/FPSRWeaponDataAsset.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMesh.h"

AFPSRWeaponAssemblerActor::AFPSRWeaponAssemblerActor()
{
	BodyMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BodyMesh"));
	RootComponent = BodyMesh;
}

void AFPSRWeaponAssemblerActor::BuildFromDA(UFPSRWeaponDataAsset* DA)
{
	if (!DA)
	{
		return;
	}

	SourceDA = DA;

	USkeletalMesh* Body = DA->WeaponMesh1P.IsNull() ? nullptr : DA->WeaponMesh1P.LoadSynchronous();
	BodyMesh->SetSkeletalMeshAsset(Body);

	for (int32 i = 0; i < DA->WeaponParts1P.Num(); ++i)
	{
		const FFPSRWeaponPartAttachment& P = DA->WeaponParts1P[i];
		UStaticMesh* M = P.Part.IsNull() ? nullptr : P.Part.LoadSynchronous();

		UStaticMeshComponent* PC = NewObject<UStaticMeshComponent>(this);
		PC->SetupAttachment(BodyMesh);
		PC->RegisterComponent();
		PC->SetStaticMesh(M);
		PC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		AddInstanceComponent(PC); // exposes the component in the details panel so the designer can gizmo-move it

		// Initial placement: prefer the part's already-wired socket transform (relative to the body), else its
		// authored Offset — either way the part starts where it would render at runtime.
		FTransform Init = P.Offset;
		if (!P.Socket.IsNone() && Body)
		{
			if (const USkeletalMeshSocket* S = Body->FindSocket(P.Socket))
			{
				Init = FTransform(S->RelativeRotation, S->RelativeLocation, S->RelativeScale) * P.Offset;
			}
		}
		PC->SetRelativeTransform(Init);

		PartComponents.Add(PC);
	}
}
