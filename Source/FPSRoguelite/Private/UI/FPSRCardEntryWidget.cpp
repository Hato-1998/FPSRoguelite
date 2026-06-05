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

	// Display magnitude. Weapon-scope percent modifiers show as a signed percentage ("+25%", "-25%");
	// flat values show as an integer when whole, otherwise with up to 2 decimals — never truncating a
	// fractional magnitude (e.g. +0.25 / +0.05) down to a misleading "+0".
	if (MagnitudeText)
	{
		const float Mag = CachedDraw.Magnitude;
		const bool bWeaponScope = (CardAsset->Scope == ECardScope::ThisWeapon || CardAsset->Scope == ECardScope::AllWeapons);

		FString MagStr;
		if (bWeaponScope && CardAsset->WeaponStatOp == EFPSRWeaponModOp::PercentMultiply)
		{
			MagStr = FString::Printf(TEXT("%+d%%"), FMath::RoundToInt(Mag * 100.0f));
		}
		else if (FMath::IsNearlyEqual(Mag, FMath::RoundToFloat(Mag)))
		{
			MagStr = FString::Printf(TEXT("%+d"), FMath::RoundToInt(Mag));
		}
		else
		{
			MagStr = FString::Printf(TEXT("%+.2f"), Mag);
		}
		MagnitudeText->SetText(FText::FromString(MagStr));
	}
}
