// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSRBossGA_Barrage.h"

#include "Boss/FPSRBossBase.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Hero/FPSRCharacter.h"
#include "Combat/FPSRTargeting.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

UFPSRBossGA_Barrage::UFPSRBossGA_Barrage()
{
}

void UFPSRBossGA_Barrage::ServerBeginExecute()
{
	// 준비 단계는 베이스가 이미 굴렸다. 여기는 "보스가 모션을 끝내고 실제로 쏘기 시작하는" 지점이라,
	// 첫 볼리를 곧바로 낸다 — 준비가 끝났는데 또 한 인터벌을 기다리면 준비 시간이 두 번 흐른 것처럼 보인다.
	IntervalAccumulatorSeconds = 0.0f;
	NumVolleysFired = 0;

	if (AFPSRBossBase* Boss = GetBoss())
	{
		FireVolley(Boss);
		++NumVolleysFired;
	}
}

bool UFPSRBossGA_Barrage::ServerTickExecute(float DeltaSeconds)
{
	AFPSRBossBase* Boss = GetBoss();
	if (!Boss)
	{
		return true;
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

	// 마지막 볼리를 쏘자마자 끝내지 않는다 — 그 표식들의 신관이 아직 타는 중이고, 실행이 끝나면 곧바로
	// 후딜(=보스가 무해해지는 구간)이 시작되기 때문이다. 마지막 신관이 터질 때까지는 실행 중이어야
	// "폭발이 다 끝난 뒤에 숨 돌린다"는 리듬이 성립한다.
	const bool bAllFired = NumVolleysFired >= ShellsPerPlayer;
	return bAllFired && IntervalAccumulatorSeconds >= FuseSeconds;
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
		// 대상 적격 판정은 스웜과 **같은 규칙**을 쓴다(FPSRTargeting::IsEligibleTarget) — 여기서 따로 쓰면
		// "다운된 팀원은 어그로를 끌지 않는다"가 스웜엔 참이고 보스엔 거짓인 상태가 조용히 생긴다.
		// 토폴로지 ack 는 통합(멀티맵) 필드 전용 개념이고 보스 아레나는 단일맵이라 false.
		if (!FPSRTargeting::IsEligibleTarget(PC, Boss->GetPatternClockSeconds(), /*bRequireTopologyAck=*/false))
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
