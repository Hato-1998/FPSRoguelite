// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "FPSRRunScheduleDataAsset.generated.h"

class UFPSRMissionDataAsset;
class UFPSRBossDefinitionDataAsset;
class UFPSREnemyRosterDataAsset;

/** One scheduled mission window: at a random time within [MinTime, MaxTime] (rolled once at run start), one
 *  mission is chosen uniformly at random from MissionPool and spawned (Game.MD §2-8). */
USTRUCT(BlueprintType)
struct FFPSRMissionWindow
{
	GENERATED_BODY()

	/** Earliest run-clock time (seconds) this window can fire. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Window", meta = (ClampMin = "0.0"))
	float MinTime = 60.0f;

	/** Latest run-clock time (seconds). Actual trigger = a random time in [MinTime, MaxTime], rolled at run
	 *  start. Set MinTime == MaxTime for an exact time. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Window", meta = (ClampMin = "0.0"))
	float MaxTime = 120.0f;

	/** Candidate missions — one is chosen uniformly at random when the window fires (empty = no-op). Restrict
	 *  the pool to control which missions can appear in this window (e.g. exclude HoldZone early). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Window")
	TArray<TObjectPtr<UFPSRMissionDataAsset>> MissionPool;
};

/** One anchor in the level-driven alive-count curve: at party Level, the spawn director targets Count alive enemies.
 *  The director interpolates piecewise-linearly between anchors (author them in ascending Level order). */
USTRUCT(BlueprintType)
struct FFPSRAliveCountAnchor
{
	GENERATED_BODY()

