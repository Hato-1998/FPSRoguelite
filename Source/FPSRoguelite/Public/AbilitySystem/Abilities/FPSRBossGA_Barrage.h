// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/FPSRBossGameplayAbility.h"
#include "GameplayTagContainer.h"
#include "FPSRBossGA_Barrage.generated.h"

class AFPSRBossBase;

/** BOSS1 패턴 1 — 포격 (`Docs/Specs/BOSS1_AbilityPatternFramework.md` §5-5·§6). 매 `IntervalSeconds` 마다 그
 *  순간 살아있는 플레이어 전원에게 지연 폭발 표식을 1개씩 예약하고, 이를 `ShellsPerPlayer` 회 반복한 뒤 스스로
 *  끝난다(사용자 결정 2026-09-02 — "전원 각각 5발", 4인이면 총 20발).
 *
 *  시간원은 `ServerTickPattern` 의 Δt 뿐이다 — 베이스(`UFPSRBossGameplayAbility`)의 약속대로 `FTimerManager`·
 *  `AbilityTask`·`GetWorld()->GetTimeSeconds()` 를 이 안에서 쓰면 그 순간부터 §2-2 전역 프리즈를 뚫는다.
 *  인터벌 누산을 `if` 가 아니라 `while` 로 굴리는 이유도 같다 — 프레임 히치 한 번이 인터벌을 통째로 여러 번
 *  건너뛰면, `if` 는 그동안의 볼리를 전부 삼켜 "전원 각각 정확히 ShellsPerPlayer 발"이라는 사용자 결정을
 *  조용히 깨뜨린다.
 *
 *  대상은 매 볼리(인터벌 통과)마다 새로 모은다 — 캐시하지 않는다. 그래야 볼리 도중 죽거나 DBNO 된 플레이어가
 *  다음 볼리에서 자동으로 빠진다. 스웜은 애초에 대상 후보에 들어올 수 없다 — 대상 수집이
 *  `World->GetPlayerControllerIterator()` 하나뿐이라 보스·스웜을 겨냥할 경로 자체가 없다(§3-2).
 *
 *  데미지 페이로드(`Damage`/`KnockbackStrength`/`DamageType`)는 **표식과 함께** `ServerAddBlastMark` 로 넘긴다.
 *  디토네이션 시점에 이 어빌리티 값을 되읽지 않는 이유 — 신관이 다 타기 전에 이 패턴이 끝났거나 취소됐을 수
 *  있고, 그러면 표식이 엉뚱한 패턴의 수치로 조용히 터진다. */
UCLASS()
class FPSROGUELITE_API UFPSRBossGA_Barrage : public UFPSRBossGameplayAbility
{
	GENERATED_BODY()

public:
	UFPSRBossGA_Barrage();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	/** 이 패턴의 유일한 시간원(베이스 헤더 참조) — 인터벌을 Δt 로 누산하고, `ShellsPerPlayer` 회를 다 쏘면
	 *  스스로 `EndAbility` 한다. */
	virtual void ServerTickPattern(float DeltaSeconds) override;

protected:
	/** 플레이어 1인당 총 발사 횟수(사용자 결정 = "전원 각각 5발"). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Barrage", meta = (ClampMin = "1"))
	int32 ShellsPerPlayer = 5;

	/** 한 볼리(=전원 1발씩) 사이 간격 — 패턴 시계 기준(초), `ServerTickPattern` 의 Δt 누산으로만 잰다. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Barrage", meta = (ClampMin = "0.1"))
	float IntervalSeconds = 2.0f;

	/** 착탄 예고(표식 등장)부터 실제 폭발까지의 신관 시간(초). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Barrage", meta = (ClampMin = "0.1"))
	float FuseSeconds = 1.4f;

	/** 폭발 판정 반경(cm). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Barrage", meta = (ClampMin = "1.0"))
	float RadiusCm = 500.0f;

	/** 폭발 1발의 데미지. 표식에 실려 나간다(클래스 주석 참조). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Barrage")
	float Damage = 25.0f;

	/** 넉백 경로 = `FPSRCombat::ApplyKnockback`. DBNO/사망 플레이어는 제외한다(무력화 대상 띄우기 금지). 0 =
	 *  넉백 없음. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Barrage")
	float KnockbackStrength = 0.0f;

	/** `FFPSRDamageSpec::DamageType` 에 실린다 — VIT1 의 per-layer 방어 계수 축에 보스 데미지를 연결하는
	 *  유일한 손잡이. 비우면 Physical. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Barrage")
	FGameplayTag DamageType;

private:
	/** 그 순간 살아있는 플레이어 전원에게 표식을 1개씩 예약한다. 서버 전용 — `Boss` non-null 은 호출부
	 *  `ServerTickPattern` 이 이미 보장하므로 여기서 다시 확인하지 않는다. */
	void FireVolley(AFPSRBossBase* Boss) const;

	/** 이번 활성화에서 흐른 인터벌 누산 시간(초). `ActivateAbility` 가 매번 0 으로 되돌린다 — 이 어빌리티는
	 *  `InstancedPerActor` 라 인스턴스가 활성화 사이에도 살아있고, 리셋을 안 하면 두 번째 발동의 첫 볼리가
	 *  이전 활성화가 남긴 누산만큼 일찍(또는 늦게) 나간다. */
	float IntervalAccumulatorSeconds = 0.0f;

	/** 지금까지 쏜 볼리 수. `ShellsPerPlayer` 에 도달하면 `ServerTickPattern` 이 스스로 `EndAbility` 한다. */
	int32 NumVolleysFired = 0;
};
