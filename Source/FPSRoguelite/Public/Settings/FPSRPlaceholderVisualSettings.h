// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "FPSRPlaceholderVisualSettings.generated.h"

class UStaticMesh;

/** Dev-placeholder visual config (Project Settings -> "FPSR Placeholder Visuals"). Holds the soft mesh
 *  references the scaffold actors (XP gem, swarm-enemy fallback, boss fallback) fall back to when their
 *  content BP has assigned no mesh — so NO asset path is hard-coded in C++ (Game.md §6-2, matching
 *  UFPSRAudioSettings). Values are authored in DefaultGame.ini [/Script/FPSRoguelite.FPSRPlaceholderVisualSettings].
 *
 *  These default to engine BasicShapes purely as dev scaffolding; U22 (Synty full art replacement) assigns real
 *  meshes on the content BPs, at which point the BeginPlay fallback simply never fires (the BP mesh wins). */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "FPSR Placeholder Visuals"))
class FPSROGUELITE_API UFPSRPlaceholderVisualSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Fallback mesh for the XP gem pickup (AFPSRXPPickup is spawned as the raw C++ class, so this is its only visual). */
	UPROPERTY(Config, EditAnywhere, Category = "Placeholder", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TSoftObjectPtr<UStaticMesh> XPGemMesh;

	/** Fallback mesh for a swarm enemy spawned via the raw AFPSREnemyBase (no roster/EnemyClass configured). */
	UPROPERTY(Config, EditAnywhere, Category = "Placeholder", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TSoftObjectPtr<UStaticMesh> EnemyPlaceholderMesh;

	/** Fallback body mesh for the C++ AFPSRBossBase scaffold (BossDefinition with no BossClass BP assigned). */
	UPROPERTY(Config, EditAnywhere, Category = "Placeholder", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TSoftObjectPtr<UStaticMesh> BossPlaceholderMesh;

	/** Settings appear under the "Game" category in Project Settings. */
	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }
};
