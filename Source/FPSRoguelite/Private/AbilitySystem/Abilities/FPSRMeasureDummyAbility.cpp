// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSRMeasureDummyAbility.h"
#include "AbilitySystem/Effects/FPSRMeasureInstantGE.h"
#include "AbilitySystemComponent.h"

UFPSRMeasureDummyAbility::UFPSRMeasureDummyAbility()
{
	// GASM1 §5-B 🔴 — 클래스 헤더 주석 참조. 둘 다 엔진 기본값과 다르다.
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	// C++ 기본값 — 콘텐츠 저작 불요(헤더의 EffectToApply 필드 주석 참조).
	EffectToApply = UFPSRMeasureInstantGE::StaticClass();
}

void UFPSRMeasureDummyAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                               const FGameplayAbilityActorInfo* ActorInfo,
                                               const FGameplayAbilityActivationInfo ActivationInfo,
                                               const FGameplayEventData* TriggerEventData)
{
	// UFPSRPassiveAbility_Lifesteal::ActivateAbility(FPSRPassiveAbility.cpp:64-96)와 같은 형태 — 이
	// 리포에서 "Instant GE 를 자신에게 적용하고 끝낸다"의 유일한 선례. CommitAbility 는 쿨다운/코스트 GE 를
	// 전혀 authoring 하지 않았으므로(헤더 주석) 항상 통과하지만, 실제 엘리트 어빌리티가 지불할 커밋 경로
	// 자체의 비용은 그대로 측정에 남는다 — 건너뛰지 않는다.
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (EffectToApply)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			const FGameplayEffectContextHandle EffectContext = ASC->MakeEffectContext();
			const FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(EffectToApply, 1.0f, EffectContext);
			if (SpecHandle.IsValid())
			{
				ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data);
			}
		}
	}

	// 🔴 즉시 종료 — 헤더 클래스 주석 참조. 안 하면 이후 TryActivateAbility 가 전부 거부된다.
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
