// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** 적 스웜의 손동기화 튜닝 상수 SSOT (F8 / RC-A). 여러 헤더에 손으로 복사돼 있던 거리밴드 반경을 한 곳으로 모은다.
 *  선례 = UFPSRFlowFieldComputer 의 셀상한 constexpr 잠금.
 *
 *  범위 주의: 이 헤더는 **거리밴드 반경만** 담는다. 캡슐 40/90·GroundSnapTolerance 60 의 산재(F7)는 별도 유닛이다. */
namespace FPSREnemyTuning
{
	// ---- 게임플레이 Significance 밴드 (Performance.md §5-1) ----
	// 평가 주체 = UFPSREnemySpawnSubsystem 서버 배치패스. 기준 = **전체 플레이어 중 최근접**(BestDistSq).
	// 소비 = 이동 스트라이드 / 공격결정 스트라이드 / NetUpdateFrequency.
	inline constexpr float SignificanceS0RadiusSq = 1500.0f * 1500.0f; // S0 근접 위협: full update
	inline constexpr float SignificanceS1RadiusSq = 3500.0f * 3500.0f; // S1 근거리: 저빈도 update
	inline constexpr float SignificanceS2RadiusSq = 6000.0f * 6000.0f; // S2 중거리 군집 (초과 = S3 원거리)

	// ---- 코스메틱 애니 프리즈 반경 ----
	// 🔁 **역할 변경(2026-08-27)**: 이 값은 더 이상 런타임이 읽는 값이 아니다. 프리즈 반경은
	//    `UFPSREnemyRenderSettings::AnimFreezeRadius`(config, cm)로 나갔고 — 순수 코스메틱 값이라
	//    조정 가능해야 하는데 코드에 잠겨 있어서 자기 자신의 PIE 검증조차 불가능했다(실사고) —
	//    여기 남은 것은 **그 config 기본값의 기준점 + 아래 F8 트립와이어**다.
	// ⚠️ 오늘 SignificanceS1RadiusSq 와 **값이 같지만 뜻이 다르다**(F8 적대재검증 결론 2026-07-08:
	//    "net-freq 티어 vs anim-freeze 가 우연히 같은 3500² 라 하나로 합치지 말 것").
	//    합치면 넷프리퀀시 튜닝이 애니 프리즈 기본값을 조용히 옮기고, 그 반대도 성립한다.
	// 아래 static_assert 는 불변식이 아니라 **트립와이어**다 — 둘 중 하나를 의도적으로 바꾸면 빌드가 깨져서
	// "정말 둘 다 옮길 셈인가"를 묻는다. 갈라놓기로 했다면 이 assert 를 지우는 것이 올바른 대응이다.
	inline constexpr float AnimFreezeRadiusSq = 3500.0f * 3500.0f;
	static_assert(AnimFreezeRadiusSq == SignificanceS1RadiusSq,
		"AnimFreezeRadiusSq 와 SignificanceS1RadiusSq 가 갈라졌다. 의도한 분리라면 이 static_assert 를 지우고 "
		"두 값이 독립임을 주석에 남겨라. 의도치 않았다면 한쪽을 되돌려라. (F8)");

	/** 뷰어/서버 공용 밴드 라벨. 값 순서 = 가까움→멀음(비교로 "이 밴드 이상 멀다"를 쓸 수 있게).
	 *  ⚠️ 네임스페이스 안 순수 C++ enum 이라 BP 에서 못 읽는다. 현행 소비자는 전부 C++(AnimProfile 패턴과 동일)라
	 *  문제없으나, 후속 U13 이 BP 에서 밴드를 읽어야 하면 그때 UENUM 래퍼를 추가한다(지금 만들지 않는다). */
	enum class EFPSRDistanceBand : uint8
	{
		S0 = 0, // 근접 위협: full update
		S1,     // 근거리: 저빈도 update
		S2,     // 중거리 군집: anim·VFX 축소
		S3,     // 원거리: coarse movement · no cosmetic
	};

	/** 제곱거리 → 밴드. 순수(월드 접근 0)라 worldless 유닛테스트 대상.
	 *  경계 규칙(`<=` 로 안쪽 포함)은 서버 패스의 기존 분기와 **같은 규칙**이다.
	 *  ⚠️ 이 함수는 **뷰어 패스와 테스트 전용**이다 — 서버 배치패스의 기존 분기를 이것으로 재작성하지 않는다(§2 비목표). */
	constexpr EFPSRDistanceBand ClassifyBand(float DistSq)
	{
		return DistSq <= SignificanceS0RadiusSq ? EFPSRDistanceBand::S0
		     : DistSq <= SignificanceS1RadiusSq ? EFPSRDistanceBand::S1
		     : DistSq <= SignificanceS2RadiusSq ? EFPSRDistanceBand::S2
		     :                                    EFPSRDistanceBand::S3;
	}

	/** 켜짐/꺼짐 코스메틱의 히스테리시스 판정. 켜져 있으면 더 넓은 반경까지 유지한다(경계 진동 방지).
	 *  기존 그림자 패스가 인라인으로 하던 것과 **같은 수식**을 순수 함수로 뽑은 것 — 그림자·헬스바가 같은 규칙을
	 *  쓰게 하고(§12-6 회귀 증명), worldless 테스트가 가능해진다. */
	constexpr bool ApplyRadiusHysteresis(bool bCurrentlyOn, float DistSq, float OnRadiusSq, float OffRadiusSq)
	{
		return DistSq <= (bCurrentlyOn ? OffRadiusSq : OnRadiusSq);
	}
}
