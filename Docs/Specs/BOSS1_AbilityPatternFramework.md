# BOSS1 — 보스 어빌리티 · 패턴 프레임워크 (공격 패턴 3종)

## 1. 메타

| 항목 | 값 |
|---|---|
| 유닛 ID / 이름 | **BOSS1** — 보스 어빌리티·패턴 프레임워크 |
| 브랜치 | `feat/boss-patterns` |
| 작성 모델 | `claude-opus-5` (G1 반려 후 재제출 초안 — §6-5 (2) 는 "Opus 가 모은 조사 + 플랜/명세 초안"을 게이트 입력으로 규정) |
| 작성일 / 최종 갱신 | 2026-09-02 |
| 상태 | **초안 (G1 재제출 대기)** |
| 관련 SSOT | `RunFlow.md` §2-7 · `Enemy.md` §2-6(시간축 계약·원거리 규격) · `Game.md` §1 · `Performance.md` §5 |
| 관련 ADR | [0010](../Architecture/0010-arena-topology-and-stage-transition.md) 아레나 위상 · [0013](../Architecture/0013-enemy-tier-axis-and-elite-gas.md) **§「전환 조건」이 이 유닛의 트리거다** · 신설 예정 0015 |
| 선례 명세 | [`VIT1_ShieldHealthTwoLayer.md`](VIT1_ShieldHealthTwoLayer.md) |
| 관련 메모리 | `[[haiku-delegation-security-wiring]]` (프리즈 대칭 = Opus 직접) · `[[fetch-before-branching-clone2-stale]]` |

### 1-1. 사용자 결정 (2026-09-02)

| # | 결정 |
|---|---|
| 페이즈 | **3페이즈 + 페이즈 수 자체를 데이터로** (임계값 배열 길이가 곧 N) |
| 포격 | **전원 각각 5발** (4인 = 20발 동시 진행) |
| 레이저 | **지형 관통** — 회피는 점프뿐 |
| 착수 | 지금 (§6-9 (8) 개시 규칙을 예외 ① *사용자 직접 지시* 로 당김 — 보드 행 로그에 사유 기입) |

---

## 2. 목표 / 비목표

### 목표 — 이 유닛이 끝나면

1. 보스가 **서버 권위로 스스로 공격한다.** 전역 프리즈(§2-2)와 스테이지 전환 중에는 **어떤 시간도 흐르지 않는다** — 신관·회전각·추적시간·쿨다운 전부.
2. 보스가 **체력 비율로 페이즈를 올린다.** 페이즈 수는 DA 의 임계값 배열 길이가 정한다(코드 상수 없음). 회복해도 내려가지 않는다.
3. **패턴 3종**이 BP 로 저작되고 수치가 전부 데이터다:
   - **포격** — 살아있는 플레이어 전원에게 각 5발, 2초 간격, 지연 신관 + 지상 표식
   - **회전 레이저** — 시계방향 스윕, 점프로 회피, 페이즈당 빔 +1
   - **추적 오브** — 대상 1명을 쫓는 5기, 체력 150, 격추 가능
4. **각 패턴을 콘솔에서 강제 발동**해 슬라이스마다 PIE 검증이 가능하다.
5. 보스가 죽거나 런이 재시작돼도 **보스가 낳은 것이 하나도 남지 않는다.**

### 비목표 — 일부러 하지 않는 것

- **보스 이동 · AIController · StateTree.** 세 패턴 전부 "정지한 중앙 타워" 전제에서 성립한다.
- **AttributeSet 신설.** 체력은 계속 `UFPSREnemyHealthComponent`(VIT1 2층). 보스 실드도 이번엔 0(데이터 플립 한 번이면 켜지지만 요구가 아니다).
- **보스가 스웜을 때리는 것.** §3-1 참조.
- **`AFPSRProjectile` 의 적팀 AOE 루프(`FPSRProjectile.cpp:344-375`)를 새 함수로 접는 것.** 라이브 경로 리팩토링이라 이 브랜치에서 뺀다(후속 행). — *G1 P3-6*
- **보스 다중화 · 패턴 절차생성 · 패턴 간 콤보.**
- **호스트 마이그레이션 대응** — `Source/`·`Docs/SSOT/` 참조 0건 = 지원 경로가 아니다. — *G1 P2-2*

---

## 3. 제1원리 3줄 (핵심원칙 4)

1. **제1원리 근거** — 보스는 **1마리**라 "액터당 비용 최소화"가 걸리지 않는다. 대신 걸리는 것은 *보스전에도 스웜 200~300 이 그대로 남는다*는 사실(`FPSRRunDirectorSubsystem.h:112`)이다. 그래서 모든 패턴 판정은 **플레이어 ≤4 에만** 붙어 O(4) 로 고정되고, 표식은 액터가 아니라 **보스 1개의 복제 배열**로 산다(스폰/파괴 0, 복제 채널 0개 추가).
2. **엔진 기본값·기존 인프라와의 관계** — 엔진 GAS 를 쓰되 **시간축만 덮는다.** 엔진은 GE duration/period 와 AbilityTask 를 월드 `FTimerManager` 로 돌리므로(`GameplayEffect.cpp:4409,4431`) 상태 게이트인 §2-2 프리즈를 그냥 뚫는다. 이 프로젝트는 이미 그 계약을 `UFPSREliteGameplayAbility` 로 확립했고, **VIT1 이 그 시간원을 전역화해 뒀다** — `AFPSRGameState::GetCombatClockSeconds()`(`FPSRGameState.h:129-137`, 월드시간 − 누적동결, 틱 0). 보스는 새 시계를 만들지 않고 **그것을 읽는다.**
3. **정합** — 체력이 `UFPSREnemyHealthComponent` 에 남으므로 전 무기 경로가 보스와 오브를 공짜로 때리는 D1 브릿지가 유지된다. 그리고 ADR 0013 「전환 조건」이 *"보스와 공유하는 코드가 `UFPSREnemyHealthComponent` 를 넘어 늘어나면 비목표(보스 흡수 없음)를 다시 본다"* 고 스스로 적어 뒀다 — **이 유닛이 정확히 그 트리거**라 0013 개정 + 0015 신설이 산출물에 들어간다.

