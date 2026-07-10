// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assembler/FPSRWeaponAssemblerHelpers.h"

#include "Weapon/FPSRWeaponDataAsset.h"

#include "FileHelpers.h"   // UEditorLoadingAndSavingUtils::SavePackages (UnrealEd — avoids the EditorScriptingUtilities plugin)
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
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

	int32 BakeSockets(UFPSRWeaponDataAsset* DA, USkeletalMesh* Body, const TArray<UStaticMeshComponent*>& PartComps)
	{
		if (!DA || !Body)
		{
			return 0;
		}

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

			// The preview scene places Body at identity with no parent, and each PartComp is likewise added to the
			// scene unparented — so a PartComp's own relative transform (no attach parent) already equals its
			// component-space transform relative to Body. Convert that into a BONE-relative socket transform.
			const FTransform Rel = PC->GetRelativeTransform();
			const FTransform SocketRel = Rel.GetRelativeTransform(RootBoneCS);
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
