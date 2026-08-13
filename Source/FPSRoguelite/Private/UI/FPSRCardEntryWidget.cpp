// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FPSRCardEntryWidget.h"
#include "Card/FPSRCardDataAsset.h"
#include "Card/FPSRCardEffect.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Core/FPSRLogChannels.h"
#include "Internationalization/StringTableRegistry.h"

UFPSRCardEntryWidget::UFPSRCardEntryWidget()
{
	// StringTable-backed default (Docs/SSOT/Localization.md L-2, CardEffect namespace) — kept out of the header's
	// default member initializer so the widget's public header doesn't need StringTableRegistry.h.
	FragmentCategoryText = LOCTABLE("CardEffect", "Fmt.FragmentCategory");
}

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
		if (CardNameText) CardNameText->SetText(LOCTABLE("UI", "Widget.CardEntry.Missing"));
		if (RarityText) RarityText->SetText(FText::GetEmpty());
		if (DescriptionText) DescriptionText->SetText(FText::GetEmpty());
		if (MagnitudeText) MagnitudeText->SetText(FText::GetEmpty());
		return;
	}

	// Card name.
	if (CardNameText)
	{
		CardNameText->SetText(CardAsset->DisplayName);
	}

	// Walk the effects (v2): collect each effect's auto-generated description line (§2-3-8) and detect whether any
	// effect carries a magnitude. A card whose effects carry no magnitude (pure behavior/fragment) shows a category
	// label in the rarity slot — matching v1's fragment-card display — while value cards show the rolled rarity.
	TArray<FText> EffectLines;
	EffectLines.Reserve(CardAsset->Effects.Num());
	bool bAnyMagnitude = false;
	for (const TObjectPtr<UFPSRCardEffect>& Effect : CardAsset->Effects)
	{
		if (!Effect)
		{
			continue;
		}
		if (Effect->RarityTiers.Num() > 0)
		{
			bAnyMagnitude = true;
		}
		const float Magnitude = Effect->ResolveMagnitude(CachedDraw.Rarity);
		const FText Line = Effect->GetDescription(CachedDraw.Rarity, Magnitude);
		if (!Line.IsEmpty())
		{
			EffectLines.Add(Line);
		}
	}

	// Rarity slot: show the rolled rarity only for value cards that actually produced a value line; otherwise the
	// designer category label. Keying on EffectLines (not bAnyMagnitude alone) stops a magnitude card that resolves
	// to no visible value at the rolled rarity from showing a rarity badge over a blank value slot.
	if (RarityText)
	{
		const bool bShowRarity = bAnyMagnitude && EffectLines.Num() > 0;
		if (!bShowRarity)
		{
			RarityText->SetText(FragmentCategoryText);
		}
		else
		{
			switch (CachedDraw.Rarity)
			{
				case ECardRarity::Common: RarityText->SetText(LOCTABLE("CardEffect", "Rarity.Common")); break;
				case ECardRarity::Rare: RarityText->SetText(LOCTABLE("CardEffect", "Rarity.Rare")); break;
				case ECardRarity::Epic: RarityText->SetText(LOCTABLE("CardEffect", "Rarity.Epic")); break;
				case ECardRarity::Legendary: RarityText->SetText(LOCTABLE("CardEffect", "Rarity.Legendary")); break;
				default: RarityText->SetText(LOCTABLE("CardEffect", "Rarity.Unknown")); break;
			}
		}
	}

	// Description slot: the authored Description (kept).
	if (DescriptionText)
	{
		DescriptionText->SetText(CardAsset->Description);
	}

	// Value slot: the auto-generated per-effect lines, newline-joined (multi-effect aware, e.g. "FireRate +25%"
	// over "Damage -10%"). Replaces the v1 single hard-read magnitude — no Scope / WeaponStatOp branching here.
	if (MagnitudeText)
	{
		MagnitudeText->SetText(FText::Join(FText::FromString(TEXT("\n")), EffectLines));
	}

	// Target weapon slot: show the weapon name if this card targets a specific weapon, else hide.
	if (TargetWeaponText)
	{
		if (CachedDraw.TargetWeapon)
		{
			TargetWeaponText->SetText(CachedDraw.TargetWeapon->DisplayName);
			TargetWeaponText->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			TargetWeaponText->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// Source-pool label: which pool this card was drawn from — the target weapon's name for weapon-scoped cards,
	// or the character-pool fallback otherwise (Fmt.WeaponStat's AllWeaponsSuffix used to carry this distinction;
	// this label replaces it, always visible rather than collapsing).
	if (SourcePoolText)
	{
		SourcePoolText->SetText(CachedDraw.TargetWeapon
			? CachedDraw.TargetWeapon->DisplayName
			: LOCTABLE("UI", "Widget.CardEntry.SourceCharacter"));
	}
}
