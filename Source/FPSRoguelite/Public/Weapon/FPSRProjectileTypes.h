// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FPSRProjectileTypes.generated.h"

UENUM(BlueprintType)
enum class EFPSRProjectileTeam : uint8
{
	Player,
	Enemy
};

UENUM(BlueprintType)
enum class EFPSRProjectileMode : uint8
{
	ServerAuthoritative,
	CosmeticPredicted
};

USTRUCT(BlueprintType)
struct FPSROGUELITE_API FFPSRProjectileParams
{
	GENERATED_BODY()

	/** Initial projectile velocity (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	float InitialSpeed = 3000.0f;

	/** Gravity scale. 0 = straight (rocket); >0 = arc (grenade). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	float GravityScale = 0.0f;

	/** Damage per hit or per target in AOE. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	float Damage = 50.0f;

	/** Lifetime before auto-release (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	float Lifetime = 5.0f;

	/** AOE explosion radius (cm). >0 = radial damage on impact; 0 = single-target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	float ExplosionRadius = 0.0f;

	/** Extra pawns a single-hit projectile passes through before stopping (ignored if AOE). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	int32 Pierce = 0;

	/** Team affiliation (determines damage targets). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	EFPSRProjectileTeam Team = EFPSRProjectileTeam::Player;

	/** Replication mode (ServerAuthoritative = server damage gate; CosmeticPredicted = cosmetic prediction). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	EFPSRProjectileMode Mode = EFPSRProjectileMode::ServerAuthoritative;

	/** The actor that fired this projectile (never damaged by its own projectile). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	TObjectPtr<AActor> InstigatorActor = nullptr;
};
