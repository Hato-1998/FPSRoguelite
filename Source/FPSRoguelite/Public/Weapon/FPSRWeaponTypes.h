// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FPSRWeaponTypes.generated.h"

/** Weapon firing archetypes. P1 uses FullAuto and Melee; others are placeholders for later phases. */
UENUM(BlueprintType)
enum class EFPSRWeaponArchetype : uint8
{
	FullAuto,
	Burst,
	AOE,
	Melee,
	ChargeLaser,
	Sniper,
	Shotgun
};

/** Per-weapon stats. In P1 these come straight from the weapon DataAsset (no per-instance modifiers yet). */
USTRUCT(BlueprintType)
struct FPSROGUELITE_API FFPSRWeaponStatBlock
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon")
	float Damage = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon")
	float FireRate = 8.0f; // shots per second

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon")
	float Range = 10000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon")
	int32 MagSize = 30;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon")
	float SpreadDegrees = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon")
	float MeleeRadius = 175.0f; // used by melee archetype
};
