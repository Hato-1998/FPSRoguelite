// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Effects/FPSRMeasureInstantGE.h"
#include "AbilitySystem/Attributes/FPSRMeasureAttributeSet.h"

UFPSRMeasureInstantGE::UFPSRMeasureInstantGE()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	// 측정 부하 발생기 — 부호/크기는 무엇이든 무방하다(소비자 0, GASM1 §9). 매 발동마다 관측 가능한 상태
	// 변화를 만드는 것이 목적이라 작은 고정 Additive 를 쓴다. UFPSRMeasureAttributeSet 은 클램프가 없으므로
	// (PreAttributeChange 오버라이드 없음, §5-B) 장시간 측정에도 안전하게 계속 감소한다 — 실제 데미지가
	// 아니라 GE 적용 경로 자체의 비용을 재는 것이 목적이다.
	FGameplayModifierInfo HealthModifier;
	HealthModifier.Attribute = UFPSRMeasureAttributeSet::GetHealthAttribute();
	HealthModifier.ModifierOp = EGameplayModOp::Additive;
	HealthModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(-1.0f));
	Modifiers.Add(HealthModifier);
}
