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
