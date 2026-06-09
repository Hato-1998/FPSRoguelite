// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRGameHUDWidget.h"
#include "Input/UIActionBindingHandle.h"
#include "CommonInputModeTypes.h"

TOptional<FUIInputConfig> UFPSRGameHUDWidget::GetDesiredInputConfig() const
{
	// Game-playing state: capture mouse for look, hide cursor, route input to the game. A modal pushed above
	// this widget overrides it (Menu config) and restores this one when dismissed.
	return FUIInputConfig(ECommonInputMode::Game, EMouseCaptureMode::CapturePermanently);
}