### 3-1. 왜 GAS 인가 (G1 P2-5 — 종전 근거 2개가 사실이 아니었다)

| 종전 근거 | 판정 |
|---|---|
| "활성화가 클라에 복제돼 연출이 공짜" | ❌ **거짓.** 오너 없는 `Minimal` ASC 에서 `ActivatableAbilities` 는 `COND_ReplayOrOwner` 라 시뮬 프록시에 안 간다. 전원에 가는 건 태그·큐·몽타주뿐 |
| "태그 배타를 공짜로 얻는다" | ❌ **부분 거짓.** 엔진 `BlockAbilitiesWithTag` 는 GAS 없이도 흉내낼 수 있고, 우리는 어차피 선택기를 따로 짠다 |
| `Game.md §1` 허용 + 엘리트 선례 일관성 + **BP 저작 가능한 어빌리티 목록** | ✅ **이것이 남는 진짜 근거다** |

**대가(명시)** — BP 자식이 `WaitDelay`·`PlayMontageAndWait` 같은 시간형 노드를 쓰면 **프리즈를 뚫는다.** 런타임 가드는 *수신 GE* 만 막고 AbilityTask 는 가드 밖이다(`Enemy.md:64`). → §9 저작 규칙에 금지로 명시하고, §12 검증에 PIE 항목으로 넣는다.

---

## 4. 파일 목록

### S1 — 구동축 + 포격 (첫 가시 슬라이스)

| 경로 | 신규/수정 | 한 줄 설명 |
|---|---|---|
| `Public/AbilitySystem/FPSRAbilitySystemComponent.h` · `Private/...cpp` | 수정 | 시간축 가드를 여기로 호이스트(`EnableTimeAxisGuard()`) — 엘리트와 보스가 **1벌**을 공유 |
| `Public/Enemy/FPSREnemyEliteBase.h` · `Private/...cpp` | 수정 | 인라인 등록 → `EnableTimeAxisGuard()` 호출로 교체(거동 무변) |
| `Public/AbilitySystem/Abilities/FPSRBossGameplayAbility.h` · `.cpp` | 신규 | 프리즈-멈춤 쿨다운 계약 + `MinPhase` + `ServerTickPattern` |
| `Public/Boss/FPSRBossTypes.h` | 신규 | `FFPSRBossBlastMark` · `FPSRBoss` 순수 네임스페이스 |
| `Public/Boss/FPSRBossBase.h` · `Private/...cpp` | 수정 | ASC · 프리즈 게이트 틱 · 페이즈 · 선택기 · 표식 배열 · 자식 회수 |
| `Public/Boss/FPSRBossDefinitionDataAsset.h` · `.cpp` | 수정 | `PhaseHealthThresholds` + `IsDataValid` |
| `Public/AbilitySystem/Abilities/FPSRBossGA_Barrage.h` · `.cpp` | 신규 | 포격 패턴 |
| `Public/Combat/FPSRTargeting.h` · `.cpp` | 신규 | `IsEligibleTarget()` **술어만** 추출 (G1 P3-11) |
| `Private/Enemy/FPSREnemySpawnSubsystem.cpp` | 수정 | 술어 호출로 교체(할당 0 · 루프 1회 유지) |
| `Private/Director/FPSRDirectorSensorSubsystem.cpp` | 수정 | `ClassifyDamageSource` 에 `GetOwner()` 폴백 3줄 (G1 P2-1) |
| `Private/Run/FPSRRunDirectorSubsystem.cpp` | 수정 | 콘솔 `FPSR.BossPattern` · `FPSR.BossPhase` |
| `Private/Tests/FPSRBossPatternTest.cpp` | 신규 | 순수 술어 테스트 |

### S2 — 회전 레이저 / S3 — 추적 오브 / S4 — 콘텐츠

| 경로 | 신규/수정 | 슬라이스 |
|---|---|---|
| `Public/AbilitySystem/Abilities/FPSRBossGA_SweepLaser.h` · `.cpp` | 신규 | S2 |
| `Public/Boss/FPSRBossLaserMath.h` (헤더 온리 `namespace`) | 신규 | S2 |
| `Public/Boss/FPSRBossHomingOrb.h` · `.cpp` | 신규 | S3 |
| `Public/AbilitySystem/Abilities/FPSRBossGA_HomingOrbs.h` · `.cpp` | 신규 | S3 |
| `Content/Character/Boss/*` · `DA_BossDefinition` | 저작 | S4 (사용자) |
| `Docs/Architecture/0015-*.md` · `0013` 개정 · `RunFlow.md` · `Enemy.md` · `Game.md` | 문서 | S4 |

---

## 5. 인터페이스 선언 (헤더 스케치)

### 5-1. `FPSRBossTypes.h` — 표식 · 순수 함수

