// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSRBossGA_Barrage.h"

#include "Boss/FPSRBossBase.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Hero/FPSRCharacter.h"
#include "Core/FPSRPlayerState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

UFPSRBossGA_Barrage::UFPSRBossGA_Barrage()
{
}

void UFPSRBossGA_Barrage::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// CommitAbility 를 건너뛰면 ApplyCooldown 이 안 찍혀 이 패턴의 쿨다운이 영원히 0으로 남는다 — BP 자식은
	// ActivateAbility 를 통째로 덮지 않는다는 §9 규칙과 짝을 이루는 C++ 쪽 절반이라, 실패 분기와 무관하게
	// 항상 먼저 부른다.
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// NetExecutionPolicy=ServerOnly(베이스 체인, UFPSRFreezeCooldownAbility) 라 사실상 항상 참이지만, 이
	// 보스 패턴 시스템의 다른 모든 Server* 진입점과 동일하게 방어적으로 한 번 더 잠근다.
	AFPSRBossBase* Boss = GetBoss();
	if (!Boss || !Boss->HasAuthority())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// InstancedPerActor 라 이 인스턴스는 활성화 사이에도 살아있다 — 리셋 없이 두 번째 발동을 맞으면 이전
	// 활성화가 남긴 누산·카운트가 그대로 이어져 첫 볼리 타이밍과 총 발수가 어긋난다.
	IntervalAccumulatorSeconds = 0.0f;
	NumVolleysFired = 0;
}

void UFPSRBossGA_Barrage::ServerTickPattern(float DeltaSeconds)
{
	AFPSRBossBase* Boss = GetBoss();
	if (!Boss)
	{
		return;
	}

	IntervalAccumulatorSeconds += DeltaSeconds;

	// while — 프레임 히치가 인터벌을 통째로 하나 이상 건너뛰어도 볼리를 하나씩 다 쏜다. if 였다면 히치 한
	// 번이 "전원 각각 정확히 ShellsPerPlayer 발"이라는 사용자 결정을 조용히 깨뜨린다.
	while (NumVolleysFired < ShellsPerPlayer && IntervalAccumulatorSeconds >= IntervalSeconds)
	{
		IntervalAccumulatorSeconds -= IntervalSeconds;
		FireVolley(Boss);
		++NumVolleysFired;
	}

	if (NumVolleysFired >= ShellsPerPlayer)
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
	}
}

void UFPSRBossGA_Barrage::FireVolley(AFPSRBossBase* Boss) const
{
	UWorld* World = Boss->GetWorld();
	if (!World)
	{
		return;
	}

	// GetLastGroundedZ 는 트레이스 0회로 "이 플레이어가 마지막으로 접지했던 Z" 를 답한다 — 공중에 뜬
	// 플레이어를 노려도 표식이 발밑 지면에 눕게 하는 유일한 값. 실패하면 아래 폴백(현재 Z)으로 넘어간다.
	UFPSREnemySpawnSubsystem* SpawnSubsystem = World->GetSubsystem<UFPSREnemySpawnSubsystem>();

	// 대상은 매 볼리마다 새로 모은다 — 캐시해 두면 이번 볼리 이전에 죽거나 DBNO 된 플레이어가 계속 맞는다
	// (요구사항 5). PlayerControllerIterator 만 순회하므로 스웜/보스는 애초에 후보에 들어올 수 없다(§3-2).
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		AFPSRCharacter* Character = Cast<AFPSRCharacter>(PC->GetPawn());
		if (!Character)
		{
			continue;
		}
		// 생존 판정 = AFPSRPlayerState::IsAlive() — DBNO/사망 둘 다 여기서 걸러진다(U9).
		const AFPSRPlayerState* PlayerState = Character->GetPlayerState<AFPSRPlayerState>();
		if (!PlayerState || !PlayerState->IsAlive())
		{
			continue;
		}

		FVector Center = Character->GetActorLocation();
		float GroundedZ = 0.0f;
		if (SpawnSubsystem && SpawnSubsystem->GetLastGroundedZ(Character, GroundedZ))
		{
			Center.Z = GroundedZ;
		}

		// 페이로드는 표식과 함께 간다 — 신관이 다 타는 시점엔 이 어빌리티가 이미 끝났거나 취소됐을 수 있어서,
		// 디토네이션이 그때 가서 어빌리티 값을 되읽는 구조였다면 다른 패턴의 수치로 조용히 터질 수 있다.
		FFPSRDamageSpec Spec;
		Spec.DamageType = DamageType;
		Boss->ServerAddBlastMark(Center, RadiusCm, FuseSeconds, Character, Damage, KnockbackStrength, Spec);
	}
}
