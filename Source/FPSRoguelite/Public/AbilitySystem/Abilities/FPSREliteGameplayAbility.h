// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/FPSRFreezeCooldownAbility.h"
#include "FPSREliteGameplayAbility.generated.h"

/** Base for elite-only GAS abilities (ADR 0013 후속 행 3 실행 2 — `AFPSREnemyEliteBase::GrantedAbilities` 가
 *  저작하는 시임).
 *
 *  🔁 **BOSS1 개편**: 프리즈-멈춤 쿨다운 계약 본체(`CheckCooldown`/`ApplyCooldown`/`GetCooldownGameplayEffect`
 *  3오버라이드 + `CooldownSeconds` + 마지막 스탬프)는 `UFPSRFreezeCooldownAbility` 로 호이스트됐다 — 보스가
 *  같은 계약을 쓰는데 2벌로 두면 언젠가 어긋나고, 어긋난 쪽이 프리즈를 뚫는다. **거동은 무변이다.**
 *
 *  이 클래스가 남겨 두는 것은 **시계 하나**다: `AFPSREnemyEliteBase::GetEliteCooldownClockSeconds()` —
 *  그 엘리트의 프리즈-멈춤 누산기(`ServerTickAttack` 에서 `Ctx.DeltaSeconds` 로 쌓인다. 스폰 서브시스템이
 *  프리즈 중 공격 패스 전체를 early-return 하므로 그 누산기는 구조적으로 프리즈-정확하다 — 원거리 차징이
 *  쓰는 바로 그 관용구). 보스는 대신 전역 전투시계를 읽는다(`UFPSRBossGameplayAbility`). */
UCLASS(Abstract)
class FPSROGUELITE_API UFPSREliteGameplayAbility : public UFPSRFreezeCooldownAbility
{
	GENERATED_BODY()

protected:
	/** 아바타가 엘리트가 아니면 -1(= 읽을 수 없음) → 베이스가 fail-open 한다. */
	virtual float GetCooldownClockSeconds(const FGameplayAbilityActorInfo* ActorInfo) const override;
};
