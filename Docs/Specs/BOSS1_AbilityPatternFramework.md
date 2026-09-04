# BOSS1 — 보스 어빌리티 · 패턴 프레임워크 (공격 패턴 3종)

## 1. 메타

| 항목 | 값 |
|---|---|
| 유닛 ID / 이름 | **BOSS1** — 보스 어빌리티·패턴 프레임워크 |
| 브랜치 | `feat/boss-patterns` |
| 작성 모델 | `claude-opus-5` (§6-5 (2) — 게이트 입력 = "Opus 가 모은 조사 + 플랜/명세 초안") |
| 작성일 / 최종 갱신 | 2026-09-02 (**G1 3차 제출본**) |
| 상태 | **초안 (G1 3차 대기)** |
| 보드 행 | [BOSS1-S1](https://app.notion.com/p/3cf3972ddd888150805fffa6ef92d2b8) · S2 · S3 · S4 (M3, 부모 XL 분할) |
| 관련 SSOT | `RunFlow.md` §2-7 · `Enemy.md` §2-6 · `Game.md` §1 · `Performance.md` §5 |
| 관련 ADR | [0010](../Architecture/0010-arena-topology-and-stage-transition.md) **D2**(단일 Z 평면) · [0011](../Architecture/0011-authored-skeleton-and-arena-validator.md) **E1**(160×160 m) · [0013](../Architecture/0013-enemy-tier-axis-and-elite-gas.md) 「전환 조건」= **이 유닛이 그 트리거** · 신설 예정 0015 |
| 선례 명세 | [`VIT1_ShieldHealthTwoLayer.md`](VIT1_ShieldHealthTwoLayer.md) |
| 관련 메모리 | `[[haiku-delegation-security-wiring]]` · `[[build-header-change-oom-parallelism]]` |

### 1-1. 사용자 결정

| # | 결정 | 시점 |
|---|---|---|
| 페이즈 | 3페이즈 + **페이즈 수 자체를 데이터로** | 2026-09-02 |
| 포격 | **전원 각각 5발** (4인 = 20발 진행) | 2026-09-02 |
| 레이저 | **지형 관통** — 회피는 점프뿐 | 2026-09-02 |
| 착수 | 지금 (§6-9 (8) 개시 규칙 예외 ① 사용자 직접 지시) | 2026-09-02 |
| G1 3차 | 돌린다 | 2026-09-02 |

### 1-2. 이 문서가 3차인 이유

G1 1차 = 반려(P1 3 · P2 7 · P3 11) → 2차 = **반려하되 "골격은 통과"**(P1 1 · P2 9 · P3 12).
2차 판정 원문: *"반려 사유는 골격이 아니라 §5 계약 오류 2건과 Sonnet 이 첫 시간 안에 멈출 선언 갭 7건"*.
이 판은 그 전량을 닫는다. **골격(전역 전투시계 재사용 · ASC 가드 1벌 호이스트 · 보스 소유 복제 배열 표식 ·
주기 도메인 상대각 · 슬라이스 절단)은 2차에서 코드 대조로 성립 확인됐으므로 건드리지 않았다.**

---

## 2. 목표 / 비목표

### 목표

1. 보스가 **서버 권위로 스스로 공격한다.** 전역 프리즈(§2-2) 동안 신관·회전각·추적시간·쿨다운이 **전부 멈춘다.**
   > ⚠️ *문구 정정(G1-2 P3-3)*: **스테이지 전환 중에는 전투시계가 흐른다**(VIT1 G2 P3 가 이미 기록).
   > 보스전이 안전한 이유는 시계가 아니라 **`UFPSRStageDirectorSubsystem` 이 보스 페이즈의 전환 자체를 거부**하고
   > BossTime 스왑이 `EnterBoss` **전에** 끝나기 때문이다. 틱 게이트는 그래도 둘 다 건다(무해).
2. 보스가 **체력 비율로 페이즈를 올린다.** 페이즈 수 = DA 임계 배열 길이 + 1. 회복해도 안 내려간다.
3. **패턴 3종**이 BP 로 저작되고 수치가 전부 데이터다.
4. **각 패턴을 콘솔에서 강제 발동**해 슬라이스마다 PIE 검증이 된다.
5. 보스가 죽거나 런이 재시작·종료돼도 **보스가 낳은 것이 하나도 남지 않는다.**

### 비목표

- 보스 이동 · AIController · StateTree (세 패턴 모두 "정지한 중앙 타워" 전제)
- **패턴 동시 발동** — 선택기는 단일 활성이다(§5-3). 필요해지면 핸들 배열 + 어빌리티별 `BlockAbilitiesWithTag` 로 후속 확장 (G1-3 P3-1)
- AttributeSet 신설 · 보스 실드(데이터 플립이면 켜지지만 요구가 아니다)
- 보스가 스웜을 때리는 것 (§3-1)
- `AFPSRProjectile` 적팀 AOE 루프(`FPSRProjectile.cpp:344-375`) 통합 — 라이브 경로, 후속 행
- 호스트 마이그레이션 (`Source/`·`Docs/SSOT/` 참조 0건 = 지원 경로 아님)
- 오브 풀링 — **명시적 결정**: 5기 × 볼리당 1회 × 보스 1마리라 spawn/destroy 로 간다(§10). "나중에"가 아니라 결정이다.

---

## 3. 제1원리 3줄

1. **제1원리** — 보스는 1마리라 "액터당 비용"이 안 걸린다. 걸리는 건 *보스전에도 스웜 200~300 이 남는다*는 것
   (`FPSRRunDirectorSubsystem.h:112`). 그래서 모든 판정이 **플레이어 ≤4 에만** 붙어 O(4) 고정이고,
   표식은 액터가 아니라 **보스 1개의 복제 배열**이다(스폰/파괴 0 · 복제 채널 0 추가).
2. **엔진 기본값과의 관계** — 엔진 GAS 를 쓰되 **시간축만 덮는다.** 엔진은 GE duration/period·AbilityTask 를
   월드 `FTimerManager` 로 돌려 상태 게이트인 프리즈를 뚫는다(`GameplayEffect.cpp:4409,4431`).
   이 프로젝트는 그 계약을 `UFPSREliteGameplayAbility` 로 확립했고 **VIT1 이 시간원을 전역화**했다
   (`AFPSRGameState::GetCombatClockSeconds()` — 서버 전용 · 틱 0 · `SetRunPaused` 엣지 누적).
   보스는 새 시계를 만들지 않고 그것을 읽는다.
3. **정합** — 체력이 `UFPSREnemyHealthComponent` 에 남아 D1 데미지 브릿지가 유지된다.
   ADR 0013 「전환 조건」이 *"보스와 공유하는 코드가 그 컴포넌트를 넘어 늘어나면 비목표를 다시 본다"* 고
   적어 뒀고 **이 유닛이 그 트리거**라, 0013 개정 + 0015 신설이 S4 산출물이다.

### 3-1. 왜 GAS 인가 (G1-1 P2-5 — 종전 근거 2/3 이 거짓이었다)

| 종전 근거 | 판정 |
|---|---|
| "활성화가 클라에 복제돼 연출이 공짜" | ❌ 거짓. 오너 없는 `Minimal` ASC 의 `ActivatableAbilities` 는 `COND_ReplayOrOwner`(`AbilitySystemComponent.cpp:1792-1793`) — 시뮬 프록시에 안 간다. **그래서 §5-3 이 빔·표식을 손으로 복제한다** |
| "태그 배타가 공짜" | ❌ 부분 거짓. 선택기를 어차피 따로 짠다 |
| `Game.md §1` 허용 + 엘리트 선례 일관성 + **BP 로 저작하는 어빌리티 목록** | ✅ 이것이 진짜 근거 |

**대가(명시)** — BP 자식이 `WaitDelay`·`PlayMontageAndWait` 를 쓰면 프리즈를 뚫는다(가드는 *수신 GE* 만 막는다,
`Enemy.md:64`). §9 저작 규칙 금지 + §12-4 PIE 항목.

### 3-2. 세 패턴 모두 플레이어만 때린다

보스전에도 스웜이 남는데 보스 공격이 스웜을 쓸면 **보스전이 더 쉬워지는 역설**이 된다. 비용도 O(4) 로 고정된다.
구현상 자동 성립 — 폭발이 `ECC_FPSRPlayerPawn` 만 오버랩하면 보스 자신도 스웜도 수집되지 않는다.

---

## 4. 파일 목록

### S1 — 구동축 + 포격 (첫 가시 슬라이스)

| 경로 | 신규/수정 | 설명 |
|---|---|---|
| `Public/Core/FPSRGameState.h` · `Private/...cpp` | 수정 | 프리즈 2필드 **복제 승격** + `GetCombatClockSecondsForClients()`(§5-7). ⚠️ VIT1 이 남긴 *"server-only, not replicated"* 주석(`.h:372` · `.cpp:251-252`)도 같은 커밋에서 고친다 (G1-3 P3-6) |
| `Public/AbilitySystem/FPSRAbilitySystemComponent.h` · `.cpp` | 수정 | `EnableTimeAxisGuard()` — 가드 **1벌**(엘리트에서 호이스트) |
| `Public/Enemy/FPSREnemyEliteBase.h` · `.cpp` | 수정 | 인라인 등록 → `EnableTimeAxisGuard()` 호출 (거동 무변) |
| `Public/AbilitySystem/Abilities/FPSREliteGameplayAbility.h` · `.cpp` | 수정 | 쿨다운 3오버라이드를 공통 베이스로 호이스트 (§5-4) |
| `Public/AbilitySystem/Abilities/FPSRFreezeCooldownAbility.h` · `.cpp` | **신규** | 엘리트·보스가 공유하는 쿨다운 계약 1벌 |
| `Public/AbilitySystem/Abilities/FPSRBossGameplayAbility.h` · `.cpp` | **신규** | 위 베이스 + `MinPhase` + `ServerTickPattern` |
| `Public/Boss/FPSRBossTypes.h` | **신규** | `FFPSRBossBlastMark` · `namespace FPSRBoss` |
| `Public/Boss/FPSRBossBase.h` · `Private/...cpp` | 수정 | ASC · **틱 활성화** · 페이즈 · 선택기 · 표식 배열 · 자식 회수 |
| `Public/Boss/FPSRBossDefinitionDataAsset.h` · `.cpp` | 수정 | `PhaseHealthThresholds` + `IsDataValid` |
| `Public/AbilitySystem/Abilities/FPSRBossGA_Barrage.h` · `.cpp` | **신규** | 포격 |
| `Public/Combat/FPSRTargeting.h` · `.cpp` | **신규** | `IsEligibleTarget()` **술어만** — 원본은 인라인 루프(`FPSREnemySpawnSubsystem.cpp:341-363`), 함수 `GatherAliveTargets` 는 존재하지 않는다 (G1-2 P3-11 문구 정정) |
| `Private/Enemy/FPSREnemySpawnSubsystem.cpp` | 수정 | 술어 호출로 교체 — **별도 커밋** + `FPSRoguelite.Enemy.*` 무회귀 (라이브 핫패스) |
| `Private/Run/FPSRRunDirectorSubsystem.cpp` | 수정 | `FPSR.BossPattern` · `FPSR.BossPhase` 콘솔 |
| `Private/Tests/FPSRBossPatternTest.cpp` | **신규** | 순수 술어 + CDO 검사 |

### S2 / S3 / S4

| 경로 | 슬라이스 |
|---|---|
| `Abilities/FPSRBossGA_SweepLaser.h/.cpp` · `Boss/FPSRBossLaserMath.h` | S2 |
| `Boss/FPSRBossHomingOrb.h/.cpp` · `Abilities/FPSRBossGA_HomingOrbs.h/.cpp` | S3 |
| `Private/Director/FPSRDirectorSensorSubsystem.cpp` (`GetOwner()` 폴백) | **S3** — 오브가 첫 소비자 |
| `Config/DefaultGameplayTags.ini` (**죽은 `Boss.Phase.One/Two` 제거**) · BP·DA 저작 · ADR 0015 신설(→ 번호는 **0016** 을 쓸 것 — 0015 는 2026-09-04 「1P 총만 표시」 ADR 이 사용했다) · 0013 개정 · SSOT 3곳 | S4 |

> **S1 이 보스 헤더에 S2/S3 필드까지 한 번에 싣는다** — 헤더 변경은 대규모 재컴파일(+OOM 위험,
> 메모리 `[[build-header-change-oom-parallelism]]`)이라 3주에 걸쳐 3번 하는 것보다 1번이 싸다.
> 죽은 필드는 S2/S3 가 곧 채운다.

---

## 5. 인터페이스 선언

### 5-1. `FPSRBossTypes.h`

```cpp
/** 지연 폭발 표식 1개. 액터가 아니라 보스가 배열로 들고 복제한다:
 *  ① 기본 NetCullDistance 150 m < 아레나 대각 226 m(ADR 0011 E1 의 160×160 m) — 액터 표식은
 *     먼 팀원에게 안 보인다. 표식은 남의 것도 보여야 피해서 달린다.
 *  ② 액터 스폰/파괴 0 · 복제 채널 0 추가(보스는 bAlwaysRelevant).
 *  ③ 프리즈·teardown 소유자가 보스 하나로 통일(§8).
 *  ④ JIP 는 배열이 곧 상태라 공짜.
 *
 *  🔴 클라는 **immediate-mode** 로 읽는다 (G1-2 P2-4): BP 가 매 틱 GetBlastMarks() 를 순회해 그리고,
 *  폭발 연출은 배열 제거가 아니라 `DetonateAtClock − 클라클럭 <= 0` 으로 **스스로 판정**한다.
 *  그래서 MarkId·클라 diff 캐시·제거사유 필드가 전부 불필요하고, **불발**(FIFO 축출·보스 사망)은
 *  그 시각 *전에* 사라지므로 폭발 연출이 자연히 안 난다. 이벤트 모드였다면 클라가 불발과 폭발을
 *  구분할 수 없었다. */
USTRUCT(BlueprintType)
struct FFPSRBossBlastMark
{
    GENERATED_BODY()

    /** 착탄점. Z = **타겟 플레이어의 마지막 접지 Z** — 스폰 서브시스템이 이미 캐시한다
     *  (`UFPSREnemySpawnSubsystem::LastGroundedZByPlayer`, `.cpp:370-374`). 트레이스 0회이고
     *  공중에 뜬 플레이어를 노려도 표식이 정확히 발밑 지면에 눕는다. (G1-2 P3-7) */
    UPROPERTY(BlueprintReadOnly) FVector_NetQuantize Center = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly) float Radius = 0.0f;

    /** 터지는 시각 — 보스 패턴 클럭 기준(월드시간 아님). */
    UPROPERTY(BlueprintReadOnly) float DetonateAtClock = 0.0f;

    /** 누구를 노렸나 — 클라가 "내 것"을 다르게 그릴 수 있게(4인 20발 판독성, §11-1). */
    UPROPERTY(BlueprintReadOnly) TWeakObjectPtr<APawn> TargetPawn;
};

namespace FPSRBoss
{
    /** HealthFraction(0~1) + **내림차순** 임계 배열 → 1-기반 페이즈. 빈 배열 → 1.
     *  경계 = "이하": Fraction <= Thresholds[i] 이면 최소 i+2. */
    FPSROGUELITE_API int32 ComputePhase(float HealthFraction, TConstArrayView<float> Thresholds);

    FORCEINLINE int32 LatchPhase(int32 Current, int32 Computed) { return FMath::Max(Current, Computed); }
}
```

### 5-2. `FPSRBossLaserMath.h`

```cpp
namespace FPSRBossLaser
{
    /** 각을 [-Period/2, +Period/2) 로 접는다. Period = 360/N 이면 등간격 빔 N개가 한 점으로 접혀
     *  **플레이어당 비교 1회**로 끝난다(빔 수 무관). */
    FORCEINLINE float WrapToPeriod(float Deg, float Period)
    {
        const float H = Period * 0.5f;
        return FMath::Fmod(FMath::Fmod(Deg + H, Period) + Period, Period) - H;
    }

    /** 🔴 **밴드 진입 엣지**만 1히트 (G1-2 P2-1 — 사용자 결정: 엄격 "1통과 = 1히트").
     *
     *  왜 레벨 트리거가 아닌가: 종전 판의 `|Cur| <= W → true` 는 상태 없는 per-frame 술어라
     *  **밴드 안에 있는 매 프레임 true** 를 돌려준다. 동방향 저속 통과(상대 0.13°/프레임)면
     *  46프레임 ≈ 1.5 s 연속 발화 → i-frame 0.25 s 에서 **6히트**. "무적시간에 안 끌려다닌다"는
     *  이 명세 자신의 약속이 그 함수로 깨졌다.
     *
     *  🔴 **진입 엣지만으로는 부족하다 — 두 조건을 OR 로 묶어야 한다** (자체검증 2026-09-02, G1-3 제출 전).
     *  `|Cur| <= W` 단독이면 빔이 한 프레임에 플레이어를 **건너뛸 때**(Prev=+5°, Cur=−5°, W=3°)
     *  `|Cur| > W` 라 false 가 되어 **영영 안 맞는다** — 부호반전 방식이 잡던 바로 그 케이스를
     *  엣지 트리거로 좁히면서 잃어버린 것이다. 그래서 "밖에 있었다" 를 게이트로 두고
     *  "안에 들어왔거나 관통했다" 를 OR 로 받는다. 다섯 경우 전부 옳다 (1 / 1 / 0 / 1 / 0):
     *    · 느린 통과 = 진입 프레임 1회(이후 Prev 가 안이라 게이트가 막는다)
     *    · 빠른 관통 = 부호반전 1회
     *    · 밴드 안에서 방향 전환 = 0회(이미 셌다)
     *    · 나갔다 재진입 = 1회(실제로 두 번째 노출이므로 옳다)
     *    · 첫 프레임 Prev=Cur → 밖이면 게이트 통과하나 OR 두 항 모두 false, 안이면 게이트가 막는다 = 무히트
     *
     *  누락(under-count)은 0 이다: r 이 0 을 단조 통과하면 |Δr| < Period/2 아래 진입 엣지가 정확히 1회.
     *  랩 컷오프는 나이키스트 가정 — N=5(H=36°)에서 최대 상대각속도 ≈ 99°/s → 3.3°/프레임@30fps 이므로
     *  **360 ms 이상 히치에서만** 깨진다(§12-2 ⑥ 이 이 경계를 단언한다).
     *
     *  @param PrevRel/CurRel  WrapToPeriod(플레이어각 − 빔각). 첫 프레임은 Prev=Cur 로 초기화(무히트).
     *  @param MaxStep         Period/2. 이보다 크게 튀면 도메인 랩 — 히트 아님.
     *  @param HalfWidthDeg    빔 반폭 + 플레이어 캡슐 각반경(CapsuleAngularRadiusDeg). */
    FORCEINLINE bool DidEnterBeam(float PrevRel, float CurRel, float MaxStep, float HalfWidthDeg)
    {
        if (FMath::Abs(CurRel - PrevRel) > MaxStep) { return false; }          // 도메인 랩 — 교차 아님
        if (FMath::Abs(PrevRel) <= HalfWidthDeg)    { return false; }          // 직전에 이미 안 — 셈 끝남
        // 밖에 있었고 → 이번에 안으로 들어왔거나(느린 통과) 아예 관통했다(빠른 통과).
        return FMath::Abs(CurRel) <= HalfWidthDeg || ((PrevRel < 0.0f) != (CurRel < 0.0f));
    }

    /** 플레이어 캡슐이 거리 r 에서 차지하는 각반경(도). 보스 코앞에서 얇은 빔이 캡슐을 시각적으로
     *  스치는데 판정만 빗나가는 것을 막는다. r→0 에서 발산하지 않게 clamp. */
    FORCEINLINE float CapsuleAngularRadiusDeg(float CapsuleRadiusCm, float DistanceCm)
    {
        return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(CapsuleRadiusCm / FMath::Max(DistanceCm, 1.0f), 0.0f, 1.0f)));
    }

    /** 서버·클라가 **같은 식**으로 각도를 낸다(§5-3 BeamStartClock). Δt 적분 금지 — 적분은 발산한다. */
    FORCEINLINE float BeamBaseAngleAt(float StartAngleDeg, float SpeedDegPerSec, float StartClock, float NowClock)
    {
        return StartAngleDeg + SpeedDegPerSec * (NowClock - StartClock);
    }
}
```

### 5-3. `AFPSRBossBase`

```cpp
UCLASS()
class FPSROGUELITE_API AFPSRBossBase : public ACharacter, public IAbilitySystemInterface
{
public:
    AFPSRBossBase();
    /** 🔴 생성자에서 `PrimaryActorTick.bCanEverTick = true;` 로 **바꾼다** (G1-2 P1-1).
     *  현행 `FPSRBossBase.cpp:21` 이 명시적으로 false 를 박아 두었다 — 안 뒤집으면 선택기·신관·
     *  레이저·프리즈 엣지 푸시가 **전부 무동작**이고 빌드·자동화는 전부 녹색이다(VIT1 힐팩 P1 과
     *  같은 형태, 이번엔 베이스가 *끄고* 있어 더 나쁘다). §12-2 ⑦ 이 CDO 로 이것을 단언한다.
     *  BeginPlay 에서 `SetActorTickEnabled(HasAuthority())` — 클라는 틱하지 않는다(연출은 BP 틱). */

    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

    UFUNCTION(BlueprintPure, Category="FPSR|Boss") int32 GetCurrentPhase() const { return CurrentPhase; }

    /** 프리즈-멈춤 패턴 시계.
     *  서버 = `GS->GetCombatClockSeconds()` 그대로.
     *  클라 = `GS->GetCombatClockSecondsForClients()`(§5-7) + `ClientVisualLeadSeconds`.
     *  🔴 클럭 값을 따로 복제하지 않는다 (G1-2 P2-2): `Replicated` 만으로는 클라가 **수신 시각을 몰라**
     *  외삽할 수 없고, 미러를 두면 앵커 상태기가 붙는다. 대신 GameState 가 **프리즈 앵커 2값**만
     *  엣지에서 복제하고(엣지당 8 B) 클라가 엔진의 `GetServerWorldTimeSeconds()` 로 유도한다 —
     *  외삽 상태 0, JIP 정확, `StagePhaseEndServerTime` 이 이미 쓰는 시계다. */
    UFUNCTION(BlueprintPure, Category="FPSR|Boss") float GetPatternClockSeconds() const;

    /** 빔 코스메틱. 클라는 이 값들 + 클럭으로 `BeamBaseAngleAt()` 을 부른다(서버와 동일 식). */
    UFUNCTION(BlueprintPure, Category="FPSR|Boss")
    bool GetBeamState(int32& OutBeamCount, float& OutBaseAngleDeg, bool& bOutWarmup) const;

    UFUNCTION(BlueprintPure, Category="FPSR|Boss")
    const TArray<FFPSRBossBlastMark>& GetBlastMarks() const { return BlastMarks; }

    // ---- 서버: 패턴이 부르는 것 ----------------------------------------------------------
    /** 표식 예약. 배열이 `MaxConcurrentMarks` 를 넘으면 가장 오래된 것을 **불발 제거**(폭발 없음) + 경고 1회.
     *  저작값이 §12 IsDataValid 를 통과하면 이 경로는 사실상 죽은 경로다. */
    void ServerAddBlastMark(const FVector& Center, float Radius, float FuseSeconds, APawn* TargetPawn);

    /** 보스가 스폰한 액터 등록 — 프리즈 엣지 푸시와 §8 회수가 이 목록 하나를 쓴다. */
    void ServerRegisterPatternActor(AActor* Actor);

    /** 빔 상태 복제. BeamCount=0 = 비활성. StartClock/WarmupEndClock 이 있어야 클라가 각도를 낼 수 있다
     *  (G1-2 P2-8 — 종전 판엔 시작 시각이 없어 계산 자체가 불가능했다). */
    void ServerSetBeamState(int32 InBeamCount, float StartAngleDeg, float SpeedDegPerSec,
                            float StartClock, float WarmupEndClock);

    /** 최근 `AirborneGraceSeconds` 안에 공중이었나(늦은 착지 보호). */
    bool WasRecentlyAirborne(const APawn* Pawn) const;

    /** 지연 히트 예약 — 레이저 진입 엣지에서 부른다. `LateJumpGraceSeconds` 뒤에 결제되며,
     *  그 사이에 대상이 공중이 되면 **취소**된다.
     *  🔴 왜 (G1-2 P2-2): 클라 화면이 서버와 완벽히 맞아도 점프 입력은 편도 지연 L 뒤에 서버에 닿는다.
     *  `WasRecentlyAirborne` 은 "방금 착지" 방향만 열어 주므로 **"늦은 점프" 방향이 닫혀 있었다.**
     *  미래를 보려면 결제를 미루는 수밖에 없다. 항목 ≤4개. */
    void ServerScheduleLaserHit(APawn* Target, float Damage, const FFPSRDamageSpec& Spec);

    // ---- 디버그 진입점 (콘솔이 부른다) ---------------------------------------------------
    void DebugForcePattern(int32 Index);   // FPSR.BossPattern <idx>
    void DebugSetPhase(int32 Phase);       // FPSR.BossPhase <n>

protected:
    /** 오너=아바타=자기 자신. `Minimal` 복제(오너 클라 없음). `AFPSREnemyEliteBase.h:114-123` 과 동형. */
    UPROPERTY(VisibleAnywhere, Category="FPSR|Boss") TObjectPtr<UFPSRAbilitySystemComponent> AbilitySystem;

    /** 콘텐츠가 저작하는 패턴 목록. 선택기가 이 순서로 라운드로빈. 보스는 풀링되지 않으므로
     *  `BeginPlay`(서버) 1회 부여(엘리트는 Activate 마다 재부여). */
    UPROPERTY(EditDefaultsOnly, Category="FPSR|Boss|Patterns")
    TArray<TSubclassOf<UFPSRBossGameplayAbility>> GrantedAbilities;

    UPROPERTY(EditDefaultsOnly, Category="FPSR|Boss|Patterns", meta=(ClampMin="0.0")) float PatternGapSeconds = 2.0f;
    UPROPERTY(EditDefaultsOnly, Category="FPSR|Boss|Patterns", meta=(ClampMin="0.0")) float AirborneGraceSeconds = 0.12f;
    UPROPERTY(EditDefaultsOnly, Category="FPSR|Boss|Patterns", meta=(ClampMin="0.0")) float LateJumpGraceSeconds = 0.15f;
    UPROPERTY(EditDefaultsOnly, Category="FPSR|Boss|Patterns", meta=(ClampMin="1"))   int32 MaxConcurrentMarks = 32;

    /** 클라 빔을 편도 지연만큼 앞당겨 그린다(0 = 끄기). 기본 = `PlayerState::GetPingInMilliseconds()/2000`
     *  를 상한 클램프해 사용. 순수 코스메틱 — 데미지는 100% 서버 권위. */
    UPROPERTY(EditDefaultsOnly, Category="FPSR|Boss|Patterns", meta=(ClampMin="0.0", ClampMax="0.3"))
    float MaxClientVisualLeadSeconds = 0.15f;

    virtual void PostInitializeComponents() override;  // InitAbilityActorInfo(this,this) + EnableTimeAxisGuard()
    virtual void BeginPlay() override;                 // SetActorTickEnabled(HasAuthority()) + 어빌리티 부여
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    /** 페이즈 배선 (G1-2 P3-2) — HealthComponent->OnHealthChanged 에 BeginPlay 에서 바인딩. */
    UFUNCTION() void HandleHealthChanged(float NewHealth, float MaxHealth);

    UFUNCTION() void OnRep_CurrentPhase();
    UFUNCTION() void OnRep_BeamState();

    /** 연출은 100% BP. 표식은 immediate-mode 라 add/detonate 이벤트가 **없다**(§5-1). */
    UFUNCTION(BlueprintImplementableEvent, Category="FPSR|Boss") void OnPhaseChangedCosmetic(int32 NewPhase);
    UFUNCTION(BlueprintImplementableEvent, Category="FPSR|Boss") void OnBeamStateChangedCosmetic(int32 InBeamCount, bool bWarmup);

private:
    UPROPERTY(ReplicatedUsing=OnRep_CurrentPhase) int32 CurrentPhase = 1;
    UPROPERTY(Replicated)                         TArray<FFPSRBossBlastMark> BlastMarks;
    UPROPERTY(ReplicatedUsing=OnRep_BeamState)    int32 BeamCount = 0;
    UPROPERTY(ReplicatedUsing=OnRep_BeamState)    float BeamStartAngleDeg = 0.0f;
    UPROPERTY(ReplicatedUsing=OnRep_BeamState)    float BeamSpeedDegPerSec = 0.0f;
    UPROPERTY(ReplicatedUsing=OnRep_BeamState)    float BeamStartClock = 0.0f;
    UPROPERTY(ReplicatedUsing=OnRep_BeamState)    float BeamWarmupEndClock = 0.0f;

    /** `InitializeFromDefinition`(`FPSRBossBase.cpp:110-118`)에서 캐시. DA 가 없으면 빈 배열 = 1페이즈. */
    TArray<float> PhaseThresholds;

    TArray<TWeakObjectPtr<AActor>> SpawnedPatternActors;
    /** `Spec` 이 없으면 레이저만 `DamageType` 을 잃어 VIT1 의 per-layer 방어 계수 축이 끊긴다(G1-3 P2-3). */
    struct FPendingLaserHit { TWeakObjectPtr<APawn> Target; float DueClock; float Damage; FFPSRDamageSpec Spec; };
    TArray<FPendingLaserHit> PendingLaserHits;
    TMap<TWeakObjectPtr<APawn>, float> LastAirborneClock;

    /** 선택기 — 단일 활성. `ActivePatternHandle` 이 유효한 동안 새 패턴을 고르지 않는다.
     *  🔴 §5-3 종전 판이 "레이저+포격 동시"를 근거로 전역 배타 태그를 거부했는데 단수 핸들과 모순이었다
     *  (G1-2 P3-1). **단일 활성으로 확정**하고, 동시 발동은 이 유닛의 비목표로 못박는다 —
     *  필요해지면 핸들 배열 + 어빌리티별 `BlockAbilitiesWithTag` 로 후속 확장한다. */
    FGameplayAbilitySpecHandle ActivePatternHandle;
    TArray<FGameplayAbilitySpecHandle> GrantedAbilityHandles;
    int32 NextPatternIndex = 0;
    float LastPatternEndClock = -1.0f;
    bool bWasFrozenLastTick = false;
};
```

**`Tick` 순서 (G1-2 P3-10 — 한 줄로 못박는다)**

```
1. !HasAuthority() → return
2. 런 종료 확인 (GS->HasRunEnded()) → 자식·표식 회수 후 틱 비활성화, return   ← 프리즈 게이트 **앞**
3. 프리즈 엣지 감지 → SpawnedPatternActors 전원에 SetSimulationPaused 푸시
4. IsRunPaused() || IsStageTransitionActive() → return
5. LastAirborneClock 갱신(생존자 순회) → 지연 히트 결제
6. 표식 만기 폭발 처리
7. 활성 패턴 ServerTickPattern(Dt) / 없으면 선택기
```
> 2번이 4번보다 앞인 이유 = `EndRunFreeze` 가 `bRunPaused` 를 **먼저** 켜므로, 뒤에 두면 회수 코드가 영영 안 돈다.

### 5-4. 쿨다운 계약 (엘리트·보스 공유 1벌)

```cpp
/** 표준 쿨다운 GE 경로 셋을 대체하는 계약. 시계만 파생이 갈아끼운다.
 *  (G1-2 P3-5 — 종전 판은 "40줄 복제 대신 시계만"이라 써 놓고 §4 에 엘리트 파일이 없었다.) */
UCLASS(Abstract)
class FPSROGUELITE_API UFPSRFreezeCooldownAbility : public UFPSRGameplayAbility
{
public:
    UFPSRFreezeCooldownAbility();   // NetExecutionPolicy = ServerOnly (FPSREliteGameplayAbility.cpp:12)
    virtual bool CheckCooldown(...) const override;
    virtual void ApplyCooldown(...) const override;
    virtual UGameplayEffect* GetCooldownGameplayEffect() const override { return nullptr; }
protected:
    /** 파생이 제공하는 프리즈-멈춤 시계. 엘리트 = EliteCooldownClockSeconds · 보스 = 전투시계. */
    virtual float GetCooldownClockSeconds(const FGameplayAbilityActorInfo* ActorInfo) const PURE_VIRTUAL(, return 0.f;);
    UPROPERTY(EditDefaultsOnly, Category="Cooldown", meta=(ClampMin="0.0")) float CooldownSeconds = 0.0f;
private:
    mutable float LastActivationClock = -1.0f;
};

UCLASS(Abstract)
class FPSROGUELITE_API UFPSRBossGameplayAbility : public UFPSRFreezeCooldownAbility
{
public:
    /** 🔴 패턴의 유일한 시간원. 보스 Tick 이 **활성 패턴에만** 부른다. AbilityTask 금지(§9). */
    virtual void ServerTickPattern(float DeltaSeconds) {}
    int32 GetMinPhase() const { return MinPhase; }
protected:
    virtual float GetCooldownClockSeconds(const FGameplayAbilityActorInfo*) const override;
    /** C++ 베이스가 ActivateAbility 에서 CommitAbility() 를 부른다 — BP 자식이 ActivateAbility 를
     *  통째로 덮으면 ApplyCooldown 이 안 찍히므로, BP 는 `ServerTickPattern` 만 확장한다(§9). */
    UPROPERTY(EditDefaultsOnly, Category="FPSR|Boss|Pattern", meta=(ClampMin="1")) int32 MinPhase = 1;
    AFPSRBossBase* GetBoss() const;
    float GetClock() const;
};
```

### 5-5. 패턴 3종

```cpp
UCLASS() class UFPSRBossGA_Barrage : public UFPSRBossGameplayAbility
{
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="1"))   int32 ShellsPerPlayer = 5;
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="0.1")) float IntervalSeconds = 2.0f;
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="0.1")) float FuseSeconds = 1.4f;
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="1.0")) float RadiusCm = 500.0f;
    UPROPERTY(EditDefaultsOnly)                        float Damage = 25.0f;
    /** 넉백 경로 = FPSRCombat::ApplyKnockback. **DBNO/사망 플레이어는 제외**한다(무력화 대상 띄우기 금지).
     *  0 = 넉백 없음. (G1-2 P3-8 — 소비자 없는 필드 금지) */
    UPROPERTY(EditDefaultsOnly)                        float KnockbackStrength = 0.0f;
    /** FFPSRDamageSpec.DamageType 에 실린다 — VIT1 의 per-layer 방어 계수 축(FPSRVitals.h:13-23)에
     *  보스 데미지를 연결하는 유일한 손잡이. 비우면 Physical. */
    UPROPERTY(EditDefaultsOnly)                        FGameplayTag DamageType;
    // 인터벌 누산은 while 루프 — 히치가 한 발을 삼키지 않게.
};

UENUM() enum class EFPSRBeamStartAngle : uint8 { ArenaFixed, MaxGapCenter };

UCLASS() class UFPSRBossGA_SweepLaser : public UFPSRBossGameplayAbility
{
    /** 🔴 웜업 = `Enemy.md:79`("히트스캔이면 차징 유예 + 사전경고 인디케이터 필수 — 부조리 탄막 금지").
     *  이 구간엔 빔이 **렌더되되 데미지 0**. 시작·종료에 기존 Client/Reliable RPC
     *  `AFPSRPlayerController::ClientNotifyRangedTarget`(`FPSRPlayerController.h:137`)을 보스 UniqueID 로
     *  브래킷 — 신규 RPC 0(원거리 적이 쓰는 그 재사용, `Enemy.md:82`).
     *  ⚠️ `false` 는 **웜업 종료·스윕 종료·EndAbility(보스 사망 CancelAbilities 포함) 전부**에서 보낸다 —
     *  안 보내면 인디케이터가 화면에 남는다(선례 `FPSREnemyBase.cpp:1157-1160` 이 Abort 에서 해제). */
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="0.0")) float WarmupSeconds = 1.5f;

    /** MaxGapCenter = 생존자 방위각들의 최대 간격 한가운데에서 시작 — 활성화 프레임에 누군가의 머리 위에서
     *  빔이 태어나는 사고를 구조적으로 없앤다. */
    UPROPERTY(EditDefaultsOnly) EFPSRBeamStartAngle StartAngleRule = EFPSRBeamStartAngle::MaxGapCenter;

    UPROPERTY(EditDefaultsOnly)                        float AngularSpeedDegPerSec = 30.0f; // 양수 = 시계방향
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="1"))   int32 BeamsPerPhase = 1;   // 빔 수 = min(페이즈*이것, MaxBeams)
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="1"))   int32 MaxBeams = 5;        // 코드 상수 아님
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="0.1")) float BeamHalfWidthDeg = 1.5f;
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="0.1")) float Revolutions = 2.0f;
    UPROPERTY(EditDefaultsOnly)                        float Damage = 30.0f;
    UPROPERTY(EditDefaultsOnly)                        FGameplayTag DamageType;

    /** 빔이 시각적으로 시작되는 반지름 = 보스 몸통 반지름. **안전지대가 아니다** — 보스 캡슐(r=1250)이
     *  플레이어를 막으므로 이 안쪽에 설 수 없다. 판정에도 r >= 이 값 조건으로 들어가지만 실질 무조건 참. */
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="0.0")) float InnerRadiusCm = 1250.0f;

    /** 🔴 **연출 전용.** 판정은 `WasRecentlyAirborne()`(=`IsFalling` 기반)이지 발 높이가 아니다.
     *  (G1-2 P2-9 · 사용자 결정) 기하 판정이면 60 cm 이상 프롭 위에 선 플레이어가 **영구 안전**해져
     *  "회피는 점프뿐"과 충돌하고, 정점 90 cm(엔진 기본 JumpZVelocity 420)에서 유효 창이 0.3 s 뿐이다. */
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="0.0")) float BeamVisualHeightCm = 60.0f;

    // 🔴 히트 판정 = **노출 + 래치** (G1-3 P2-1 — 단순 AND 는 통과 하나를 통째로 건너뛴다)
    //
    // 엣지에 게이트를 AND 로 걸면, 엣지 프레임에 게이트가 닫혀 있다가 **밴드 안에서 열리는** 두 경우가
    // 그 통과 전체를 0히트로 만든다: ① 웜업 중에도 빔은 돈다(BeamBaseAngleAt 이 BeamStartClock 기준)
    // → WarmupEndClock 시점에 이미 밴드 안이던 플레이어 ② 공중에서 빔이 들어오고 **밴드 안에 착지**한
    // 플레이어. ②는 §12-4 #3("빔 안으로 뛰어들면 맞는다")과 정면 충돌한다.
    // 대안(게이트를 좁히고 ①을 감수)은 사용자 결정 "회피는 점프뿐"과 모순이라 기각 — 땅에 있으면 맞아야 한다.
    //
    //   bExposed = |CurRel| <= W || DidEnterBeam(...)      // 밴드 안이거나 이번 프레임에 관통했다
    //   bGateOpen = !WasRecentlyAirborne(P) && r >= InnerRadiusCm && 클럭 >= WarmupEndClock
    //   if (bExposed && !bLatched && bGateOpen) { ServerScheduleLaserHit(P, Damage, Spec); bLatched = true; }
    //   if (!bExposed) { bLatched = false; }                // 밴드를 벗어나면 다음 통과를 위해 해제
    //
    // over-count 0 은 래치가 보장하고, under-count 0 은 DidEnterBeam 이 보장한다(§5-2).
    // 상태: TMap<TWeakObjectPtr<APawn>, FBeamTrack{ float PrevRel; bool bLatched; }> — 이 GA 인스턴스 소유
    //       (InstancedPerActor, FPSRGameplayAbility.cpp:10 → 보스 1마리분). 이탈 폰은 매 틱 정리.
    // 🔒 BeamCount·Period 는 **활성화 시점에 고정**한다(G1-3 P3-5) — 스윕 도중 페이즈가 올라 Period 가
    //    바뀌면 PrevRel 이 다른 도메인의 값이 되어 무의미해진다. 늘어난 빔은 다음 발동부터.
};

UCLASS() class UFPSRBossGA_HomingOrbs : public UFPSRBossGameplayAbility
{
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="1", ClampMax="8")) int32 OrbCount = 5;
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="1.0"))  float OrbHealth = 150.0f;
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="0.1"))  float TrackSeconds = 8.0f;
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="0.0"))  float SpawnIntervalSeconds = 0.25f;
    UPROPERTY(EditDefaultsOnly)                         float SpawnSpreadDeg = 40.0f;
    UPROPERTY(EditDefaultsOnly)                         float Damage = 20.0f;
    UPROPERTY(EditDefaultsOnly)                         FGameplayTag DamageType;
    UPROPERTY(EditDefaultsOnly)                         TSubclassOf<AFPSRBossHomingOrb> OrbClass;
    // 생존자 중 1명을 골라 전 기수가 락온. 스폰이 끝나면 어빌리티 즉시 종료(오브는 독립 생명).
};
```

### 5-6. `AFPSRBossHomingOrb`

```cpp
/** 격추 가능한 추적 투사체.
 *
 *  🔴 **APawn 이 아니라 AActor 다** (G1-2 P2-5). `FPSRCombat::CanAffectTarget` 은
 *  `Target->IsA(APawn)` 일 때만 P-C 도달성 게이트를 건다(`FPSRCombatStatics.cpp:41-44`) — 폰 오브가
 *  차단 셀(프롭 ≥60 cm, ADR 0010) 위를 지나면 **전 무기에 0 뎀 = 무적**이 된다. 선례
 *  AFPSRMissionFleeTarget 이 APawn 인데 안 걸린 건 지면을 달리기 때문일 뿐이다.
 *  오브젝트 타입은 클래스와 무관하므로 AActor + ECC_Pawn 스피어면 무기 질의
 *  (`AddDamageablePawnObjectTypes`, `FPSRCombatStatics.cpp:74-78`)에는 그대로 잡히고 게이트만 벗어난다.
 *  컨트롤러·빙의가 필요 없으므로 APawn 을 유지할 이유가 없다.
 *
 *  AFPSRProjectile 이 아닌 이유: 그 헤더가 결정론 직선을 클라 예측의 전제로 못박았고
 *  (`FPSRProjectile.h:13-16`) 오브젝트 타입이 WorldDynamic 이라 데미지 질의가 못 찾는다. */
UCLASS()
class FPSROGUELITE_API AFPSRBossHomingOrb : public AActor
{
public:
    AFPSRBossHomingOrb();
    /** 🔴 생성자에서 `PrimaryActorTick.bCanEverTick = true;` **명시** — AActor 기본값은 false 이고
     *  베이스가 켜 주지 않는다(VIT1 힐팩 P1). `bReplicates=true`, `SetReplicateMovement(true)`,
     *  `bAlwaysRelevant=true`(NetCull 150 m < 아레나 대각 226 m). */

    /** 서버. Instigator = **이 오브 자신**(피격 방향 인디케이터가 인스티게이터 *위치*를 쓴다 —
     *  `FPSRCharacter.cpp:1850-1855`). 텔레메트리는 `SetOwner(Boss)` + `ClassifyDamageSource` 의
     *  `GetOwner()` 폴백(S3, `Owner != Instigator` 가드 필수)이 Boss 로 분류한다. */
    void ServerLaunch(AFPSRBossBase* OwningBoss, APawn* Target, float InHealth, float InTrackSeconds,
                      float InDamage, const FFPSRDamageSpec& InSpec);

    /** 보스가 프리즈 엣지에 **밀어 준다**(각자 폴링 금지 — 선례 `UFPSRProjectileSubsystem::Tick:25-63`
     *  이 감지기 1개 + 푸시 구조). `Movement->Deactivate()` 로 속도 보존.
     *  수명은 **Δt 누산기**이지 FTimerManager 가 아니다 — 이 유닛 전체에서 타이머는 0회다. */
    void SetSimulationPaused(bool bPaused);

protected:
    UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Sphere;   // 루트
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh; // NoCollision
    UPROPERTY(VisibleAnywhere) TObjectPtr<UFPSREnemyHealthComponent> Health;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProjectileMovementComponent> Movement;

    UPROPERTY(EditDefaultsOnly, Category="Orb", meta=(ClampMin="1.0")) float InitialSpeed = 900.0f;
    UPROPERTY(EditDefaultsOnly, Category="Orb", meta=(ClampMin="1.0")) float MaxSpeed = 1400.0f;
    UPROPERTY(EditDefaultsOnly, Category="Orb", meta=(ClampMin="0.0")) float HomingAccelerationMagnitude = 2200.0f;
    UPROPERTY(EditDefaultsOnly, Category="Orb", meta=(ClampMin="0.0")) float PostTrackLifetimeSeconds = 3.0f;
    UPROPERTY(EditDefaultsOnly, Category="Orb", meta=(ClampMin="0.0")) float DeathDwellSeconds = 0.15f;
};
```

**콜리전 응답표 (G1-2 P2-6 — 엔진 기본 프로파일은 `OverlapAllDynamic` 이라 명시가 필수)**

| 채널 | 응답 | 왜 |
|---|---|---|
| 오브젝트 타입 | **`ECC_Pawn`** | 무기 질의가 이미 수집(신규 데미지 코드 0) |
| `WorldStatic` · `Destructible` | **Block** | 벽·프롭을 통과하지 않는다(관통은 레이저만) |
| `PlayerPawn` | **Overlap** | 접촉 판정용. 몸빵하지 않는다 |
| `Pawn`(스웜·보스·다른 오브) | **Ignore** | 스웜 사이를 자유 통과. 서로 밀지 않는다 |
| `Weakpoint` | Ignore | 오브에 약점 없음 |
| `Visibility` | Block | 조준 트레이스에 잡혀야 격추 가능 |

**거동 규칙**

| 사건 | 동작 |
|---|---|
| `PlayerPawn` 오버랩 | `Target->ApplyContactDamage(Damage, this, Spec)` → 자기 파괴 |
| 대상 DBNO/사망 | **가장 가까운 생존자로 재지정.** 생존자 0 → 호밍 해제(직진) |
| `TrackSeconds` 만료 | `bIsHomingProjectile=false` → 직진 → `PostTrackLifetimeSeconds` 뒤 파괴 |
| 체력 0 (격추) | `bDead` 복제가 클라에 닿게 **`DeathDwellSeconds` dwell 뒤** 파괴 — 즉시 Destroy 하면 클라가 격추 연출을 못 낸다 |
| 벽/프롭 Block 히트 | 파괴(자폭 없음) |

> ℹ️ **`APawn` → `AActor` 전환이 바꾸지 않는 것** — `FPSRCombat::ApplyKnockback` 은
> `AFPSRCharacter` → `AFPSREnemyBase` 순으로만 캐스트하므로(`FPSRCombatStatics.cpp` `ApplyKnockback`)
> 오브는 **폰이든 아니든 no-op** 이다. 로켓이 오브를 밀지 못하는 것은 전환 전후가 같고 의도된 거동이다.
> 약점(`UFPSRWeakpointComponent`)·적 애님·significance 는 전부 `AFPSREnemyBase` 계열에만 붙으므로 무관하다.

> 🔴 `Health->SetCountsAsKill(false)` — 막는 것은 **흡혈 · DealtDamage 이벤트 · 킬 크레딧 · on-kill 프래그먼트**다
> (`FPSRCombatStatics.cpp:168-183`). **XP 는 무관하다** — XP 는 `AFPSREnemyBase::HandleDeath` 에서만 떨어지고
> 오브는 그 클래스가 아니다(G1-2 P3-1 — 종전 판의 "XP 무한 파밍" 근거는 틀렸다).

### 5-7. `AFPSRGameState` 추가분 (클라 시계 유도)

```cpp
/** 현행 두 필드(`AccumulatedFrozenSeconds` · `FreezeStartedAtWorldTime`, `FPSRGameState.h:373-378`)는
 *  VIT1 이 **server-only, not replicated** 로 선언했다. 클라가 전투시계를 유도하려면 이 둘이 필요하므로
 *  **복제로 승격**한다(Push Model, `SetRunPaused` 엣지에서만 dirty — 엣지당 8 B).
 *
 *  🔑 이름을 바꾸지 않는다: 서버에서 `AGameStateBase::GetServerWorldTimeSeconds()` 는
 *  `World->GetTimeSeconds()` 와 같은 값이므로, `FreezeStartedAtWorldTime` 은 그대로
 *  **서버 월드시각 스탬프**로 읽힌다. 이 프로젝트는 이미 같은 방식의 스탬프를 쓰고 있다 —
 *  `StagePhaseEndServerTime`(`FPSRGameState.h:388-390`) · `LobbyCountdownEndServerTime`(`:161`).
 *
 *  클라 유도식 = `GetServerWorldTimeSeconds() − AccumulatedFrozenSeconds
 *                 − (bRunPaused ? GetServerWorldTimeSeconds() − FreezeStartedAtWorldTime : 0)`
 *  → 서버의 `GetCombatClockSeconds()`(`FPSRGameState.cpp:249-266`)와 **같은 식**이고, 다른 것은
 *  시간원이 `World->GetTimeSeconds()` 냐 `GetServerWorldTimeSeconds()` 냐 뿐이다.
 *  RTT 보정은 엔진이 하지 않으므로 잔차 = 편도 지연 L — 코스메틱은 보스의
 *  `MaxClientVisualLeadSeconds` 로 앞당기고, 판정 쪽은 지연 히트로 연다(§5-3). */
UPROPERTY(Replicated) float AccumulatedFrozenSeconds = 0.0f;   // server-only → Replicated 로 승격
UPROPERTY(Replicated) float FreezeStartedAtWorldTime = 0.0f;   // 서버에선 GetServerWorldTimeSeconds() 와 동일

UFUNCTION(BlueprintPure, Category="FPSR|Run") float GetCombatClockSecondsForClients() const;
```

---

## 6. 함수별 계약

| 함수 | 권위 | 호출자 | 전제 | 실패 시 |
|---|---|---|---|---|
| `AFPSRBossBase::Tick` | 서버 | 엔진 | `GS` 유효 | 조기 반환 |
| `ServerAddBlastMark` | 서버 | `GA_Barrage` | — | 상한 초과 시 최고참 **불발 제거** + 경고 1회 |
| `ServerSetBeamState` | 서버 | `GA_SweepLaser` | — | — |
| `ServerScheduleLaserHit` | 서버 | `GA_SweepLaser` | `Target` 유효 | 무시 |
| `WasRecentlyAirborne` | 서버 | `GA_SweepLaser` | — | 기록 없으면 현재 `IsFalling()` |
| `DebugForcePattern/SetPhase` | 서버 | 콘솔 | `!UE_BUILD_SHIPPING` | 인덱스 범위 밖 → 로그 |
| `ServerTickPattern` | 서버 | 보스 `Tick` | 이 어빌리티가 활성 | — |
| `ComputePhase` · `DidEnterBeam` · `WrapToPeriod` · `BeamBaseAngleAt` | 순수 | 패턴 · 테스트 | — | — |
| `IsEligibleTarget` | 서버 | 스폰 서브시스템 · 패턴 | — | `false` |
| `ServerLaunch` | 서버 | `GA_HomingOrbs` | `Target` 유효 | 스폰 취소 |
| `GetCombatClockSecondsForClients` | 양쪽 | 보스 | — | 서버에선 `GetCombatClockSeconds()` 와 동일값 |

**데미지 호출 경로는 하나다** — `Character->ApplyContactDamage(Damage, Instigator, Spec)`
(선례 `FPSRProjectile.cpp:488-502`). `FPSRCombat::ApplyDamage` 는 **플레이어 인스티게이터 전제**라 못 쓴다
(`FPSRCombatStatics.h:25-27`). 포격 인스티게이터 = **보스**(발밑 폭발이라 방향 무의미 · 이미 Boss 로 분류됨,
`FPSRDirectorSensorSubsystem.cpp:44-46`) — 종전 판의 *"표식 자신을 인스티게이터로"* 는 표식이 USTRUCT 라
**성립할 수 없는 문장이었다**(G1-2 P2-3).

---

## 7. 복제표

| 프로퍼티 | 소유 | 조건 | 빈도 |
|---|---|---|---|
| `CurrentPhase` | 보스 | Push Model | 전환 시 1회 |
| `BlastMarks` | 보스 | Push Model | 추가·제거 시 (~20 B/개) |
| `BeamCount`/`BeamStartAngleDeg`/`BeamSpeedDegPerSec`/`BeamStartClock`/`BeamWarmupEndClock` | 보스 | Push Model | 활성화·해제 시 1회씩 |
| `AccumulatedFrozenSeconds`/`FreezeStartedAtWorldTime` (**server-only → 복제 승격**, §5-7) | GameState | Push Model | **프리즈 엣지에서만** (8 B) |
| 오브 위치 | 오브 | `SetReplicateMovement(true)` · `bAlwaysRelevant` | 엔진 기본 |

> 🔴 **모든 코스메틱은 두 반쪽이다** — 클라 `OnRep` + **권위측 세터 직접 브로드캐스트**.
> RepNotify 는 권위 머신에서 돌지 않는다. VIT1 이 이 하나로 결함을 냈고(리슨서버 호스트·솔로에서
> 적 실드파손 연출이 영영 안 남), 협동 기준형이 리슨서버라 **호스트는 매 세션 존재하는 1/4** 이며
> 2인 PIE 는 클라 화면만 보면 정상으로 보인다.

---

## 8. 수명주기 · 소유권

**보스가 자기가 낳은 것 전부를 소유한다.** 폴링하는 자식 N개가 아니라 감지기 1개 + 푸시.

| 사건 | 동작 |
|---|---|
| 프리즈 엣지 | 보스 `Tick` 이 엣지를 잡아 `SpawnedPatternActors` 전원에 `SetSimulationPaused` 푸시. 표식은 클럭이 멈추므로 자동 |
| **보스 사망** | `CancelAbilities()` · `ServerSetBeamState(0,…)` · 경고 RPC `false` · 표식 전부 **불발 제거** · 오브 전부 파괴 · `SetActorTickEnabled(false)`. 🔴 보스는 사망해도 Destroy 되지 않으므로(`FPSRBossBase.cpp:164-166`) 안 닫으면 **승리 후에도 시체가 계속 포격한다** |
| **런 종료(승/패)** | `EndRunFreeze` 가 `bRunPaused` 를 **영구 고정**(`FPSRGameState.h:134-137`) → 회수를 **프리즈 게이트 앞**에 둔다(§5-3 Tick 순서 2번). 더 단순한 대안 = `GS->OnRunEnded` 구독(선례 `FPSRDirectorSensorSubsystem.cpp:139`) |
| **런 재시작** | `StartRun` 이 `ActiveBoss->Destroy()`(`FPSRRunDirectorSubsystem.cpp:93-97`) → `EndPlay` 회수. `EndPlay` 는 클라에서도 도니 **`HasAuthority` 게이트** |
| 스테이지 전환 | 보스 페이즈에서 **성립 불가**(StageDirector 가 거부 + BossTime 스왑은 `EnterBoss` 전에 종료). 게이트는 그래도 건다 |
| 플레이어 이탈·JIP | `LastAirborneClock` · `BeamTrackByPawn` · `PendingLaserHits` 에서 무효 항목 제거. JIP 는 복제 상태가 전부 |

> **보스 사망·런 종료에서 비우는 것 전량**(G1-3 P3-2 — 목표 5 "하나도 남지 않는다"의 열거):
> `SpawnedPatternActors`(파괴) · `BlastMarks`(불발 제거) · `PendingLaserHits` · `LastAirborneClock` ·
> `BeamTrackByPawn` · `ActivePatternHandle`. 틱이 꺼지므로 결제는 어차피 불가하지만, 열거에서 빠지면
> "남지 않는다"가 검증 불가능한 문장이 된다.
>
> **지연 히트 취소 술어**(G1-3 P3-3) — `LastAirborneClock[T] > (DueClock − LateJumpGraceSeconds)`.
> `WasRecentlyAirborne()` 를 그대로 쓰면 그 0.12 s 후행창이 **예약 *이전*의 공중**까지 잡아 취소가 과해진다.
> 같은 대상에 대한 중복 예약은 허용한다(빔이 여럿이면 실제로 여러 번 노출된 것이고, i-frame 0.25 s 가 흡수한다).
> `PendingLaserHits` 의 "항목 ≤4" 는 가정이 아니라 **관찰**이다 — 상한은 빔 수 × 생존자 수.

---

## 9. 데이터드리븐 경계

| 산다 | 어디에 |
|---|---|
| 페이즈 임계값 (개수 = N−1) | `UFPSRBossDefinitionDataAsset::PhaseHealthThresholds` |
| 패턴 목록·순서 | `AFPSRBossBase::GrantedAbilities` (BP CDO) |
| 패턴별 수치 전부 | 각 어빌리티 BP `EditDefaultsOnly` |
| 표식·빔·오브 연출 | BP — C++ 에 데칼·Niagara 호출 0 |
| 메시·머티리얼 | BP. **C++ 에 에셋 경로 0** |

🔴 **BP 저작 금지** — ① 패턴 BP 안의 시간 기반 AbilityTask(`WaitDelay`·`PlayMontageAndWait`): 가드 밖이라
프리즈를 뚫는다. ② `ActivateAbility` 통째 오버라이드: C++ 베이스의 `CommitAbility()` 를 건너뛰어
쿨다운이 안 찍힌다. BP 는 **`ServerTickPattern` 과 코스메틱 이벤트만** 확장한다.

---

## 10. 성능 예산

| 항목 | 예산 | 근거 |
|---|---|---|
| 보스 틱 | 액터 1개 | 스웜 200~300 대비 무시 가능 |
| 레이저 판정 | **플레이어당 1회/프레임** (빔 수 무관) | 주기 도메인 접기. 트레이스 0회 |
| 포격 판정 | 표식당 구 오버랩 1회, 최대 4개/초 | `PlayerPawn` 타입만 수집 |
| 표식 복제 | ~20 B × 최대 32 | 액터 20개 대비 채널 0 추가. 저작값 실동시 최대 = 4 × ⌈1.4/2.0⌉ = **4** |
| 오브 | 액터 5개 · **spawn/destroy**(풀링 안 함 — §2 비목표의 명시적 결정) | 볼리당 1회 × 보스 1마리 |

---

## 11. 미결정 · 갭 처리

1. **표식 4개 판독성** — 4인 동시. `TargetPawn` 으로 "내 것"을 구분 가능. 판정은 **PIE 후 사용자**.
2. **`DamageInvulnerabilityDuration`(0.25 s)** — VIT1 이 "실드 도입 후 과할 수 있다, 권장 탐색 0.10~0.25 s"로 남겨 뒀다.
   레이저는 **진입 엣지 1회**라 이 값에 히트 수가 좌우되지 않지만, 포격과 겹칠 때의 총량은 함께 본다.
3. **편도 지연 잔차** — 코스메틱은 `MaxClientVisualLeadSeconds` 로, 입력은 `LateJumpGraceSeconds` 로 연다.
   완전 제거는 불가(서버 권위) — **감수하는 결정**임을 여기 명시한다.
4. **적 투사체 AOE 루프 통합** — 후속 행(§2 비목표).
5. **`Boss.Phase.One/Two` ini 제거** — S4. 지금 지우면 S1~S3 기간 동안 참조 0 인 태그가 남는데, 어차피 코드 참조가 0이라 무해.

---

## 12. 검증 기준

### 12-1. 빌드
```bash
Build.bat FPSRogueliteEditor Win64 Development -Project=... -WaitMutex -DisableAdaptiveUnity -ForceUnity -MaxParallelActions=2
```
헤더 변경이 커서 `-MaxParallelActions=2` 로 OOM 회피(메모리 `[[build-header-change-oom-parallelism]]`).

### 12-2. 자동화 `FPSRoguelite.Boss.*` (월드리스)
1. `ComputePhase` — 빈 배열 → 1 · 경계 정확히 임계값 · 내림차순 아닌 입력 방어 · N=1~5
2. `LatchPhase` — 회복해도 안 내려감
3. `DidEnterBeam` — 빔 회전·플레이어 정지에서 **정확히 1회**
4. `DidEnterBeam` — **빔 정지·플레이어가 가로질러 이동**에서 정확히 1회 (구 호 방식이 놓치던 케이스)
5. `DidEnterBeam` — 동방향 저속 통과에서 **1회**(레벨 트리거였다면 46회)
6. `DidEnterBeam` — 도메인 랩 오탐 0 · **360 ms 히치 경계**에서 처음 깨지는지
7. **`AFPSRBossBase::StaticClass()->GetDefaultObject()` 의 `PrimaryActorTick.bCanEverTick == true`**
   (+ `AFPSRBossHomingOrb` 도 동일) — G1-2 P1-1 재발 방지
8. `WrapToPeriod` — N=1~5 등간격 빔이 한 점으로 접히는지
9. `CapsuleAngularRadiusDeg` — r→0 에서 발산 없음
10. 선택기 `FPSRBoss::BuildSelectionOrder` (`Boss.Selection`) — Sequential 은 커서에서 출발해 순환 ·
    **Random 도 후보를 전부 방문**(한 번 굴리고 포기하면 쿨다운에 걸린 후보 하나 때문에 트리거가 낭비된다) ·
    두 정책이 서로의 입력(커서/난수)을 침범하지 않음 · 범위 밖 커서가 접힘 · `NumEligible == 0`
11. **시간축 가드** (`Boss.TimeAxisGuard`) — `EnableTimeAxisGuard` 멱등(2회 호출 → 쿼리 1개) ·
    `FPSRAbilitySystem::IsTimeBasedEffect` 가 Instant=false / `HasDuration`=true / **Infinite+Period=true** ·
    등록된 델리게이트가 Instant 를 통과시킴. 거부 경로를 델리게이트로 직접 돌리지 않는 이유는 §13-7 ② 참조
    (G1-2 P3-6 이 지적한 커버리지 0 건은 이로써 해소)
12. 🔴 **결함을 실제로 잡는 케이스** (G1-3 P2-4) — ③④⑤ 는 프레임 스텝(1.0°·1.4°)이 밴드폭(6°)보다 작은
    *느린* 통과라 **수정 전 코드에서도 녹색**이다. 실패할 수 있어야 테스트다:
    - **빠른 관통** `Prev=+5, Cur=-5, W=3` → **1회** (수정 전 = 0회. §13-3 이 잡은 결함)
    - **밴드 내 방향전환** `+5→+1→+2→+1` → **1회** (레벨 트리거였다면 4회)
    - **첫 프레임** `Prev==Cur` → 밖이든 안이든 **0회**
    - **래치 해제 후 재진입** `+5→+1→+5→+1` → **2회** (실제로 두 번째 노출)
    - **웜업 종료 시 밴드 안** → 게이트가 열리는 프레임에 **1회** (단순 AND 였다면 0회. G1-3 P2-1)
13. **`IsDataValid` 의 순수 규칙** (`Boss.Authoring` — G1-3 P3-1 · G2 P3-2):
    `FPSRBoss::ValidateTrigger` 가 `Threshold <= 0` 과 `HealthBelow >= 1` 만 잡고 **정상 케이스는 통과**시키는지
    (임계 > 1 은 `PatternCount` 에선 정상이므로 두 축을 혼동하면 안 된다) ·
    `FPSRBoss::EstimatePeakBlastMarks` = `MaxPlayers × (⌊Fuse/Interval⌋ + 1)`, 0 인자에서 나눗셈 없이 0.
    이 값이 `MaxConcurrentMarks` 이하이면 런타임 FIFO 축출이 죽은 경로가 된다.
    `IsDataValid` 자체는 이 규칙들 위의 얇은 어댑터라 문구만 고른다(§13-7).

### 12-3. 엘리트 무회귀
쿨다운 계약을 공통 베이스로 호이스트 + 가드를 ASC 로 이동하므로 기존 엘리트 테스트 전부 + `FPSR.EliteDump` 대조.

### 12-4. PIE (사용자 · `FPSR.BossPattern <idx>` / `FPSR.BossPhase <n>`)
1. 포격 착탄점이 **각자 발밑**, 공중이어도 표식은 지면
2. 레이저 **웜업 동안 무피해** + 경고 인디케이터가 뜨고 **꺼진다**
3. 점프로 넘을 수 있다 / **빔 안으로 뛰어들면 맞는다**
4. **원격 클라(2인 PIE)에서 보이는 빔과 맞는 순간이 일치**
5. 오브가 총에 부서지고 **격추 연출이 클라에도 보인다** · XP 안 나옴 · **프롭 위에서도 데미지가 들어간다**(P2-5)
6. **패턴 중 레벨업 프리즈 30초** — 신관·회전각·오브 진행 0
7. 페이즈 2·3 에서 빔 2줄·3줄
8. 보스 처치 후 시체가 공격 안 함 · 표식·오브 잔존 0
9. **리슨서버 호스트 화면**에서 페이즈·빔·격추 연출이 전부 난다
10. 런 재시작 후 이전 런 잔존물 0
11. BP 패턴에서 `WaitDelay` 를 일부러 써 보고 프리즈를 뚫는지 확인(금지 규칙의 근거 실증)

### 12-5. 통과 정의
P1 잔존 0 · 자동화 전부 녹색 · PIE 11항목 사용자 확인 · Fable 머지 게이트 통과.

---

## 13. 게이트 원장

### 13-1. G1 1차 (2026-09-02, Fable) — 반려 (P1 3 · P2 7 · P3 11)
레이저 사전경고 부재 · 호 교차 판정 결함 · 클라 드리프트 / 인스티게이터 · 자식 수명 · 풀링 미루기 ·
페이즈 태그 상한 · GAS 근거 오류 · 명세 형식 · 슬라이스. **2차 제출본에서 전량 반영.**

### 13-2. G1 2차 (2026-09-02, Fable) — 반려하되 **"골격은 통과"** (P1 1 · P2 9 · P3 12)

| 지적 | 처리 | 위치 |
|---|---|---|
| **P1-1** 보스 생성자가 `bCanEverTick=false` 를 명시 중인데 명세가 안 뒤집음 | 생성자 변경 선언 + **CDO 테스트** | §5-3 · §12-2 ⑦ |
| **P2-1** `DidCross` 분기 ②가 레벨 트리거 → over-count(46프레임 = 6히트) | **진입 엣지**만(`DidEnterBeam`) + `PrevRelByPawn` 저장소 선언 | §5-2 · §5-5 |
| **P2-2** 편도 지연 L 미제거 · `Replicated` 만으론 외삽 불가 | GameState 프리즈 앵커 2값 복제 + 클라 유도 + 지연 히트 | §5-7 · §5-3 |
| **P2-3** 표식은 USTRUCT 라 인스티게이터 불가 | 포격 인스티게이터 = **보스** | §6 |
| **P2-4** `MarkId` 가 UPROPERTY 아님 → 복제 0 | **immediate-mode** 로 전환, 키 자체를 제거 | §5-1 |
| **P2-5** `APawn` 오브가 차단 셀 위에서 무적 | **`AActor` + `ECC_Pawn` 스피어** | §5-6 |
| **P2-6** 콜리전 응답표·PMC·접촉 규칙 없음 | 응답표 + 3필드 + 거동표 | §5-6 |
| **P2-7** 오브 "고정 5기 재사용" vs "dwell 후 파괴" 모순 | **spawn/destroy 로 확정**, §10·비목표 정정 | §2 · §10 |
| **P2-8** 빔에 시작 시각·웜업 종료 없어 클라가 각도 계산 불가 | `BeamStartClock`·`BeamWarmupEndClock` + 서버·클라 동일 식 | §5-3 · §5-2 |
| **P2-9** `InnerRadiusCm` 의미 · 점프 판정 축 미정 | 히트 규칙 한 줄 + `BeamVisualHeightCm`(연출 전용) | §5-5 |
| P3-1 선택기 계약·동시 패턴 모순 | **단일 활성 확정**, 동시 발동은 비목표 | §5-3 |
| P3-2 페이즈 배선 미선언 | `HandleHealthChanged`·`PhaseThresholds`·디버그 진입점 | §5-3 |
| P3-3 문구 정정(전환 중 시계·ADR 0010 D2·인용 줄번호·달리기 900) | 전량 반영 | §2 · §5-1 · §5-2 · §5-6 |
| P3-4 죽은 태그 | S4 에서 ini 제거 | §4 · §11-5 |
| P3-5 §5-4 vs §4 모순 | 공통 베이스 신설 + 엘리트 파일을 §4 에 추가 + `ServerOnly` 명시 | §4 · §5-4 |
| P3-6 가드 커버리지 0 | 월드리스 가드 테스트 신설 | §12-2 ⑪ |
| P3-7 바닥 Z 원천 없음 | `LastGroundedZByPlayer` 사용 | §5-1 |
| P3-8 소비자 없는 필드 | `KnockbackStrength` 경로 · `DamageType` 를 `FFPSRDamageSpec` 에 | §5-5 |
| P3-9 32 상한 IsDataValid | 저작값 검사로 런타임 FIFO 를 죽은 경로화 | §5-3 · §12 |
| P3-10 런 종료 회수 순서 | **Tick 순서 7단계** 명시(회수가 프리즈 게이트 앞) | §5-3 |
| P3-11 슬라이스 세부 | 센서 폴백 → S3 · 헤더 1회 변경 · 술어 추출 별도 커밋 | §4 |
| P3-12 경고 브래킷 종료 | 웜업종료·스윕종료·`EndAbility` 전부에서 `false` | §5-5 · §8 |

**2차가 확인해 준 것(건드리지 않음)** — 전역 전투시계 재사용 판단 · ASC 가드 1벌 호이스트 ·
보스 소유 복제 배열 표식 · 주기 도메인 상대각 · 슬라이스 절단.

### 13-3. G1 3차 — 1회 중단 후 재시도 (2026-09-02)

**첫 시도가 Fable 세션 한도(429)로 중단**됐다(16:30 KST 리셋). 중단 구간 동안 Opus 가 3차 질문표를
코드로 직접 검증했고, **결함 1건 + 정정 2건**이 나왔다. 셋 다 반영 후 재제출했다.

| # | 발견 | 처리 |
|---|---|---|
| 🔴 결함 | **`DidEnterBeam` 이 빠른 관통을 놓친다.** 2차 지적(레벨 트리거 over-count)을 고치며 판정을 "밴드 진입"으로 좁혔는데, 빔이 한 프레임에 플레이어를 건너뛰면 `\|Cur\| > W` 라 **영영 히트가 안 난다** — 부호반전 방식이 잡던 케이스를 잃었다 | "밖에 있었다" 게이트 + "들어왔거나 관통했다" OR (§5-2). 다섯 경우 전부 옳다(1/1/0/1/0) |
| 정정 | §5-7 이 존재하지 않는 필드명(`ReplicatedFreezeStartedServerTime`)을 썼다. 실제는 `AccumulatedFrozenSeconds`·`FreezeStartedAtWorldTime`(`FPSRGameState.h:373-378`, server-only) | 실명으로 교체 + **복제 승격**임을 명시. 서버에선 `GetServerWorldTimeSeconds() == World->GetTimeSeconds()` 라 스탬프가 그대로 유효하고, 같은 관용구를 `StagePhaseEndServerTime` 이 이미 쓴다 |
| 확인 | `APawn`→`AActor` 전환의 부수효과(2차 질문 5) — `ApplyKnockback` 은 `AFPSRCharacter`/`AFPSREnemyBase` 만 캐스트 | **전환 전후 동일(no-op)**. 약점·애님·significance 도 무관. §5-6 에 명시 |

### 13-4. G1 3차 판정 (2026-09-02, Fable) — ✅ **통과 · P1 0건**

> *"§13-3 의 결함 1건·정정 2건은 본문에 실제로 반영돼 있고, 2차가 남긴 P1-1 은 `FPSRBossBase.cpp:21` 이
> 여전히 false 인 사실과 §5-3 · §12-2 ⑦ 이 정확히 맞물린다. P2 4건은 전부 골격 무변·10줄 이하다."*
> §13-2 표 22행 중 19행 일치, 어긋난 3행도 아래에서 닫았다.

| 지적 | 처리 |
|---|---|
| **P2-1** 히트 규칙 합성 구멍 — 엣지에 게이트를 AND 로 걸면 ①웜업 종료 시 밴드 안 ②밴드 안에 착지 두 경우가 **통과 전체를 0히트**로 만든다(②는 §12-4 #3 과 정면 충돌) | **노출 + 래치**로 재작성 (§5-5). 대안(게이트를 좁히고 ①감수)은 *"회피는 점프뿐"* 과 모순이라 기각 — 새 사용자 결정이 필요한 게 아니라 **기존 결정에서 답이 나온다** |
| **P2-2** §7 복제표가 폐기된 이름을 씀 (§13-3 정정 #2 미전파) | 실명 + "복제 승격" 표기 (§7) |
| **P2-3** `FPendingLaserHit` 에 `FFPSRDamageSpec` 없음 → 레이저만 `DamageType` 손실 | 필드 추가 (§5-3) |
| **P2-4** 자동화 ③④⑤ 가 **수정 전 코드에서도 녹색** (스텝 1.0~1.4° < 밴드폭 6°) | 실패할 수 있는 5케이스를 ⑫로 추가 (§12-2) |
| P3-1 `IsDataValid` 항목이 §12 에 없음 | ⑬ 신설 |
| P3-2 teardown 열거 누락 | §8 인용 블록 |
| P3-3 취소 술어 미선언 | §8 인용 블록 (`WasRecentlyAirborne` 를 그대로 쓰면 취소가 과해진다) |
| P3-4 Tick 순서 — **순환 없음 확인.** 유일한 1프레임 지연 = 오브 PMC(≤46 cm, 해제 시 대칭) | 필요 시 `AddTickPrerequisiteActor(Boss)` — 지금은 감수 |
| P3-5 스윕 중 페이즈 상승 | `BeamCount`·Period **활성화 시 고정** (§5-5) |
| P3-6 VIT1 "server-only" 주석 | §4 파일 목록에 명시 |
| P3-7 문구("네 경우" vs 5불릿) | §5-2 · §13-3 정정 |
| P3-8 (nit) Prev 게이트의 W 변동 | 래치 구조가 "밖이었다"를 저장하므로 자동으로 닫힘 |

**G1 종료.** 다음 = S1 구현(§6-5: Sonnet 구현 → Opus 검증 → G2 머지 게이트).
단 **ASC 부착·프리즈 클럭·복제·수명주기는 Opus 직접**(프리즈 대칭은 하위 모델 위임 금지 영역).
### 13-5. C3 검증 원장 (2026-09-02, Opus)

**빌드** — `-DisableAdaptiveUnity -ForceUnity` 통과.
⚠️ 중간에 1회 실패했다(`Result: Failed`, os error 5 = 액세스 거부). 컴파일 오류가 아니라 환경 문제였고
(같은 클론에서 다른 세션이 동시 작업 + XGE), `-NoXGE` 로 통과했다. **파이프 때문에 exit code 가 0으로 나와
성공으로 오독할 뻔했다** — 빌드 판정은 exit code 가 아니라 `Result:` 줄로 한다.

**자동화** — 63개 중 61 통과. 신규 `FPSRoguelite.Boss.{Phase, LaserSweep, TickEnabled, Trigger}` 4종 전부 통과.
실패 2건은 **이 브랜치 이전부터** 깨져 있던 선행 건으로 커밋 조상까지 확인했다:

| 실패 | 원인 |
|---|---|
| `FPSRoguelite.Enemy.BlueprintParent` | `776e8441`(VIT1 콘텐츠)이 `Content/Character/Enemy/Data/` 에 DataAsset 을 넣었는데, 먼저 쓰인 테스트(`b7ded3cc`)가 그 폴더를 재귀로 훑어 전부 BP 클래스(`_C`)로 로드되길 요구한다 |
| `FPSRoguelite.Editor.CardCsv.RoundTrip` | `776e8441` 이 카드풀에서 HealthRegen 을 뺐으나 `Content/Authoring/Cards.csv:2` 에는 남아 있어, 임포터가 되살리며 패키지를 dirty 로 만든다 |

둘 다 별도 유닛 소관이라 이 브랜치에서 고치지 않고 작업 칩으로 올렸다.
⚠️ VIT1 워크로그는 *"빌드 2회 + 자동화 3종 전부 통과"* 라고 기록했는데 실제로는 그때 이미 이 둘이 빨간 상태였다 —
**자동화 "통과"는 실행한 스위트에 한정된 진술이다.**

**PIE (사용자, 2026-09-02)**

| 확인됨 ✅ | 미확인 ⚠️ |
|---|---|
| 패턴 3종 정상 동작(포격·레이저·미사일) | **패턴 진행 중 레벨업 프리즈에서 신관·빔 각도·오브가 멈추는가** |
| **미사일이 지목 대상의 사망 지점으로 가서 폭발** — §14-1 핵심 설계 결정이 의도대로 성립 | **2인 PIE 호스트·클라 양쪽 화면**(호스트는 OnRep 을 못 받는다 / 빔의 서버-클라 일치) |

🔴 **미확인 2건은 이 유닛에서 가장 위험한 축이다.** 프리즈는 이 설계의 본체이고(엔진은 GE·타이머를 월드
타이머로 돌려 상태 게이트인 프리즈를 그냥 뚫는다), 호스트 OnRep 누락은 **직전 유닛 VIT1 이 실제로 낸 결함**
그 형태다(2인 PIE 에서 클라 화면만 보면 정상으로 보인다). 코드는 두 축 모두를 겨냥해 작성됐고 자동화도
프리즈 쪽을 순수 함수 수준에서 덮지만, **런타임 관측은 아직 없다.**
→ `FPSR.BossDebugDraw 1` 의 클럭 숫자(프리즈 중 정지)와 `SERVER`/`CLIENT` 라벨(양쪽 화면 비교)이 이 둘을
보라고 만든 것이다. 머지 후라도 확인 권장.

### 13-6. G2 머지 게이트 (2026-09-02, Fable) — **조건부 통과 · P1 0건** → 조건 이행 완료

P2 3건 전량 + P3 5건 중 4건 반영. 게이트가 "확인했으나 지적 없음"으로 근거를 남긴 축(프리즈 정확성 ·
수명주기 · 복제 대칭 · 엘리트 무회귀 · 스웜 핫패스 · 신규 데미지 경로 · 콘텐츠 커밋)은 손대지 않았다.

| 지적 | 처리 |
|---|---|
| **P2-1 🔴 `GetBeamState()` 가 서버 판정과 다른 각도식** — §14 에서 각도를 2구간으로 바꾸며 판정·디버그만 고치고 BP 접근자를 빠뜨렸다. BP 빔이 판정 빔보다 `Speed × Grace`(페이즈 3에서 90°) 앞서, **점프는 빈 공기를 넘고 그 뒤 보이지 않는 빔에 맞는다.** 디버그 오버레이가 올바른 쪽 식을 써서 PIE 로도 안 보였다 | 접근자를 `BeamBaseAngleAt` 로 교체. **모든 소비자가 같은 함수를 부른다**는 규칙을 주석으로 못박고, 회귀 테스트 추가(§12-2 ⑭) |
| **P2-2 오브 코스메틱이 권위 반쪽뿐** — 격추·폭발 둘 다 호스트만 본다. VIT1 결함의 거울상 | 격추 = `OnDeathCosmetic` 을 **전 머신 바인딩**(클라 반쪽) + 권위측 직접 호출. 폭발 = `bDetonated` 복제 + `OnRep` + dwell 후 파괴(즉시 Destroy 는 클라에 엣지를 안 남긴다) |
| **P2-3 `OnRep_BeamState` 가 클라 6회 / 호스트 1회** — 엔진은 RepNotify 를 프로퍼티마다 큐잉한다 | 알림은 `BeamCount` 하나만. 나머지 5필드는 `Replicated`(같은 번치라 값이 먼저 도착). 해제 중복 호출도 no-op 가드 |
| P3-1 `FPSR.BossPattern` 이 쿨다운을 못 넘고, 로그가 `CurrentPhase` 를 두 번 찍어 원인을 오도 | `DebugClearCooldown()` 추가 + 로그를 `MinPhase` vs 현재 페이즈로 정정 |
| P3-3 표식 Z 가 캡슐 **중심**(≈+90cm) | 캡슐 half-height 를 빼서 실제 지면으로 |
| P3-4 `MarkedPlayer` 가 원거리 클라에 null 로 도착 | 폰 → **`APlayerState`** 로 복제(항상 관련). `GetMarkedPawn()` 은 편의용이고 null 이 "지목 없음"이 아님을 가이드에 명시 |
| P3-5 죽은 약참조가 프리즈 엣지에서만 정리 | 등록 시에도 정리 |
| P3-2 누락 테스트 + `IsDataValid` 가 없는데 헤더가 있다고 적음 | **이행 완료** — §13-7 |
| (범위 밖) 가이드의 `Event ActivateAbility` 설명이 부정확 | 정정 — 베이스가 `Super` 를 안 부르므로 그 BP 이벤트는 *애초에 실행되지 않는다* |

**검증** — 빌드 통과 · 전체 63개 중 61 통과(실패 2건은 §13-5 의 선행 건, 무변).

### 13-7. G2 P3-2 이행 (사용자 지시 2026-09-03 — "1번으로 간다")

머지 게이트가 남긴 유일한 미이행 항목. **헤더와 §14-3 이 "`IsDataValid` 가 경고한다"고 적어 두고 실제로는
없었던** 상태를 해소한다. 런타임은 FIFO·clamp·폴백으로 안전했으므로 P3 였지만, *문서가 약속한 것이 존재하지
않는* 상태 자체가 결함이다.

**① `AFPSRBossBase::IsDataValid`(`WITH_EDITOR`) 신설** — 경고 = `GrantedAbilities` 빈 배열 · `PatternTriggers`
빈 배열(= 보스가 영영 공격하지 않거나, 의도치 않은 폴백 케이던스로 도는 두 경우) · **포격의
`4 × (⌊Fuse/Interval⌋+1)` 이 `MaxConcurrentMarks` 를 넘는 조합**. 오류 = `Threshold <= 0` ·
`HealthBelow >= 1`.

**② 판정 규칙은 순수 함수로 분리한다.** `FPSRBoss::ValidateTrigger`(→ `ETriggerAuthoringIssue`) ·
`FPSRBoss::EstimatePeakBlastMarks` · `FPSRBoss::BuildSelectionOrder` · `FPSRAbilitySystem::IsTimeBasedEffect`.
`IsDataValid` 와 선택기·가드는 그 위의 **얇은 어댑터**만 남는다 — `namespace FPSRVitals`(VIT1) 및 이 명세가
이미 `ComputePhase`/`ShouldTriggerFire` 에 쓴 형식과 같다.

> 🔴 `IsTimeBasedEffect` 를 굳이 뽑아낸 실무적 이유: 가드의 거부 경로는 **의도적으로 `ensureMsgf` 를
> 발화**하는데, 자동화 러너가 그 ensure 의 콜스택을 **테스트 에러로 캡처**해 테스트를 실패시킨다(1차 실행에서
> 실제로 그렇게 실패했다). `AddExpectedError` 로 콜스택 라인을 전부 매칭하는 것은 취약하므로 **규칙을 ensure
> 밖으로 꺼냈다.** 어댑터 극성은 살아있는 델리게이트의 *허용* 경로로 계속 단언한다 — 반환을 뒤집으면 그쪽이
> 깨진다.

**③ `BuildSelectionOrder` 는 난수를 인자로 받는다.** 선택기가 `FMath::RandHelper` 를 호출하는 것은
`Random` 일 때뿐 — 전역 스트림을 `Sequential` 보스가 흔들지 않게 한다.

**신규 테스트 3종** — `FPSRoguelite.Boss.{Selection, Authoring, TimeAxisGuard}` + `Boss.TickEnabled` 에
**오브 CDO** 단언 추가(§12-2 ⑦ 의 나머지 절반). 이로써 §12-2 ⑦⑩⑪⑬ · §14-5 `Boss.Selection` 이 모두 실재한다.

**검증** — 빌드 통과 · 브랜치 기준 66개 중 64 통과(실패 2건은 §13-5 와 동일한 선행 건).

**머지 후 재검증** — `origin/main` 이 21커밋 앞서 있어(HUD 아트·README·U22a 정리) 스테일 로컬 main 이 아니라
**현재 `origin/main` 위로** `--no-ff` 머지했다. 머지 커밋 = `2a8a5351`.
머지 트리 풀빌드 `Result: Succeeded`(178초 — 유니티 재편이 forward-decl UPROPERTY 를 incomplete-type 으로
표면화하는 함정 때문에 머지 후 풀빌드는 형식이 아니라 실효 검사다) · 자동화 **66개 중 65 통과**.
잔여 실패 1건은 `Editor.CardCsv.RoundTrip`(카드 CSV 임포트가 패키지를 더럽힘) 뿐이며 이 유닛과 무관하다 —
나머지 1건 `Enemy.BlueprintParent` 는 그 21커밋 안의 `305810cd` 가 이미 고쳐 놨다.

---

## 14. 개정 — 공통 패턴 수명주기 + 트리거 축 (사용자 지시 2026-09-02, S1~S3 구현 후)

> 배경: S1~S3 는 "패턴이 쿨다운이 되면 곧바로 이어서 나간다" 구조였다. 사용자 지시로 **모든 패턴에 공통인
> 준비/후딜 구간**과 **패턴 시작을 결정하는 트리거 축**을 도입한다. 패턴 3종의 내부 로직도 함께 개정한다.

### 14-1. 사용자 결정

| # | 결정 | 비고 |
|---|---|---|
| 트리거 | **"패턴 N회 수행"으로 지금 구현**, 평타는 별도 슬라이스 | 보스에게 평타가 없어 "단순 공격 5회"를 셀 대상이 없었다. 스키마는 트리거 종류만 추가하면 되게 열어 둔다 |
| 빔 배치 | **첫 빔만 12/3/6/9 중 랜덤**, 나머지는 등간격 | 방위 고정은 3개일 때 90/90/180 이 되어 180° 쪽에 넓은 안전지대가 고정된다. 등간격이면 페이즈가 오를수록 압박이 고르게 오른다 + 주기 도메인 접기(§5-2)가 그대로 산다 |
| 미사일 타겟 사망 | **마지막 위치로 가서 폭발** | "전부 제거"는 *지목당한 동료를 살리지 않는 쪽이 이득*이 된다(DBNO 부활 압박 소거). 사망 지점 폭발은 반대로 **부활하러 모인 팀원을 노린다** — 뭉침을 벌하는 방향이라 협동 설계와 맞는다 |

### 14-2. 공통 수명주기 — 준비 → 실행 → 후딜

**3층으로 나눈다.** 시스템 준비(보스 모션, 아무것도 안 나옴) / 패턴 내부 유예(표식 신관·빔 정지·미사일 대기) /
시스템 후딜(유휴). 가운데 층은 패턴마다 의미가 달라 **공통화하지 않는다** — 셋 다 별개 데이터다.

```cpp
UENUM() enum class EFPSRBossPatternStage : uint8 { Prep, Execute, Recovery, Finished };

// UFPSRBossGameplayAbility (베이스가 상태기를 소유한다)
UPROPERTY(EditDefaultsOnly) float PrepSeconds = 1.5f;      // 시스템 준비
UPROPERTY(EditDefaultsOnly) float RecoverySeconds = 1.0f;  // 시스템 후딜

// 파생이 채우는 것은 실행 구간뿐 — 준비/후딜을 각자 구현하면 세 벌이 어긋난다.
virtual void ServerBeginExecute() {}
virtual bool ServerTickExecute(float DeltaSeconds) { return true; }  // true = 실행 종료
virtual void ServerEndExecute() {}
```

보스는 `EFPSRBossPatternStage` 를 복제해 BP 가 준비/후딜 모션을 재생할 수 있게 한다(§7 에 추가).

### 14-3. 트리거 축

패턴은 **연속으로 이어지지 않는다.** 후딜이 끝나면 보스는 유휴 상태로 들어가고, 트리거가 발화해야 다음 패턴이 나간다.

```cpp
UENUM() enum class EFPSRBossTriggerKind : uint8
{
    Elapsed,       // 보스전 경과 시간(초). bRepeating 이면 Threshold 마다 반복
    PatternCount,  // 지금까지 수행한 패턴 수. bRepeating 이면 Threshold 배수마다
    HealthBelow,   // 체력 비율이 Threshold 이하로 처음 떨어질 때 1회
    // 후속: BasicAttackCount — 평타 슬라이스가 붙을 때 여기만 늘리면 된다
};

USTRUCT() struct FFPSRBossPatternTrigger { EFPSRBossTriggerKind Kind; float Threshold; bool bRepeating; };

UENUM() enum class EFPSRBossPatternSelection : uint8 { Sequential, Random };
```

- 보스가 `TArray<FFPSRBossPatternTrigger> PatternTriggers` + `SelectionPolicy` 를 저작한다.
- 트리거 발화 → `MinPhase` 를 만족하고 쿨다운이 찬 패턴 중 정책대로 하나 선택.
- 🔴 **빈 트리거 배열 = 보스가 영원히 아무것도 안 한다.** 그 무성 실패를 막기 위해, 배열이 비면
  `PatternGapSeconds` 주기의 반복 트리거 하나로 폴백하고 `IsDataValid` 가 경고한다.
- `HealthBelow` 는 `MinPhase` 와 겹쳐 보이지만 다르다 — `MinPhase` 는 *그 페이즈부터 계속 사용 가능*(게이트)이고
  `HealthBelow` 는 *그 순간 1회 발화*(시계)다. 축이 다르므로 둘 다 남긴다.

### 14-4. 패턴별 개정

**포격** — 구조 무변(준비/후딜만 베이스로 이관). 요구대로 준비가 끝나면 살아있는 전원의 현재 위치에 영역을
깔고, 신관 뒤 폭발한다. 폭발은 반경 안 **모든 플레이어**를 때린다(단일 대상 아님 — 이미 그렇다).

**레이저**
- 첫 빔 = 12/3/6/9 중 랜덤, 추가 빔 = `360/N` 등간격(14-1).
- 생성 후 **`BeamGraceSeconds` 동안 정지 + 무해**, 그 뒤 회전하며 살아난다.
  종전의 `WarmupSeconds` 가 이 역할을 하되 **회전하지 않는다**(종전엔 웜업 중에도 돌았다).
- 회전 속도 = `TArray<float> AngularSpeedByPhase`(인덱스 = 페이즈−1, 초과 시 마지막 값). 코드 상수 없음.
- 각도식이 2구간이 되므로 서버·클라 공용 함수로 승격: `BeamAngleAt(Start, Speed, StartClock, GraceEnd, Now)`.

**유도 미사일**
- 준비가 끝나면 대상 1명 지정 → **전원에게 공지**. 보스에 복제 필드 + OnRep + BP 훅(연출 요구).
- 미사일은 **보스 주위에 링으로 생성**되어 `OrbGraceSeconds` 동안 대기, 그 뒤 추적 시작.
- 대상 사망/다운 → **마지막 위치로 이동해 그곳에서 폭발**(전부 제거 아님, 14-1).
- 물체에 막히면 폭발 후 제거. 플레이어에 닿으면 **반경 내 모든 플레이어**에게 데미지 후 제거.
- 폭발은 전부 `FPSRCombat::ApplyHostileExplosion` 한 경로를 탄다(인스티게이터 = 미사일, 오너 = 보스).

### 14-5. 검증 추가

- `FPSRoguelite.Boss.Trigger` — Elapsed/PatternCount/HealthBelow 각각의 발화 시점, 반복/1회, 빈 배열 폴백
- `FPSRoguelite.Boss.Selection` — Sequential 순환, Random 이 `MinPhase`·쿨다운 미달을 건너뛰는지
- `FPSRoguelite.Boss.LaserSweep` 에 **유예 구간 무회전** 케이스 추가(유예 중 각도 불변, 종료 직후부터 증가)
- PIE: 준비/후딜 모션이 보이는가 · 유휴 구간에 보스가 정말 아무 데미지도 안 주는가 ·
  미사일이 **부활 지점에서 터지는가**
