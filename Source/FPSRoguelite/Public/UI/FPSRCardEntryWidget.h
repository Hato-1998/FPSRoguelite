// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonUserWidget.h"
#include "Card/FPSRCardTypes.h"
#include "FPSRCardEntryWidget.generated.h"

class UFPSRCardDataAsset;
class UTextBlock;
class UButton;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCardEntrySelected, int32, CardIndex);

/** Individual card entry widget. Displays card name, rarity, description, and magnitude.
 *  Placeholder styling (Game.MD §3-C). */
UCLASS()
class FPSROGUELITE_API UFPSRCardEntryWidget : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	/** Set the card data to display. */
	void SetCardDraw(const FFPSRCardDraw& Draw, int32 InCardIndex);

	UPROPERTY(BlueprintAssignable, Category = "FPSR|Card")
	FOnCardEntrySelected OnCardSelected;

	/** Label shown in the rarity slot for behavior-fragment cards (which have no meaningful rarity).
	 *  Designer-overridable per WBP. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPSR|Card")
	FText FragmentCategoryText = NSLOCTEXT("FPSRCardEntry", "FragmentCategory", "Weapon Modifier");

protected:
	/** Bind the select button here (runs once per instance) — NOT NativeConstruct, which the CommonUI
	 *  activatable stack calls every time a pooled/reused widget is shown, causing a duplicate AddDynamic. */
	virtual void NativeOnInitialized() override;

	/** Called when select button is pressed. */
	UFUNCTION()
	void OnSelectButtonPressed();

	/** Update display from cached draw data. */
	void UpdateDisplay();

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CardNameText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RarityText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DescriptionText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MagnitudeText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> SelectButton;

	FFPSRCardDraw CachedDraw;
	int32 CachedCardIndex = -1;
};
