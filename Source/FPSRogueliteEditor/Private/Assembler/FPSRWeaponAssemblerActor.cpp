// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assembler/FPSRWeaponAssemblerActor.h"

#include "Weapon/FPSRWeaponDataAsset.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMesh.h"

namespace
{
	/** Representative, variant-stripped name for a part's preview component (SM_Wep_Mod_A_Barrel_01 -> Barrel). The
	 *  component name doubles as the mount-socket name on capture, so the designer can rename it to control the slot
	 *  (e.g. share one socket across barrel variants). Null part -> Part<Index>. */
	FString MakePartDisplayName(const TSoftObjectPtr<UStaticMesh>& Part, int32 Index)
	{
		if (Part.IsNull())
		{
			return FString::Printf(TEXT("Part%d"), Index);
		}
		FString Name = Part.GetAssetName();
		Name.RemoveFromStart(TEXT("SM_Wep_Mod_A_"));
		Name.RemoveFromStart(TEXT("SM_Wep_Mod_"));
		int32 UnderscorePos = INDEX_NONE;
		if (Name.FindLastChar(TEXT('_'), UnderscorePos))
		{
			const FString Tail = Name.Mid(UnderscorePos + 1);
			if (!Tail.IsEmpty() && Tail.IsNumeric())
			{
				Name = Name.Left(UnderscorePos);
			}
		}
		return Name.IsEmpty() ? FString::Printf(TEXT("Part%d"), Index) : Name;
	}

	/** Component-space transform of the skeletal mesh's root bone (bone 0 ref pose = component space, it has no parent).
	 *  This pack's weapon-body root bone carries a 90° roll, so bone-relative sockets must convert through it. */
	FTransform RootBoneComponentSpace(const USkeletalMesh* Mesh)
	{
		if (Mesh && Mesh->GetRefSkeleton().GetRefBonePose().Num() > 0)
		{
			return Mesh->GetRefSkeleton().GetRefBonePose()[0];
		}
		return FTransform::Identity;
	}
}

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

	const FTransform RootBoneCS = RootBoneComponentSpace(Body);

	for (int32 i = 0; i < DA->WeaponParts1P.Num(); ++i)
	{
		const FFPSRWeaponPartAttachment& P = DA->WeaponParts1P[i];
		UStaticMesh* M = P.Part.IsNull() ? nullptr : P.Part.LoadSynchronous();

		// Name the component after its part (variant-stripped) so the designer can tell parts apart in the details
		// panel and rename it to control the slot; CaptureToSockets derives the mount-socket name from this name.
		const FName CompName = MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), FName(*MakePartDisplayName(P.Part, i)));
		UStaticMeshComponent* PC = NewObject<UStaticMeshComponent>(this, CompName);
		PC->SetupAttachment(BodyMesh);
		PC->RegisterComponent();
		PC->SetStaticMesh(M);
		PC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		AddInstanceComponent(PC); // exposes the component in the details panel so the designer can gizmo-move it

		// Initial placement (component-space). A wired socket is BONE-relative — bring it into component space through
		// the root bone (which carries a 90° roll here) so a re-spawned preview shows parts exactly where they attach
		// at runtime; else fall back to the authored Offset.
		FTransform Init = P.Offset;
		if (!P.Socket.IsNone() && Body)
		{
			if (const USkeletalMeshSocket* S = Body->FindSocket(P.Socket))
			{
				const FTransform SocketRel(S->RelativeRotation, S->RelativeLocation, S->RelativeScale);
				Init = P.Offset * (SocketRel * RootBoneCS);
			}
		}
		PC->SetRelativeTransform(Init);

		PartComponents.Add(PC);
	}
}
