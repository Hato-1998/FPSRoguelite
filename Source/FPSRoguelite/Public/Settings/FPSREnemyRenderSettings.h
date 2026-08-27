// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "FPSREnemyRenderSettings.generated.h"

class UFPSREnemyHealthBarWidget;

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

	/** 적 머리 위 체력바로 쓸 위젯. **에셋 경로를 C++ 에 하드코딩하지 않기 위한 소프트경로**(핵심원칙 2,
	 *  선례 = UFPSRPlaceholderVisualSettings / UFPSRStageFadeSettings). 기본값은 콘텐츠가 ini 에 저작한다 —
	 *  코드에는 `ConstructorHelpers` 도, 하드코딩 경로도 두지 않는다.
	 *
	 *  비어 있으면 **체력바 기능 전체가 꺼진다** — 컴포넌트는 존재하되 위젯이 없고, InitHealthBarWidget 이 그때
	 *  TickMode 를 Disabled 로 내려 **틱 비용까지 0** 으로 만든다(그 한 줄이 없으면 위젯 없는 스크린 공간 컴포넌트가
	 *  영구히 매 프레임 틱한다 — 엔진 실측, 명세 §6-2 step 4). 그것이 "체력바를 끄는" 지원되는 방법이다.
	 *
	 *  ⚠️ BP 가 네이티브 컴포넌트의 WidgetClass 를 덮었으면 **BP 가 이긴다** — 이 값은 덮이지 않은 경우의
	 *  기본값이다(엘리트 전용 바 같은 아키타입별 분기를 코드 변경 없이 열어 두기 위함). */
	UPROPERTY(Config, EditAnywhere, Category = "Health Bar",
		meta = (DisplayName = "적 체력바 위젯"))  // MetaClass 불요 — TSoftClassPtr 템플릿 인자가 이미 피커를 필터한다(G1 P3-4)
	TSoftClassPtr<UFPSREnemyHealthBarWidget> HealthBarWidgetClass;

	/** 헬스바 거리 LOD 마스터 스위치. OFF = 밴드가 헬스바를 더는 건드리지 않는다(수명주기 가시성만 남는다).
	 *  그림자 스위치와 분리한 이유: 비용 성격이 다르고(그림자=셰도우패스, 헬스바=스크린 레이어 등재+컴포넌트 틱 —
	 *  🔴 HB1 정정: 이 헬스바는 스크린 공간이라 RT 드로우가 없다, HB1 §10), 한쪽만 끄고 재는 것이 perf 베이스라인
	 *  (M0 후행 행)의 대조군에 필요하다.
	 *
	 *  ⚠️ **런 단위 토글이다 — 세션 도중 전환은 미지원.** 이미 밴드로 숨겨진 바는 OFF 로 바꿔도 되살아나지
	 *  않는다(패스가 더는 setter 를 부르지 않고, 거리 축은 풀 재사용 때도 일부러 리셋하지 않으므로 그 액터는
	 *  세션 내내 숨은 채로 남는다). `bEnableEnemyShadowLOD` 도 동형이다. 대조군 측정은 **재시작 후** 잴 것. */
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

	/** 이 거리보다 먼 적은 **애니메이션이 멈춘다**(재생속도 0). CPU 쪽 CPD 스칼라 쓰기와 원거리 GPU WPO 전진을
	 *  둘 다 끊는 절감이며, 코스메틱 전용이다 — 서버의 이동/공격 스트라이드·NetUpdateFrequency 밴드와는 **다른
	 *  값이고 섞이지 않는다**(F8: 오늘 기본값이 S1 반경과 같은 3500 인 것은 우연이지 결속이 아니다).
	 *
	 *  ⚠️ **스위치가 없는 것은 의도다.** 프리즈는 종전부터 어떤 설정과도 무관하게 항상 동작했고, 여기에 마스터
	 *  스위치를 주면 그걸 끈 순간 프리즈가 조용히 사라진다(= UFPSREnemyCosmeticLODSubsystem::ShouldCreateSubsystem
	 *  이 피하려던 바로 그 회귀). 끄고 싶으면 반경을 아주 크게 잡아라 — 0 이 "전부 프리즈"로 해석되는 사고를
	 *  막으려고 ClampMin 을 두었다.
	 *
	 *  ⚠️ 이 값은 **게임필 값**이라 최종 조정은 사용자 몫이다. 코드 기본값 3500 = `FPSREnemyTuning::
	 *  AnimFreezeRadiusSq` 의 sqrt(그 상수는 이제 이 기본값의 기준점이자 F8 트립와이어로 남는다). */
	UPROPERTY(Config, EditAnywhere, Category = "Anim LOD",
		meta = (DisplayName = "애니 프리즈 반경", ClampMin = "100.0", UIMax = "6000.0", ForceUnits = "cm"))
	float AnimFreezeRadius = 3500.0f;

	/** Settings appear under the "Game" category in Project Settings. */
	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }
};
