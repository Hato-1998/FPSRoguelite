// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/** Native UI-layer gameplay tags — the single source of truth for the PrimaryGameLayout's four layer stacks
 *  (Lyra's native-tag pattern). Call sites reference these constants instead of re-typing "UI.Layer.X" string
 *  literals, so a typo is a compile error instead of a silent invalid-tag lookup. Defined in FPSRUITags.cpp;
 *  native registration replaces the former DefaultGameplayTags.ini entries (no ini duplication). */
namespace FPSRUITags
{
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_UI_Layer_Game);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_UI_Layer_GameMenu);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_UI_Layer_Menu);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_UI_Layer_Modal);
}
