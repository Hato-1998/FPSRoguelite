// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/FPSRGameplayAbility.h"
#include "FPSRFreezeCooldownAbility.generated.h"

/** Base for abilities on an ACTOR-owned ASC (elite enemies, boss). Grants THIS project's actual cooldown timing
 *  contract in place of the engine's standard duration-GE cooldown.
 *
 *  🔴 왜: §2-2 전역 프리즈(레벨업 카드 선택 — 4인 협동에서 1인 레벨업 = 전원 프리즈)는 **상태 게이트**이지
 *  `TimeDilation` 이 아닌데, 엔진은 GE duration/period 를 월드 `FTimerManager` 로 돌린다
 *  (`GameplayEffect.cpp:4409` duration · `:4431` period `bLoop=true`) — 프리즈 여부와 무관하게 계속
 *  카운트다운한다. 그리고 표준 쿨다운도 duration GE 다: `UGameplayAbility::ApplyCooldown`
 *  (`GameplayAbility.cpp:1099`) 은 `GetCooldownGameplayEffect()` 가 반환한 GE 를 그대로 적용한다.
 *  그대로 뒀다면 레벨업 프리즈 중에 쿨다운·디버프가 공짜로 흘러가 버렸을 것이다.
 *
 *  대신 이 클래스가 표준 쿨다운 경로를 셋 다(엔진에서 전부 virtual 확인) 대체한다:
 *   - `GetCooldownGameplayEffect()` → 항상 null(적용할 duration GE 자체가 없다)
 *   - `ApplyCooldown()`  → 파생이 제공하는 **프리즈-멈춤 시계**를 스탬프한다
 *   - `CheckCooldown()`  → 그 스탬프 이후 시계가 `CooldownSeconds` 만큼 흘렀는지로 답한다
 *
 *  파생이 채우는 것은 시계 하나뿐이다(`GetCooldownClockSeconds`):
 *   - 엘리트 = `AFPSREnemyEliteBase::GetEliteCooldownClockSeconds()` — 스폰 서브시스템의 공격 패스가
 *     프리즈 중 통째로 early-return 하므로 구조적으로 프리즈-정확한 per-actor 누산기
 *   - 보스   = `AFPSRGameState::GetCombatClockSeconds()` — VIT1 이 만든 전역 프리즈-멈춤 시계(틱 0)
 *
 *  이것만으로는 문서상 약속일 뿐이라, ACTOR-owned ASC 는 별도로
 *  `UFPSRAbilitySystemComponent::EnableTimeAxisGuard()` 로 시간형 GE 의 **적용 자체를 엔진 레벨에서 차단**한다
 *  — 문서 약속과 런타임 가드, 두 겹.
 *
 *  🔁 이 클래스는 BOSS1 에서 `UFPSREliteGameplayAbility` 로부터 호이스트됐다. 계약을 2벌로 두면 언젠가
 *  어긋나고, 어긋난 쪽이 프리즈를 뚫는다. */
UCLASS(Abstract)
class FPSROGUELITE_API UFPSRFreezeCooldownAbility : public UFPSRGameplayAbility
{
	GENERATED_BODY()

public:
	UFPSRFreezeCooldownAbility();

	//~UGameplayAbility — 클래스 주석 참조. 셋 다 엔진에서 override 가능(virtual) 확인 후 작성.
	virtual bool CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;
	virtual UGameplayEffect* GetCooldownGameplayEffect() const override;
	//~End UGameplayAbility

protected:
	/** 파생이 제공하는 프리즈-멈춤 시계(초). **음수를 돌려주면 "시계를 읽을 수 없다"** 는 뜻이고, 그때
	 *  `CheckCooldown` 은 fail-open 한다 — 아바타가 없거나 기대한 타입이 아닌 상황이 어빌리티를 영구히
	 *  brick 하면 안 된다(`UFPSRGameplayAbility::IsFirePermittedByMovementState` 의 fail-open 철학과 동일). */
	virtual float GetCooldownClockSeconds(const FGameplayAbilityActorInfo* ActorInfo) const
		PURE_VIRTUAL(UFPSRFreezeCooldownAbility::GetCooldownClockSeconds, return -1.0f;);

	/** 기획자가 BP 인스턴스에서 숫자로 저작하는 쿨다운(초).
	 *  0(디폴트) = 쿨다운 없음(항상 즉시 재사용 가능 — CheckCooldown 이 이 값을 먼저 본다). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Cooldown", meta = (ClampMin = "0.0"))
	float CooldownSeconds = 0.0f;

public:
#if !UE_BUILD_SHIPPING
	/** Debug only: forget this instance's cooldown stamp so the next CheckCooldown passes.
	 *  🔴 Cancelling an ability does NOT clear its cooldown (the stamp lives on the instance, and CancelAbilities
	 *  never touches it), so a "force this pattern now" console command was silently refused whenever the pattern
	 *  had run within its cooldown — which is exactly when you are most likely to be testing it. */
	void DebugClearCooldown() { LastActivationClockSeconds = -1.0f; }
#endif

private:
	/** 마지막으로 ApplyCooldown 이 스탬프한 시계 값. -1 = 이 인스턴스에서 아직 한 번도 활성화되지 않음 —
	 *  그 경우 CheckCooldown 은 무조건 허용한다(그렇지 않으면 "-1 과 지금 시계 값의 차"가 우연히
	 *  CooldownSeconds 보다 작을 때 첫 활성화가 막히는 버그가 생긴다).
	 *  CheckCooldown/ApplyCooldown 이 (엔진 시그니처상) const 라 mutable — 이 어빌리티는 InstancedPerActor
	 *  (UFPSRGameplayAbility 생성자)라 소유 액터 1개당 별도 인스턴스이므로 상태가 서로 섞이지 않는다. */
	mutable float LastActivationClockSeconds = -1.0f;
};
