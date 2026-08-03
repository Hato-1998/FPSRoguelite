// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "FPSREnemyRenderSettings.generated.h"

/** Swarm render-LOD policy (Project Settings -> "FPSR Enemy Rendering"). Values are authored in DefaultGame.ini
 *  [/Script/FPSRoguelite.FPSREnemyRenderSettings], matching UFPSRPlaceholderVisualSettings / UFPSRAudioSettings.
 *
 *  Why this exists: UMeshComponent's constructor sets CastShadow = true (MeshComponent.cpp — the UPrimitiveComponent
 *  base is false), so every swarm enemy is a dynamic shadow caster by default, and the directional light's cascade
 *  distance is far larger than the field the swarm lives in, so the renderer culls none of them. At the target scale
 *  that is 200-300 casters standing in for a readability cue only the nearest few actually provide.
 *
 *  These are the knobs, not the policy: the policy lives in UFPSREnemyShadowLODSubsystem, which reads them. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "FPSR Enemy Rendering"))
class FPSROGUELITE_API UFPSREnemyRenderSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Master switch for distance-based enemy shadow LOD. OFF leaves every enemy at whatever its content BP authored
	 *  (i.e. the engine default, all casting) and the subsystem is never created — no registration, no pass, no cost. */
	UPROPERTY(Config, EditAnywhere, Category = "Shadow LOD",
		meta = (DisplayName = "적 그림자 LOD 사용"))
	bool bEnableEnemyShadowLOD = true;

	/** Enemies closer than this to the local viewer cast a dynamic shadow; the rest do not. Defaults to the movement
	 *  LOD pass's S0 radius (15m) so "near enough to matter" means one thing in this project.
	 *
	 *  Set to 0 to turn enemy shadows OFF entirely — that is the supported way to get the blanket-off behaviour
	 *  without a code change (every enemy is then always beyond the radius). */
	UPROPERTY(Config, EditAnywhere, Category = "Shadow LOD",
		meta = (DisplayName = "그림자 반경", ClampMin = "0.0", UIMax = "6000.0", ForceUnits = "cm",
			EditCondition = "bEnableEnemyShadowLOD", EditConditionHides))
	float ShadowCastRadius = 1500.0f;

	/** Extra distance an already-casting enemy keeps its shadow for. Without this an enemy walking the boundary
	 *  flips every pass and its shadow strobes. Applied on the OFF side only (turn on at R, turn off at R + this). */
	UPROPERTY(Config, EditAnywhere, Category = "Shadow LOD",
		meta = (DisplayName = "히스테리시스", ClampMin = "0.0", UIMax = "2000.0", ForceUnits = "cm",
			EditCondition = "bEnableEnemyShadowLOD", EditConditionHides))
	float ShadowCastHysteresis = 300.0f;

	/** Seconds between passes. This is a cosmetic band, not a gameplay one, so it runs far below frame rate: the
	 *  whole point is to avoid per-actor per-frame work (Game.md §1 — batch, never per-actor tick). */
	UPROPERTY(Config, EditAnywhere, Category = "Shadow LOD",
		meta = (DisplayName = "갱신 주기", ClampMin = "0.05", UIMax = "1.0", ForceUnits = "s",
			EditCondition = "bEnableEnemyShadowLOD", EditConditionHides))
	float ShadowUpdateInterval = 0.2f;

	/** Settings appear under the "Game" category in Project Settings. */
	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }
};
