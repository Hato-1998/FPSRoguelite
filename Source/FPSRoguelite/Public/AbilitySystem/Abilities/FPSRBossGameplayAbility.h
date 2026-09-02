// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/FPSRFreezeCooldownAbility.h"
#include "FPSRBossGameplayAbility.generated.h"

class AFPSRBossBase;

/** Base for boss attack patterns (BOSS1 — `Docs/Specs/BOSS1_AbilityPatternFramework.md`).
 *  `AFPSRBossBase::GrantedAbilities` 가 이 클래스의 BP 자식들을 저작한다.
 *
 *  시계 = `AFPSRBossBase::GetPatternClockSeconds()` → 서버에서는 VIT1 의 전역 프리즈-멈춤 시계
 *  `AFPSRGameState::GetCombatClockSeconds()` 다. 엘리트처럼 per-actor 누산기를 따로 두지 않는 이유:
 *  전역 시계가 이미 존재하고 틱이 0이며(`SetRunPaused` 엣지에서만 누적), 보스는 1마리라 per-actor
 *  누산기를 둘 이유가 없다.
 *
 *  🔴 **패턴의 시간은 오직 `ServerTickPattern` 의 Δt 와 이 시계에서만 나온다.**
 *  `FTimerManager` · 시간 기반 AbilityTask(`WaitDelay`·`PlayMontageAndWait`) · duration/periodic GE 는
 *  전부 §2-2 프리즈를 뚫는다. 런타임 가드(`UFPSRAbilitySystemComponent::EnableTimeAxisGuard`)가 막는 것은
 *  이 ASC 가 **받는** GE 뿐이고, AbilityTask 는 가드 밖이라 **문서 약속으로만** 금지된다.
 *
 *  🔴 **BP 자식은 `ActivateAbility` 를 통째로 오버라이드하지 않는다.** C++ 베이스가 거기서
 *  `CommitAbility()` 를 부르므로, 덮어 버리면 `ApplyCooldown` 이 안 찍혀 쿨다운이 영원히 0이 된다.
 *  BP 는 `ServerTickPattern` 과 코스메틱 이벤트만 확장한다. */
UCLASS(Abstract)
class FPSROGUELITE_API UFPSRBossGameplayAbility : public UFPSRFreezeCooldownAbility
{
	GENERATED_BODY()

public:
	/** 🔴 이 패턴의 유일한 시간원. 보스 `Tick` 이 **활성 패턴 하나에만** 부른다(서버 전용).
	 *  DeltaSeconds 는 프리즈·스테이지 전환 중에는 애초에 도착하지 않는다 — 보스 Tick 이 그 앞에서
	 *  early-return 하기 때문이다(원거리 차징 누산기가 쓰는 바로 그 관용구). */
	virtual void ServerTickPattern(float DeltaSeconds) {}

	/** 선택기가 읽는다: 이 페이즈 미만에서는 고르지 않는다. 1 = 처음부터. */
	int32 GetMinPhase() const { return MinPhase; }

protected:
	virtual float GetCooldownClockSeconds(const FGameplayAbilityActorInfo* ActorInfo) const override;

	/** 아바타 보스. 없으면 null(서버 전용 경로에서만 유효). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Boss")
	AFPSRBossBase* GetBoss() const;

	/** 프리즈-멈춤 패턴 시계(초). 보스가 없으면 -1. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Boss")
	float GetPatternClock() const;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Pattern", meta = (ClampMin = "1"))
	int32 MinPhase = 1;
};