	/** Party level (FPSRGameState::GetPartyLevel) at this anchor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Alive Count", meta = (ClampMin = "1"))
	int32 Level = 1;

	/** Target alive enemy count at this level. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Alive Count", meta = (ClampMin = "0"))
	int32 Count = 10;
};

/** Data-driven run schedule (redesign 2026-06-04 / windows 2026-06-11, §2-8): time-windowed mission spawns
 *  (each fires once at a random time in its range, picking a random mission from its pool) + boss time + a
 *  level-scaled (preferred) or time-scaled enemy target count. No rounds — the run is continuous, frozen only for
 *  card selection. */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRRunScheduleDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Time-windowed missions (any order; the director rolls each window's trigger time at run start). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	TArray<FFPSRMissionWindow> MissionWindows;

	/** Data-driven enemy archetype mix for this run (Game.MD §2-6). The spawn director picks a class by weighted
	 *  random from the roster's rules each spawn. Null = the swarm spawns the single configured EnemyClass (no
	 *  regression — the melee-only behaviour before U5). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	TObjectPtr<UFPSREnemyRosterDataAsset> EnemyRoster;

	/** Run-clock time (seconds) at which the boss appears (Combat -> Boss; after this no missions / no timer). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	float BossTime = 300.0f;

	/** Boss to spawn at BossTime (which class + tuning). Null = the director spawns the C++ AFPSRBossBase
	 *  placeholder (so the victory loop is testable before boss content exists). Game.MD §2-7/§2-8. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	TObjectPtr<UFPSRBossDefinitionDataAsset> BossDefinition;

	/** Level-driven target alive count (preferred): piecewise-linear anchors over party level. When NON-EMPTY this
	 *  REPLACES the time ramp below — target = interp(GetPartyLevel()) (below the first anchor uses its Count, above
	 *  the last stays flat at its Count), clamped to MaxAliveCount. Empty = legacy time ramp (BaseAliveCount + …).
	 *  e.g. (1,10),(20,30),(30,50): density scales with progression, not the clock. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	TArray<FFPSRAliveCountAnchor> AliveCountByLevel;

	/** Target alive enemy count at run start (the spawn director's base intensity). LEGACY time ramp — used only when
	 *  AliveCountByLevel is empty. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	int32 BaseAliveCount = 40;

	/** Added to the target alive count per minute of survival, BEFORE the boss appears. LEGACY — used only when
	 *  AliveCountByLevel is empty. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	float AliveCountPerMinute = 30.0f;

	/** Added per minute AFTER the boss appears — the swarm persists and keeps ramping at this (higher) rate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	float AliveCountPerMinuteAfterBoss = 50.0f;

	/** Hard cap on the time-scaled target alive count. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	int32 MaxAliveCount = 300;

	/** Per director-tick spawn cap = enemies spawned each director tick. Combined with SpawnIntervalSeconds this is the
	 *  swarm FILL RATE (MaxSpawnPerTick / SpawnIntervalSeconds per second). Lower = enemies trickle in and the crowd
	 *  builds up / recovers gradually; higher = the swarm snaps to the target count fast. Tune for pacing feel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run", meta = (ClampMin = "1"))
	int32 MaxSpawnPerTick = 3;

	/** Director tick interval (seconds) = how OFTEN the swarm director spawns a batch. The per-second fill rate is
	 *  MaxSpawnPerTick / SpawnIntervalSeconds (e.g. 1 per 0.1s = 10/sec; 1 per 0.25s = 4/sec). Raise to slow the
	 *  spawn PACE without changing the target count. Tune for pacing feel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run", meta = (ClampMin = "0.02"))
	float SpawnIntervalSeconds = 0.1f;

	/** 스테이지 전환에서 **페이드아웃이 시작되기 전까지 버티는 구간**(초). 적은 정지하고 플레이어는 그대로
	 *  사격 가능. (정정 2026-08-20, 사용자 결정: 종전에는 이 창이 닫히면 잔존 적이 무적이 됐으나, 이제 전환
	 *  **전 구간** 피격 가능이다 — 페이즈가 전부 고정 길이라 보상 시간도 저작값의 합으로 고정된다. 이 값은
	 *  이제 "화면 변화 없이 갈아먹는 앞구간"의 길이일 뿐이다.)
	 *  ⚠️ 8초는 **미검증 시작값**이다 — PIE 로 체감을 보고 정한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run", meta = (ClampMin = "0.5"))
	float StageGraceSeconds = 8.0f;

	/** 다음 아레나가 **전원에게 준비될 때까지** 스왑을 미룰 최대 시간(초). ADR 0012 축5 는 다음 아레나를 한
	 *  스테이지 앞서 파킹하므로 보통은 0초 대기다 — 이 값은 저장장치가 느린 클라이언트 하나가 못 따라왔을 때만
	 *  쓰인다. 다 지나면 **경고를 남기고 그냥 스왑한다**(무한 대기가 더 나쁘다).
	 *  딜링 창(`StageGraceSeconds`)은 이 대기 前에 이미 닫히므로 **불변식 8 은 이 값과 무관하다** — 여기서
	 *  늘어나는 것은 보상 시간이 아니라 암전 대기다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run", meta = (ClampMin = "0.0"))
	float StageSwapReadyTimeoutSeconds = 5.0f;

	/** 스왑 전 **지형 페이드아웃** 길이(초, Phase B 연출 — 여기선 서버 상태기 타이밍만). 딜링 창(`StageGraceSeconds`)이
	 *  이미 닫힌 **뒤**의 고정 연출 구간이라 불변식 8과 무관하다. 0 = 페이드 생략(하드컷, 기존과 동일). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run", meta = (ClampMin = "0.0"))
	float StageFadeOutSeconds = 0.8f;

	/** 페이드아웃이 끝난 뒤 **완전 암전을 유지하는 최소 시간**(초). 이 시간이 지나야 스왑(텔레포트·이월)이
	 *  실행되고 페이드인이 시작된다 — "깜빡"하고 지나가는 암전이 싫을 때 올린다. 목적지 아레나가 아직 준비
	 *  안 된 클라이언트 대기(`StageSwapReadyTimeoutSeconds`)는 이 시간과 **별도로** 뒤에 이어질 수 있다(보통
	 *  0초). 0 = 유지 없이 즉시 스왑(이 파라미터 도입 전과 동일). ⚠️ 0.5초는 미검증 시작값 — PIE 체감으로
	 *  정한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run", meta = (ClampMin = "0.0"))
	float StageBlackoutHoldSeconds = 0.5f;

	/** 스왑 뒤 **페이드인** 길이(초, Phase B 연출). 0 = 생략(하드컷). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run", meta = (ClampMin = "0.0"))
	float StageFadeInSeconds = 0.8f;

	/** 전환이 끝나는 순간(페이드인 완료 = 스웜 언프리즈)부터 플레이어에게 주는 **그레이스**(초 — 무적 +
	 *  적 통과, 부활 그레이스와 같은 BeginGraceWindow 기제). 잔존 적을 이월하면서 필요해졌다: 전환 시작 때
	 *  근접거리에 있던 적이 상대 위치 그대로 이월돼 재개 프레임에 반응 시간 0으로 선공할 수 있다(종전엔
	 *  잔존 적이 소멸돼 구조적으로 불가능했던 패턴 — 머지 리뷰 C2). 0 = 그레이스 없음. ⚠️ 1초는 미검증
	 *  시작값. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run", meta = (ClampMin = "0.0"))
	float StagePostSwapGraceSeconds = 1.0f;

	/** 전환 시 새 아레나로 **이월**할 잔존 적의 비율 상한(0~1). 초과분은 새 진입 지점에서 먼 순으로 풀 반납된다.
	 *  1.0 = 전부 이월. 페이싱 손잡이 — 전환 후 새 아레나의 스폰 캡 계상은 이월된 ActiveEnemies 잔존 수로 자동
	 *  반영되므로(director tick의 GlobalAliveCap 게이트), 이 값을 낮추면 새 스테이지 시작이 더 비어 보인다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StageCarryOverMaxFraction = 1.0f;
};
