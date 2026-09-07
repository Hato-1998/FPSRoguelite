// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "Status/FPSRStatusTypes.h" // full type: ResolveCatalog's LoadSynchronous() needs it, not just a fwd-decl
#include "Core/FPSRLogChannels.h"   // ResolveCatalog's one-time "unset catalog" warning
#include "FPSRStatusEffectSettings.generated.h"

/** STAT1 §5-2 / §9 config (Project Settings -> "FPSR Status Effects"), matching UFPSREnemyRenderSettings'
 *  HealthBarWidgetClass / UFPSRGameFlowSettings' LoadoutPool soft-path idiom (핵심원칙 2 — no hardcoded asset path
 *  in C++). One project-wide catalog: every UFPSRStatusApplyFragment on every weapon resolves the SAME slot table
 *  from here rather than each card authoring its own catalog reference (STAT1 §5-2's catalog is a single shared
 *  DataAsset, not per-fragment data).
 *
 *  Unset/unloadable = status application/progression is a silent no-op everywhere it is attempted (logged once, not
 *  per-hit/per-frame — see ResolveCatalog below). STAT1 §5-6's target gate already makes "no status this hit" a
 *  safe outcome; an empty catalog just means "none is ever offered", the same shape HealthBarWidgetClass being
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

	/** STAT1 §5-2/§9 (C2단계 — promoted from a FPSRWeaponFragment.cpp-local static so the batch pass and boss Tick
	 *  can resolve the SAME catalog the weapon-fragment apply path does, instead of three independent copies of this
	 *  soft-load + one-time-warning helper). Logs the unset/unloadable case exactly once, ever: this now runs on
	 *  both the per-hit damage path AND the per-frame status-progression pass, so a per-call warning would spam the
	 *  log over a content-authoring gap every caller's own null check already turns into a harmless no-op. */
	static const UFPSRStatusCatalogDataAsset* ResolveCatalog()
	{
		static bool bHasLoggedMissingCatalog = false;
		const UFPSRStatusEffectSettings* Settings = GetDefault<UFPSRStatusEffectSettings>();
		const UFPSRStatusCatalogDataAsset* Catalog = Settings ? Settings->StatusCatalog.LoadSynchronous() : nullptr;
		if (!Catalog && !bHasLoggedMissingCatalog)
		{
			UE_LOG(LogFPSR, Warning, TEXT("[Status] FPSRStatusEffectSettings.StatusCatalog is unset or failed to ")
				TEXT("load — status-effect application/progression is a silent no-op until a catalog is authored (STAT1 §5-2)."));
			bHasLoggedMissingCatalog = true;
		}
		return Catalog;
	}
};
