# STAT1 — 경량 상태이상 기반 (약한 4종 + 강한 2종 + 라이플 부여 카드 4장) · **rev4**

> 보드 행: [경량 적 상태이상 서브시스템 (비-GE)](https://app.notion.com/3b93972ddd8881c09b88ee600cc486f0) · 마일스톤 **M1** · 갈래 **코어(T1·T3·T5)**
> 상위 계약: `Docs/SSOT/Enemy.md` §2-6 · `Docs/SSOT/Roadmap.md` §7-6 M1 · `Docs/SSOT/Performance.md` §5 · `Docs/Specs/VIT1_ShieldHealthTwoLayer.md`
>
> **rev1 → rev2**: G1 1회차(Opus 레드팀 15건 + Codex 9건) 전건 반영.
> **rev3 → rev4**: G1 3회차(새 Opus 인스턴스 14 + Codex 7) 반영 + **사용자 결정으로 배타 축 철회**.
> 🔴 시간축 처방이 **세 번 연속** 틀렸다(rev1 전제 · rev2 앵커쌍 · rev3 refcount·실드앵커) — 구현 시 이 축을
> 최우선 검증 대상으로 볼 것. 원장 = §12.
>
> **rev2 → rev3**: G1 2회차(이행검증 + 신규 11건) 반영. 🔴 **rev2 의 시간축 처방이 그 자체로 버그였다** —
> 전투시계 확장은 프리즈·전환 중첩에서 시계를 역행시키고(앵커 1쌍), 애초에 "잠복 버그 해소"라는 전제가
> 틀렸다(전환은 사격을 안 막으므로 VIT1 의 근거가 성립하지 않는다). **상태 전용 시계로 교체**했다. 원장 = §12.

---

## 1. 목표

적(스웜·엘리트·보스)에 **지속시간 있는 상태이상**을 거는 공용 기반을 만들고, 약한 4종과 강한 2종을 얹는다.
라이플에 부여 카드 4장을 붙여 **4인 전원이 눈으로 확인 가능한 상태**까지 간다.

이름·설명·수치는 **미확정**(사용자 2026-09-06). 이 유닛이 고정하는 것은 **구조**이고, 이름·지속시간·배율·
조합표·쿨다운은 전부 콘텐츠 값으로 노출한다.

| 축 | 산출물 |
|---|---|
| 약한 4종 | 둔화 · 도트데미지 · 방어력감소 · 공격속도저하 |
| 강한 2종 | 실명(공격불가) = 둔화+도트 · 속박(이동불가) = 도트+방어력감소 |
| 부여 | 라이플 `UnlockableFeatures` 에 4장 (**배타 없음** — 사용자 결정 2026-09-06 3회차) |
| 가시성 | `StatusBits` 복제 + **OnRep 클라 반쪽** + GMS 이벤트 + 플레이스홀더 큐 |

**범위 = 6축 유지**(사용자 결정 2026-09-06). `Roadmap.md:194` 는 M1 에 상태축 2~3개를 적었으나, 사용자가
설계한 **조합 구조가 통째로 돌아야 재미를 판정할 수 있다**는 이유로 6축을 그대로 간다. G2 판정 해상도가
축 수에 비례하지 않는다는 레드팀 지적(§12 G1-19)은 수용하되, 판정 대상이 "축 개수"가 아니라 "조합"이라는
점에서 이 유닛의 최소 단위가 6축이다.

## 2. 비목표

- **피아식별 반전** — 2단계 별도 행(사용자 결정). 근거 = §3-C. 슬롯만 예약한다.
- **플레이어에게 걸리는 상태이상** — 적→플레이어 데미지는 브릿지를 우회한다(§3-C). 대상 = 적 전용.
- **`DamageType` 트리거 부여** — `Enemy.md:71-72` 는 착지점을 "속성 피격 → 공통 디버프"로 규정했다. 이 유닛은
  **프래그먼트 트리거 하나만** 쓴다. `UFPSRCardEffect::GetDamageTypeTag()`(오버라이드 0건) 시임은 **이번에도
  건드리지 않고 미사용으로 남긴다** — 기제를 둘로 만들지 않기 위해서다(G1-7).
- **프로덕션 VFX·오디오** — M2. 이번엔 플레이스홀더.
- 라이플 외 무기의 부여 카드.
- 🔴 **프래그먼트 배타 축(`ExclusionGroup`)** — 사용자 결정으로 **철회**(§3-D). 그 결과 이 유닛은 공용 헤더
  `UFPSRWeaponFragment` · `FPSRCardSubsystem` 추첨부 · `UCardEffect_WeaponBehavior::CanApply` 를 **전혀
  건드리지 않는다.** 직전 유닛(CRIT2)이 방금 손댄 시너지 가중·2단 추출과의 충돌 위험도 함께 소멸한다.

## 3. C0 조사 실측

### 3-A. 클린 슬레이트 (레드팀 재확인 완료)
- `Source/` 에 `StatusEffect|Shock|Frost|Rupture|Burn|Debuff|Ailment` 심볼 **0건**.
- `Status.Burning/Slowed/Stunned`(`Config/DefaultGameplayTags.ini:76-78`) = 스캐폴드 이후 참조 0건.
- `UFPSRWeaponFragment::OnStatusKill`(`FPSRWeaponFragment.h:108`) = 선언만, 호출부 0.
- `UCardEffect_*::GetDamageTypeTag()`(`FPSRCardEffect.h:80-81`) = 오버라이드 0건.

### 3-B. 붙일 자리 (rev1 의 오류 2건 정정)

| 사실 | 근거 |
|---|---|
| 적은 **액터 틱 0** | `FPSREnemyBase.cpp:58` |
| 적·컴포넌트에 `FTimerHandle` **0건** | `grep FTimerHandle Private/Enemy/` |
| 이동속도 후크 = **1함수, 읽는 곳 2곳** | `GetEffectiveMoveSpeed()` — `FPSREnemyBase.cpp:1276, 1413` |
| "사용 시점에 곱한다" 관용구 검증됨 | `FPSREnemyBase.cpp:36-54` (`FPSR.Debug.EnemySpeedScale`) |
| 데미지 브릿지 단일 | `FPSRCombat::ApplyDamage`(`FPSRCombatStatics.h:138`) |
| 전투시계 존재, 틱 0 | `AFPSRGameState::GetCombatClockSeconds()`(`FPSRGameState.cpp:256-273`) |
| 적 per-instance 방어 층 **없음** | `FPSREnemyHealthComponent.cpp:81-83` |

🔴 **rev1 정정 ① — 배치 패스의 early-return 은 하나가 아니라 셋이다** (G1-2):

| 지점 | 조건 | 상태 진행에 대한 함의 |
|---|---|---|
| `FPSREnemySpawnSubsystem.cpp:306` | `bFrozen = IsRunPaused() \|\| IsStageTransitionActive()` | 멈춰야 **한다** |
| `:312` | `ActiveEnemies.Num() == 0` | 무해(진행시킬 대상 없음) |
| `:385` | `PlayerPawns.Num() == 0` — **4인 전원 DBNO 포함** | 멈추면 **안 된다**. U9 협동 사망모델의 정상 상태이고, 이때 시계는 계속 흐르므로 소생 시 전 상태가 일괄 만료된다 |

🔴 **rev1 정정 ② — "`EditDefaultsOnly` 라 CDO 공유"는 틀렸다** (G1-9). `RangedChargeTime`/`RangedFireCooldown`
(`FPSREnemyBase.h:905-910`)은 **액터 인스턴스 멤버**이고 `EditDefaultsOnly` 는 에디터 노출 제한일 뿐이다.
원본을 안 건드리는 진짜 이유는 **풀 재사용 시 값이 다음 생으로 새기 때문**이다.

🔴 **정정 ③ — 이 체력 컴포넌트를 갖는 액터는 5종인데 진행 드라이버가 있는 것은 2종뿐** (G1-3):

| 액터 | 드라이버 |
|---|---|
| `AFPSREnemyBase` (+ Elite) | 배치 패스 ✅ |
| `AFPSRBossBase` (`FPSRBossBase.h:44` = `ACharacter` 상속, 자기 틱 `FPSRBossBase.cpp:47`) | 자기 틱 ✅ |
| `AFPSRDestructible`(문) `FPSRDestructible.h:165` | **없음** ❌ |
| `AFPSRMissionFleeTarget` `.h:33`(`bCanEverTick=false`) | **없음** ❌ |
| `AFPSRBossHomingOrb` `.h:98` | 자기 틱(수명 짧음) ⚠️ |

### 3-C. 이번 유닛이 하지 않는 이유 (피아식별 반전)

| 막힌 지점 | 근거 |
|---|---|
| 타겟 후보가 `PlayerControllerIterator` 로만, `TInlineAllocator<4>` | `FPSREnemySpawnSubsystem.cpp:335-341` |
| 적격 판정이 `APlayerController*` 타입에 묶임 | `FPSRTargeting.h:36` |
| 적 팀 투사체는 `IsA(AFPSRCharacter)` 만 적대 | `FPSRProjectile.cpp:415-419` |
| 적 브랜치가 데미지 브릿지를 안 탐 | `FPSRProjectile.cpp:499-514` |
| 적 근접 축은 죽은 코드로 제거됨(ADR 0013 C0) | `FPSREnemyBase.h:26-28` |

### 3-D. 락다운 방지 — 배타 철회, 쿨다운 단일 손잡이 (사용자 결정 3회차)

**결정 경위** — 3회차 레드팀이 반증한 것을 사용자가 받아 뒤집었다:

1. rev3 까지의 안 = "무기당 상태이상 1개 배타". 그런데 **배타가 무기당이고 이 유닛은 라이플만 카드를 내므로,
   한 플레이어가 가질 수 있는 약한 상태 소스가 정확히 1개**가 된다 →
   - **솔로에서 강한 상태가 원리적으로 발동 불가**(재료 2개를 못 모은다). 그런데 §1 은 강한 2종을 산출물로
     올리고 PIE 는 "무기 2정으로 실명 발동"을 검증 항목으로 적었다 — **그 2정을 만들 카드가 이 유닛에 없다.**
     산출물과 검증이 성립하지 않는 상태였다.
   - **4인에서는 정반대** — 4명이 각각 다른 축을 고르면 약한 4비트가 상시 켜지고, 쿨다운 0 + 재료 소모이므로
     소모→즉시 재충전→재발동 루프가 된다. rev3 의 락다운 분석은 성립하지 않는 솔로 시나리오 위에 서 있었고,
     정작 기준선인 4인 협동(`CLAUDE.md` 핵심원칙 3 · [[reason-in-multiplayer-terms]])을 안 셌다.
2. **사용자 결정 = 배타를 무르고 솔로 조합을 허용한다.** 라이플 하나로 `MaxFragmentSlots`(3) 범위에서 약한
   상태를 여러 개 들 수 있고, 솔로도 강한 상태를 본다.

**따라서 락다운을 막는 손잡이는 `RetriggerCooldownSeconds` 하나뿐이다**(§5-1, 기본값 0).
- 기본값 0 = "무한 유지를 상정한다"는 사용자 결정(2회차)을 유지한다.
- ⚠️ **레드팀이 두 라운드 연속 지적한 사항을 기록으로 남긴다**: 배타까지 사라졌으므로 4인에서 실명(공격불가)
  +속박(이동불가)이 스웜 전체에 상시 걸리는 것이 **더 쉬워졌다.** 이건 밸런스 판정이고 코드가 아니라 값으로
  해결된다 — **§10 PIE 에 락다운 전용 판정 항목**을 넣어 플레이테스트에서 반드시 눈으로 확인하게 한다.

**상태 비트는 시전자를 구분하지 않는다** — 서로 다른 플레이어가 건 약한 상태가 같은 적에서 **합쳐져** 조합을
성립시킨다(4인 기준선의 정상 거동). 이 문장이 rev3 까지 어디에도 없어서 4인 분석이 통째로 빠져 있었다.

## 4. 제1원리 3줄

1. **제1원리 근거** — 동시 생존 240, 액터당 비용 최소화. ① 신규 컴포넌트 0 ② 액터 틱·타이머 0(기존 배치
   패스에 스텝 1개) ③ 복제는 **`uint8` 1개**만 는다.
2. **엔진 기본값과의 관계 = 덮는다.** 표준은 duration/periodic `GameplayEffect` 인데 ① 스웜·보스에 ASC 가
   없어(`Enemy.md:71-72`) 전 티어에 안 걸리고 ② 엔진이 월드 `FTimerManager` 로 돌려(`GameplayEffect.cpp:4409`·
   `:4431`) §2-2 프리즈를 뚫는다. 대체 = **상태 전용 시계 타임스탬프 + 배치 패스**(§6-1 — rev2 가 쓰려던
   전투시계는 파급이 커서 철회했다).
3. **프로젝트 정합** — `FFPSRDamageSpec` 은 *"sibling units (lightweight enemy status effects) can add fields
   without touching this signature again"*(`FPSRVitals.h:8-12`)로 이 유닛을 위해 열린 자리다. 저장소를 체력
   컴포넌트에 두면 스웜·엘리트·보스가 같은 컴포넌트를 공유한다 — **단 그 컴포넌트를 갖는 다른 3종에는
   드라이버가 없으므로 §5-6 의 대상 게이트가 함께 있어야 성립한다.**

## 5. 인터페이스

### 5-1. 상태 정의 = 무상태 공유 DataAsset

```cpp
UENUM() enum class EFPSRStatusKind    : uint8 { Weak, Strong };
UENUM() enum class EFPSRStatusRefresh : uint8 { RefreshDuration, Ignore };

UCLASS() class UFPSRStatusEffectDataAsset : public UPrimaryDataAsset
{
    UPROPERTY(EditDefaultsOnly) FGameplayTag StatusTag;               // Status.*
    UPROPERTY(EditDefaultsOnly) uint8 SlotIndex = 0;                  // 0..7
    UPROPERTY(EditDefaultsOnly) EFPSRStatusKind Kind = Weak;
    UPROPERTY(EditDefaultsOnly) float DurationSeconds = 5.f;
    UPROPERTY(EditDefaultsOnly) EFPSRStatusRefresh Refresh = RefreshDuration;

    // 효과 축 — 전부 기본값이 무효과. 소비자는 §6 에 1:1로 명시돼 있어야 한다(G1-16).
    UPROPERTY(EditDefaultsOnly) float MoveSpeedMultiplier      = 1.f;
    UPROPERTY(EditDefaultsOnly) float AttackIntervalMultiplier = 1.f;  // >1 = 느려짐
    UPROPERTY(EditDefaultsOnly) float IncomingDamageMultiplier = 1.f;  // 방어력감소 = >1
    UPROPERTY(EditDefaultsOnly) float DamagePerSecond          = 0.f;
    /** 도트 적용 주기. 0 이면 매 프레임 ApplyDamage 가 되어 §8 회계가 깨진다(ClampMin 으로 막는다). */
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="0.05")) float DotTickIntervalSeconds = 0.5f;
    UPROPERTY(EditDefaultsOnly) bool  bDisableAttack           = false;
    UPROPERTY(EditDefaultsOnly) bool  bDisableMovement         = false;

    // Strong 전용 (EditCondition: Kind==Strong)
    /** 🔴 이것은 **발동 재료쌍**이지 "합성"이 아니다(G1r3). 강한 상태가 발동하면 재료 2비트가 꺼지므로
     *  그 적은 더 이상 느려지지도, 도트를 받지도 않는다. 강한 상태가 약한 효과를 **계속 갖게** 하려면
     *  이 DA 의 효과 축(위)에 그 값을 직접 저작한다 — 즉 "실명이면서 여전히 느림"은 데이터로 만든다. */
    UPROPERTY(EditDefaultsOnly) TArray<uint8> RequiredWeakSlots;       // 정확히 2개
    UPROPERTY(EditDefaultsOnly) bool  bConsumeSources = true;          // 사용자 결정
    /** 이 대상에 다시 걸릴 수 있게 되기까지의 시간. **기본값 0 = 쿨다운 없음**(사용자 결정 2026-09-06 —
     *  무한 유지를 상정한다). 필드를 지금 여는 이유 = PIE 에서 락다운이 확인되면 코드가 아니라 이 숫자로
     *  해결하기 위해서다(G1-1·G1-12). */
    UPROPERTY(EditDefaultsOnly, meta=(ClampMin="0.0")) float RetriggerCooldownSeconds = 0.f;
};
```

### 5-2. 카탈로그

```cpp
UCLASS() class UFPSRStatusCatalogDataAsset : public UPrimaryDataAsset
{
    UPROPERTY(EditDefaultsOnly) TArray<TObjectPtr<UFPSRStatusEffectDataAsset>> Statuses;
    // IsDataValid: SlotIndex 중복 0 · 범위 0..7 · Strong 의 RequiredWeakSlots 는 정확히 2개이고 둘 다 Weak ·
    //   Strong 끼리 같은 재료쌍 금지 · Weak 은 RequiredWeakSlots 를 갖지 않음 ·
    //   DamagePerSecond>0 인 상태가 2개 이상이면 경고(§7-4 의 단일 DotSource 전제)
};
```
경로 = **config 소프트경로**(`FPSREnemyRenderSettings.h:70` 선례). C++ 에셋 경로 하드코딩 금지.

### 5-3. 런타임 상태 — 🔴 복제 필드는 **컴포넌트 직속**이어야 한다 (G1-11)

이 리포의 `MARK_PROPERTY_DIRTY_FROM_NAME`/`DOREPLIFETIME_WITH_PARAMS_FAST` 호출부는 전부 **클래스 직속
UPROPERTY** 를 받는다(`FPSREnemyHealthComponent.cpp:35-46, 54-62, 95-97`). 중첩 struct 멤버에는 못 건다.
`Enemy.md:126` ④("저장소는 합치지 않는다 — 복제 정책이 다르다")와도 분리 쪽이 맞다.

```cpp
// UFPSREnemyHealthComponent 직속
UPROPERTY(ReplicatedUsing = OnRep_StatusBits)
uint8 StatusBits = 0;                          // 복제되는 유일한 상태 필드

// 서버 전용(비-UPROPERTY 서버 상태 — TWeakObjectPtr 포함이라 POD 는 아니다, G2 정정)
struct FFPSRStatusServerState
{
    float SlotExpiry[8]        = {};           // 🔴 **상태 시계**(§6-1) 타임스탬프 — 전투시계가 아니다
    float SlotCooldownUntil[8] = {};           // 동 축. 재발동 쿨다운(기본 0 = 즉시 가능)
    float LastStatusStepClock  = 0.f;          // 직전 상태 스텝의 상태 시계 시각(§7-4 의 구간 시작점)
    float DotAccumulator       = 0.f;          // 다음 도트 적용까지 남은 초
    TWeakObjectPtr<AActor>              DotInstigator;   // 킬 크레딧(§7-4)
    TWeakObjectPtr<UFPSRWeaponInstance> DotSourceWeapon; // OnStatusKill 이 컨텍스트를 만들려면 필요(G1-15)
};
FFPSRStatusServerState StatusServer;
FFPSRResolvedStatus    ResolvedStatus;         // StatusBits 가 바뀔 때만 재계산
bool bStatusDriverPresent = false;             // §5-6 대상 게이트
```

### 5-4. 규칙 = 상태 없는 순수 함수 (`FPSRVitals` 선례)

```cpp
namespace FPSRStatus
{
    bool Apply(uint8& InOutBits, FFPSRStatusServerState&, const UFPSRStatusCatalogDataAsset&,
               uint8 Slot, float NowStatusClock, float WeakResist, float StrongResist,
               TArray<uint8, TInlineAllocator<8>>& OutFired);   // 슬롯 상한이 8이라 힙 0 (G2-I)
    /** 구간 = [State.LastStatusStepClock, NowStatusClock]. 함수가 끝나며 LastStatusStepClock 을 갱신한다.
     *  🔴 저항을 Apply 와 **똑같이** 받는다(구현 중 교정) — §7-2 가 Advance 에 독립적인 조합 재판정을 시키는데,
     *  Strong 은 직접 부여되는 일이 없고 오직 조합으로만 켜지므로, 저항을 모르는 Advance 는 §6 의
     *  "보스는 하드 CC 면역"을 뚫는 유일한 경로가 된다. */
    bool Advance(uint8& InOutBits, FFPSRStatusServerState&, const UFPSRStatusCatalogDataAsset&,
                 float NowStatusClock, float WeakResist, float StrongResist, float& OutDotDamage,
                 TArray<uint8, TInlineAllocator<8>>& OutExpired,
                 TArray<uint8, TInlineAllocator<8>>& OutFired);
    FFPSRResolvedStatus Resolve(uint8 Bits, const UFPSRStatusCatalogDataAsset&);
}
```

### 5-5. 부여 = 프래그먼트 + 배타 + **데미지 결과 뒤** (G1-6)

🔴 VIT1 이 *"부여 판정은 `FPSRVitals::FResult` 를 본다 — `HealthSpent > 0` 로 '실드에 막힌 타격은 상태이상도
막힌다'를 표현한다"*(`VIT1 §11-4 (3)`, `Enemy.md:126` ③)를 **"여기서 정하고 그쪽이 따른다"**로 못박았다.
`OnHitActor` 는 `ResolveDamage`/`ApplyDamage` **앞**에서 돌아 그 결과가 아직 없다 → **새 훅면 1개를 연다.**

```cpp
// UFPSRWeaponFragment 에 추가 (NotifyKill 과 대칭)
virtual void OnDamageApplied(const FFPSRFireContext&, AActor* Target, const FDamageResult&) const {}
// FPSRWeaponHooks::NotifyDamageApplied(...) 를 **4경로**가 ApplyDamage 직후 호출:
//   히트스캔 · 차지레이저 · 근접 · 투사체 직격. **폭발은 제외**(G2-D) — ApplyExplosion 에는 FFPSRFireContext
//   가 없고 FExplosionResult 는 KilledEnemies 만 담으며(FPSRCombatStatics.h:64-67), Combat 레이어는 무기
//   프래그먼트 헤더를 include 하지 않는다. 폭발 부여를 넣으려면 Combat→Weapon 역의존 신설이나 per-target
//   결과 배열 반환(스웜 240 × 로켓 = 프레임당 수백 엔트리)이 필요하다 → **이번 유닛 비목표**(§2).
// 🔴 이 훅은 FPSRWeaponHooks 의 "5경로 균일" 계약(FPSRWeaponFragment.h:126-129)에서 **의도적으로 이탈**한다.
//   구현 산출물에 그 헤더 주석의 예외 표기를 포함할 것 — 안 그러면 다음 유닛이 그 주석을 믿는다(G1r3).
//   체감 규칙: **상태 부여는 직격 피해에만 적용되고 스플래시에는 적용되지 않는다.** 카드 문구와 PIE 에 명시.

UCLASS() class UFPSRStatusApplyFragment : public UFPSRWeaponFragment
{
    UPROPERTY(EditDefaultsOnly) TObjectPtr<UFPSRStatusEffectDataAsset> Status;
    UPROPERTY(EditDefaultsOnly) float ApplyChance = 1.f;
    UPROPERTY(EditDefaultsOnly) bool  bRequireHealthDamage = true;  // 실드에 막힌 타격은 부여 안 함
    virtual void OnDamageApplied(...) const override;
    // IsDataValid: MaxStacks 는 1 (스택이 늘어도 같은 슬롯이라 지속시간 갱신뿐 — 저작 혼동 방지)
};
```
🔴 **배타 축은 없다**(§2·§3-D, 사용자 결정 3회차). 라이플이 `MaxFragmentSlots`(3) 안에서 약한 상태를 여러 개
들 수 있고, 그것이 솔로 조합의 유일한 경로다. 공용 헤더·추첨부·`CanApply` 무접촉.

🔴 **`FDamageResult` 를 확장해야 한다**(G1r3). §5-5 가 인용한 VIT1 규칙(*"부여 판정은 `HealthSpent > 0` 을
본다"*)을 현재 `FPSRCombat::FDamageResult` 로는 **표현할 수 없다** — 필드가
`bApplied / bKilled / bWasEnemy / DamageDealt / bShieldBroke / bTargetIsPlayer` 뿐이고, `DamageDealt` 는 VIT1 이
**의도적으로** `ShieldSpent + HealthSpent` 로 재정의한 값이다(`FPSRCombatStatics.cpp:229-233`). 실드가 전부
흡수한 타격도 `DamageDealt > 0` 이라 `bRequireHealthDamage` 가 **조용히 무효**가 된다.
→ `FDamageResult` 에 `float ShieldSpent` · `float HealthSpent` 를 추가하고 4경로에서 채운다.
**이 유닛의 훅면 비용은 1개가 아니라 "훅면 1 + 결과 struct 2필드"다.**

### 5-6. 대상 게이트 — 드라이버 없는 액터에 걸리지 않는다 (G1-3)

부여 훅은 임의의 `HitActor` 로 불린다(위 4경로). 문·미션 도주 타겟은 같은 체력
컴포넌트를 갖지만 `Advance` 를 부를 주체가 없어 **비트가 켜진 채 영원히 안 꺼진다**.

→ `UFPSREnemyHealthComponent::bStatusDriverPresent`. `ApplyStatus` 는 false 면 **조용히 거부**한다.

🔴 **플래그를 세우는 곳은 액터 초기화가 아니라 드라이버 등록/해제 시점이다**(G2-G). 스웜의 드라이버는
액터가 아니라 **`ActiveEnemies` 멤버십**이고, 살아 있는 채로 그 집합 밖인 구간이 있다 — `BeginDying` 이
즉시 제거하는 death-dwell(`FPSREnemySpawnSubsystem.cpp:298-302`)과 풀 대기. 초기화에서 켜면 그 구간에도
켜져 있어 계약이 거짓이 된다.

| 대상 | 켜는 곳 | 끄는 곳 |
|---|---|---|
| 스웜·엘리트 | `ActiveEnemies` 추가 | `ActiveEnemies` 제거(`BeginDying` 포함) |
| 보스 | `SetActorTickEnabled(true)` 와 같은 자리 | **두 곳 모두** — 격파(`FPSRBossBase.cpp:500`)와 런 종료(`:604`) |
| `AFPSRBossHomingOrb` | **비대상** — 수명이 짧고 상태이상을 걸 이유가 없다. 플래그를 안 켜므로 조용히 거부된다 |

계약: **서버에서, 플래그가 켜져 있는 동안 매 프레임 `AdvanceStatus` 가 보장된다.**
(보스의 `SetActorTickEnabled(true)` 자리는 **모든 머신에서** 실행되므로 `HasAuthority()` 조건이 필요하다 — G1r3.) §3-B 정정 ③ 표·§9 의 "드라이버 2개"
서술은 이 표를 정본으로 한다(rev2 는 세 절이 서로 다르게 말했다).

## 6. 배선 지점

| 축 | 지점 | 방식 |
|---|---|---|
| 둔화 | `AFPSREnemyBase::GetEffectiveMoveSpeed()` | `× MoveMult` (사용 시점 곱) |
| **속박** | 동 + 넉백 임펄스 | `bMoveDisabled` → 속도 0 **그리고 넉백 억제**(`FPSREnemyBase.cpp:1388-1391` 은 속도를 안 읽고 액터를 직접 민다 — 안 막으면 속박 걸린 적이 폭발에 날아간다, G1-16) |
| 공격속도저하 | `ServerTickAttack` 의 **임계 비교 시점** | `× AttackIntervalMult`. 🔴 배율이 바뀌면 `AttackAnimHoldUntil`·애니 rate 를 **함께 재계산**한다(`FPSREnemyBase.cpp:1011-1018` — 안 하면 차징 중 Attack 애니가 walk/idle 에 덮인다, G1-9) |
| **실명** | 동 | **실명이 켜지는 모든 경로의 onset 에서 `ReleaseRangedHold()` + `ResetRangedCycle()`** — 부여(`Apply`)와
**조합 발동(`Advance`) 둘 다**(G2 잔여) — 를 부르고 그 다음 패스부터 early-out. 이 쌍은 `Deactivate()` 가
쓰는 검증된 조합이다(`FPSREnemyBase.cpp:701-702`). 🔴 진입부에서만 return 하면 토큰(`RangedAttackTokenLimit=3`)과 클라 방향경고가 실명 내내 붙잡혀 그 플레이어를 향한 스웜 사격이 통째로 멈춘다(G1-8) |
| 방어력감소 | `UFPSREnemyHealthComponent::ApplyDamage`(`.cpp:81-89`) | `FMitigation` 합성 시 per-instance 층을 곱한다. `DirectionalArmorDR`(열려만 있고 항상 0) 선례. VIT1 불변식 V1 은 안 깨진다(증폭은 `MinKeep` 하한을 통과) |
| 도트 | 배치 패스 → **`FPSRCombat::ApplyDamage`**(브릿지 경유 — 흡혈·`bWasEnemy`·미션/디렉터 축이 살아야 한다). 🔴 단 `Spec.bSuppressDealtDamageEvent = true` — 안 하면 **매 도트 틱마다 `SendDealtDamageEvent` 가 시전자 ASC 로 `GameplayEvent.Player.DealtDamage` 를 쏘고**, 그건 흡혈 패시브의 어빌리티 트리거다(`FPSRPassiveAbility.cpp:56-62`). 240마리 × 0.5s 주기 = **초당 480회 `TryActivateAbility`** 가 한 플레이어 ASC 에 몰린다(G1r3) | `Spec.DamageType` = **빈 태그(무속성)**. 🔴 **근거 정정(G2-J)**: rev2 는 "검증기가 `DamageType.Status` 를 막는다"고 적었으나 **틀렸다** — 그 검증기(`FPSRVitalsProfile.cpp:81-88`)가 막는 것은 `DamageType.` 으로 **시작하지 않는** 태그뿐이라 `DamageType.Status` 는 통과한다. 실제 이유는 ①그 태그가 `DefaultGameplayTags.ini` 에 미선언(한 줄로 해소 가능) ②**속성 트리거를 살리면 기제가 둘이 된다**는 설계 결정(§2)이다. **그리고 `Spec.bSuppressRegenDelayOnly = true`**(§6-2) |
| 진행·조합 | 배치 패스 **`bFrozen` 판정 직후(`:306`)·`PlayerPawns` 수집 앞**, `!bFrozen` 게이트 안 | 전원 DBNO 에 안 걸리되 프리즈·전환에는 안 돌게(G1-2 · G1r3). 🔴 **`ActiveEnemies` 전수를 돌지 않는다** — 서브시스템에 **"상태 보유 적" 압축 리스트**를 두어 비용을 **O(감염된 적)** 으로 만든다(첫 부여에서 등록, 전 비트 소거·드라이버 해제에서 제거). 전수 루프는 제1원리(액터당 비용 최소화)에 어긋나고, `Agents`/`Locations` 스크래치가 만들어지기 전이라 기존 루프에 얹히지도 않는다. Out 배열은 서브시스템 멤버 스크래치(`:409-411` 선례) |
| 보스 | `AFPSRBossBase::Tick` 에서 같은 `AdvanceStatus` | 프리즈 조건식이 배치 패스와 동일(`FPSRBossBase.cpp:608`)이라 대칭 성립. 런 종료 시 자기 틱을 끄므로(`:601-606`) 상태는 영구 동결 — 코스메틱이라 무해, 명시만 한다(G1-17) |
| 저항 | `UFPSRVitalsProfileDataAsset` 에 **`WeakResistScale` · `StrongResistScale`** 2개 | 단일 스케일로는 "보스는 하드 CC 면역, 도트는 받음"을 표현 못 한다. 🔴 **둘 다 기본값 1.0 이고 프로파일이 null 이면 1.0/1.0 폴백**(`ResolveDefense` 와 같은 규칙, `FPSREnemyHealthComponent.cpp:81-87`) — 0 으로 잡으면 **오늘 프로파일이 저작돼 있지 않은 스웜 전체가 상태이상 완전면역**이 되고 PIE 가 전부 "아무 일도 안 일어남"으로 나온다(G1r3) |
| 킬 시임 | `FPSRWeaponHooks::NotifyStatusKill` | `DotSourceWeapon` 이 있어야 `FFPSRFireContext.Instance` 를 채울 수 있다 — 없으면 훅이 빈 목록에 대고 도는 no-op(G1-15) |

### 6-1. 🔴 시간축 — **상태 전용 시계를 신설한다. 전투시계는 건드리지 않는다** (G1-1 · G2-A/B/C)

> **rev2 의 처방을 철회한다.** rev2 는 전투시계를 스테이지 전환에서도 멈추게 확장하겠다고 했는데,
> 그 처방이 **두 가지 독립적인 이유로 틀렸다**:
>
> **① 시계가 역행한다.** 프리즈와 전환은 배타가 아니라 **중첩**한다 — 세 경로가 동시 활성을 설계로 인정한다
> (`FPSRStageDirectorSubsystem.cpp:387-394` `Pending` · `:453-462` `bSwapDeferredByFreeze` · `:418-437`
> `EndRunFreeze`). 그런데 앵커(`FreezeStartedAtWorldTime`)와 누산기(`AccumulatedFrozenSeconds`)는 **각각
> 하나뿐**이다(`FPSRGameState.h:452-462`). 전환 중 프리즈가 들어오면 앵커가 덮여 겹친 구간이 이중 차감되고
> **`GetCombatClockSeconds()` 가 뒤로 간다** → `Now - LastDamageCombatTime` 음수 = 실드 재생 영구 정지
> (`FPSRCharacter.cpp:257-262`, `FPSREnemyHealthComponent.cpp:133-136`) · 크릿 버프 영구 미만료
> (`FPSRWeaponInstance.cpp:234-242`) · 힐팩 영구 미리스폰(`FPSRHealthPickup.cpp:84`).
>
> **② "VIT1 잠복 버그 동반 해소"라는 전제 자체가 틀렸다.** 코드는 정반대를 의도적으로 정해 놨다 —
> 전환은 **이동만** 얼리고 사격·ADS·재장전은 `IsRunFrozen()` 만 읽어 살아 있다
> (`FPSRCharacter.cpp:739-745`, `.h:66-70`). 배치 패스 주석은 그 창을 **보상**으로 규정한다:
> *"the player grinds down enemies that cannot move or fight back"*(`FPSREnemySpawnSubsystem.cpp:389-393`).
> VIT1 이 전투시계를 만든 근거는 *"프리즈는 사격을 막으므로 공짜 재생을 주면 안 된다"*
> (`FPSRGameState.h:195-201`)인데 **전환은 사격을 막지 않으므로 그 근거가 전환에 성립하지 않는다.**
> 즉 버그 수정이 아니라 **설계 변경**이고(슬라이드 크릿 버프를 단 채 `StageGraceSeconds=8.0` 을 공짜로
> 얻는다), 사용자 결정으로 올라간 적이 없다.

**처방** — `AFPSRGameState` 에 **상태 전용 시계**를 하나 더 둔다.

```cpp
/** 상태이상 만료 전용 시계. 상태 진행 패스가 멈추는 축과 정확히 같은 축에서 멈춘다
 *  (= IsRunPaused() || IsStageTransitionActive()). 전투시계와 분리하는 이유 = §6-1 —
 *  전환은 사격을 막지 않으므로 전투시계를 전환에서 멈추면 크릿 버프·힐팩·양쪽 실드 재생이
 *  함께 바뀐다(설계 변경). 이 시계는 소비자가 상태이상 하나뿐이라 파급이 0이다. */
float GetStatusClockSeconds() const;   // 서버 전용
```

- 🔴 **중첩 안전 = 합성 불린 1개의 엣지 감지**(refcount 아님, G1r3). 두 세터(`SetRunPaused`·
  `SetStageTransition`) **끝에서** `bStatusFrozen = IsRunPaused() || IsStageTransitionActive()` 를 재계산해
  `false→true` 에서만 앵커를 찍고 `true→false` 에서만 누산한다.
  ⚠️ **refcount 는 반드시 샌다** — `SetStageTransition` 은 6값 phase 세터(`None/Pending/Grace/Swapping/
  FadeOut/FadeIn`)이고 호출부가 13곳이며, 전환 1회가 비-None 을 **4~5회** 지난다
  (`Grace → Pending → FadeOut → Swapping → FadeIn → None`). 게다가 `FPSRStageDirectorSubsystem.cpp:504` 는
  **이미 Grace 인 상태에서 Grace 를 다시** 세운다. "비-None ++ / None --" 로 짜면 첫 전환에 잔여 3~4가 남아
  **상태 시계가 영구 정지 → 실명·속박이 스웜 전체에 영구 고착**한다. 소스가 2개뿐이라 refcount 는 더 안전하지
  않고 이 누수만 새로 만든다.
- 🔴 **런 종료·재시작 계약**: `EndRunFreeze` 는 **해제되지 않는 영구 동결**이다(`FPSRGameState.cpp:293-302`).
  같은 월드에서 런을 다시 시작하면 `bStatusFrozen` 이 true 로 남아 시계가 영원히 멈춘다.
  → `ResetStatusClockForNewRun()` 을 런 시작 경로에 둔다. (현행 코드는 *"resets naturally on the next run
  (fresh GameState)"* 로 레벨 리로드에 기대는데, 신설 시계가 그 전제를 그대로 상속하면 안 된다.)
- **클라 식은 필요 없다** — 만료 판정은 전부 서버 권위이고 클라는 `StatusBits` 만 본다. rev2 가 전투시계를
  건드리려다 만들 뻔한 클라/서버 괴리(G2-B)가 여기서는 성립하지 않는다.
- **적 공격 타이밍은 world time 축**이다(`Ctx.Now = World->GetTimeSeconds()`, `FPSREnemySpawnSubsystem.cpp:293`).
  §6 의 공격속도저하는 그 축 위의 **임계값만** 곱하므로 시계 축을 섞지 않는다(범위 밖 발견 반영).

### 6-2. 🔴 도트가 실드 재생을 봉인하지 않게 — **앵커 2개를 구분한다** (G1-14 · G2-E)

`ApplyDamage` 가 매 호출 찍는 것은 **둘**이다 — `ShieldAtLastDamage = Shield`(`.cpp:100`) 와
`LastDamageCombatTime`(`:101`). rev2 는 "재생 앵커를 갱신하지 않는다"고만 적어 **시간만 얼리는** 것으로
읽혔는데, 그러면 실드 앵커가 옛 높은 값에 남아 `ComputeRegeneratedShield` 가 **도트가 깎은 실드를 되돌려
준다**(`.cpp:133-141` 단조증가 가드를 통과한다). 실드 있는 적에게 도트가 사실상 무효가 된다.

🔴 **rev3 의 처방("시간 앵커만 유지")도 틀렸다 — 그건 실드를 만충으로 폭증시킨다**(G1r3).
`ComputeRegeneratedShield` 는 증분이 아니라 **절대식**이다:
`Clamp(ShieldAtLastDamage + RegenPerSecond × (Elapsed − Delay))`(`FPSRVitals.cpp:48-68`).
두 앵커는 **같이 움직여야만** 성립한다. 시간만 유지하면 `Now − LastDamageCombatTime` 이 계속 커지는데 값
앵커는 이미 앞선 회복분을 포함한 값으로 갱신돼 **회복이 복리로 누적**된다 — 검산(MaxShield 100, Regen 10/s,
Delay 3s, 도트 5/0.5s, t=0 실드 50): t=3.5 → 25 · t=4.5 → 40 · t=5.5 → 75 · **t=6.0 → 100(만충)**.
게다가 `Delay` 는 함수 안에서 `ShieldAtLastDamage <= 0 ? BrokenDelay : PartialDelay` 로 **값 앵커에서
파생**되므로 도트가 실드를 0으로 만들면 지연이 바뀐다.

→ **두 앵커를 함께 옮기되 시간 앵커를 역날짜로 찍는다**:
```cpp
// bDotRegenAnchorPolicy = true 일 때 (FFPSRDamageSpec 신규 플래그)
ShieldAtLastDamage   = Shield;
LastDamageCombatTime = Now - (Shield <= 0.f ? ShieldBrokenRegenDelaySeconds : ShieldRegenDelaySeconds);
```
= "**새 지연을 걸지 않되 회복은 지금부터 증분으로**". 어느 지연을 빼는지는 값 앵커가 결정한다(위 파생과 동일).
§10 단위테스트에 "실드 보유 적에 도트 → **지연을 넘긴 뒤까지 굴려도** 실드가 순감소" 케이스를 넣는다
(지연 안에서 끝나는 테스트는 이 버그를 통과시킨다).
## 7. 함수별 계약

1. **`Apply`** — 재적용은 지속시간 갱신(기본값). 스택 없음. `SlotCooldownUntil` 이 미래면 거부.
   **재적용 시 시전자는 갱신한다**(마지막 시전자 승계) — 4인에서 A가 걸고 B가 재적용하면 이후 도트 킬 크레딧은
   B 것이다. 최초 시전자 고정보다 단순하고, "마지막으로 손댄 사람"이 협동에서 덜 이상하다(G1r3).
   저항은 `Kind` 에 따라 `WeakResistScale`/`StrongResistScale` 을 지속시간에 곱하고, 0 이면 부여 거부.
   🔴 **조합 판정을 `Apply` 안에서 즉시 한다**(G1-P2-1 · Codex) — 조합은 엣지 이벤트라 배치 패스에만 두면
   S3 에서 최대 8프레임(≈130ms) 늦고, 그 사이 만료된 재료 쌍을 놓친다.
2. **`Advance`** — 만료를 먼저, 그 다음 조합(부여 없이 성립하는 경우 대비). 만료는 타임스탬프 비교이므로
   `DeltaSeconds` 를 쓰지 않는다. 🔴 **`DeltaSeconds × Stride` 보정은 "완전히 활성인 연속 효과"에만
   유효하다** — 만료·조합에는 아무것도 사 주지 않는다(rev1 의 근거 오류, G1-P2-1).
3. **조합 = 재료 소모**(사용자 결정). 강한 상태 발동 시 `RequiredWeakSlots` 2개의 비트를 끈다.
   다중 성립(둔화+도트+방어력감소)은 **카탈로그 배열 순서대로 판정하고 재료가 소모된 조합은 미성립** →
   앞선 것 하나만. 순서 = 데이터.
4. **DoT** — 구간은 **`[State.LastStatusStepClock, NowStatusClock]`** 이고 축은 **상태 시계**다(§6-1).
   🔴 world time 으로 재면 `StageGraceSeconds=8.0` 전환 직후 첫 스텝에서 **8초치 도트가 한 번에** 들어가
   스웜이 통째로 즉사한다(G1r3). 적용량 = 그 구간과 **`[부여시각, 만료시각]` 의 교집합**만 누산한다(활성 구간
   클램프). `DotAccumulator` 는 **다음 적용까지 남은 초**이고, `DotTickIntervalSeconds`(데이터, 기본 0.5s,
   `ClampMin 0.05`)마다 `ApplyDamage` 를 부른다 — 매 프레임 호출은 §8 회계를 깨뜨린다(호출마다
   `CatchUpShieldRegen` + `OnHealthChanged` + dirty 2건, `FPSREnemyHealthComponent.cpp:73, 95-105`).
   `LastStatusStepClock` 은 **§7-6 폐쇄 지점에서 반드시 리셋**한다 — 안 하면 풀 재사용 적이 전생의 스텝 시각을
   물고 첫 프레임에 도트 폭탄을 맞는다.
   킬 크레딧은 `DotInstigator`(약참조); 풀리면 **DoT 는 계속 굴리되 Instigator=null** 로 넘긴다.
5. **`Resolve` 합성** — 배율은 곱, 불리언은 OR. 캐시 무효화는 `StatusBits` 변화 시에만
   (재적용은 `SlotExpiry` 만 바꾸므로 무효화 안 함 — 레드팀이 비용 안전을 확인).
6. **🔴 수명주기 폐쇄 — 실제 지점은 넷이고 rev3 이 첫 번째를 빠뜨렸다**(G1r3).
   `ResetForReuse` 는 `AFPSREnemyBase::Activate` **안에서만** 불리므로(`FPSREnemyBase.cpp:483`, 리포 유일
   호출부) rev3 의 목록은 독립 지점이 셋뿐이었다. `Enemy.md:55-57` 의 4중은 **`EnterDyingState`** ·
   `Deactivate` · `Activate` · `ServerResetEliteForStageCarry` 다.
   → **`EnterDyingState` 를 폐쇄 지점에 추가**한다. 안 넣으면 시체가 `GetDeathDwellSeconds()` 동안
   `StatusBits` 를 켠 채 복제돼 **원격 3인에게 시체가 상태 아이콘을 달고 서 있다**(누수는 아니지만 보인다).
   각 지점에서 `StatusBits=0` · `SlotExpiry`/`SlotCooldownUntil` 클리어 · **`LastStatusStepClock=0`** ·
   `DotAccumulator=0` · 약참조 2개 null · `ResolvedStatus` 리셋 · **Push Model dirty 마킹**.
7. **공개 API 로 제한** (G1-P3-2) — 외부는 `ApplyStatus / AdvanceStatus / GetResolvedStatus /
   ClearStatusForReuse / HasStatus` 만 쓴다. 상태 필드 직접 접근 금지.

## 8. 복제표 (§6-3 서버권위 + Push Model)

| 프로퍼티 | 소유 | 복제 | 비고 |
|---|---|---|---|
| `uint8 StatusBits` | `UFPSREnemyHealthComponent` **직속** | Push Model, 변할 때만 | **6번째** 복제 프로퍼티 |
| `FFPSRStatusServerState` | 동 | **복제 0** | 클라는 "무엇이 걸렸나"만 필요 |
| 코스메틱 | GMS 로컬 pub/sub | 복제 0 | 이 유닛이 GMS 첫 실사용 프로듀서 |

🔴 **클라 반쪽이 없으면 호스트만 본다** (G1-5). GMS 는 로컬 버스이고 부여·만료는 서버에서만 일어난다.
VIT1 이 정확히 이 함정을 G2 에서 맞았다(`FPSREnemyHealthComponent.cpp:108-118` 주석).
→ **`OnRep_StatusBits` 에서 클라 로컬 엣지를 계산해 GMS 로 브로드캐스트**한다(`OnRep_Shield` 관용구 그대로).
`Roadmap.md:197` 이 M1 필수라고 못박은 "보이지 않으면 판정할 수 없다"가 **4인 중 3인**에 대해 성립해야 한다.

🔴 **그러나 OnRep 만으로는 부족하다 — 거리 LOD 가 가시성을 먹는다**(G2-F). NetFreq 는 S0 30 / S1 10 / S2 5 /
**S3 2 Hz**(`FPSREnemySpawnSubsystem.cpp:559-562`)이고, Push Model 은 "바뀐 사실"이 아니라 **현재 값**을 보낸다
— 두 복제 사이에 켜졌다 꺼진 비트는 `OnRep_StatusBits` 를 **한 번도 못 띄운다**. 실명 onset 에서 재료 2비트가
꺼지는 엣지, 짧은 도트, S3 에서 0.5초 미만 전이가 원격 3인에게 안 보인다.
→ **PIE 9 를 "S2/S3 거리의 적에서도 보이는가"로 판정**하고, 실패하면 **상태 지속시간 하한** 또는
**"상태 보유 시 NetFreq 하한"** 을 데이터 손잡이로 연다(코드가 아니라 값으로 해결).

🔴 **비용 회계 정정** (G1-10) — `Performance.md:130` 은 *"Push Model 전제는 출시 빌드에선 성립하지 않는다…
호스트 프레임 예산을 계산할 때 이 차이를 빼고 세지 말 것"*이라 했는데 rev1 이 뺐다.
- 패키지 빌드: 프로퍼티 5→6 = 적당 매 rep 프레임 비교 **+20%**. 그 비용이 걸리는 곳은 `Performance.md:118`
  이 이미 "relevant ≈ alive 전량, 예산 ~150 초과"라 적어 둔 자리다.
- **더 큰 항목**: 도트가 `Health` 를 **이벤트성 → 연속 변화 프로퍼티로 바꾼다**. `ApplyDamage` 는 매 호출
  `Health`/`Shield` 를 dirty 로 찍으므로(`.cpp:95-97`), 도트 걸린 N마리가 각자의 NetFreq(S0 30Hz~S3 2Hz)로
  `Health` 를 계속 흘린다. **§10-PIE 9 는 "도트를 다수에 건 상태"로 측정한다.**

`Performance.md:117` 은 아직 "3프로퍼티"인데 실제는 5다(VIT1 미반영) → 이 유닛에서 **6으로 정정**한다.

## 9. 데이터드리븐 경계

- **코드**: 비트 저장 · 시간축(**상태 전용 시계** + 4중 폐쇄) · 배치 진행 · 조합 알고리즘 · 해석 캐시 · §6 배선 지점 ·
  효과 축 6종 · **진행 드라이버가 2개(배치 패스·보스 틱)라는 사실**.
- **데이터**: 이름 · 태그 · 슬롯 번호 · 지속시간 · 전 배율 · 조합 재료쌍 · 소모 여부 · **재발동 쿨다운** ·
  **`DotTickIntervalSeconds`** · **`ApplyChance`** · **`bRequireHealthDamage`** · 판정 순서 · 저항 2종 ·
  카드 레어도 티어 · (§8 이 여는 손잡이) 상태 지속시간 하한 · 상태 보유 시 NetFreq 하한.
  ~~배타 그룹~~ — 철회(§3-D).

## 10. 검증 기준

**순수 함수 자동화 (`FPSRoguelite.Status.Unit`)**
1. 재적용 = 지속시간 갱신, 스택 없음 · 2. 조합 성립 → 강한 ON + 재료 2비트 OFF · 3. 재료 하나로는 미성립 ·
4. 다중 성립 시 앞선 것 하나만 · 5. 저항 0=거부 / 0.5=절반, Weak·Strong 독립 · 6. `Resolve` 축별 곱·OR ·
7. 쿨다운 미래면 부여 거부 · 8. **강한 상태 재발동이 기존 강한 비트의 만료를 갱신** ·
9. **DoT 가 활성 구간으로 클램프** — `[LastStatusStepClock, Now]` ∩ `[부여, 만료]` 만 적용 ·
10. 카탈로그 `IsDataValid` 음성 검사 · 11. **프로파일 null 이면 저항 1.0/1.0**(완전면역 사고 방지) ·
12. **실드 보유 적에 도트 → 재생 지연을 넘긴 뒤까지 굴려도 실드가 순감소**(§6-2 복리 회복 버그를 잡는
    유일한 테스트 — 지연 안에서 끝나면 통과해 버린다) ·
13. **`bRequireHealthDamage`** — 실드가 전부 흡수한 타격은 부여 안 함(`HealthSpent==0`) ·
14. 강한 상태 `Resolve` 가 §1 의 의미와 일치 — **재료를 소모하므로 약한 효과는 사라진다**(§5-1 재료쌍 주석)

**월드 자동화 (신규 — 순수 함수로는 위험의 대부분을 못 잡는다, G1-P3-3)**
11. `ResetForReuse` 후 `StatusBits == 0`(풀 재사용 누수)
12. **상태 시계 단조증가** — 겹침을 **명시적으로 만드는** 케이스여야 한다:
    ① 전환 진행 중 카드 프리즈 진입/해제를 겹쳐서 끼움 ② `Grace → Pending → FadeOut → Swapping → FadeIn
    → None` 전 phase 순회 ③ `EndRunFreeze` 중간 진입 ④ 실패한 `RequestTransition`(카운트 변화 0).
    rev2 는 ①에서, rev3 은 ②에서 터졌다
12-b. **전환을 끼운 만료** — 전환 8초 뒤 남은 시간 불변 · 그 사이 **전투시계는 정상 진행**(크릿 버프·힐팩이
    영향을 안 받는지 = §6-1 이 전투시계를 안 건드린다는 것의 확인)
13. **드라이버 없는 액터(문)에 부여가 거부**되는가(§5-6)
14. 전원 DBNO 구간을 끼운 만료 — 상태 진행이 멈추지 않는가(§6 삽입 위치)

**기존 회귀** — `Enemy.*`(5) · **`Combat.Vitals`**(§6-2 **앵커 변경** 파급 — rev3 의 "시계 확장 파급"은
철회된 사유다) · `Boss.*` · `Editor.CardCsv.*` · `Smoke.ModuleLoads`.
`Card.Synergy` 는 **더 이상 관련 없다** — 배타 철회로 추첨부를 안 건드린다.

**PIE 사용자 스모크**
1. 둔화 — 적이 눈에 띄게 느려지는가 · 2. 도트 — 사격을 멈춰도 깎이는가 · **레벨업 프리즈 중 멈추는가** ·
**스테이지 전환 뒤에도 남은 시간이 유지되는가** · 3. 방어력감소 — 데미지가 커지는가 ·
4. 공격속도저하 — 발사 간격이 벌어지는가, **차징 애니가 안 깨지는가** ·
5. 무기당 배타 — **이미 하나를 든 뒤** 다른 상태 카드가 그 무기 오퍼에 안 뜨는가(⚠️ 아무것도 안 든
상태에서는 배타 카드 2장이 한 오퍼에 함께 뜰 수 있다 — 픽이 1회라 무해, G2-(3)) ·
6. 무기 2정으로 **실명 발동** — 공격이 멈추는가, 재료 2개가 사라지는가, **다른 적의 사격이 정상인가**(토큰) ·
7. 도트 킬이 XP 를 주는가 · 8. 보스에 하드 CC 가 안 걸리고 **도트는 걸리는가** ·
9. **원격 클라(호스트 아님)에서 상태가 보이는가** — 2인 PIE, **S2/S3 거리의 적 포함**(G2-F) ·
10. **도트를 다수에 건 상태**로 적 200+ 프레임 예산 유지 ·
11. 🔴 **락다운 판정** — 4인에서 약한 4종을 나눠 들고 교전했을 때 스웜이 실명·속박으로 **상시 고착되는가.**
    고착되면 `RetriggerCooldownSeconds` 를 0 에서 올린다(§3-D — 이 값이 유일한 손잡이다) ·
12. **솔로에서도 강한 상태가 발동하는가**(배타 철회의 목적. 라이플 하나에 약한 2종을 얹어 확인)

## 11. 미결정

1. **다중 조합 동시 성립 시 "앞선 것 하나만"이 맞는가** — §7-3. 기본값 진행, PIE 사용자 판정.
2. ⚠️ **적 머티리얼이 CPD 를 아직 아무것도 안 읽는다**(`FPSRAnimCPDParams.h:22`). 가시성 1차 = **적 헬스바
   위젯 아이콘 + GMS 오디오 큐**로 확정하고, CPD 틴트는 M2 로 이월한다(머티리얼 작업 = 콘텐츠).
3. **미사용 조합 3쌍**(둔화+방어력감소 / 둔화+공속저하 / 도트+공속저하) — 의도인지 확장 슬롯인지 미확인.
4. **방어력감소 증폭에 상한이 없다**(`MaxTotalReduction` 은 감산만 막는다) — 프로파일에 증폭 상한을 둘지.

## 12. 레드팀 지적 원장

### G1 1회차 (2026-09-06) — **반려** · 구현시 P1 등가 8 · P2 4 · P3 4 + 범위 밖 3

> ⚠️ **원래 게이트(Fable)를 태우지 못했다** — 사용량 한도(`claude-fable-5-1`, HTTP 429). `Workflow.md:161`
> 폴백에 따라 **대체 검증**으로 채웠고 그 사실을 여기 남긴다. 대체 = ① **Opus 레드팀 서브에이전트**(독립
> 인스턴스, 설계 변호 미제공) 15건 ② **Codex 적대 리뷰**(비-Claude 교차검증) 9건. Fable 호출은 G2 로 이월.
> Codex 1차는 컨텍스트 소진으로 무산 — 열어도 되는 파일을 줄 범위까지 못박은 2차만 유효.

| # | 지적 | 처리 |
|---|---|---|
| G1-1 | 전투시계와 배치 패스의 프리즈 조건 비대칭 — 스테이지 전환에서 상태 일괄 만료 | **수용** §6-1 (VIT1 잠복 버그도 동반 해소) |
| G1-2 | early-return 이 셋이고 그중 `PlayerPawns==0`(전원 DBNO)은 프리즈가 아니다 | **수용** §3-B 표 · §6 삽입 위치 |
| G1-3 | 체력 컴포넌트 소유 액터 5종 중 드라이버 2종 — 문·미션 타겟에 상태가 영구 고착 | **수용** §5-6 대상 게이트 |
| G1-4 | 수명주기 폐쇄 0개 — 풀 재사용·이월 엘리트 누수 | **수용** §7-6 4중 폐쇄 |
| G1-5 | 원격 클라가 상태를 못 본다(GMS 는 로컬) — M1 Exit Criteria 직접 위반 | **수용** §8 `OnRep_StatusBits` |
| G1-6 | VIT1 이 못박은 `FResult` 기반 부여 판정을 구현 불가능한 자리에 배선 | **수용** §5-5 `OnDamageApplied` 신규 훅면 |
| G1-7 | `DamageType.Status` 는 검증기가 막아 저작 불가 + 트리거 이원화 미결론 | **수용** §6 무속성 · §2 비목표에 명시 |
| G1-8 | 실명 early-out 이 원거리 홀드를 안 닫아 토큰·클라 경고가 붙잡힘 | **수용** §6 onset 해제 |
| G1-9 | 공격간격 배율이 코스메틱 타이밍과 갈림 + rev1 의 "CDO 공유" 근거가 틀림 | **수용** §3-B 정정 ② · §6 |
| G1-10 | Push Model 패키지 빌드 차이를 빼고 셈 + 도트가 `Health` 를 연속 복제로 바꾸는 비용 누락 | **수용** §8 |
| G1-11 | `MARK_PROPERTY_DIRTY_FROM_NAME` 은 중첩 struct 멤버 불가 | **수용** §5-3 직속 승격 |
| G1-12 | `ExclusionGroup` 이 락다운을 못 막고 `MaxStacks` 와 충돌 | **수용(사용자 결정)** §3-D · §5-5 `MaxStacks==1` |
| G1-13 | 배치 패스 삽입 위치 미정 + Out 배열 프레임당 480회 할당 | **수용** §6 멤버 스크래치 |
| G1-14 | 도트가 실드 재생을 무기한 봉인 | **수용** §6-2 |
| G1-15 | `NotifyStatusKill` 이 `FFPSRFireContext` 를 못 만들어 영구 no-op | **수용** §5-3 `DotSourceWeapon` |
| G1-16 | 효과 축 중복 + 넉백이 속박을 무시 | **수용** §6 넉백 억제 · §7-5 합성 규칙 |
| G1-17 | 보스 드라이버 2차(런 종료 동결·틱 순서) | **수용** §6 보스 행 |
| G1-18 | 자동화 8항목이 위험 대부분을 구조적으로 못 잡음 | **수용** §10 월드 테스트 4개 신설 |
| G1-19 | 로드맵은 2~3축인데 6축 | **사용자 결정으로 6축 유지** §1 |
| Codex-2 | 오퍼 필터만으로는 `ApplyCard` 공통 경로를 못 막음 | **수용** §5-5 취득 확정 게이트 |
| Codex-7 | 단일 저항 스케일로 "하드 CC 면역, 도트 허용" 표현 불가 | **수용** §6 저항 2종 |
| Codex-8 | 책임 경계 미문서화 | **수용** §7-7 공개 API |

**기각 0건.** 레드팀이 **안전하다고 확인한 것**(기각 근거로 쓰지 말 것 — 확인일 뿐): §3-A 4개 주장 실측 일치 ·
§3-B 13개 인용 중 12개 일치 · 해석 캐시 무효화 빈도 · ~~메모리 ≈12KB/240마리~~ → **rev3 재계산 필요**(G2-K): `SlotCooldownUntil[8]`(32B)·`DotSourceWeapon`(8B)·
`ResolvedStatus`·`bStatusDriverPresent` 추가로 인스턴스당 **≈110B**, 240마리 **≈26KB**. 그리고 이 비용은
문·미션 타겟·오브를 **포함한 모든 `UFPSREnemyHealthComponent` 인스턴스**가 낸다 · `IncomingDamageMultiplier` 가
VIT1 불변식 V1 을 안 깬다 · 배타의 **경로 커버리지 자체는 성립**(치트 벡터 아님).

### G1 2회차 (2026-09-06, Opus 레드팀) — **반려** · 이행 11 · 부분이행 9 · **새 문제 유발 2**

1회차 22건의 **이행 검증**과 rev2 가 새로 연 위험을 함께 봤다. 🔴 **rev2 의 처방 2건이 그 자체로 버그였다.**

| # | 지적 | 처리 |
|---|---|---|
| **G2-A** | 프리즈와 전환은 **중첩**하는데 앵커·누산기가 1쌍뿐 → 전투시계 확장 시 **시계 역행**(실드 재생 영구 정지·크릿 버프 미만료·힐팩 미리스폰) | **수용 — 처방 철회.** §6-1 을 **상태 전용 시계 + refcount** 로 교체 |
| **G2-B** | 클라 전투시계가 전환 동안 서버와 갈리고, 보스 스윕 레이저가 그 시계 위에 그려진다 | **소멸** — 전투시계를 안 건드리므로 성립하지 않는다 |
| **G2-C** | "VIT1 잠복 버그 동반 해소"라는 **전제가 틀렸다** — 전환은 사격을 안 막으므로 VIT1 의 근거가 전환에 성립하지 않는다. 버그 수정이 아니라 **설계 변경**(크릿 버프를 단 채 8초 공짜) | **수용** — §6-1 에 반증을 원문으로 남기고 전투시계 불가침 확정 |
| **G2-D** | `OnDamageApplied` 의 폭발 경로는 배선 불가 + 문서가 4경로/5경로로 갈림 | **수용** — §5-5 **4경로 확정**, 폭발 부여 = 비목표 |
| **G2-E** | `bSuppressRegenAnchor` 가 **도트가 깎은 실드를 되돌려준다**(`ShieldAtLastDamage` 도 같이 찍힌다) | **수용** — §6-2 앵커 2개 구분 + 단위테스트 |
| **G2-F** | 원격 가시성이 NetFreq(S3=2Hz)에 먹혀 짧은 전이가 **원격 3인에게 안 보인다** | **수용** — §8 · PIE 9 를 S2/S3 거리로 판정, 실패 시 데이터 손잡이 |
| **G2-G** | `bStatusDriverPresent` 계약을 켜는 주체가 못 지킨다(드라이버 = `ActiveEnemies` 멤버십, death-dwell 구간) · 보스는 틱을 **두 곳**에서 끈다 · 오브가 세 절에서 다르게 서술됨 | **수용** — §5-6 등록/해제 시점 표로 통일 |
| **G2-H** | 삽입 위치에는 stride 가 **존재하지 않는데** §7-4 가 "stride 구간"으로 규칙을 씀 | **수용** — §7-4 "직전 스텝 이후 경과분" + 도트 주기를 데이터로 |
| **G2-I** | `Apply` 의 Out 배열이 히트마다 힙 할당(프래그먼트는 무상태라 스크래치 불가) | **수용** — §5-4 `TInlineAllocator<8>` |
| **G2-J** | `DamageType.Status` 를 못 쓴다는 **근거가 틀렸다** — 검증기는 `DamageType.` 밖만 막는다 | **수용** — §6 근거 재작성(미선언 + 트리거 이원화 회피) |
| **G2-K** | §12 의 "확인됨" 메모리 수치가 rev2 확장 뒤 미갱신 | **수용** — ≈26KB 로 재계산 |
| (3) | §10 이 §6-1 파급을 못 잡음 · PIE 5 문구 · §2 포인터 오류 | **수용** — 테스트 12 를 겹침 케이스로 재작성 |

**기각 0건.** 레드팀이 **안전 확인**: `TWeakObjectPtr` × 2 를 비-UPROPERTY struct 에 두는 것(선례 2건 —
`FPSRProjectileTypes.h:78`, `FPSREnemySpawnSubsystem.h:364`) · §5-5 오퍼 필터 위치가 코드 구조와 정확히
일치(`FPSRCardSubsystem.cpp:596-680`) · 취득 확정 게이트가 `ApplyCard` CanApply 전수 패스에 얹힘(`:408-423`) ·
§7-6 이월 엘리트 전제(`FPSREnemySpawnSubsystem.cpp:1782-1786`).

**범위 밖(수용, §6-1 에 반영)**: 적 공격 타이밍은 world time 축이라 긴 프리즈 뒤 전 적의 쿨다운이 동시 만료된다.
STAT1 이 만든 문제는 아니나 공격속도저하가 그 축에 얹히므로 시계 축을 §6-1 에 명시했다.

### G1 3회차 (2026-09-06, 새 Opus 인스턴스 14 + Codex 7) — **반려** · 상한 P2 · 기각 0

> 사용자 결정으로 3회차를 태웠다(§6-5-2 (5) 보고 후). Fable 은 여전히 한도라 **이 명세를 처음 보는 새 Opus
> 인스턴스**(앞 두 라운드에 투자되지 않은 눈)와 **Codex** 를 함께 돌렸다. 두 리뷰어가 독립적으로 같은 P1급
> 2건(refcount 누수 · 도트 스텝 시각 부재)에 도달했다.

🔴 **이번 라운드의 결론**: rev3 의 시간축 처방이 **또** 틀렸다. rev1(전제) · rev2(앵커쌍) · rev3(refcount·
실드앵커)로 **세 번 연속**이다. 구현 시 이 축이 최우선 검증 대상이다.

| # | 지적 | 처리 |
|---|---|---|
| **R3-1** | **refcount 는 반드시 샌다** — `SetStageTransition` 은 6값 phase 세터(호출부 13곳)이고 전환 1회가 비-None 을 4~5회 지난다. 잔여 3~4 → 상태 시계 영구 정지 → 실명·속박 영구 고착 | **수용** — §6-1 을 **합성 불린 엣지 감지**로 교체 |
| **R3-2** | `EndRunFreeze` 는 해제 없는 영구 동결 — 같은 월드 재시작에서 시계가 영원히 멈춘다 | **수용** — §6-1 `ResetStatusClockForNewRun()` |
| **R3-3** | **§6-2 처방이 실드를 만충으로 폭증시킨다** — `ComputeRegeneratedShield` 는 절대식이라 두 앵커가 같이 움직여야 한다. 검산: 실드 50 → 6초 만에 100 | **수용** — 두 앵커 동시 이동 + **시간 앵커 역날짜** |
| **R3-4** | `FDamageResult` 에 `HealthSpent` 가 없어 VIT1 의 "실드에 막힌 타격은 상태이상도 막는다"가 조용히 무효 | **수용** — `ShieldSpent`/`HealthSpent` 2필드 추가 |
| **R3-5** | **배타 + 라이플 전용 ⇒ 한 명이 약한 상태 1개 ⇒ 솔로는 강한 상태 원리적 발동 불가 / 4인은 상시 고착.** 산출물과 검증(PIE 6)이 성립하지 않았고, 락다운 분석이 4인을 안 셌다 | **사용자 결정으로 배타 철회** — §2·§3-D·§5-5. 지적 3건(G1-12·G2-2·Codex-2)이 함께 소멸 |
| **R3-6** | DoT 의 "직전 스텝 시각"을 담을 필드도 계산할 인자도 없다. 시계 축도 미정 — world time 으로 재면 전환 직후 8초치가 한 번에 들어간다 | **수용** — `LastStatusStepClock` 신설, `Advance` 가 구간을 소유 |
| **R3-7** | 저항 기본값·프로파일 null 경로 미정 — 0 이면 **오늘 프로파일 없는 스웜 전체가 완전면역** | **수용** — 기본 1.0, null = 1.0/1.0 |
| **R3-8** | 도트가 `FPSRCombat::ApplyDamage` 를 타면 **초당 480회 흡혈 어빌리티 트리거**가 한 ASC 에 몰린다 | **수용** — `bSuppressDealtDamageEvent` |
| **R3-9** | 상태 스텝이 새 O(alive) 전수 루프인데 회계에 없고 `bFrozen` 게이트 앞이다 | **수용** — **상태 보유 적 압축 리스트**로 O(감염된 적), `!bFrozen` 게이트 |
| **R3-10** | 폐쇄 지점이 실제로 3중 — `EnterDyingState` 누락 → 시체가 상태 비트를 켠 채 복제 | **수용** — §7-6 |
| **R3-11** | §5-3 이 `SlotExpiry` 를 "전투시계"라 적어 §6-1 과 정면 충돌 | **수용** — 주석 정정 |
| **R3-12** | §6-2 가 약속한 테스트가 §10 에 없다 · `bRequireHealthDamage` 검증도 없다 | **수용** — 단위 11~14 |
| **R3-13** | 5경로 균일 계약 이탈이 명시되지 않음 · 스플래시 부여 규칙 미정 | **수용** — §5-5 주석 + 카드 문구 규칙 |
| **R3-14** | 재적용 시 시전자 갱신 여부 미정(4인 크레딧) | **수용** — §7-1 마지막 시전자 승계 |
| **R3-15** | §9 데이터 목록·§10 잔여 stale(철회된 "stride 구간" 표현, `Combat.Vitals` 사유) | **수용** |
| **R3-16** | 강한 상태가 "합성"인지 "재료쌍"인지 갈림 | **수용** — §5-1 재료쌍으로 확정, 약한 축은 데이터로 저작 가능 |

**기각 0건.** 레드팀 **안전 확인**: §3-A 스캐폴드 태그 3종 · §3-B 액터 틱 0·이동속도 후크 2곳·넉백 직접 이동 ·
`Deactivate` 의 홀드 해제 쌍 · §5-6 보스 끄는 곳 2개 · §6 삽입 위치가 두 게이트보다 앞이라는 점 ·
§8 의 복제 5→6 과 `Performance.md:117` 이 아직 3이라는 지적 · G2-J 의 검증기 정정.

**범위 밖(수용, 기록만)**: 같은 월드 런 재시작에서 `bRunEnded`/`bRunPaused` 를 푸는 경로가 없고 현행 코드는
레벨 리로드에 기댄다 — 신설 상태 시계가 그 전제를 상속하지 않도록 R3-2 로 닫았다.

### G2 머지 게이트 — *(푸시 직전 · Fable)*
