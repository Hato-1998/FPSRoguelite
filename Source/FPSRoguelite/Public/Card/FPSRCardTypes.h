// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FPSRCardTypes.generated.h"

UENUM(BlueprintType)
enum class ECardScope : uint8
{
	Character   UMETA(DisplayName = "Character"),
	ThisWeapon  UMETA(DisplayName = "This Weapon"),
	AllWeapons  UMETA(DisplayName = "All Weapons")
};

UENUM(BlueprintType)
enum class ECardRarity : uint8
{
	Common    UMETA(DisplayName = "Common"),
	Rare      UMETA(DisplayName = "Rare"),
	Epic      UMETA(DisplayName = "Epic"),
	Legendary UMETA(DisplayName = "Legendary")
};
