// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRColorPresetButton.h"

void UFPSRColorPresetButton::InitPresetButton(int32 InPresetIndex)
{
	PresetIndex = InPresetIndex;

	// Bind to self: the inherited dynamic OnClicked can only reach a UFUNCTION, and it passes no sender — so this
	// button converts its own click into the native, index-carrying OnPresetClicked the owning widget listens to.
	OnClicked.AddDynamic(this, &UFPSRColorPresetButton::HandleClickedInternal);
}

void UFPSRColorPresetButton::HandleClickedInternal()
{
	OnPresetClicked.Broadcast(PresetIndex);
}
