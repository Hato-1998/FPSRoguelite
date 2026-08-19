// Copyright Epic Games, Inc. All Rights Reserved.

#include "Run/FPSRStageFadeCameraModifier.h"
#include "Materials/MaterialInstanceDynamic.h"

void UFPSRStageFadeCameraModifier::SetFadeState(UMaterialInstanceDynamic* InFadeMID, float InAlpha)
{
	FadeMID = InFadeMID;
	CurrentAlpha = InAlpha;
}

void UFPSRStageFadeCameraModifier::ModifyPostProcess(float DeltaTime, float& PostProcessBlendWeight, FPostProcessSettings& PostProcessSettings)
{
	// Alpha<=0 (or no material yet resolved): leave PostProcessBlendWeight at its caller-supplied 0 and add nothing
	// — UCameraModifier::ModifyCamera only forwards this struct to AddCachedPPBlend when the weight is > 0
	// (CameraModifier.cpp), so this is the actual zero-overhead no-op path, not just an empty-looking one.
	if (CurrentAlpha <= 0.0f || !FadeMID)
	{
		return;
	}

	// The material itself is what carries the fade — FadeAlpha (set by UFPSRStageFadeSubsystem's Tick) is the
	// value that actually drives the visible fade, so this blend weight only needs to say "fully apply this PP
	// settings struct", not re-encode the alpha a second time.
	PostProcessBlendWeight = 1.0f;
	PostProcessSettings.AddBlendable(FadeMID, 1.0f);
}
