// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayEffect.h"
#include "FPSRMeasureInstantGE.generated.h"

/** GASM1(Docs/Specs/GASM1_SwarmASCCostMeasurement.md §5-B) 측정 전용 Instant GE — Health Modifier 1개.
 *
 *  Instant 라 UFPSRAbilitySystemComponent::EnableTimeAxisGuard() 에 걸리지 않는다: 그 가드는
 *  DurationPolicy==HasDuration 이거나 GetPeriod()>0 인 스펙만 거부하는데, Instant 스펙은 GetPeriod() 가
 *  무조건 NO_PERIOD 를 반환하므로(GameplayEffect.h) FPSRAbilitySystem::IsTimeBasedEffect 가 항상 false —
 *  FPSRAbilitySystemComponent.cpp 의 RejectTimeBasedGameplayEffect 판정 조건과 동일하다. 시간형
 *  (HasDuration/periodic) GE 는 §2-2 프리즈 계약 위반이라 이 유닛에서 금지된다(GASM1 §3-3, §7 미결정 항목
 *  아님 — 절대 규칙). */
UCLASS()
class FPSROGUELITE_API UFPSRMeasureInstantGE : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UFPSRMeasureInstantGE();
};
