// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Containers/ArrayView.h" // TConstArrayView (EvalStageAt / EvalPartySizeMultiplier / EvalAliveCountByLevel)
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

/** One anchor in the stage-driven difficulty curve (신설 2026-08-26, ADR 0010 D6 비용 축 "일찍 부수려 할수록
 *  비싸다"): at StageIndex, AliveCountBonus/AliveCountMultiplier scale the alive-count target ON TOP OF the level
 *  curve (목표 = (레벨곡선 + Bonus) × Multiplier — 사용자 결정, 손잡이 2개), and InhibitorDurabilityMultiplier scales
 *  the active arena's suppressor durability (곱해지는 상대편 손잡이 = InhibitorDurabilityByPartySize, 아래).
 *  파티 레벨과 **별개** 축이다 — 레벨에 접는 대안은 기각됐다(설계 문서 "대안과 trade-off" 참고: "별개" 요구가
 *  깨지고, 내구도 경로엔 애초에 레벨 입력이 없어 표현이 안 된다). **MaxEliteAlive 는 네 번째, 독립된 축이다**
 *  (엘리트 동시 마릿수 하드캡, ADR 0013 불변식 6 + 후속 행 3 「구현 사양 B」) — 절대값이라 AliveCountMultiplier
 *  가 이 필드에는 적용되지 않는다(그 필드 자신의 주석 참조). Anchors are authored in ascending StageIndex and
 *  interpolated piecewise-linearly — the SAME convention as FFPSRAliveCountAnchor just above (below the first
 *  anchor uses its values, above the last stays flat), reusing this codebase's only curve idiom rather than
 *  introducing FScalableFloat/UCurveFloat (그러면 밸리데이터가 커브 내용을 검사할 수 없다 — 설계 §2). */
USTRUCT(BlueprintType)
struct FFPSRStageDifficultyAnchor
{
	GENERATED_BODY()

