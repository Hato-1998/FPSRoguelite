// Copyright Epic Games, Inc. All Rights Reserved.

#include "Card/FPSRCardPoolDataAsset.h"

float UFPSRCardPoolDataAsset::GetRarityBaseWeight(ECardRarity Rarity) const
{
	switch (Rarity)
	{
		case ECardRarity::Common:
			return CommonWeight;
		case ECardRarity::Rare:
			return RareWeight;
		case ECardRarity::Epic:
			return EpicWeight;
		case ECardRarity::Legendary:
			return LegendaryWeight;
		default:
			return CommonWeight;
	}
}

float UFPSRCardPoolDataAsset::GetLuckPerRarity(ECardRarity Rarity) const
{
	switch (Rarity)
	{
		case ECardRarity::Common:
			return LuckPerRarity_Common;
		case ECardRarity::Rare:
			return LuckPerRarity_Rare;
		case ECardRarity::Epic:
			return LuckPerRarity_Epic;
		case ECardRarity::Legendary:
			return LuckPerRarity_Legendary;
		default:
			return LuckPerRarity_Common;
	}
}

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#include "Card/FPSRCardDataAsset.h"

#define LOCTEXT_NAMESPACE "FPSRCardPoolDataAsset"

EDataValidationResult UFPSRCardPoolDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// Reject two distinct cards sharing a CardId within this pool — the meta save keys unlocks by CardId, so a
	// collision would make unlocking one card alias the other. (Empty CardId is flagged on the card itself.)
	TMap<FName, const UFPSRCardDataAsset*> SeenKeys;
	auto CheckList = [&](const TArray<TObjectPtr<UFPSRCardDataAsset>>& List)
	{
		for (const TObjectPtr<UFPSRCardDataAsset>& Card : List)
		{
			if (!Card || Card->CardId.IsNone())
			{
				continue; // null / empty handled elsewhere
			}
			if (const UFPSRCardDataAsset* const* Existing = SeenKeys.Find(Card->CardId))
			{
				Context.AddError(FText::Format(
					LOCTEXT("DuplicateCardId", "Two cards in this pool share CardId '{0}' ({1} and {2}) — CardIds must be unique so the meta save can key unlocks."),
					FText::FromName(Card->CardId),
					FText::FromString((*Existing)->GetName()),
					FText::FromString(Card->GetName())));
				Result = EDataValidationResult::Invalid;
			}
			else
			{
				SeenKeys.Add(Card->CardId, Card);
			}
		}
	};
	CheckList(Cards);
	CheckList(WeaponUnlockCards);

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
