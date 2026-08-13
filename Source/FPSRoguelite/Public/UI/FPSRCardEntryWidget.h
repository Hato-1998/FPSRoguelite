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
	UFPSRCardEntryWidget();

	/** Set the card data to display. */
	void SetCardDraw(const FFPSRCardDraw& Draw, int32 InCardIndex);

	UPROPERTY(BlueprintAssignable, Category = "FPSR|Card")
	FOnCardEntrySelected OnCardSelected;

	/** Label shown in the rarity slot for behavior-fragment cards (which have no meaningful rarity).
	 *  Designer-overridable per WBP. Default set in the .cpp constructor (LOCTABLE reads the CardEffect string table,
	 *  which would drag Internationalization/StringTableRegistry.h into this header if resolved as a default member
	 *  initializer here — Docs/SSOT/Localization.md). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPSR|Card")
	FText FragmentCategoryText;

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

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TargetWeaponText;

	/** Source-pool label: the target weapon's display name for weapon-scoped cards, or the "Character" fallback
	 *  (Widget.CardEntry.SourceCharacter) for character-pool cards. Optional — not yet placed in WBP_CardEntry
	 *  (A-4~6 WBP pass); left unbound this stays null and UpdateDisplay skips it. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SourcePoolText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> SelectButton;

	FFPSRCardDraw CachedDraw;
	int32 CachedCardIndex = -1;
};
