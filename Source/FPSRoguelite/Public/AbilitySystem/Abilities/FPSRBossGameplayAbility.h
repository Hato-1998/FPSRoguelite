// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/FPSRFreezeCooldownAbility.h"
#include "Boss/FPSRBossTypes.h"
#include "FPSRBossGameplayAbility.generated.h"

class AFPSRBossBase;

/** Base for boss attack patterns (BOSS1 — `Docs/Specs/BOSS1_AbilityPatternFramework.md`).
 *  `AFPSRBossBase::GrantedAbilities` 가 이 클래스의 BP 자식들을 저작한다.
 *
 *  시계 = `AFPSRBossBase::GetPatternClockSeconds()` → 서버에서는 VIT1 의 전역 프리즈-멈춤 시계다.
 *
 *  🔴 **모든 패턴은 준비 → 실행 → 후딜 3단계를 거친다** (§14-2). 상태기는 **베이스가 소유**하고, 파생은
 *  실행 구간만 채운다 — 준비/후딜을 각 패턴이 따로 구현하면 세 벌이 서로 다른 규칙으로 어긋난다.
 *  가운데 "패턴 내부 유예"(표식 신관 · 빔 정지 · 미사일 대기)는 패턴마다 의미가 달라 여기서 공통화하지
 *  않는다: 공통화하면 성격이 다른 셋을 같은 숫자 하나로 묶게 된다.
 *
 *  🔴 **패턴의 시간은 `ServerTickExecute` 의 Δt 와 이 시계에서만 나온다.** `FTimerManager` ·
 *  시간 기반 AbilityTask(`WaitDelay`·`PlayMontageAndWait`) · duration/periodic GE 는 전부 §2-2 프리즈를
 *  뚫는다. 런타임 가드가 막는 것은 이 ASC 가 **받는** GE 뿐이고 AbilityTask 는 가드 밖이라 문서 약속으로만
 *  금지된다.
 *
 *  🔴 **BP 자식은 `ActivateAbility` 를 통째로 오버라이드하지 않는다.** C++ 베이스가 거기서 `CommitAbility()`
 *  를 부르고 상태기를 시작하므로, 덮으면 쿨다운이 안 찍히고 준비 단계도 사라진다. */
UCLASS(Abstract)
class FPSROGUELITE_API UFPSRBossGameplayAbility : public UFPSRFreezeCooldownAbility
{
	GENERATED_BODY()

public:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	/** 보스 `Tick` 이 활성 패턴 하나에만 부른다(서버 전용). 베이스가 3단계를 굴리고, 실행 구간에서만
	 *  `ServerTickExecute` 로 내려보낸다. 파생은 이 함수를 오버라이드하지 않는다. */
	virtual void ServerTickPattern(float DeltaSeconds) final;

	/** 취소(보스 사망의 `CancelAbilities` 포함)로 끝날 때도 실행 구간의 정리가 반드시 돌게 한다 —
	 *  정상 종료 경로에만 정리를 두면, 정확히 "죽는 순간"에만 뒷정리가 빠진다. */
	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	/** 선택기가 읽는다: 이 페이즈 미만에서는 고르지 않는다. 1 = 처음부터. */
	int32 GetMinPhase() const { return MinPhase; }

	EFPSRBossPatternStage GetPatternStage() const { return Stage; }

protected:
	// ---- 파생이 채우는 것: 실행 구간뿐 -----------------------------------------------------------

	/** 준비가 끝난 직후 1회. 여기서 스폰·조준·대상 지정을 한다. */
	virtual void ServerBeginExecute() {}

	/** 실행 구간의 매 틱. **true 를 돌려주면 실행 종료**(→ 후딜로 넘어간다). */
	virtual bool ServerTickExecute(float DeltaSeconds) { return true; }

	/** 실행 종료 직후 1회(정상 종료·취소 공통). 패턴이 켜 둔 것을 여기서 끈다. */
	virtual void ServerEndExecute() {}

	// ---- 저작값 ---------------------------------------------------------------------------------

	/** 보스가 모션을 잡는 구간. 이 동안 패턴은 **아무것도 스폰하지 않는다** — 사전경고의 시스템 절반이다. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Pattern", meta = (ClampMin = "0.0"))
	float PrepSeconds = 1.5f;

	/** 실행이 끝난 뒤의 유휴. 이 동안 다음 패턴이 시작되지 않는다 — 패턴이 쉼 없이 이어지지 않게 하는 축. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Pattern", meta = (ClampMin = "0.0"))
	float RecoverySeconds = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Pattern", meta = (ClampMin = "1"))
	int32 MinPhase = 1;

	virtual float GetCooldownClockSeconds(const FGameplayAbilityActorInfo* ActorInfo) const override;

	UFUNCTION(BlueprintPure, Category = "FPSR|Boss")
	AFPSRBossBase* GetBoss() const;

	/** 프리즈-멈춤 패턴 시계(초). 보스가 없으면 -1. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Boss")
	float GetPatternClock() const;

	/** 현재 단계에서 흐른 시간(초). 파생이 자기 유예를 재는 데 쓴다. */
	float GetStageElapsedSeconds() const { return StageElapsedSeconds; }

private:
	/** 단계 전이 한 곳 — 보스에 복제 상태를 알리는 것도 여기서만 한다(두 곳이면 한쪽이 빠진다). */
	void EnterStage(EFPSRBossPatternStage NewStage);

	EFPSRBossPatternStage Stage = EFPSRBossPatternStage::Finished;
	float StageElapsedSeconds = 0.0f;
};
