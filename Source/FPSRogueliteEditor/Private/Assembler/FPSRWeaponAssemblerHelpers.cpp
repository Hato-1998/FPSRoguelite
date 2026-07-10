// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assembler/FPSRWeaponAssemblerHelpers.h"

#include "Assembler/FPSRWeaponAssemblerActor.h"
#include "Weapon/FPSRWeaponDataAsset.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetData.h"
#include "Editor.h"
#include "FileHelpers.h"   // UEditorLoadingAndSavingUtils::SavePackages (UnrealEd — avoids the EditorScriptingUtilities plugin)
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"

namespace FPSRWeaponAssemblerHelpers
{
	UFPSRWeaponDataAsset* GetSelectedWeaponDA()
	{
		FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		TArray<FAssetData> Selected;
		ContentBrowser.Get().GetSelectedAssets(Selected);

		for (const FAssetData& AssetData : Selected)
		{
			if (UFPSRWeaponDataAsset* DA = Cast<UFPSRWeaponDataAsset>(AssetData.GetAsset()))
			{
				return DA;
			}
		}
		return nullptr;
	}

	AFPSRWeaponAssemblerActor* SpawnPreview(UFPSRWeaponDataAsset* DA)
	{
		if (!DA || !GEditor)
		{
			return nullptr;
		}

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			return nullptr;
		}

		if (AFPSRWeaponAssemblerActor* Existing = FindPreview())
		{
			Existing->Destroy();
		}

		AFPSRWeaponAssemblerActor* A = World->SpawnActor<AFPSRWeaponAssemblerActor>(FVector(0.0f, 0.0f, 200.0f), FRotator::ZeroRotator);
		if (!A)
		{
			return nullptr;
		}

		A->BuildFromDA(DA);
		A->SetActorLabel(FString::Printf(TEXT("WeaponAssembler_%s"), *DA->GetName()));
		GEditor->SelectActor(A, true, true);
		return A;
	}

	AFPSRWeaponAssemblerActor* FindPreview()
	{
		if (!GEditor)
		{
			return nullptr;
		}

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			return nullptr;
		}

		for (TActorIterator<AFPSRWeaponAssemblerActor> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	int32 CaptureToSockets(AFPSRWeaponAssemblerActor* Preview)
	{
		if (!Preview)
		{
			return 0;
		}

		UFPSRWeaponDataAsset* DA = Preview->SourceDA;
		USkeletalMesh* Body = Preview->BodyMesh ? Preview->BodyMesh->GetSkeletalMeshAsset() : nullptr;
		if (!DA || !Body)
		{
			return 0;
		}

		const FReferenceSkeleton& RefSkel = Body->GetRefSkeleton();
		const FName RootBone = RefSkel.GetNum() > 0 ? RefSkel.GetBoneName(0) : NAME_None;
		// Sockets are BONE-relative. This pack's root bone carries a 90° roll, so convert the designer's component-
		// space placement through the root bone's component-space transform (bone 0 ref pose) — else the captured part
		// lands rotated at runtime.
		const FTransform RootBoneCS = RefSkel.GetRefBonePose().Num() > 0 ? RefSkel.GetRefBonePose()[0] : FTransform::Identity;

		Body->Modify();
		DA->Modify();

		// Clear the tool's previous sockets (it owns the SOCKET_Mount_* namespace) so a re-capture or a component
		// rename replaces them instead of leaving orphans.
		{
			TArray<TObjectPtr<USkeletalMeshSocket>>& MeshSockets = Body->GetMeshOnlySocketList();
			MeshSockets.RemoveAll([](const TObjectPtr<USkeletalMeshSocket>& S)
			{
				return S && S->SocketName.ToString().StartsWith(TEXT("SOCKET_Mount_"));
			});
		}

		int32 n = 0;
		const int32 Count = FMath::Min(Preview->PartComponents.Num(), DA->WeaponParts1P.Num());
		for (int32 i = 0; i < Count; ++i)
		{
			UStaticMeshComponent* PC = Preview->PartComponents[i];
			if (!PC)
			{
				continue;
			}

			const FTransform Rel = PC->GetRelativeTransform();                 // component-space (parent = body root)
			const FTransform SocketRel = Rel.GetRelativeTransform(RootBoneCS); // -> bone-relative
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
