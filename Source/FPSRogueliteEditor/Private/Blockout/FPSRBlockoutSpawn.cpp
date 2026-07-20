// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blockout/FPSRBlockoutSpawn.h"

#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Blueprint.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"

AActor* FFPSRBlockoutSpawn::SpawnPiece(UWorld* World, const FAssetData& Asset, const FTransform& Transform, bool bTransientGhost)
{
	if (!World || !Asset.IsValid())
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	if (bTransientGhost)
	{
		SpawnParams.ObjectFlags |= RF_Transient;
		SpawnParams.bTemporaryEditorActor = true;   // excluded from outliner / save
	}
	else
	{
		SpawnParams.ObjectFlags |= RF_Transactional;
	}

	AActor* NewActor = nullptr;
	const bool bIsBP = (Asset.AssetClassPath == UBlueprint::StaticClass()->GetClassPathName());
	if (bIsBP)
	{
		UBlueprint* BP = Cast<UBlueprint>(Asset.GetAsset());
		if (!BP || !BP->GeneratedClass || !BP->GeneratedClass->IsChildOf(AActor::StaticClass()))
		{
			return nullptr;
		}
		NewActor = World->SpawnActor<AActor>(BP->GeneratedClass, Transform.GetLocation(), Transform.Rotator(), SpawnParams);
	}
	else
	{
		UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset());
		if (!Mesh)
		{
			return nullptr;
		}
		AStaticMeshActor* MeshActor = World->SpawnActor<AStaticMeshActor>(Transform.GetLocation(), Transform.Rotator(), SpawnParams);
		if (MeshActor && MeshActor->GetStaticMeshComponent())
		{
			if (!bTransientGhost)
			{
				MeshActor->GetStaticMeshComponent()->Modify();
			}
			MeshActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
			if (!bTransientGhost)
			{
				// K4=B / K14 guardrail: placed blockout meshes are WorldStatic + block-all so the flow-field's obstacle
				// mask (BuildObstacleMask ECC_WorldStatic downtrace) treats them as obstacles. The engine's standard
				// "BlockAll" profile is exactly WorldStatic object type + QueryAndPhysics + block-all (BaseEngine.ini).
				MeshActor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			}
		}
		NewActor = MeshActor;
	}

	if (!NewActor)
	{
		return nullptr;
	}

	if (bTransientGhost)
	{
		NewActor->bIsEditorPreviewActor = true;
		NewActor->SetActorEnableCollision(false);
	}
	else
	{
		NewActor->Modify();
		NewActor->SetActorLabel(Asset.AssetName.ToString());
		NewActor->SetFolderPath(TEXT("Blockout"));
	}

	return NewActor;
}
