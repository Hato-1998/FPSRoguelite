// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRCardEntryWidget.h"
#include "Card/FPSRCardDataAsset.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Core/FPSRLogChannels.h"

void UFPSRCardEntryWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// One-time binding. NativeConstruct runs again whenever the activatable stack re-shows a pooled widget,
	// which would re-AddDynamic the same (this, OnSelectButtonPressed) pair and trip the duplicate ensure.
	if (SelectButton)
	{
		SelectButton->OnClicked.AddDynamic(this, &UFPSRCardEntryWidget::OnSelectButtonPressed);
	}
}

void UFPSRCardEntryWidget::SetCardDraw(const FFPSRCardDraw& Draw, int32 InCardIndex)
{
	CachedDraw = Draw;
	CachedCardIndex = InCardIndex;
	UpdateDisplay();
}

void UFPSRCardEntryWidget::OnSelectButtonPressed()
{
	if (CachedCardIndex >= 0)
	{
		OnCardSelected.Broadcast(CachedCardIndex);
	}
}

void UFPSRCardEntryWidget::UpdateDisplay()
{
	const UFPSRCardDataAsset* CardAsset = CachedDraw.Card.Get();

	if (!CardAsset)
	{
		if (CardNameText) CardNameText->SetText(FText::FromString(TEXT("[Missing]")));
		if (RarityText) RarityText->SetText(FText::FromString(TEXT("")));
		if (DescriptionText) DescriptionText->SetText(FText::FromString(TEXT("")));
		if (MagnitudeText) MagnitudeText->SetText(FText::FromString(TEXT("")));
		return;
	}

	// Display card name
	if (CardNameText)
	{
		CardNameText->SetText(CardAsset->DisplayName);
	}

	// Display rarity
	if (RarityText)
	{
		switch (CachedDraw.Rarity)
		{
			case ECardRarity::Common: RarityText->SetText(FText::FromString(TEXT("Common"))); break;
			case ECardRarity::Rare: RarityText->SetText(FText::FromString(TEXT("Rare"))); break;
			case ECardRarity::Epic: RarityText->SetText(FText::FromString(TEXT("Epic"))); break;
			case ECardRarity::Legendary: RarityText->SetText(FText::FromString(TEXT("Legendary"))); break;
			default: RarityText->SetText(FText::FromString(TEXT("?"))); break;
		}
	}

	// Display description
	if (DescriptionText)
	{
		DescriptionText->SetText(CardAsset->Description);
	}

	// Display magnitude
	if (MagnitudeText)
	{
		const FString MagStr = FString::Printf(TEXT("+%.0f"), CachedDraw.Magnitude);
		MagnitudeText->SetText(FText::FromString(MagStr));
	}
}
