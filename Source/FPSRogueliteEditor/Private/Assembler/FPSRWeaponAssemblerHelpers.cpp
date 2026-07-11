// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assembler/FPSRWeaponAssemblerHelpers.h"

#include "Weapon/FPSRWeaponDataAsset.h"

#include "FileHelpers.h"   // UEditorLoadingAndSavingUtils::SavePackages (UnrealEd — avoids the EditorScriptingUtilities plugin)
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"

namespace FPSRWeaponAssemblerHelpers
{
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

	FTransform RootBoneComponentSpace(const USkeletalMesh* Mesh)
	{
		if (Mesh && Mesh->GetRefSkeleton().GetRefBonePose().Num() > 0)
		{
			return Mesh->GetRefSkeleton().GetRefBonePose()[0];
		}
		return FTransform::Identity;
	}

	int32 BakeSockets(UFPSRWeaponDataAsset* DA, USkeletalMeshComponent* BodyComp, const TArray<UStaticMeshComponent*>& PartComps)
	{
		USkeletalMesh* Body = BodyComp ? BodyComp->GetSkeletalMeshAsset() : nullptr;
		if (!DA || !Body)
		{
			return 0;
		}

		// The body's world transform in the preview scene — parts are captured RELATIVE TO THIS (not the world
		// origin), so the bake stays correct even when the whole assembly was dragged via "전체 이동".
		const FTransform BodyWorld = BodyComp->GetComponentTransform();

		const FReferenceSkeleton& RefSkel = Body->GetRefSkeleton();
		const FName RootBone = RefSkel.GetNum() > 0 ? RefSkel.GetBoneName(0) : NAME_None;
		// Sockets are BONE-relative. This pack's root bone carries a 90° roll, so convert the designer's component-
		// space placement through the root bone's component-space transform (bone 0 ref pose) — else the captured part
		// lands rotated at runtime.
		const FTransform RootBoneCS = RootBoneComponentSpace(Body);

		Body->Modify();
		DA->Modify();

		// Clear the tool's previous sockets (it owns the SOCKET_Mount_* namespace) so a re-bake or a part-component
		// rename replaces them instead of leaving orphans.
		{
			TArray<TObjectPtr<USkeletalMeshSocket>>& MeshSockets = Body->GetMeshOnlySocketList();
			MeshSockets.RemoveAll([](const TObjectPtr<USkeletalMeshSocket>& S)
			{
				return S && S->SocketName.ToString().StartsWith(TEXT("SOCKET_Mount_"));
			});
		}

		int32 n = 0;
		const int32 Count = FMath::Min(PartComps.Num(), DA->WeaponParts1P.Num());
		for (int32 i = 0; i < Count; ++i)
		{
			UStaticMeshComponent* PC = PartComps[i];
			if (!PC)
			{
				continue;
			}

			// Part transform relative to the BODY COMPONENT (robust to the body being moved — was previously the
			// part's world transform, which only matched when the body sat at the world origin). Then bone-relative.
			const FTransform PartRelBody = PC->GetComponentTransform().GetRelativeTransform(BodyWorld);
			const FTransform SocketRel = PartRelBody.GetRelativeTransform(RootBoneCS);
			// Mount-socket name follows the (renameable) component name: one representative slot, no variant suffix.
			const FName SocketName(*FString::Printf(TEXT("SOCKET_Mount_%s"), *PC->GetName()));

			USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(Body);
			Socket->SocketName = SocketName;
			Socket->BoneName = RootBone;
			Socket->RelativeLocation = SocketRel.GetLocation();
			Socket->RelativeRotation = SocketRel.GetRotation().Rotator();
			Socket->RelativeScale = FVector(1.0f);
			Body->AddSocket(Socket, false);

			DA->WeaponParts1P[i].Socket = SocketName;
			DA->WeaponParts1P[i].Offset = FTransform::Identity;
			++n;
		}

		Body->RebuildSocketMap();
		Body->MarkPackageDirty();
		DA->MarkPackageDirty();

		// Save both packages via UnrealEd (no EditorScriptingUtilities plugin dependency).
		TArray<UPackage*> PackagesToSave;
		PackagesToSave.Add(Body->GetOutermost());
		PackagesToSave.Add(DA->GetOutermost());
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, /*bOnlyDirty=*/false);
		return n;
	}
}
