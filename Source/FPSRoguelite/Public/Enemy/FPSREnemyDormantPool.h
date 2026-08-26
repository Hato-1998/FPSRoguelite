// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SubclassOf.h"
#include "FPSREnemyDormantPool.generated.h"

class AFPSREnemyBase;

/** One archetype's slice of the dormant pool — the array AcquireOfClass scans when its class matches. Its own
 *  USTRUCT (rather than a bare TArray as the outer TMap's value) because that is the UHT-verified shape for a
 *  TMap<TSubclassOf<...>, ...> — see FFPSREnemyDormantPool's comment for the engine precedent. */
USTRUCT()
struct FFPSREnemyDormantBucket
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TArray<TObjectPtr<AFPSREnemyBase>> Enemies;
};

/** Dormant (hidden, disabled) enemy pool, bucketed by EXACT class (ADR 0013 「Docs/Architecture/0013-enemy-tier-
 *  axis-and-elite-gas.md」 불변식 7 — "풀 취득 비용은 클래스 수와 무관하다"). Replaces the flat TArray + linear-scan
 *  pool UFPSREnemySpawnSubsystem used to own: 안 A(티어 클래스)를 채택하면서 클래스 수가 티어×형태 곱집합으로
 *  늘게 됐고(문서화된 하한 6종, 불변식 7 각주), 그 대가를 갚는 **동반 조건**이 이 버킷화다 — "나중에 하면 좋은
 *  최적화"가 아니다. AcquireOfClass 를 클래스별 TMap 조회로 만들어 취득 비용을 그 버킷 하나의 크기에만
 *  비례하게 만든다(전체 풀 크기·클래스 수와 무관).
 *
 *  엔진 선례(추론 금지 원칙에 따라 소스로 확인): UPlatformSettingsManager::SettingsMap 이 이미
 *  TMap<TSubclassOf<UPlatformSettings>, FPlatformSettingsInstances> 형태로 UHT 를 통과한다
 *  (Engine/Source/Runtime/DeveloperSettings/Public/Engine/PlatformSettingsManager.h) — TSubclassOf 키 + 컨테이너를
 *  품은 USTRUCT 값(FPlatformSettingsInstances 자신도 TMap<FName, TObjectPtr<...>> 을 품는다), 검증된 형태다.
 *
 *  ⚠️ 키는 TSubclassOf<AFPSREnemyBase> 이고 조회는 항상 그 정확한 타입으로(raw UClass* 를 바로 Find 에 넘기지
 *  않고 명시적으로 TSubclassOf 로 감싸서) 수행한다 — TSubclassOf::GetTypeHash 는 내부 UClass 포인터를 그대로
 *  해시하고, operator== 은 IsChildOf 로 검증한 뒤의 포인터 비교라(SubclassOf.h), 이 조합에 기대는 대신 동일
 *  타입끼리만 비교해 포인터 동일성을 직접 보장한다. **IsChildOf 로 구현하지 않는다** — AFPSREnemyEliteBase 는
 *  AFPSREnemyBase 의 자식이라, IsChildOf 였다면 엘리트 요청이 일반 휴면체를 집어갔을 것이다.
 *
 *  🔴 알려진 한계(고치지 않음 — ADR 0013 후속 행 3 「엘리트 ASC 실부착 + 어트리뷰트 셋 + 엘리트 캡 회계」의
 *  결정 대상) — 기아(starvation) 모드: UFPSREnemySpawnSubsystem::TotalSpawned(하드캡 MaxActiveEnemies=500)는
 *  증가만 하고 클래스 무관 총량이다. 전반 스테이지가 클래스 A 로만 풀/캡을 채우면, 후반에 클래스 B(엘리트 등)를
 *  요청해도 그 버킷은 비어 있고(버킷 미스) 새로 스폰할 여지도 없어(캡 도달) 영구 거부로 이어질 수 있다. 버킷화가
 *  만든 문제가 아니라 평면 배열 시절부터 있던 문제(그때도 "다른 클래스 휴면체"는 재사용 불가였다)를 버킷화가
 *  드러낸 것이다. 해법(휴면체 파기 재도입 = 불변식 5 개정 / 클래스별 캡 / TotalSpawned 감소 회계)은 전부 후속
 *  행 3 의 결정 대상이며 이미 그 행으로 라우팅돼 있다 — 여기서 임의로 고치지 않는다. */
USTRUCT()
struct FPSROGUELITE_API FFPSREnemyDormantPool
{
	GENERATED_BODY()

	/** 반납. 키는 Enemy->GetClass()(정확 클래스). Null/무효 액터는 no-op. */
	void Add(AFPSREnemyBase* Enemy);

	/** 정확히 이 클래스인 휴면체를 O(1)(그 버킷 크기에만 비례)로 하나 꺼낸다(없으면 null). 꺼내는 길에 그
	 *  버킷에서 만난 무효(GC 된) 슬롯을 버린다 — 풀은 액터를 Destroy 하지 않으므로(ADR 0013 불변식 5) 무효
	 *  슬롯은 외부 파괴(에디터 강제삭제 등) 시에만 생기고, 남아 있어도 무해한 널 슬롯일 뿐이다. */
	AFPSREnemyBase* AcquireOfClass(UClass* ClassToSpawn);

	/** 전 버킷 합계. 버킷 수에 비례한다(디버그·테스트용 — 핫패스인 Add/AcquireOfClass 는 이 함수를 쓰지 않는다). */
	int32 Num() const;

private:
	UPROPERTY(Transient)
	TMap<TSubclassOf<AFPSREnemyBase>, FFPSREnemyDormantBucket> BucketsByClass;
};
