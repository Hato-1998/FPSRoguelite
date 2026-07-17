// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRUITags.h"

// Native definitions for the UI-layer tags. Registering them here (with the DevComment as the second arg) makes
// C++ the single source of truth; the former Config/DefaultGameplayTags.ini "UI.Layer.*" entries are removed so the
// tag exists exactly once.
namespace FPSRUITags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_UI_Layer_Game, "UI.Layer.Game", "PrimaryGameLayout: gameplay HUD layer");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_UI_Layer_GameMenu, "UI.Layer.GameMenu", "PrimaryGameLayout: in-game menu layer");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_UI_Layer_Menu, "UI.Layer.Menu", "PrimaryGameLayout: front-end / lobby menu layer");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_UI_Layer_Modal, "UI.Layer.Modal", "PrimaryGameLayout: modal (card select) layer");
}