```cpp
/** 지연 폭발 표식 1개. 액터가 아니라 보스가 배열로 들고 복제한다 (G1 P2-3):
 *  ① 기본 NetCullDistance 150 m < 아레나 대각 226 m 라 액터 표식은 먼 팀원에게 안 보인다 —
 *     표식은 남의 것도 보여야 피해서 달린다.
 *  ② 액터 스폰/파괴 0, 복제 채널 0개 추가(보스는 bAlwaysRelevant).
 *  ③ 프리즈·teardown 소유자가 보스 하나로 통일된다(§8).
 *  ④ 지연합류(JIP)는 배열이 곧 상태라 공짜. */
USTRUCT(BlueprintType)
struct FFPSRBossBlastMark
{
    GENERATED_BODY()

    /** 아레나 바닥 Z 로 스냅된 착탄점 — 공중에 뜬 플레이어를 노려도 표식은 지면에 눕는다.
     *  단층 평면(ADR 0010 D4)이라 트레이스 없이 아레나 Z 상수로 스냅한다. (G1 P3-5) */
    UPROPERTY(BlueprintReadOnly) FVector_NetQuantize Center = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly) float Radius = 0.0f;

    /** 터지는 시각 — 보스 패턴 클럭(§5-3 PatternClockSeconds) 기준. 클라는 복제된 클럭과 비교해
     *  남은 시간을 그린다. 절대 월드시간이 아니라 프리즈-멈춤 시계다. */
    UPROPERTY(BlueprintReadOnly) float DetonateAtClock = 0.0f;

    /** 어느 플레이어를 노린 것인가 — 클라가 "내 것"을 다르게 그릴 수 있게(4인 20발 판독성, §11-1). */
    UPROPERTY(BlueprintReadOnly) TWeakObjectPtr<APawn> TargetPawn;

    uint8 MarkId = 0; // 배열 재정렬과 무관하게 코스메틱을 짝짓기 위한 키
};

namespace FPSRBoss
{
    /** HealthFraction(0~1) + **내림차순** 임계 배열 → 1-기반 페이즈. 빈 배열이면 항상 1.
     *  경계는 "이하"로 연다: Fraction <= Thresholds[i] 이면 페이즈 i+2. */
    FPSROGUELITE_API int32 ComputePhase(float HealthFraction, TConstArrayView<float> Thresholds);

    /** 회복으로 페이즈가 되돌아가지 않게 하는 단조 래치. */
    FORCEINLINE int32 LatchPhase(int32 Current, int32 Computed) { return FMath::Max(Current, Computed); }
}
```

### 5-2. `FPSRBossLaserMath.h` — 레이저 판정 (헤더 온리 순수 함수)

> `namespace FPSRVitals`(상태 0 · 복제 0 · 순수 O(1), `FPSRVitals.h:24-31`)와 같은 형식이다.

```cpp
namespace FPSRBossLaser
{
    /** 각을 [-Half, +Half) 로 접는다. Period=360/N 이면 빔 N개를 **한 번의 비교**로 처리한다
     *  — 빔은 등간격이므로 주기 도메인에서 전부 같은 점이다. (G1 P1-2: 비용 12→4회/프레임) */
    FORCEINLINE float WrapToPeriod(float Deg, float Period)
    {
        const float H = Period * 0.5f;
        return FMath::Fmod(FMath::Fmod(Deg + H, Period) + Period, Period) - H;
    }

    /** 상대각의 **부호 반전** = 빔 1회 통과 = 1히트.
     *
     *  🔴 왜 "빔이 이번 프레임에 쓸고 간 호"가 아닌가 (G1 P1-2):
     *  호 방식은 플레이어의 *현재* 방위각 하나만 보므로, 플레이어가 움직이면 폭 Δp 만큼 구간이 어긋난다.
     *  30°/s 빔 · 33 ms 프레임(Δb=1°) 기준 보스 코앞(r≈13 m)에서 달리기 600 cm/s = 26°/s(Δp≈0.87°) 면
     *  **교차의 약 46% 를 놓치고**, 대시로 빔을 추월하면 전부 놓치면서 "호 안이지만 빔 뒤"인
     *  플레이어를 거짓 히트한다. 상대각은 두 움직임을 한 값에 합치므로 누가 움직이든 교차가 잡힌다.
     *
     *  @param PrevRel  직전 프레임의 WrapToPeriod(플레이어각 − 빔각)
     *  @param CurRel   이번 프레임의 같은 값
     *  @param MaxStep  이보다 크게 튄 전이는 도메인 랩으로 보고 히트로 치지 않는다(= Period/2)
     *  @param HalfWidthDeg  빔 반폭 + 플레이어 캡슐 각반경 asin(R/r) (G1 P3-4) */
    FORCEINLINE bool DidCross(float PrevRel, float CurRel, float MaxStep, float HalfWidthDeg)
    {
        if (FMath::Abs(CurRel - PrevRel) > MaxStep) { return false; } // 랩 — 교차 아님
        if (FMath::Abs(CurRel) <= HalfWidthDeg)     { return true;  } // 빔 안에 들어와 있다
        return (PrevRel < 0.0f) != (CurRel < 0.0f);                   // 부호 반전 = 통과
    }

    /** 플레이어 캡슐이 반지름 r 에서 차지하는 각반경(도). 보스 코앞에서 얇은 빔이 캡슐을
     *  시각적으로 스치는데 판정만 빗나가는 것을 막는다. (G1 P3-4) */
    FORCEINLINE float CapsuleAngularRadiusDeg(float CapsuleRadiusCm, float DistanceCm)
    {
        return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(CapsuleRadiusCm / FMath::Max(DistanceCm, 1.0f), 0.0f, 1.0f)));
    }
}
```

### 5-3. `AFPSRBossBase` 추가분

