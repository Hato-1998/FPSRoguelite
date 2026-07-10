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

namespace
{
	/** Derives the mount-socket name from a part's static mesh asset name: strips the pack's modular-part prefix
	 *  and adds SOCKET_Mount_ (e.g. SM_Wep_Mod_A_Barrel_01 -> SOCKET_Mount_Barrel_01). Falls back to
	 *  SOCKET_Mount_Part<Index> when the part reference is null (index supplied by the caller). */
	FName MakeMountSocketName(const TSoftObjectPtr<UStaticMesh>& Part, int32 Index)
	{
		if (Part.IsNull())
		{
			return FName(*FString::Printf(TEXT("SOCKET_Mount_Part%d"), Index));
		}

		FString Name = Part.GetAssetName();
		Name.RemoveFromStart(TEXT("SM_Wep_Mod_A_"));
		Name.RemoveFromStart(TEXT("SM_Wep_Mod_"));
		return FName(*FString::Printf(TEXT("SOCKET_Mount_%s"), *Name));
	}
}

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

		const FName RootBone = Body->GetRefSkeleton().GetNum() > 0 ? Body->GetRefSkeleton().GetBoneName(0) : NAME_None;

		Body->Modify();
		DA->Modify();

		int32 n = 0;
		const int32 Count = FMath::Min(Preview->PartComponents.Num(), DA->WeaponParts1P.Num());
		for (int32 i = 0; i < Count; ++i)
		{
			UStaticMeshComponent* PC = Preview->PartComponents[i];
			if (!PC)
			{
				continue;
			}

			const FTransform Rel = PC->GetRelativeTransform(); // relative to BodyMesh (attach parent)
			const FName SocketName = MakeMountSocketName(DA->WeaponParts1P[i].Part, i);

			USkeletalMeshSocket* Socket = Body->FindSocket(SocketName);
			if (!Socket)
			{
				Socket = NewObject<USkeletalMeshSocket>(Body);
				Socket->SocketName = SocketName;
				Body->AddSocket(Socket, false);
			}
			Socket->BoneName = RootBone;
			Socket->RelativeLocation = Rel.GetLocation();
			Socket->RelativeRotation = Rel.GetRotation().Rotator();
			Socket->RelativeScale = FVector(1.0f);

			DA->WeaponParts1P[i].Socket = SocketName;
			DA->WeaponParts1P[i].Offset = FTransform::Identity;
			++n;
		}

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
