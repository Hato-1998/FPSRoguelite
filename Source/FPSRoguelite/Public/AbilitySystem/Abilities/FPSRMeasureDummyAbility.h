// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Abilities/GameplayAbility.h"
#include "FPSRMeasureDummyAbility.generated.h"

class UGameplayEffect;

/** GASM1(Docs/Specs/GASM1_SwarmASCCostMeasurement.md §5-B) 측정 부하용 더미 어빌리티. ActivateAbility 에서
 *  EffectToApply 를 자신에게 적용하고 **즉시 EndAbility** 한다.
 *
 *  🔴 즉시 종료하지 않으면 활성 상태로 남아 이후 TryActivateAbility 가 전부 거부되고, 구성 ③(로드아웃 켬)
 *  이 조용히 ②(ASC 만 켬)와 같아져 측정이 무음으로 무효가 된다. 리포의 유일한 자기활성 선례
 *  UFPSRPassiveAbility(FPSRPassiveAbility.h:10-43)는 **상시 활성 패시브**라 그대로 따라가면 이 함정에
 *  빠진다 — 그래서 이 클래스는 UFPSRPassiveAbility/UFPSRGameplayAbility 가 아니라 엔진 UGameplayAbility
 *  를 직접 상속한다.
 *
 *  부모가 UFPSREliteGameplayAbility 도 아닌 이유 — 그 클래스의 쿨다운은 엘리트 전용 프리즈-멈춤 시계
 *  (AFPSREnemyEliteBase::GetEliteCooldownClockSeconds)를 읽는데, 이 어빌리티가 붙는 대상은 일반 적
 *  (AFPSREnemyBase)이다. 발동 주기는 이 어빌리티가 아니라 AFPSREnemyBase 의 누산기(MeasureClockSeconds)
 *  가 소유한다(GASM1 §5-C·§6-B) — 그래서 CooldownGameplayEffect/CostGameplayEffect 를 전혀 authoring
 *  하지 않는다(엔진 기본값 = 둘 다 null이라 CommitAbility 가 항상 통과한다). */
UCLASS()
class FPSROGUELITE_API UFPSRMeasureDummyAbility : public UGameplayAbility
{
	GENERATED_BODY()
public:
	// ⚠️ 생성자에서 아래 2개를 반드시 명시한다(GASM1 §5-B 🔴 — 빠뜨리면 측정이 오염된다):
	//  - InstancingPolicy = InstancedPerActor — 엔진 기본값은 InstancedPerExecution
	//    (GameplayAbility.cpp:102)이라 그대로 두면 N=0.2 캐던스에서 초당 1500개 UObject 생성 + GC 압력이
	//    구성 ③ 의 측정치에 섞여, "GAS 실사용 비용"이 아니라 "잘못된 InstancingPolicy 비용"을 재게 된다.
	//  - NetExecutionPolicy = ServerOnly — 액터 소유 ASC 의 리포 선례(FPSRFreezeCooldownAbility.cpp:5-11)
	//    와 같은 경로. 실제 엘리트 어빌리티가 지불할 경로와 맞춘다.
	UFPSRMeasureDummyAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	                             const FGameplayAbilityActorInfo* ActorInfo,
	                             const FGameplayAbilityActivationInfo ActivationInfo,
	                             const FGameplayEventData* TriggerEventData) override;

	/** 적용할 Instant GE. C++ 기본값 = UFPSRMeasureInstantGE(콘텐츠 저작 불요 — 측정 전용, 소비자 0).
	 *  BP/DataAsset 오버라이드를 막지는 않지만, 이 유닛의 7회 러너 프로토콜(§12-A)은 전부 이 기본값을
	 *  그대로 쓴다. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Measure")
	TSubclassOf<UGameplayEffect> EffectToApply;
};