```cpp
UCLASS()
class FPSROGUELITE_API AFPSRBossBase : public ACharacter, public IAbilitySystemInterface
{
public:
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

    /** 1-기반. 복제 — HUD 페이즈 표시와 BP 연출이 읽는다.
     *  🔴 루즈 게임플레이 태그는 쓰지 않는다 (G1 P2-4): `Boss.Phase.One/Two` 는 참조 0건이고,
     *  태그 집합을 One~Five 로 선언하는 순간 "페이즈 수를 데이터로"라는 사용자 결정이 C++ 상수로
     *  되돌아간다. 게이트는 이 int 와 어빌리티의 MinPhase 로 충분하다.
     *  (덧붙여 AddLooseGameplayTag 는 애초에 복제되지 않는다.) */
    UFUNCTION(BlueprintPure, Category = "FPSR|Boss")
    int32 GetCurrentPhase() const { return CurrentPhase; }

    /** 프리즈-멈춤 패턴 시계. 서버 = AFPSRGameState::GetCombatClockSeconds() 를 그대로 통과시킨다.
     *  클라 = 아래 ReplicatedPatternClock + 수신 후 로컬 외삽(프리즈 중 정지).
     *  🔴 왜 복제하는가 (G1 P1-3): 이 기믹의 유일한 회피 단서가 **눈에 보이는 빔**이라, 클라가
     *  각도를 자기 마음대로 적분하면 편도 지연만큼 서버 히트 호와 어긋나고(30°/s · 100 ms = 3° =
     *  빔 반폭 1~2개분) 지연합류는 시작점이 없어 무한 드리프트다. 표식 신관·오브 연출도 같은
     *  시계를 쓰므로 세 패턴의 클라 시간축이 하나로 통일된다. */
    UFUNCTION(BlueprintPure, Category = "FPSR|Boss")
    float GetPatternClockSeconds() const;

    /** 클라 코스메틱용 — 현재 빔 각도(도). 빔 비활성이면 0 과 함께 false. */
    UFUNCTION(BlueprintPure, Category = "FPSR|Boss")
    bool GetBeamState(int32& OutBeamCount, float& OutBaseAngleDeg) const;

    UFUNCTION(BlueprintPure, Category = "FPSR|Boss")
    const TArray<FFPSRBossBlastMark>& GetBlastMarks() const { return BlastMarks; }

    // ---- 서버: 패턴이 부르는 것 ------------------------------------------------------------

    /** 표식 1개 예약. 반환 = MarkId. 배열은 Push Model 로 복제된다. */
    uint8 ServerAddBlastMark(const FVector& Center, float Radius, float FuseSeconds, APawn* TargetPawn);

    /** 보스가 스폰한 액터(오브)를 등록 — §8 회수 목록에 들어간다. */
    void ServerRegisterPatternActor(AActor* Actor);

    /** 빔 상태 복제(활성화/해제 시 1회씩). BeamCount=0 이면 비활성. */
    void ServerSetBeamState(int32 BeamCount, float BaseAngleDeg, float AngularSpeedDegPerSec);

    /** 서버 관대 창 — "최근 AirborneGraceSeconds 내에 공중이었으면 공중으로 본다".
     *  🔴 왜 (G1 P1-3 ②): 시각 동기가 완벽해도 클라의 점프 입력은 RTT/2 뒤에 서버에 닿는다.
     *  PvE 협동에서 판정은 플레이어 유리 방향으로 여는 것이 표준이다. */
    bool WasRecentlyAirborne(const APawn* Pawn) const;

protected:
    /** 소유자 = 액터 자신. AFPSREnemyEliteBase.h:114-123 과 동형(오너 클라이언트가 없으므로 Minimal). */
    UPROPERTY(VisibleAnywhere, Category = "FPSR|Boss")
    TObjectPtr<UFPSRAbilitySystemComponent> AbilitySystem;

    /** 콘텐츠가 저작하는 패턴 목록. 선택기가 이 순서로 라운드로빈한다.
     *  보스는 풀링되지 않으므로 BeginPlay(서버)에서 1회만 부여한다(엘리트는 Activate 마다 재부여). */
    UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Patterns")
    TArray<TSubclassOf<UFPSRBossGameplayAbility>> GrantedAbilities;

    /** 패턴 종료 후 다음 시도까지의 최소 간격(초). 패턴 간 배타는 여기가 아니라 어빌리티별
     *  BlockAbilitiesWithTag 로 BP 에서 저작한다 — 전역 배타를 코드에 박으면 후반 페이즈의
     *  자연스러운 상승(레이저 + 포격 동시)이 원천 봉쇄된다. (G1 P3-9) */
    UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Patterns", meta = (ClampMin = "0.0"))
    float PatternGapSeconds = 2.0f;

    UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Patterns", meta = (ClampMin = "0.0"))
    float AirborneGraceSeconds = 0.12f;

    virtual void PostInitializeComponents() override; // InitAbilityActorInfo + EnableTimeAxisGuard
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    UFUNCTION() void OnRep_CurrentPhase();
    UFUNCTION() void OnRep_BeamState();
    UFUNCTION() void OnRep_BlastMarks();

    /** 클라 코스메틱 훅 — 연출은 100% BP. Source/ 에는 데칼·Niagara 호출이 0건이고 그 규약을 깨지 않는다. */
    UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Boss")
    void OnPhaseChangedCosmetic(int32 NewPhase);
    UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Boss")
    void OnBlastMarkAdded(const FFPSRBossBlastMark& Mark);
    UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Boss")
    void OnBlastMarkDetonated(const FFPSRBossBlastMark& Mark);
    UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Boss")
    void OnBeamStateChangedCosmetic(int32 BeamCount, bool bWarmup);

private:
    UPROPERTY(ReplicatedUsing = OnRep_CurrentPhase) int32 CurrentPhase = 1;
    UPROPERTY(ReplicatedUsing = OnRep_BlastMarks)   TArray<FFPSRBossBlastMark> BlastMarks;
    UPROPERTY(ReplicatedUsing = OnRep_BeamState)    int32 BeamCount = 0;
    UPROPERTY(ReplicatedUsing = OnRep_BeamState)    float BeamBaseAngleDeg = 0.0f;
    UPROPERTY(ReplicatedUsing = OnRep_BeamState)    float BeamAngularSpeedDegPerSec = 0.0f;

    /** 저빈도 미러 — SetRunClockSeconds(FPSRGameState.cpp:352-362)의 데드밴드 관용구를 그대로 쓴다. */
    UPROPERTY(Replicated) float ReplicatedPatternClock = 0.0f;

    /** 보스가 스폰한 것 전부(오브). 프리즈 엣지 푸시와 §8 회수가 이 목록 하나를 쓴다. */
    TArray<TWeakObjectPtr<AActor>> SpawnedPatternActors;

    int32 NextPatternIndex = 0;
    float LastPatternEndClock = -1.0f;
    FGameplayAbilitySpecHandle ActivePatternHandle;
    TArray<FGameplayAbilitySpecHandle> GrantedAbilityHandles;
    TMap<TWeakObjectPtr<APawn>, float> LastAirborneClock;
    bool bWasFrozenLastTick = false;
};
```

