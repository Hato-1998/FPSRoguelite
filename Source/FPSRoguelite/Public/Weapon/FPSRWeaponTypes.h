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

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Ammo")
	float ReloadTime = 1.5f; // seconds; reserve ammo is infinite (always refills to MagSize)

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
	float HipVerticalScale = 0.4f; // hip-fire vertical climb scale (weak; RecoilVertical * this)

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Recoil")
	float ADSVerticalScale = 1.0f; // ADS vertical climb scale (strong)

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Recoil")
	float HipHorizontalRandom = 0.9f; // hip-fire horizontal random fraction (high = scattered)

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Recoil")
	float ADSHorizontalRandom = 0.15f; // ADS horizontal random fraction (low = pattern shows)

	// --- Melee ---
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Melee")
	float MeleeRadius = 175.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Melee")
	float MeleeAttackDelay = 0.5f; // seconds between melee attacks (also rate-limits rapid clicks)

	// --- ADS (aim down sights) ---
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|ADS")
	bool bHasADS = false; // melee / no-ADS weapons leave this false

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|ADS")
	float ADSFieldOfView = 55.0f; // zoomed FOV while aiming (default camera ~90)

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|ADS")
	float ADSSpreadMultiplier = 0.4f; // spread scale while aiming (lower = tighter)

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|ADS")
	float ADSInterpSpeed = 14.0f; // FOV interpolation speed
};

/** Stat axis a weapon modifier targets. Maps 1:1 to an FFPSRWeaponStatBlock field (compile-checked switch in
 *  the resolver). P4-B-1 card pool uses MagSize / FireRate / RecoilVertical; extend by adding a case in
 *  UFPSRWeaponInstance::RecomputeResolved. */
UENUM(BlueprintType)
enum class EFPSRWeaponStat : uint8
{
	MagSize        UMETA(DisplayName = "Magazine Size"),
	FireRate       UMETA(DisplayName = "Fire Rate"),
	RecoilVertical UMETA(DisplayName = "Recoil (Vertical)"),
	Damage         UMETA(DisplayName = "Damage"),
	SpreadDegrees  UMETA(DisplayName = "Spread"),
	ReloadTime     UMETA(DisplayName = "Reload Time")
};

/** How a modifier combines with the base stat. Resolution per axis = (base + Σadditive) × (1 + Σpercent). */
UENUM(BlueprintType)
enum class EFPSRWeaponModOp : uint8
{
	Additive        UMETA(DisplayName = "Additive"),         // flat add (e.g. +10 MagSize)
	PercentMultiply UMETA(DisplayName = "Percent Multiply")  // fractional (e.g. +0.10 = ×1.10; -0.15 = ×0.85)
};

/** One numeric weapon-stat modifier (from a stat-modifier card). Accumulated on a UFPSRWeaponInstance
 *  (ThisWeapon scope) or on the PlayerState (AllWeapons scope). */
USTRUCT(BlueprintType)
struct FFPSRWeaponStatMod
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Mod")
	EFPSRWeaponStat Stat = EFPSRWeaponStat::FireRate;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Mod")
	EFPSRWeaponModOp Op = EFPSRWeaponModOp::PercentMultiply;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Mod")
	float Value = 0.0f;
};

/** A replicated stack of weapon-stat modifiers. Shared shape for ThisWeapon (per-instance) and
 *  AllWeapons (per-PlayerState) scopes. Modifier count is small; a plain array suffices (FastArraySerializer
 *  delta replication is a future upgrade if counts grow). */
USTRUCT(BlueprintType)
struct FFPSRWeaponModContainer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Mod")
	TArray<FFPSRWeaponStatMod> Mods;
};
