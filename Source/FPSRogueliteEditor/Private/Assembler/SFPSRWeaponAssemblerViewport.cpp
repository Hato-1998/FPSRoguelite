// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assembler/SFPSRWeaponAssemblerViewport.h"

#include "Assembler/FPSRWeaponAssemblerViewportClient.h"

#include "PreviewScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"

void SFPSRWeaponAssemblerViewport::Construct(const FArguments& InArgs, const TSharedRef<FPreviewScene>& InPreviewScene)
{
	PreviewScene = InPreviewScene;

	SEditorViewport::Construct(SEditorViewport::FArguments());
}

TSharedRef<FEditorViewportClient> SFPSRWeaponAssemblerViewport::MakeEditorViewportClient()
{
	AssemblerViewportClient = MakeShared<FFPSRWeaponAssemblerViewportClient>(*PreviewScene, SharedThis(this));
	return AssemblerViewportClient.ToSharedRef();
}

void SFPSRWeaponAssemblerViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (!AssemblerViewportClient.IsValid())
	{
		return;
	}

	// Local TObjectPtr temporaries so this goes through the non-deprecated FReferenceCollector overloads (the
	// client itself exposes raw pointers, matching FStaticMeshEditorViewportClient's own GetXxx() accessors).
	TObjectPtr<USkeletalMeshComponent> Body = AssemblerViewportClient->GetBodyComp();
	Collector.AddReferencedObject(Body);

	for (UStaticMeshComponent* PartComp : AssemblerViewportClient->GetPartComps())
	{
		TObjectPtr<UStaticMeshComponent> Part = PartComp;
		Collector.AddReferencedObject(Part);
	}
}