### 5-4. `UFPSRBossGameplayAbility`

```cpp
/** 보스 패턴의 베이스. UFPSREliteGameplayAbility 와 **같은 계약**(표준 쿨다운 GE 경로를 셋 다 대체)
 *  이지만 시계가 다르다: 엘리트는 자기 ServerTickAttack 누산기, 보스는 전역
 *  AFPSRGameState::GetCombatClockSeconds()(VIT1). 40줄을 복제하는 대신 시계만 갈아끼운다. */
UCLASS(Abstract)
class FPSROGUELITE_API UFPSRBossGameplayAbility : public UFPSRGameplayAbility
{
public:
    virtual bool CheckCooldown(...) const override;      // 전투시계 스탬프 비교
    virtual void ApplyCooldown(...) const override;      // 전투시계 스탬프 기록
    virtual UGameplayEffect* GetCooldownGameplayEffect() const override { return nullptr; }

    /** 🔴 패턴의 유일한 시간원. 보스 Tick 이 활성 패턴에만 부른다.
     *  AbilityTask(WaitDelay 등)를 쓰면 프리즈를 뚫는다 — 가드 밖이라 문서 약속으로만 금지된다. */
    virtual void ServerTickPattern(float DeltaSeconds) {}

    int32 GetMinPhase() const { return MinPhase; }

protected:
    UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Cooldown", meta = (ClampMin = "0.0"))
    float CooldownSeconds = 0.0f;

    /** 이 페이즈 이상에서만 선택기가 고른다. 1 = 처음부터. */
    UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Pattern", meta = (ClampMin = "1"))
    int32 MinPhase = 1;

    AFPSRBossBase* GetBoss() const;
    float GetClock() const; // GetBoss()->GetPatternClockSeconds()

private:
    mutable float LastActivationClock = -1.0f;
};
```

### 5-5. 패턴 3종 — 저작 파라미터

```cpp
UCLASS() class UFPSRBossGA_Barrage : public UFPSRBossGameplayAbility
{
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="1")) int32 ShellsPerPlayer = 5;
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="0.1")) float IntervalSeconds = 2.0f;
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="0.1")) float FuseSeconds = 1.4f;
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="1.0")) float RadiusCm = 500.0f;
    UPROPERTY(EditDefaultsOnly) float Damage = 25.0f;
    UPROPERTY(EditDefaultsOnly) float KnockbackStrength = 0.0f;
    // 인터벌 누산은 while 루프 — 히치가 한 발을 삼키지 않게. (G1 P3-5)
};

UENUM() enum class EFPSRBeamStartAngle : uint8 { ArenaFixed, MaxGapCenter };

UCLASS() class UFPSRBossGA_SweepLaser : public UFPSRBossGameplayAbility
{
    /** 🔴 웜업 = Enemy.md:79 요구("히트스캔이면 차징 유예 + 사전경고 인디케이터 필수 — 부조리 탄막 금지").
     *  이 구간에는 빔이 **렌더되되 데미지가 0**이고, 시작·종료에 기존 Client/Reliable RPC
     *  AFPSRPlayerController::ClientNotifyRangedTarget(FPSRPlayerController.h:137)을 보스 UniqueID 로
     *  브래킷한다 — 신규 RPC 0(원거리 적이 쓰는 바로 그 재사용, Enemy.md:82). (G1 P1-1) */
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="0.0")) float WarmupSeconds = 1.5f;

    /** 시작각 규칙. MaxGapCenter = 생존자 방위각들의 가장 큰 간격 한가운데에서 시작 —
     *  활성화 프레임에 누군가의 머리 위에서 빔이 태어나는 사고를 구조적으로 없앤다. (G1 P1-1) */
    UPROPERTY(EditDefaultsOnly) EFPSRBeamStartAngle StartAngleRule = EFPSRBeamStartAngle::MaxGapCenter;

    UPROPERTY(EditDefaultsOnly) float AngularSpeedDegPerSec = 30.0f;   // 양수 = 시계방향
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="1")) int32 BeamsPerPhase = 1;  // 빔 수 = min(페이즈*이것, MaxBeams)
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="1")) int32 MaxBeams = 5;       // 코드 상수 아님 (G1 P2-4)
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="0.0")) float BeamHeightCm = 60.0f;
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="0.1")) float BeamHalfWidthDeg = 1.5f;
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="0.0")) float InnerRadiusCm = 1250.0f;
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="0.1")) float Revolutions = 2.0f;
    UPROPERTY(EditDefaultsOnly) float Damage = 30.0f;
};

UCLASS() class UFPSRBossGA_HomingOrbs : public UFPSRBossGameplayAbility
{
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="1", ClampMax="8")) int32 OrbCount = 5;
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="1.0")) float OrbHealth = 150.0f;
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="0.1")) float TrackSeconds = 8.0f;
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="0.0")) float SpawnIntervalSeconds = 0.25f;
    UPROPERTY(EditDefaultsOnly) float SpawnSpreadDeg = 40.0f;
    UPROPERTY(EditDefaultsOnly) float Damage = 20.0f;
    UPROPERTY(EditDefaultsOnly) TSubclassOf<AFPSRBossHomingOrb> OrbClass;
    // 스폰이 끝나면 어빌리티는 즉시 종료한다 — 오브는 독립 생명(보스가 오브를 기다리며 서 있지 않게).
};
```

### 5-6. `AFPSRBossHomingOrb`

