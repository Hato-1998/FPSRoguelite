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
