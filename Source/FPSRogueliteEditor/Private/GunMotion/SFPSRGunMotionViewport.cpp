// Copyright Epic Games, Inc. All Rights Reserved.

#include "GunMotion/SFPSRGunMotionViewport.h"

#include "GunMotion/FPSRGunMotionViewportClient.h"

#include "PreviewScene.h"

void SFPSRGunMotionViewport::Construct(const FArguments& InArgs, const TSharedRef<FPreviewScene>& InPreviewScene)
{
	PreviewScene = InPreviewScene;

	SEditorViewport::Construct(SEditorViewport::FArguments());
}

TSharedRef<FEditorViewportClient> SFPSRGunMotionViewport::MakeEditorViewportClient()
{
	GunMotionViewportClient = MakeShared<FFPSRGunMotionViewportClient>(*PreviewScene, SharedThis(this));
	return GunMotionViewportClient.ToSharedRef();
}