	/** Stage index (AFPSRGameState::GetStageIndex) at this anchor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Difficulty", meta = (DisplayName = "스테이지 인덱스", ClampMin = "0"))
	int32 StageIndex = 0;

	/** Added to the level-curve alive-count target at/after this stage, BEFORE AliveCountMultiplier below. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Difficulty", meta = (DisplayName = "마릿수 가산"))
	int32 AliveCountBonus = 0;

	/** Multiplies (level-curve target + AliveCountBonus). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Difficulty", meta = (DisplayName = "마릿수 배수", ClampMin = "0.01"))
	float AliveCountMultiplier = 1.0f;

	/** Multiplies InhibitorBaseDurability, on top of the party-size multiplier (InhibitorDurabilityByPartySize,
	 *  EvalPartySizeMultiplier) — the two axes compose by straight multiplication (실효 내구도 = 기본값 × 이 값 ×
	 *  인원수 배수). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Difficulty", meta = (DisplayName = "억제기 내구도 배수", ClampMin = "0.01"))
	float InhibitorDurabilityMultiplier = 1.0f;

	/** 엘리트 동시 마릿수 상한(ADR 0013 불변식 6 "엘리트 동시 마릿수는 하드캡을 갖는다" + 후속 행 3 「구현 사양
	 *  B」) — 스테이지별 **절대값**이다. AliveCountBonus 처럼 레벨곡선에 "가산"되는 값이 아니라 그 자체가
	 *  상한이고, **AliveCountMultiplier 는 이 필드에 적용되지 않는다**(그 배수는 일반 마릿수 축 전용). 소비자 =
	 *  UFPSREnemySpawnSubsystem 의 엘리트 캡 회계(ActiveEliteCount) — 실효 상한은 이 값과 그 서브시스템의
	 *  EliteHardCap(곡선과 무관한 절대 상한) 중 작은 쪽이다.
	 *  🔴 **기본값 0 = "엘리트 없음"이며 이게 옳은 무회귀 기본값이다 — "0 = 무제한"이 아니다.** 저작 안 된
	 *  StageDifficulty 배열은 EvalStageAt 이 항등(전 필드 기본값)을 돌려주고, 이 필드가 생기기 전의 현실이
	 *  정확히 "엘리트 0마리"였기 때문이다(엘리트 티어 골격 자체가 이 필드보다 나중에 생겼다). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stage Difficulty",
			  meta = (DisplayName = "엘리트 동시 상한", ClampMin = "0"))
	int32 MaxEliteAlive = 0;
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

	/**
	 * 보스 아레나를 **BossTime 보다 이만큼 먼저** 파킹한다(초). 보스 스테이지, 2026-08-28.
	 *
	 * ADR 0012 축 5 와 같은 논리다 — 목적지 서브레벨의 `AddToWorld` 는 프레임당 5ms 로 잘려 여러 프레임에 걸쳐
	 * 들어오므로, 전환 창 안에서 시작하면 창 길이가 하드웨어 함수가 된다(불변식 8 위반). 억제기 전환은 "한
	 * 스테이지 앞서" 파킹으로 해결하지만 보스 전환에는 앞선 스테이지가 없다 — 시계가 부른다. 그래서 시간으로 앞당긴다.
	 *
	 * 이 값이 `BossTime` 이상이면 사실상 **런 시작 시 파킹**이 된다(음수 시각은 0 으로 잘린다). 0 이면 사전
	 * 파킹을 하지 않고 전환이 목적지 준비를 기다린다(`StageSwapReadyTimeoutSeconds` 상한).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run", meta = (DisplayName = "보스 아레나 사전 파킹 리드(초)", ClampMin = "0.0"))
	float BossArenaParkLeadSeconds = 45.0f;

	/** Level-driven target alive count (preferred): piecewise-linear anchors over party level. When NON-EMPTY this
	 *  REPLACES the time ramp below — target = interp(GetPartyLevel()) (below the first anchor uses its Count, above
	 *  the last stays flat at its Count), clamped to MaxAliveCount. Empty = legacy time ramp (BaseAliveCount + …).
	 *  e.g. (1,10),(20,30),(30,50): density scales with progression, not the clock. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	TArray<FFPSRAliveCountAnchor> AliveCountByLevel;

	/** 스테이지 난이도 축 앵커(신설 2026-08-26, ADR 0010 D6 비용 축) — 파티 레벨과 **별개로** 스테이지가 오를
	 *  때마다 마릿수·억제기 내구도를 함께 올린다. **비어 있으면 항등**(가산 0·배수 1.0 — EvalStageAt 의 빈 배열
	 *  폴백) = 기존 스케줄은 완전 무회귀. StageIndex 오름차순 저작, 첫 앵커 아래·마지막 앵커 위는 flat clamp
	 *  (AliveCountByLevel 과 동일 규약). 적용 지점: UFPSRRunDirectorSubsystem::ComputeTargetAliveCount (마릿수
	 *  축) / ApplyStageDifficultyToArena (억제기 축, 스테이지 커밋마다 1회).
	 *  🔴 **첫 앵커는 StageIndex 0 의 항등 앵커(+0, x1.0, x1.0)로 시작할 것.** 첫 앵커 아래가 flat clamp 라,
	 *  첫 앵커를 (3, +8, x1.15, x1.6) 처럼 두면 스테이지 0 이 이미 x1.6 내구도로 시작하고 스테이지 0->3 의
	 *  억제기 파괴 3회가 난이도를 전혀 올리지 않는다 — 이 축의 전제가 앞 세 스테이지에서 죽는다. 의도적
	 *  저작일 수도 있어 밸리데이터는 Warning 으로만 짚는다(FPSRRunScheduleValidator, StageFirstAnchorNotIdentity). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run|난이도 축", meta = (DisplayName = "스테이지 난이도 앵커"))
	TArray<FFPSRStageDifficultyAnchor> StageDifficulty;

	/** 억제기(Suppressor) 기본 내구도의 SSOT — 종전 `BP_Inhibitor` CDO 하드값(실측 50, 문서·커밋 표기는 5000)을
	 *  대체한다(ADR 0010 §512 위험 재현 — "5000 고정이면 4인 파티는 약 13초"). 실효 내구도 = 이 값 ×
	 *  StageDifficulty 의 InhibitorDurabilityMultiplier × InhibitorDurabilityByPartySize(아래). 일반 파괴물
	 *  (상자·프롭)의 Durability(액터별 저작값, FPSRDestructible.h)는 이 축과 무관하게 그대로 남는다 — 억제기만
	 *  스테이지·인원수로 스케일한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run|난이도 축", meta = (DisplayName = "억제기 기본 내구도", ClampMin = "1.0"))
	float InhibitorBaseDurability = 5000.0f;

	/** 인원수(참가자 수, `AFPSRGameMode::GetParticipantCount` — DBNO 포함, 스펙테이터만 제외)별 억제기 내구도
	 *  배수. index 0 = 1인. 4인 협동의 화력 상쇄를 **부분 상쇄**한다(2026-08-26 사용자 결정: `×3.1` = 솔로 대비
	 *  4인 소요시간 1.3배 — 완전 상쇄 `×4.0` 은 인원수를 무의미하게 만들고, 상쇄 없음 `×1.0` 은 §512 위험
	 *  그 자체라 기각됨). 인원수가 배열 길이를 넘으면 마지막 값 고정(외삽 없음). 비었으면 배수 1.0 폴백
	 *  (문서화된 동작 — 5단계 밸리데이터가 Warning으로 짚는다). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run|난이도 축", meta = (DisplayName = "인원수별 억제기 내구도 배수"))
	TArray<float> InhibitorDurabilityByPartySize = {1.0f, 1.8f, 2.5f, 3.1f};

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

	// ---------------------------------------------------------------------------------------------------------
	// Pure, worldless evaluators (신설 2026-08-26) — unit-tested directly with no instance/world (FPSRoguelite.
	// Run.Difficulty). Class statics rather than a free function in a new FPSRRunDifficulty.h: a standalone
	// evaluator header would need FFPSRStageDifficultyAnchor/FFPSRAliveCountAnchor (both defined above, in THIS
	// header), while this header's own UPROPERTY arrays need the evaluator header's complete types back — a
	// mutual-include cycle. Class statics keep data and evaluator in one file with no cycle, mirroring
	// UFPSRStageDirectorSubsystem::ComputeStageSeed — this codebase's existing precedent for "class-static pure
	// function + worldless unit test" (the other pure-function idiom here, FPSREnemyAllocator, is a free-function
	// namespace, but that one has no struct-type mutual-dependency to avoid).
	// ---------------------------------------------------------------------------------------------------------

	/** Piecewise-linear interpolation of the stage-difficulty anchors at StageIndex. Returns the WHOLE anchor
	 *  interpolated as one unit (not per-field) — walking the array once per field would need three (now four)
	 *  separate interpolation passes that could drift from each other, which is exactly why MaxEliteAlive (ADR
	 *  0013's elite-cap curve, landed C3 「구현 사양 B」) was just one more struct field + one more Lerp line in the
	 *  middle-interpolation branch below, not a second walk. Empty Anchors -> identity (StageIndex 0, Bonus 0, both
	 *  multipliers 1.0, MaxEliteAlive 0), so an unauthored StageDifficulty array is a complete no-op. Anchors must be
	 *  authored in ascending StageIndex (enforced by UFPSRRunScheduleValidator); below the first / above the last
	 *  clamps flat, same rule as EvalAliveCountByLevel. */
	static FFPSRStageDifficultyAnchor EvalStageAt(TConstArrayView<FFPSRStageDifficultyAnchor> Anchors, int32 StageIndex);