```cpp
/** 격추 가능한 추적 투사체. 선례 = AFPSRMissionFleeTarget(APawn + ECC_Pawn 캡슐 +
 *  UFPSREnemyHealthComponent + 복제 이동, 스웜 풀과 독립 — FPSRMissionFleeTarget.h:17).
 *  AFPSRProjectile 이 아닌 이유: 그 헤더가 결정론 직선을 클라 예측의 전제로 못박았고
 *  (FPSRProjectile.h:13-16) 오브젝트 타입이 WorldDynamic 이라 데미지 질의가 못 찾는다. */
UCLASS()
class FPSROGUELITE_API AFPSRBossHomingOrb : public APawn
{
public:
    AFPSRBossHomingOrb(); // 🔴 PrimaryActorTick.bCanEverTick = true 를 **명시** — AActor 계열 기본값은
                          //    false 이고, VIT1 이 정확히 이걸 빠뜨려 힐팩이 통째로 죽어 있었다(P1).

    /** 서버. Instigator 는 **이 오브 자신**을 넘긴다 — 피격 방향 인디케이터가 인스티게이터 *위치*를
     *  쓰므로(FPSRCharacter.cpp:1645-1650) 보스를 넘기면 등 뒤 오브가 "중앙에서 맞았다"가 된다.
     *  텔레메트리 분류는 SetOwner(Boss) + ClassifyDamageSource 의 GetOwner() 폴백이 책임진다. (G1 P2-1) */
    void ServerLaunch(AFPSRBossBase* OwningBoss, APawn* Target, float InHealth, float InTrackSeconds, float InDamage);

    /** 보스가 프리즈 엣지에 **밀어 준다**(각자 폴링하지 않는다 — 선례
     *  UFPSRProjectileSubsystem::Tick:25-63 이 감지기 1개 + 푸시 구조다). PMC Deactivate 로 속도 보존.
     *  수명은 FTimerManager 가 아니라 Δt 누산기다 (G1 P3-3). */
    void SetSimulationPaused(bool bPaused);

protected:
    UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Sphere;        // ECC_Pawn, PlayerPawn=Overlap
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;      // NoCollision
    UPROPERTY(VisibleAnywhere) TObjectPtr<UFPSREnemyHealthComponent> Health;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProjectileMovementComponent> Movement;

    /** 🔴 SetCountsAsKill(false) — 막는 것은 **흡혈 / DealtDamage 이벤트 / 킬 크레딧 / on-kill 프래그먼트**다
     *  (FPSRCombatStatics.cpp:168-183). XP 는 애초에 AFPSREnemyBase::HandleDeath 에서만 떨어지므로
     *  이 플래그와 무관하다 — 종전 초안이 "XP 무한 파밍"이라 적은 것은 **틀렸다**. (G1 P3-1) */

    /** 격추 연출을 클라가 보게 하려면 OnDeath 에서 즉시 Destroy 하면 안 된다(bDead 복제가 안 나간다).
     *  N프레임 dwell 후 파괴한다. (G1 P3-7) */
};
```

---

## 6. 함수별 계약

| 함수 | 권위 | 호출자 | 전제조건 | 실패 시 |
|---|---|---|---|---|
| `AFPSRBossBase::Tick` | 서버 전용 | 엔진 | `GS` 유효 | `GS` null → 조기 반환(로그 없음) |
| ↳ 프리즈 게이트 | — | — | `!GS->IsRunPaused() && !GS->IsStageTransitionActive()` | 반환 **전에** 프리즈 엣지를 자식에 1회 푸시 |
| `ServerAddBlastMark` | 서버 전용 | `GA_Barrage` | 배열 크기 < `MaxConcurrentMarks`(32) | 가장 오래된 것을 즉시 불발 처리(FIFO), 경고 1회 |
| `ServerSetBeamState` | 서버 전용 | `GA_SweepLaser` | — | — |
| `WasRecentlyAirborne` | 서버 전용 | `GA_SweepLaser` | — | 기록 없으면 현재 `IsFalling()` |
| `UFPSRBossGameplayAbility::ServerTickPattern` | 서버 전용 | 보스 `Tick` | 이 어빌리티가 활성 | — |
| `FPSRBoss::ComputePhase` | 순수 | 보스 · 테스트 | — | 빈 배열 → 1 |
| `FPSRBossLaser::DidCross` | 순수 | `GA_SweepLaser` · 테스트 | — | — |
| `FPSRTargeting::IsEligibleTarget` | 서버 전용 | 스폰 서브시스템 · 보스 패턴 | — | `false` |
| `AFPSRBossHomingOrb::ServerLaunch` | 서버 전용 | `GA_HomingOrbs` | `Target` 유효 | 대상 없으면 스폰 취소 |

---

## 7. 복제표 (§6-3 서버권위 + Push Model)

| 프로퍼티 | 소유 | 조건 | 빈도 | 비고 |
|---|---|---|---|---|
| `CurrentPhase` | `AFPSRBossBase` | Push Model | 전환 시 1회 | `OnRep` + **권위측 직접 호출** 두 반쪽 |
| `BlastMarks` | `AFPSRBossBase` | Push Model | 추가·제거 시 | 표식당 ~20 B. 액터 20개 대신 배열 1개 |
| `BeamCount`/`BeamBaseAngleDeg`/`BeamAngularSpeedDegPerSec` | `AFPSRBossBase` | Push Model | 활성화·해제 시 1회씩 | |
| `ReplicatedPatternClock` | `AFPSRBossBase` | Push Model | **데드밴드 0.25 s** | `SetRunClockSeconds`(`FPSRGameState.cpp:352-362`) 관용구 |
| 오브 위치 | `AFPSRBossHomingOrb` | `SetReplicateMovement(true)` | 엔진 기본 | `bAlwaysRelevant=true` — NetCull 150 m < 대각 226 m (G1 P3-7) |

> 🔴 **모든 코스메틱은 두 반쪽이다** — 클라 `OnRep` + 권위측 세터 직접 브로드캐스트.
> RepNotify 는 권위 머신에서 돌지 않는다. VIT1 이 이 한 가지로 결함 1건을 냈고(리슨서버 호스트·솔로에서
> 적 실드파손 연출이 영영 안 남), 협동 기준형이 리슨서버라 **호스트는 매 세션 존재하는 1/4** 이며
> 2인 PIE 는 클라 화면만 보면 정상으로 보인다.

---

## 8. 수명주기 · 소유권

**보스가 자기가 낳은 것 전부를 소유한다.** 폴링하는 자식 25개가 아니라 감지기 1개 + 푸시
(선례 `UFPSRProjectileSubsystem::Tick:25-63`).

