// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Attributes/FPSRMeasureAttributeSet.h"
#include "Net/UnrealNetwork.h"

UFPSRMeasureAttributeSet::UFPSRMeasureAttributeSet()
{
	// GASM1 §9 데이터드리븐 경계 — 측정 전용 상수, 콘텐츠로 빼지 않는다(소비자 0).
	InitHealth(100.0f);
	InitMaxHealth(100.0f);
	InitAttackPower(10.0f);
	InitMoveSpeedMult(1.0f);
}

void UFPSRMeasureAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// GASM1 §7 복제표 — 4종 전부 COND_None: 복제 트래픽 자체가 측정 대상이므로 조건을 좁히지 않는다.
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRMeasureAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRMeasureAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRMeasureAttributeSet, AttackPower, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRMeasureAttributeSet, MoveSpeedMult, COND_None, REPNOTIFY_Always);
}

void UFPSRMeasureAttributeSet::ResetForMeasure()
{
	// 🔴 필수 — 헤더 주석 참조(CancelAbilities/RemoveActiveEffects/ClearAbility 중 무엇도 값을 되돌리지
	// 않는다). Set 경유(Init 아님) — 이 함수는 MeasureASC 부착 "이후"에만 불리므로
	// GetOwningAbilitySystemComponent() 가 이미 유효하고, 런타임 재설정은 Init(구성 시점 전용)이 아니라
	// Set(FPSRHealthSet::PostAttributeChange 의 SetHealth/SetShield 런타임 조정과 같은 경로)이 맞다.
	SetHealth(100.0f);
	SetMaxHealth(100.0f);
	SetAttackPower(10.0f);
	SetMoveSpeedMult(1.0f);
}

void UFPSRMeasureAttributeSet::OnRep_Health(const FGameplayAttributeData& Old)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRMeasureAttributeSet, Health, Old);
}

void UFPSRMeasureAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& Old)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRMeasureAttributeSet, MaxHealth, Old);
}

void UFPSRMeasureAttributeSet::OnRep_AttackPower(const FGameplayAttributeData& Old)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRMeasureAttributeSet, AttackPower, Old);
}

void UFPSRMeasureAttributeSet::OnRep_MoveSpeedMult(const FGameplayAttributeData& Old)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRMeasureAttributeSet, MoveSpeedMult, Old);
}
