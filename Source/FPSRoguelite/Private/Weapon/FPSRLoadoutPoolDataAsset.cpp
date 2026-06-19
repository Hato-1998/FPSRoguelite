// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRLoadoutPoolDataAsset.h"
#include "Weapon/FPSRWeaponDataAsset.h"

UFPSRWeaponDataAsset* UFPSRLoadoutPoolDataAsset::GetWeaponAt(int32 Index) const
{
	return SelectableWeapons.IsValidIndex(Index) ? SelectableWeapons[Index] : nullptr;
}

bool UFPSRLoadoutPoolDataAsset::IsValidIndex(int32 Index) const
{
	return SelectableWeapons.IsValidIndex(Index) && SelectableWeapons[Index] != nullptr;
}
