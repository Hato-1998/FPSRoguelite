// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonUserWidget.h"
#include "GameplayTagContainer.h"
#include "FPSRPrimaryGameLayout.generated.h"

class UCommonActivatableWidget;
class UCommonActivatableWidgetStack;

/** Lightweight PrimaryGameLayout (Lyra pattern, no CommonGame plugin). Hosts 4 named layer stacks
 *  (Game/GameMenu/Menu/Modal) registered by UI.Layer.* gameplay tag; widgets are pushed/popped per layer. */
UCLASS(Abstract)
class FPSROGUELITE_API UFPSRPrimaryGameLayout : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	/** Push an activatable widget class onto the stack registered to LayerTag. Returns the instance (or nullptr). */
	UCommonActivatableWidget* PushWidgetToLayer(FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> WidgetClass);

	/** Remove a previously pushed widget from whichever layer holds it. */
	void RemoveWidgetFromLayer(UCommonActivatableWidget* Widget);

	/** Find the stack registered to a layer tag (nullptr if none). */
	UCommonActivatableWidgetStack* GetLayerStack(FGameplayTag LayerTag) const;

protected:
	virtual void NativeOnInitialized() override;

	/** Register a stack under a layer tag (called for each bound stack in NativeOnInitialized). */
	void RegisterLayer(FGameplayTag LayerTag, UCommonActivatableWidgetStack* Stack);

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCommonActivatableWidgetStack> Layer_Game;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCommonActivatableWidgetStack> Layer_GameMenu;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCommonActivatableWidgetStack> Layer_Menu;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCommonActivatableWidgetStack> Layer_Modal;

private:
	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UCommonActivatableWidgetStack>> Layers;
};
