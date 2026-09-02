// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystemComponent.h"
#include "FPSRAbilitySystemComponent.generated.h"

struct FActiveGameplayEffectsContainer;
struct FGameplayEffectSpec;

/** Project AbilitySystemComponent. THREE owners with DIFFERENT patterns (do not assume PlayerState):
 *  - Players: owned by AFPSRPlayerState (survives respawn/seamless travel), replication mode Mixed.
 *  - Elite-tier enemies: owned by the ACTOR itself (AFPSREnemyEliteBase — enemies have no PlayerState),
 *    replication mode Minimal (no owning client at all). ADR 0013.
 *  - Boss: owned by the ACTOR itself (AFPSRBossBase), replication mode Minimal, same reason as elites — the
 *    boss has no owning client either. BOSS1 (Docs/Specs/BOSS1_AbilityPatternFramework.md).
 *
 *  The two ACTOR-owned kinds are both bound by the time-axis contract (Docs/SSOT/Enemy.md §2-6): no duration /
 *  periodic / cooldown GEs. EnableTimeAxisGuard() below is the one enforcement copy both of them opt into.
 *  The player ASC deliberately does NOT enable it — card GEs are legitimately time-based and the player's own
 *  freeze handling is a separate mechanism. */
UCLASS()
class FPSROGUELITE_API UFPSRAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UFPSRAbilitySystemComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Server: block time-based GameplayEffects from being applied to THIS ASC, at the engine level.
	 *
	 *  🔴 왜: §2-2 전역 프리즈(레벨업 카드 선택 — 4인 협동에서 1인 레벨업 = 전원 프리즈)는 **상태 게이트**이지
	 *  `TimeDilation` 이 아닌데, 엔진은 GE duration/period 를 월드 `FTimerManager` 로 돌린다
	 *  (`GameplayEffect.cpp:4409` duration · `:4431` period) — 프리즈 여부와 무관하게 계속 카운트다운한다.
	 *  UFPSRFreezeCooldownAbility 가 쿨다운 경로를 대체하는 것은 *문서상 약속*일 뿐이라, 실수로(또는 다른
	 *  코드가) 시간형 GE 를 적용해 버리면 똑같이 샌다. 이 가드가 그 적용 자체를 엔진 레벨에서 막는다 —
	 *  문서 약속과 런타임 가드, 두 겹.
	 *
	 *  ⚠️ 막는 것은 이 ASC 가 **받는** GE 뿐이다. 이 ASC 의 어빌리티가 **플레이어에게 거는** 시간형 GE 는
	 *  플레이어 ASC(가드 없음)의 월드 타이머로 돌므로 가드 밖이고, 시간 기반 AbilityTask(`WaitDelay` 등)도
	 *  가드 밖이다 — 둘 다 문서 약속으로만 금지된다(`Enemy.md` §2-6).
	 *
	 *  Idempotent: 두 번 불러도 콜백은 한 번만 등록된다. 액터 실수명당 1회(`PostInitializeComponents`)면
	 *  충분하다 — 델리게이트 배열이 이 ASC 인스턴스 소유라 풀 재사용으로 사라지지 않는다. */
	void EnableTimeAxisGuard();

private:
	/** `GameplayEffectApplicationQueries` 콜백. `false` 를 돌려주면 그 GE 는 아예 적용되지 못한다(엔진 호출부 =
	 *  `AbilitySystemComponent.cpp` — `ApplyGameplayEffectSpecToSelf` 안의 순회, 하나라도 거부하면 즉시
	 *  `FActiveGameplayEffectHandle()` 반환).
	 *
	 *  거부 조건 = `DurationPolicy == HasDuration` **또는** `GetPeriod() > 0`. Period 를 반드시 같이 걸러야
	 *  하는 이유: Infinite(무기한) GE 도 Period 를 가질 수 있어(주기적으로 영원히 Execute) DurationPolicy
	 *  검사 하나만으로는 새는 구멍이 있다. `GetPeriod()` 는 Instant 이면 강제로 NO_PERIOD 를 돌려주므로
	 *  Instant GE 는 이 조건에 안 걸린다(`GameplayEffect.h`). */
	bool RejectTimeBasedGameplayEffect(const FActiveGameplayEffectsContainer& ActiveGEContainer, const FGameplayEffectSpec& Spec) const;

	/** EnableTimeAxisGuard 의 멱등성 래치. 서버 전용 상태라 복제하지 않는다. */
	bool bTimeAxisGuardRegistered = false;
};
