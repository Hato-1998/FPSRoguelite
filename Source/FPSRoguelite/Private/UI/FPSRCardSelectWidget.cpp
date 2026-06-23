// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRCardSelectWidget.h"
#include "UI/FPSRCardEntryWidget.h"
#include "Core/FPSRPlayerController.h"
#include "Core/FPSRPlayerState.h"
#include "Input/UIActionBindingHandle.h"
#include "CommonInputModeTypes.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

void UFPSRCardSelectWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	CardEntries.Reset();
	if (CardEntry_0) { CardEntries.Add(CardEntry_0); }
	if (CardEntry_1) { CardEntries.Add(CardEntry_1); }
	if (CardEntry_2) { CardEntries.Add(CardEntry_2); }

	for (UFPSRCardEntryWidget* Entry : CardEntries)
	{
		if (Entry)
		{
			Entry->OnCardSelected.AddDynamic(this, &UFPSRCardSelectWidget::OnCardEntrySelected);
		}
	}

	if (RerollButton)
	{
		RerollButton->OnClicked.AddDynamic(this, &UFPSRCardSelectWidget::OnRerollPressed);
	}

	// Keep the reroll count fresh even when the charge decrement replicates after the offer arrives.
	if (AFPSRPlayerState* PS = GetOwningPlayerState<AFPSRPlayerState>())
	{
		PS->OnRerollChargesChanged.AddDynamic(this, &UFPSRCardSelectWidget::UpdateRerollCharges);
	}
}

void UFPSRCardSelectWidget::NativeDestruct()
{
	// The reroll-charge delegate lives on the PlayerState (which outlives this widget) — remove the binding on
	// teardown to match the Lobby/RunHUD widgets' cleanup convention. The owned CardEntry/Button delegates die with
	// their sub-widgets, so only the PlayerState binding needs explicit removal.
	if (AFPSRPlayerState* PS = GetOwningPlayerState<AFPSRPlayerState>())
	{
		PS->OnRerollChargesChanged.RemoveDynamic(this, &UFPSRCardSelectWidget::UpdateRerollCharges);
	}

	Super::NativeDestruct();
}

TOptional<FUIInputConfig> UFPSRCardSelectWidget::GetDesiredInputConfig() const
{
	return FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture);
}

void UFPSRCardSelectWidget::SetCardOffers(int32 OfferId, const TArray<FFPSRCardDraw>& InOffers)
{
	ShownOfferId = OfferId;

	for (int32 i = 0; i < CardEntries.Num(); ++i)
	{
		UFPSRCardEntryWidget* Entry = CardEntries[i];
		if (!Entry)
		{
			continue;
		}

		if (InOffers.IsValidIndex(i))
		{
			Entry->SetCardDraw(InOffers[i], i);
			Entry->SetVisibility(ESlateVisibility::Visible);
		}
		else
		{
			Entry->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	UpdateRerollCharges();
}

void UFPSRCardSelectWidget::UpdateRerollCharges()
{
	if (!RerollChargesText)
	{
		return;
	}

	const int32 Charges = GetOwningPlayerState<AFPSRPlayerState>() ? GetOwningPlayerState<AFPSRPlayerState>()->GetRunRerollCharges() : 0;
	RerollChargesText->SetText(FText::AsNumber(Charges));
}

void UFPSRCardSelectWidget::OnCardEntrySelected(int32 CardIndex)
{
	// Send intent only — the server validates the index + offer id against its cached offer and applies.
	if (AFPSRPlayerController* PC = GetOwningPlayer<AFPSRPlayerController>())
	{
		PC->ServerSelectCard(CardIndex, ShownOfferId);
	}
}

void UFPSRCardSelectWidget::OnRerollPressed()
{
	if (AFPSRPlayerController* PC = GetOwningPlayer<AFPSRPlayerController>())
	{
		PC->ServerRerollOffer(ShownOfferId);
	}
}
