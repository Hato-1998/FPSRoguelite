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
 *  These are the knobs, not the policy: the policy lives in UFPSREnemyCosmeticLODSubsystem, which reads them. */
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

	/** Enemies closer than this to the local viewer cast a dynamic shadow; the rest do not. Same value as
	 *  FPSREnemyTuning::SignificanceS0RadiusSq (S0 radius, 15m) so "near enough to matter" means one thing in this
	 *  project — kept as its own config default rather than reading the constexpr directly (an authored ini value
	 *  must be free to win; a diverging ini is a legitimate content choice, not a bug — see UFPSREnemyCosmeticLOD
	 *  Subsystem::Tick's one-time alignment log).
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

	/** 헬스바 거리 LOD 마스터 스위치. OFF = 밴드가 헬스바를 건드리지 않는다(수명주기 가시성만 남는다).
	 *  그림자 스위치와 분리한 이유: 비용 성격이 다르고(그림자=셰도우패스, 헬스바=RT 드로우+슬레이트 틱),
	 *  한쪽만 끄고 재는 것이 perf 베이스라인(M0 후행 행)의 대조군에 필요하다. */
	UPROPERTY(Config, EditAnywhere, Category = "Health Bar LOD",
		meta = (DisplayName = "적 헬스바 거리 LOD 사용"))
	bool bEnableHealthBarLOD = true;

	/** 이 거리 안의 적만 헬스바를 그린다. 기본값 = S1 반경(3500cm) — §5-1 이 "S2 중거리 = 코스메틱 축소,
	 *  S3 원거리 = no cosmetic" 이라 헬스바는 S1 밖에서 떨어지는 것이 계약에 맞다.
	 *  ⚠️ 이 숫자는 **게임필/가독성 값**이라 최종 조정은 사용자 몫이다(코드는 기본값만 제시). 0 = 헬스바 전면 OFF. */
	UPROPERTY(Config, EditAnywhere, Category = "Health Bar LOD",
		meta = (DisplayName = "헬스바 표시 반경", ClampMin = "0.0", UIMax = "6000.0", ForceUnits = "cm",
			EditCondition = "bEnableHealthBarLOD", EditConditionHides))
	float HealthBarVisibleRadius = 3500.0f;

	/** 이미 보이는 헬스바가 유지되는 추가 거리. 없으면 경계를 걷는 적의 바가 깜빡인다(그림자와 같은 이유). */
	UPROPERTY(Config, EditAnywhere, Category = "Health Bar LOD",
		meta = (DisplayName = "헬스바 히스테리시스", ClampMin = "0.0", UIMax = "2000.0", ForceUnits = "cm",
			EditCondition = "bEnableHealthBarLOD", EditConditionHides))
	float HealthBarHysteresis = 300.0f;

	/** Settings appear under the "Game" category in Project Settings. */
	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }
};
