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

/** Trigger behavior for the weapon fire component. */
UENUM(BlueprintType)
enum class EFPSRFireMode : uint8
{
	Single,
	Burst,
	FullAuto
};

/** When the view auto-recovers from vertical recoil after firing stops.
 *  Auto = recover only for single-shot weapons (snipers/railguns); rapid-fire requires manual pull-down.
 *  Reserved for a later weapon-upgrade that can unlock recovery on any weapon. */
UENUM(BlueprintType)
enum class ERecoilRecovery : uint8
{
	Auto,
	Always,
	Never
};

/** Per-weapon stats. In P1 these come straight from the weapon DataAsset (no per-instance modifiers yet). */
USTRUCT(BlueprintType)
struct FPSROGUELITE_API FFPSRWeaponStatBlock
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon")
	float Damage = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Fire")
	EFPSRFireMode FireMode = EFPSRFireMode::FullAuto;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Fire")
	float FireRate = 8.0f; // shots per second

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Fire")
	int32 BurstCount = 3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon")
	float Range = 10000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Ammo")
	int32 MagSize = 30;

	// --- Spread / bloom ---
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Spread")
	float SpreadDegrees = 1.0f; // base half-angle

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Spread")
	float BloomPerShot = 0.3f; // added spread per shot

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Spread")
	float MaxBloom = 4.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Spread")
	float BloomRecoveryRate = 6.0f; // degrees per second

	// --- Recoil (camera kick, degrees) ---
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Recoil")
	float RecoilVertical = 1.0f; // up kick per shot

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Recoil")
	float RecoilHorizontal = 0.3f; // random +/- side kick per shot

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Recoil")
	ERecoilRecovery RecoilRecovery = ERecoilRecovery::Auto; // Auto: single-shot recovers, rapid-fire doesn't

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Recoil")
	float RecoilRecoveryRate = 10.0f; // degrees per second recovered when not firing

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Recoil")
	float RecoilRiseRate = 25.0f; // how fast the per-shot up-kick is applied (deg/sec, snappy rise)

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Recoil")
	float RecoilHorizontalPatternFreq = 0.6f; // horizontal pattern frequency (radians per shot)

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Recoil")
	float RecoilHorizontalVariance = 0.25f; // random horizontal variation as a fraction of RecoilHorizontal (0..1)

	// --- Melee ---
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Melee")
	float MeleeRadius = 175.0f;
};
