// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRPrimaryGameLayout.h"
#include "UI/FPSRUITags.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "CommonActivatableWidget.h"

void UFPSRPrimaryGameLayout::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (Layer_Game)
	{
		RegisterLayer(FPSRUITags::TAG_UI_Layer_Game.GetTag(), Layer_Game);
	}
	if (Layer_GameMenu)
	{
		RegisterLayer(FPSRUITags::TAG_UI_Layer_GameMenu.GetTag(), Layer_GameMenu);
	}
	if (Layer_Menu)
	{
		RegisterLayer(FPSRUITags::TAG_UI_Layer_Menu.GetTag(), Layer_Menu);
	}
	if (Layer_Modal)
	{
		RegisterLayer(FPSRUITags::TAG_UI_Layer_Modal.GetTag(), Layer_Modal);
	}
}

void UFPSRPrimaryGameLayout::RegisterLayer(FGameplayTag LayerTag, UCommonActivatableWidgetStack* Stack)
{
	if (LayerTag.IsValid() && Stack)
	{
		Layers.Add(LayerTag, Stack);
	}
}

UCommonActivatableWidgetStack* UFPSRPrimaryGameLayout::GetLayerStack(FGameplayTag LayerTag) const
{
	const TObjectPtr<UCommonActivatableWidgetStack>* Found = Layers.Find(LayerTag);
	return Found ? *Found : nullptr;
}

UCommonActivatableWidget* UFPSRPrimaryGameLayout::PushWidgetToLayer(FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> WidgetClass)
{
	if (!WidgetClass)
	{
		return nullptr;
	}

	UCommonActivatableWidgetStack* Stack = GetLayerStack(LayerTag);
	if (!Stack)
	{
		return nullptr;
	}

	return Stack->AddWidget<UCommonActivatableWidget>(WidgetClass);
}

void UFPSRPrimaryGameLayout::RemoveWidgetFromLayer(UCommonActivatableWidget* Widget)
{
	if (!Widget)
	{
		return;
	}

	for (const auto& Pair : Layers)
	{
		if (Pair.Value)
		{
			Pair.Value->RemoveWidget(*Widget);
		}
	}
}
