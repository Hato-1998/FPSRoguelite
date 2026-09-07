// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "FPSRStatusEffectSettings.generated.h"

class UFPSRStatusCatalogDataAsset;

/** STAT1 §5-2 / §9 config (Project Settings -> "FPSR Status Effects"), matching UFPSREnemyRenderSettings'
 *  HealthBarWidgetClass / UFPSRGameFlowSettings' LoadoutPool soft-path idiom (핵심원칙 2 — no hardcoded asset path
 *  in C++). One project-wide catalog: every UFPSRStatusApplyFragment on every weapon resolves the SAME slot table
 *  from here rather than each card authoring its own catalog reference (STAT1 §5-2's catalog is a single shared
 *  DataAsset, not per-fragment data).
 *
 *  Unset/unloadable = status application is a silent no-op everywhere it is attempted (logged once, not per-hit —
 *  see FPSRWeaponFragment.cpp's ResolveStatusCatalog). STAT1 §5-6's target gate already makes "no status this hit"
 *  a safe outcome; an empty catalog just means "none is ever offered", the same shape HealthBarWidgetClass being
 *  unset means "the health-bar feature is off" rather than a crash. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "FPSR Status Effects"))
class FPSROGUELITE_API UFPSRStatusEffectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** The full status-effect slot table (STAT1 §5-2 UFPSRStatusCatalogDataAsset). Soft reference — content authors
	 *  this DataAsset instance in DefaultGame.ini [/Script/FPSRoguelite.FPSRStatusEffectSettings]; C++ never spells
	 *  out a hardcoded content path (핵심원칙 2). */
	UPROPERTY(Config, EditAnywhere, Category = "Status",
		meta = (DisplayName = "상태이상 카탈로그"))
	TSoftObjectPtr<UFPSRStatusCatalogDataAsset> StatusCatalog;

	/** Settings appear under the "Game" category in Project Settings (matches every sibling FPSR settings class). */
	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }
};