| 사건 | 무엇이 일어나야 하나 |
|---|---|
| 프리즈 엣지 (`bRunPaused` 변화) | 보스 `Tick` 이 엣지를 잡아 `SpawnedPatternActors` 전원에 `SetSimulationPaused` 푸시. 표식은 클럭이 멈추므로 자동 |
| **보스 사망** | `CancelAbilities()` · `ServerSetBeamState(0,…)` · 표식 전부 불발 제거 · 오브 전부 파괴 · `SetActorTickEnabled(false)`. 🔴 보스는 **사망해도 Destroy 되지 않는다**(`FPSRBossBase.cpp:164-166`) — 안 닫으면 **승리 후에도 시체가 계속 포격한다** |
| **런 재시작** | `StartRun` 이 `ActiveBoss->Destroy()`(`FPSRRunDirectorSubsystem.cpp:93-97`) → `EndPlay` 에서 위와 같은 회수 |
| **패배** | `EndRunFreeze` 가 `bRunPaused` 를 **영구 고정**(`FPSRGameState.h:134-137`) → 틱이 안 도므로 자동 정지. 단 결과 화면에 표식·오브가 **남는다** → `bRunEnded` 를 보고 회수 |
| 스테이지 전환 | 보스 페이즈에서 **성립 불가** — `UFPSRStageDirectorSubsystem` 이 Boss 페이즈 전환을 거부하고 BossTime 스왑은 `EnterBoss` 전에 끝난다. 그래도 게이트는 건다(무해, 스폰 서브시스템 선례) |
| 플레이어 이탈·JIP | `LastAirborneClock`·레이저 상대각 맵에서 무효 항목 제거. JIP 는 복제 상태가 곧 전부라 추가 처리 없음 |

---

## 9. 데이터드리븐 경계 (핵심원칙 2)

| 산다 | 어디에 |
|---|---|
| 페이즈 임계값 (개수 = N-1) | `UFPSRBossDefinitionDataAsset::PhaseHealthThresholds` |
| 패턴 목록·순서 | `AFPSRBossBase::GrantedAbilities` (BP CDO) |
| 패턴별 수치 전부 | 각 어빌리티 BP 의 `EditDefaultsOnly` |
| 패턴 간 배타 | 어빌리티별 `BlockAbilitiesWithTag` (BP) |
| 표식·빔·오브 연출 | BP `BlueprintImplementableEvent` — C++ 에 데칼·Niagara 호출 0 |
| 메시·머티리얼 | BP. **C++ 에 에셋 경로 0** |

🔴 **BP 저작 금지 사항** — 패턴 BP 안에서 `WaitDelay`·`PlayMontageAndWait` 등 **시간 기반 AbilityTask** 금지.
런타임 가드는 *수신 GE* 만 막고 AbilityTask 는 가드 밖이라, 쓰면 프리즈를 뚫는다(`Enemy.md:64`).
시간이 필요하면 `ServerTickPattern` 의 Δt 를 쓴다.

---

## 10. 성능 예산 (핵심원칙 1)

| 항목 | 예산 | 근거 |
|---|---|---|
| 보스 틱 | 액터 1개 | 스웜 200~300 대비 무시 가능 |
| 레이저 판정 | **플레이어 4회/프레임** (빔 수 무관) | 주기 도메인 `360/N` 접기 (G1 P1-2). 트레이스 0회 |
| 포격 판정 | 표식당 구 오버랩 1회, 최대 4개/초 | `PlayerPawn` 타입만 수집 → 스웜 미포함 |
| 표식 복제 | 표식당 ~20 B, 최대 32개 | 액터 20개 스폰/파괴 대비 채널 0개 |
| 오브 | 액터 5개 · `bAlwaysRelevant` | 보스 소유 고정 배열로 재사용(신규 풀 클래스 없음) |

---

## 11. 미결정 항목 · 명세 갭 처리

1. **표식 20개 판독성** — 4인 × 5발. `TargetPawn` 을 표식에 실어 "내 것"을 다르게 그릴 수 있게 열어 뒀다. 실제 판정은 **PIE 후 사용자**.
2. **`BeamHeightCm` 실측** — `JumpZVelocity` 가 어디에도 저작돼 있지 않아(전수 검색 대입 0건) 엔진 기본 420 cm/s 로 추정, 중력 ~980 에서 정점 ≈ 90 cm. 기본 60 은 보수적 추정치이고 **PIE 에서 확정**한다. `IsDataValid` 에 상한 경고.
3. **`DamageInvulnerabilityDuration`(0.25 s)** — VIT1 이 이미 "실드 도입 후 과할 수 있다, 권장 탐색 0.10~0.25 s"로 남겨 뒀다. 레이저 피격 간격이 여기 물리므로 **함께 본다**. ⚠️ 레이저 히트도 `ApplyContactDamage` 를 타므로 무적시간에 **그대로 걸린다** — 상대각 래치의 가치는 "프레임레이트 무관 1통과=1히트"뿐이다(종전 초안의 "무적시간 의존 배제"는 틀렸다, G1 P3-2).
4. **적 투사체 AOE 루프 통합** — 후속 행(§2 비목표).

---

## 12. 검증 기준

### 12-1. 빌드
```bash
Build.bat FPSRogueliteEditor Win64 Development -Project=... -WaitMutex -DisableAdaptiveUnity -ForceUnity
```
⚠️ 헤더 변경이 커서 `-MaxParallelActions=2` 로 OOM 회피(메모리 `[[build-header-change-oom-parallelism]]`).