	/** InhibitorDurabilityByPartySize lookup — index (PartySize - 1): index 0 = 1 player. PartySize <= 0 clamps to
	 *  index 0 (defensive; GetParticipantCount should never actually be <= 0 when this is called); PartySize
	 *  beyond the array holds the LAST entry (no extrapolation — a 5th player just gets the 4-player rate rather
	 *  than an authored value). Empty ByPartySize -> 1.0 (documented fallback — see the validator's Warning on an
	 *  empty array). */
	static float EvalPartySizeMultiplier(TConstArrayView<float> ByPartySize, int32 PartySize);

	/** Piecewise-linear interpolation of the level->alive-count anchors at Level. Moved here (2026-08-26) from an
	 *  anonymous namespace in FPSRRunDirectorSubsystem.cpp — pure relocation, behavior is byte-identical — so it is
	 *  unit-testable without a world; ComputeTargetAliveCount's call site was already being touched to compose this
	 *  output with the new stage-difficulty multiplier, so moving it out cost no extra rework. Anchors authored in
	 *  ascending Level; below the first / above the last clamps flat. Empty Anchors -> 0.0 (matches the original
	 *  inline function's own early-out). */
	static float EvalAliveCountByLevel(TConstArrayView<FFPSRAliveCountAnchor> Anchors, int32 Level);
};
