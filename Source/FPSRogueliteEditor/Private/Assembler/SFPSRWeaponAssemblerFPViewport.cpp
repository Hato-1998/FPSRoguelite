// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assembler/SFPSRWeaponAssemblerFPViewport.h"

#include "Assembler/FPSRWeaponAssemblerFPViewportClient.h"

#include "PreviewScene.h"

void SFPSRWeaponAssemblerFPViewport::Construct(const FArguments& InArgs, const TSharedRef<FPreviewScene>& InPreviewScene,
                                               const TWeakPtr<SFPSRWeaponAssemblerTab>& InAssemblerTab)
{
	PreviewScene = InPreviewScene;
	AssemblerTab = InAssemblerTab;

	SEditorViewport::Construct(SEditorViewport::FArguments());
}

TSharedRef<FEditorViewportClient> SFPSRWeaponAssemblerFPViewport::MakeEditorViewportClient()
{
	FPViewportClient = MakeShared<FFPSRWeaponAssemblerFPViewportClient>(*PreviewScene, SharedThis(this), AssemblerTab);
	return FPViewportClient.ToSharedRef();
}