### 12-2. 자동화 `FPSRoguelite.Boss.*` (월드리스 순수)
1. `ComputePhase` — 빈 배열 → 1 / 경계 정확히 임계값 / 내림차순 아닌 입력 방어 / N=1~5
2. `LatchPhase` — 회복해도 안 내려감
3. `DidCross` — ①빔 회전·플레이어 정지 ②**빔 정지·플레이어가 가로질러 이동**(초안이 놓쳤던 케이스) ③둘 다 이동 ④동방향 추월 ⑤도메인 랩에서 오탐 0 ⑥한 프레임에 반바퀴
4. `WrapToPeriod` — N=1~5 각각에서 등간격 빔이 한 점으로 접히는지
5. `CapsuleAngularRadiusDeg` — r→0 에서 발산하지 않는지
6. 선택기 — 쿨다운·`MinPhase` 스킵, 라운드로빈 순환

### 12-3. 엘리트 무회귀
시간축 가드를 ASC 로 호이스트하므로 기존 엘리트 테스트 전부 + `FPSR.EliteDump` 로 대조.

### 12-4. PIE (사용자 · 콘솔 `FPSR.BossPattern <idx>` / `FPSR.BossPhase <n>` 로 강제 발동)
1. 포격 — 착탄점이 **각자 발밑**, 공중이어도 표식은 지면
2. 레이저 — **웜업 동안 무피해**, 웜업 중 경고 인디케이터가 뜬다
3. 레이저 — 점프로 넘을 수 있다 / **빔 안으로 뛰어들면 맞는다**(P1-2 케이스)
4. 레이저 — 원격 클라(2인 PIE 클라 화면)에서 **보이는 빔과 맞는 순간이 일치**
5. 오브 — 총으로 부서진다 · **XP 가 안 나온다** · 격추 연출이 클라에도 보인다
6. **패턴 진행 중 레벨업 프리즈** — 신관·회전각·오브가 전부 멈춘다 (30초 후에도 진행 0)
7. 페이즈 2·3 에서 빔이 2줄·3줄
8. 보스 처치 후 **시체가 공격하지 않고** 표식·오브가 남지 않는다
9. **리슨서버 호스트 화면**에서 페이즈 전환·빔·격추 연출이 전부 난다(VIT1 결함 4의 재발 방지)
10. 런 재시작 후 이전 런의 표식·오브가 월드에 없다

### 12-5. 통과 정의
P1 잔존 0 · 위 자동화 전부 녹색 · PIE 10항목 사용자 확인 · Fable 머지 게이트 통과.

---

## 13. 게이트 원장

### 13-1. G1 1차 (2026-09-02, Fable) — **반려**
P1 3건 · P2 7건 · P3 11건. 이 명세는 그 전량을 반영한 재제출 초안이다.

| 지적 | 처리 |
|---|---|
| P1-1 레이저에 사전경고·웜업 없음 (`Enemy.md:79` 위반) | `WarmupSeconds` + `StartAngleRule=MaxGapCenter` + 기존 `ClientNotifyRangedTarget` 재사용 (§5-5) |
| P1-2 호 교차 판정이 움직이는 플레이어를 ~46% 놓침 | 주기 도메인 상대각 부호반전 (§5-2) |
| P1-3 클라 빔 각도 로컬 적분 = 드리프트·JIP 무한 | 데드밴드 복제 클럭 + 서버 `AirborneGraceSeconds` (§5-3) |
| P2-1 인스티게이터 정체 미정 | 오브/표식 자신을 인스티게이터로, `SetOwner(Boss)` + 분류기 `GetOwner()` 폴백 (§5-6) |
| P2-2 자식 액터 수명·프리즈 소유자 없음 | §8 표 전체 |
| P2-3 "풀링은 나중에" = 미루기 금지 위반 | 표식 = 보스 소유 복제 배열, 오브 = 보스 소유 고정 5기 (§5-1, §10) |
| P2-4 페이즈 태그 5개 상한 | 태그 기제 폐기, 복제 int + `MinPhase` + `BeamsPerPhase`/`MaxBeams` 데이터 (§5-3) |
| P2-5 GAS 기각근거 2/3 거짓 | §3-1 재작성 + ADR 0013 전환조건 트리거 명시 |
| P2-6 명세가 아니라 서사형 플랜 | 이 문서 |
| P2-7 B1 단독 검증 불가 | 슬라이스 재절단(S1=구동축+포격) + 콘솔 강제발동 (§4, §12-4) |
| P3-1 `SetCountsAsKill` 근거 오류(XP 아님) | §5-6 주석 정정 |
| P3-2 무적시간 서술 거꾸로 | §11-3 정정 |
| P3-3 `SetSimulationPaused` 가 FTimerManager 사용 | 오브 수명 = Δt 누산기 (§5-6) |
| P3-4 캡슐 각반경 미반영 | `CapsuleAngularRadiusDeg` (§5-2) |
| P3-5 공중 플레이어 발밑 표식 / 히치 | 아레나 Z 스냅 + `while` 누산 (§5-1, §5-5) |
| P3-6 적 투사체 AOE 통합은 라이브 경로 | §2 비목표로 이동 |
| P3-7 오브 NetCull·격추 연출 | `bAlwaysRelevant` + dwell 후 파괴 (§5-6, §7) |
| P3-8 시체 틱 | `HandleDeath` 에서 `SetActorTickEnabled(false)` (§8) |
| P3-9 전역 배타 태그 | 어빌리티별 `BlockAbilitiesWithTag` (§5-3) |
| P3-10 오브 선례 | `AFPSRMissionFleeTarget` (§5-6) |
| P3-11 `GatherAliveTargets` 추출 | 술어 `IsEligibleTarget` 만 추출 (§4) |

**게이트가 몰랐던 것 (동기화 전 트리를 봤다)** — 「놓친 대안 #1: ASC 가 프리즈 클럭을 소유」는
**VIT1 의 `AFPSRGameState::GetCombatClockSeconds()` 가 이미 해결**했다(그 커밋이 당시 이 클론에 없었다).
클럭은 전역 것을 읽고, 대안의 나머지 취지인 **가드 1벌 통합**만 채택했다(§4 S1).

### 13-2. G1 2차 — *(대기)*
### 13-3. C3 검증 원장 — *(구현 후)*
### 13-4. G2 머지 게이트 — *(머지 직전)*
