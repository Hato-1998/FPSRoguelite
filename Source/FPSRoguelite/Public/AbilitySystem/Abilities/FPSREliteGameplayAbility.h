// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/FPSRGameplayAbility.h"
#include "FPSREliteGameplayAbility.generated.h"

/** Base for elite-only GAS abilities (ADR 0013 후속 행 3 실행 2 — AFPSREnemyEliteBase::GrantedAbilities 가 저작
 *  하는 시임). Grants THIS project's actual cooldown timing contract, in place of the engine's standard duration-GE
 *  cooldown:
 *
 *  🔴 왜: §2-2 전역 프리즈(레벨업 카드 선택 — 4인 협동에서 1인 레벨업 = 전원 프리즈)는 **상태 게이트**이지
 *  `TimeDilation` 이 아닌데, 엔진은 GE duration/period 를 월드 `FTimerManager` 로 돌린다
 *  (GameplayEffect.cpp:4409 `TimerManager.SetTimer(DurationHandle, ..., FinalDuration, false)`,
 *  :4431 `TimerManager.SetTimer(PeriodHandle, ..., GetPeriod(), true)`) — 프리즈 여부와 무관하게 계속 카운트다운
 *  한다. 그리고 표준 쿨다운도 duration GE 다: `UGameplayAbility::ApplyCooldown` (GameplayAbility.cpp:1099) 은
 *  `GetCooldownGameplayEffect()` 가 반환한 GE 를 그대로 적용한다. 그대로 뒀다면 레벨업 프리즈 중에 엘리트
 *  쿨다운·디버프가 공짜로 흘러가 버렸을 것이다.
 *
 *  대신 이 클래스는 표준 쿨다운 경로를 셋 다(엔진에서 전부 virtual 확인) 대체한다:
 *   - `GetCooldownGameplayEffect()` → 항상 null(적용할 duration GE 자체가 없다)
 *   - `ApplyCooldown()` → 표준 GE 를 적용하는 대신 `AFPSREnemyEliteBase::GetEliteCooldownClockSeconds()`(그
 *     엘리트의 프리즈-멈춤 누산기 — ServerTickAttack 에서 Ctx.DeltaSeconds 로 쌓인다. 서브시스템이 프리즈 중
 *     공격 패스 전체를 early-return 하므로 그 누산기는 구조적으로 프리즈-정확하다, 원거리 차징이 쓰는 바로
 *     그 관용구)를 스탬프한다
 *   - `CheckCooldown()` → 그 스탬프 이후 누산기가 `CooldownSeconds` 만큼 흘렀는지로 답한다
 *
 *  이것만으로는 문서상 약속일 뿐이다 — 실수로(또는 다른 코드가) HasDuration/주기적 GE 를 이 어빌리티가 직접
 *  적용해 버리면 똑같이 샌다. 그래서 `AFPSREnemyEliteBase` 는 별도로
 *  `UAbilitySystemComponent::GameplayEffectApplicationQueries` 런타임 가드를 등록해 그런 GE 의 **적용 자체를
 *  엔진 레벨에서 차단**한다 — 문서 약속과 런타임 가드, 두 겹. 타이밍이 필요하면 이 클래스의 `CooldownSeconds`
 *  또는 (쿨다운이 아닌 다른 지속효과라면) `ServerTickAttack` 의 프리즈-멈춤 누산기를 쓴다. */
UCLASS(Abstract)
class FPSROGUELITE_API UFPSREliteGameplayAbility : public UFPSRGameplayAbility
{
	GENERATED_BODY()

public:
	UFPSREliteGameplayAbility();

	//~UGameplayAbility — 클래스 주석 참조. 셋 다 엔진에서 override 가능(virtual) 확인 후 작성.
	virtual bool CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;
	virtual UGameplayEffect* GetCooldownGameplayEffect() const override;
	//~End UGameplayAbility

protected:
	/** 기획자가 BP 인스턴스에서 숫자로 저작하는 쿨다운(초) — 사용자 지시("BP 에서 숫자로 저작하는 손잡이").
	 *  0(디폴트) = 쿨다운 없음(항상 즉시 재사용 가능 — CheckCooldown 이 이 값을 먼저 본다). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Elite|Cooldown", meta = (ClampMin = "0.0"))
	float CooldownSeconds = 0.0f;

private:
	/** 마지막으로 ApplyCooldown 이 스탬프한 시점의 엘리트 프리즈-멈춤 누산기 값. -1 = 이번 삶(Activate 이후,
	 *  풀 재사용 포함)에서 아직 한 번도 활성화되지 않음 — 그 경우 CheckCooldown 은 무조건 허용한다(그렇지 않으면
	 *  "-1 과 지금 누산기 값의 차"가 우연히 CooldownSeconds 보다 작을 때 첫 활성화가 막히는 버그가 생긴다).
	 *  CheckCooldown/ApplyCooldown 이 (엔진 시그니처상) const 라 mutable — 이 어빌리티는 InstancedPerActor
	 *  (UFPSRGameplayAbility 생성자)라 엘리트 1마리당 별도 인스턴스이므로 이 상태가 서로 섞이지 않는다. */
	mutable float LastActivationClockSeconds = -1.0f;
};
