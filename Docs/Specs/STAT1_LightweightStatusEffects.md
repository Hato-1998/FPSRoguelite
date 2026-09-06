# STAT1 — 경량 상태이상 기반 (약한 4종 + 강한 2종 + 라이플 부여 카드 4장) · **rev2**

> 보드 행: [경량 적 상태이상 서브시스템 (비-GE)](https://app.notion.com/3b93972ddd8881c09b88ee600cc486f0) · 마일스톤 **M1** · 갈래 **코어(T1·T3·T5)**
> 상위 계약: `Docs/SSOT/Enemy.md` §2-6 · `Docs/SSOT/Roadmap.md` §7-6 M1 · `Docs/SSOT/Performance.md` §5 · `Docs/Specs/VIT1_ShieldHealthTwoLayer.md`
>
> **rev1 → rev2**: G1 1회차(Opus 레드팀 15건 + Codex 9건) 전건 반영. rev1 의 시간축 근거·CDO 근거·복제
> 레이아웃·부여 시점이 **사실 오류**로 반증돼 설계가 바뀌었다. 원장 = §12.

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
| 부여 | 라이플 `UnlockableFeatures` 에 4장 (무기당 1개 배타) |
| 가시성 | `StatusBits` 복제 + **OnRep 클라 반쪽** + GMS 이벤트 + 플레이스홀더 큐 |

**범위 = 6축 유지**(사용자 결정 2026-09-06). `Roadmap.md:194` 는 M1 에 상태축 2~3개를 적었으나, 사용자가
설계한 **조합 구조가 통째로 돌아야 재미를 판정할 수 있다**는 이유로 6축을 그대로 간다. G2 판정 해상도가
축 수에 비례하지 않는다는 레드팀 지적(§12 G1-19)은 수용하되, 판정 대상이 "축 개수"가 아니라 "조합"이라는
점에서 이 유닛의 최소 단위가 6축이다.

## 2. 비목표

- **피아식별 반전** — 2단계 별도 행(사용자 결정). 근거 = §3-C. 슬롯만 예약한다.
- **플레이어에게 걸리는 상태이상** — 적→플레이어 데미지는 브릿지를 우회한다(§3-B). 대상 = 적 전용.
- **`DamageType` 트리거 부여** — `Enemy.md:71-72` 는 착지점을 "속성 피격 → 공통 디버프"로 규정했다. 이 유닛은
  **프래그먼트 트리거 하나만** 쓴다. `UFPSRCardEffect::GetDamageTypeTag()`(오버라이드 0건) 시임은 **이번에도
  건드리지 않고 미사용으로 남긴다** — 기제를 둘로 만들지 않기 위해서다(G1-7).
- **프로덕션 VFX·오디오** — M2. 이번엔 플레이스홀더.
- 라이플 외 무기의 부여 카드.

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

### 3-D. 락다운 방지 — 사용자 결정과 그 한계 (G1-12, 갱신)

사용자 결정(2026-09-06 · 2회차): **배타 유지 + 재발동 쿨다운 필드만 예약(기본값 0).**

- **현행 카드 배타는 이 목적에 못 쓴다**: family 배타는 추첨 시점 + **같은 레어도** 한정이고
  (*"same family at a different rarity co-presents"*, `FPSRCardSubsystem.cpp:25-28, 340-344`) 다음 오퍼에서
  또 받는 것을 막지 않으며, 프래그먼트는 `MaxFragmentSlots = 3`(`FPSRWeaponDataAsset.h:392`)이다.
- **배타가 실제로 사는 것은 "무기 1정이냐 2정이냐"뿐이다**(레드팀 반증). 무기 슬롯 3개 + 면역창 0 이면
  `둔화(라이플) → 무기교체 → 도트(SMG)` 사이클(≈2~3초)로 5초짜리 실명을 **혼자 무한 유지**할 수 있다.
- 그래도 배타를 넣는 이유 = **"한 무기 = 하나의 정체성"이라는 빌드 설계**(사용자). 락다운은 쿨다운이 맡는다.
- 🔴 **쿨다운은 필드만 만들고 기본값 0** — 오늘 거동은 사용자 결정(무한 유지 상정) 그대로이고, PIE 에서
  게임이 안 되면 **DataAsset 숫자 하나**로 해결된다. 지금 안 열면 나중에 코드 재작업이다.
- **레드팀이 반증하지 못한 것(=처방의 경로 커버리지는 성립)**: 오퍼는 서버가 짓고 픽 1회마다 재추첨하며
  WeaponUnlock 은 리롤 불가(`FPSRPlayerController.cpp:399-425, 452-455`), 오프닝시드·레벨업 풀은 라우팅
  검증기가 행동 프래그먼트를 차단한다(`FPSRCardPoolValidator.cpp:81-100`). **치트 벡터는 아니다.**
  남는 우회로는 **디버그 `FPSR.ApplyCard` 와 교체 흐름**뿐이고 §5-5 가 그것을 닫는다.

## 4. 제1원리 3줄

1. **제1원리 근거** — 동시 생존 240, 액터당 비용 최소화. ① 신규 컴포넌트 0 ② 액터 틱·타이머 0(기존 배치
   패스에 스텝 1개) ③ 복제는 **`uint8` 1개**만 는다.
2. **엔진 기본값과의 관계 = 덮는다.** 표준은 duration/periodic `GameplayEffect` 인데 ① 스웜·보스에 ASC 가
   없어(`Enemy.md:71-72`) 전 티어에 안 걸리고 ② 엔진이 월드 `FTimerManager` 로 돌려(`GameplayEffect.cpp:4409`·
   `:4431`) §2-2 프리즈를 뚫는다. 대체 = **전투시계 타임스탬프 + 배치 패스**.
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
    UPROPERTY(EditDefaultsOnly) bool  bDisableAttack           = false;
    UPROPERTY(EditDefaultsOnly) bool  bDisableMovement         = false;

    // Strong 전용 (EditCondition: Kind==Strong)
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

// 서버 전용(비-UPROPERTY POD)
struct FFPSRStatusServerState
{
    float SlotExpiry[8]        = {};           // 전투시계 타임스탬프
    float SlotCooldownUntil[8] = {};           // 재발동 쿨다운(기본 0 = 즉시 가능)
    float DotAccumulator       = 0.f;
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
               TArray<uint8>& OutFired);
    bool Advance(uint8& InOutBits, FFPSRStatusServerState&, const UFPSRStatusCatalogDataAsset&,
                 float NowStatusClock, TArray<uint8>& OutExpired, TArray<uint8>& OutFired);
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
// FPSRWeaponHooks::NotifyDamageApplied(...) 를 5경로가 ApplyDamage 직후 호출

UCLASS() class UFPSRStatusApplyFragment : public UFPSRWeaponFragment
{
    UPROPERTY(EditDefaultsOnly) TObjectPtr<UFPSRStatusEffectDataAsset> Status;
    UPROPERTY(EditDefaultsOnly) float ApplyChance = 1.f;
    UPROPERTY(EditDefaultsOnly) bool  bRequireHealthDamage = true;  // 실드에 막힌 타격은 부여 안 함
    virtual void OnDamageApplied(...) const override;
    // IsDataValid: MaxStacks 는 반드시 1 (G1-12 — 배타와 스택이 서로를 무효화한다)
};

// UFPSRWeaponFragment 에 추가
UPROPERTY(EditDefaultsOnly) FGameplayTag ExclusionGroup;   // 같은 그룹은 무기당 1개
```

**배타는 두 곳에서 강제한다** (G1-12 · Codex-2):
1. **오퍼 후보 수집 단계** — `DrawWeaponUnlockOffer` 가 후보를 모을 때. 🔴 **그룹 id·`BaselineWeights` 를
   만들기 *전*에 걸러야 한다** — 뒤에 걸면 비워진 그룹의 몫이 남아 CRIT2 2단 추출의 재분배가 왜곡된다.
2. **취득 확정 게이트** — `UCardEffect_WeaponBehavior::CanApply`. 디버그 `FPSR.ApplyCard` 와 교체 흐름이
   `ApplyCard` 공통 경로를 함께 타므로(`FPSRCardSubsystem.cpp:422-449`) 오퍼 필터만으로는 안 닫힌다.
   교체 흐름은 **"교체로 제거될 프래그먼트"를 제외한 상태**로 검사한다.

⚠️ **알려진 분포 부작용(수용)**: 후보 제거는 그룹 B(기능 카드)의 baseline 합을 줄이므로 그룹 A(새 무기) 몫이
자동으로 커진다. CRIT2 가 보존을 약속한 것은 "시너지 때문에 몫이 줄지 않는 것"이라 계약 위반은 아니다.
§10 에 회귀 케이스를 추가한다.

### 5-6. 대상 게이트 — 드라이버 없는 액터에 걸리지 않는다 (G1-3)

부여 훅은 임의의 `HitActor` 로 불린다(4경로가 `ResolveDamage` 앞에서 호출). 문·미션 도주 타겟은 같은 체력
컴포넌트를 갖지만 `Advance` 를 부를 주체가 없어 **비트가 켜진 채 영원히 안 꺼진다**.

→ `UFPSREnemyHealthComponent::bStatusDriverPresent` 를 **드라이버가 있는 액터가 자기 초기화에서 true 로
세운다**(`AFPSREnemyBase`, `AFPSRBossBase`). `ApplyStatus` 는 false 면 **조용히 거부**한다.
계약: **이 플래그를 켜는 액터는 매 프레임 `AdvanceStatus` 를 보장한다.**

## 6. 배선 지점

| 축 | 지점 | 방식 |
|---|---|---|
| 둔화 | `AFPSREnemyBase::GetEffectiveMoveSpeed()` | `× MoveMult` (사용 시점 곱) |
| **속박** | 동 + 넉백 임펄스 | `bMoveDisabled` → 속도 0 **그리고 넉백 억제**(`FPSREnemyBase.cpp:1388-1391` 은 속도를 안 읽고 액터를 직접 민다 — 안 막으면 속박 걸린 적이 폭발에 날아간다, G1-16) |
| 공격속도저하 | `ServerTickAttack` 의 **임계 비교 시점** | `× AttackIntervalMult`. 🔴 배율이 바뀌면 `AttackAnimHoldUntil`·애니 rate 를 **함께 재계산**한다(`FPSREnemyBase.cpp:1011-1018` — 안 하면 차징 중 Attack 애니가 walk/idle 에 덮인다, G1-9) |
| **실명** | 동 | **onset 에서 `ReleaseRangedHold()` + `ResetRangedCycle()`** 를 부르고 그 다음 패스부터 early-out. 🔴 진입부에서만 return 하면 토큰(`RangedAttackTokenLimit=3`)과 클라 방향경고가 실명 내내 붙잡혀 그 플레이어를 향한 스웜 사격이 통째로 멈춘다(G1-8) |
| 방어력감소 | `UFPSREnemyHealthComponent::ApplyDamage`(`.cpp:81-89`) | `FMitigation` 합성 시 per-instance 층을 곱한다. `DirectionalArmorDR`(열려만 있고 항상 0) 선례. VIT1 불변식 V1 은 안 깨진다(증폭은 `MinKeep` 하한을 통과) |
| 도트 | 배치 패스 → `ApplyDamage(Dot, DotInstigator, Spec)` | `Spec.DamageType` = **빈 태그(무속성)**. 🔴 `DamageType.Status` 는 못 쓴다 — 프로파일 검증기가 `DamageType.*` 밖을 에러로 잡는다(`FPSRVitalsProfile.cpp:82-86`), G1-7. **그리고 `Spec.bSuppressRegenAnchor = true`**(아래) |
| 진행·조합 | 배치 패스 **`bFrozen` 판정 직후(`:306`)·`PlayerPawns` 수집 앞** | 전원 DBNO 에 안 걸리게(G1-2). Out 배열은 **서브시스템 멤버 스크래치**(`:409-411` 선례 — 프레임당 480회 할당 방지, G1-13) |
| 보스 | `AFPSRBossBase::Tick` 에서 같은 `AdvanceStatus` | 프리즈 조건식이 배치 패스와 동일(`FPSRBossBase.cpp:608`)이라 대칭 성립. 런 종료 시 자기 틱을 끄므로(`:601-606`) 상태는 영구 동결 — 코스메틱이라 무해, 명시만 한다(G1-17) |
| 저항 | `UFPSRVitalsProfileDataAsset` 에 **`WeakResistScale` · `StrongResistScale`** 2개 | 단일 스케일로는 "보스는 하드 CC 면역, 도트는 받음"을 표현 못 한다(G1-P3-1) |
| 킬 시임 | `FPSRWeaponHooks::NotifyStatusKill` | `DotSourceWeapon` 이 있어야 `FFPSRFireContext.Instance` 를 채울 수 있다 — 없으면 훅이 빈 목록에 대고 도는 no-op(G1-15) |

### 6-1. 🔴 시간축 — 전투시계를 **스테이지 전환에서도 멈춘다** (G1-1)

현재 `AFPSRGameState` 는 `SetRunPaused` 엣지에서만 `AccumulatedFrozenSeconds` 를 누산한다(`.cpp:226-247`).
그런데 배치 패스는 `IsRunPaused() || IsStageTransitionActive()` 둘 다에서 멈춘다 → **전환 중엔 패스가 멈춘 채
시계만 흐르고, 전환이 끝나는 첫 패스에서 전 적의 상태가 일괄 만료**된다.

→ `SetStageTransitionPhase` 엣지에서도 동결 구간을 누산하도록 **전투시계 계약을 확장**한다.
이러면 "시계가 멈춘다 ⟺ 패스가 멈춘다"가 성립한다.

⚠️ **VIT1 실드 재생에도 파급**된다 — 지금은 전환 중 패스가 멈춘 채 재생만 진행되는 **같은 뿌리의 잠복
버그**가 있고, 이 확장이 그것도 고친다. 의도한 부수효과이며 §10 회귀에 `Combat.Vitals` 를 넣는다.

### 6-2. 🔴 도트가 실드 재생을 봉인하지 않게 (G1-14)

`ApplyDamage` 는 매 호출 `LastDamageCombatTime` 을 다시 찍는다(`.cpp:100-101`). 도트가 배치 패스마다 이걸
부르면 `ElapsedSinceDamage` 가 `ShieldRegenDelaySeconds`(기본 3s)를 **절대 못 넘겨 1 dps 도트 한 장이 실드
재생을 완전히 막는다.** 저작된 결정이 아니라 부작용이다.

→ `FFPSRDamageSpec` 에 `bool bSuppressRegenAnchor = false` 를 추가하고 도트가 true 로 넘긴다.
"도트는 실드 재생을 지연시키지 않는다"가 **명시 규칙**이다.

## 7. 함수별 계약

1. **`Apply`** — 재적용은 지속시간 갱신(기본값). 스택 없음. `SlotCooldownUntil` 이 미래면 거부.
   저항은 `Kind` 에 따라 `WeakResistScale`/`StrongResistScale` 을 지속시간에 곱하고, 0 이면 부여 거부.
   🔴 **조합 판정을 `Apply` 안에서 즉시 한다**(G1-P2-1 · Codex) — 조합은 엣지 이벤트라 배치 패스에만 두면
   S3 에서 최대 8프레임(≈130ms) 늦고, 그 사이 만료된 재료 쌍을 놓친다.
2. **`Advance`** — 만료를 먼저, 그 다음 조합(부여 없이 성립하는 경우 대비). 만료는 타임스탬프 비교이므로
   `DeltaSeconds` 를 쓰지 않는다. 🔴 **`DeltaSeconds × Stride` 보정은 "완전히 활성인 연속 효과"에만
   유효하다** — 만료·조합에는 아무것도 사 주지 않는다(rev1 의 근거 오류, G1-P2-1).
3. **조합 = 재료 소모**(사용자 결정). 강한 상태 발동 시 `RequiredWeakSlots` 2개의 비트를 끈다.
   다중 성립(둔화+도트+방어력감소)은 **카탈로그 배열 순서대로 판정하고 재료가 소모된 조합은 미성립** →
   앞선 것 하나만. 순서 = 데이터.
4. **DoT** — 🔴 **활성 구간으로 클램프**한다(G1-P2-1): stride 구간 안에서 만료됐으면 만료 시각까지만 적용한다.
   안 하면 최대 stride 만큼 과다 적용된다. 킬 크레딧은 `DotInstigator`(약참조); 풀리면 **DoT 는 계속 굴리되
   Instigator=null** 로 넘긴다(중단시키면 "시전자가 죽으면 적이 살아난다"는 더 나쁜 거동).
5. **`Resolve` 합성** — 배율은 곱, 불리언은 OR. 캐시 무효화는 `StatusBits` 변화 시에만
   (재적용은 `SlotExpiry` 만 바꾸므로 무효화 안 함 — 레드팀이 비용 안전을 확인).
6. **🔴 수명주기 = 4중 폐쇄** (G1-4 — `Enemy.md:55-57` 이 같은 문제에 이미 두 번 요구한 패턴):
   `ResetForReuse`(풀 재사용) · `Deactivate` · `Activate`(방어적 재클리어) · `ServerResetEliteForStageCarry`
   (이월 엘리트는 `Activate`/`Deactivate` 를 **둘 다 안 밟는다**). 각 지점에서 `StatusBits=0` ·
   `SlotExpiry/SlotCooldownUntil` 클리어 · `DotAccumulator=0` · 약참조 2개 null · `ResolvedStatus` 리셋 ·
   **Push Model dirty 마킹**까지.
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

🔴 **비용 회계 정정** (G1-10) — `Performance.md:130` 은 *"Push Model 전제는 출시 빌드에선 성립하지 않는다…
호스트 프레임 예산을 계산할 때 이 차이를 빼고 세지 말 것"*이라 했는데 rev1 이 뺐다.
- 패키지 빌드: 프로퍼티 5→6 = 적당 매 rep 프레임 비교 **+20%**. 그 비용이 걸리는 곳은 `Performance.md:118`
  이 이미 "relevant ≈ alive 전량, 예산 ~150 초과"라 적어 둔 자리다.
- **더 큰 항목**: 도트가 `Health` 를 **이벤트성 → 연속 변화 프로퍼티로 바꾼다**. `ApplyDamage` 는 매 호출
  `Health`/`Shield` 를 dirty 로 찍으므로(`.cpp:95-97`), 도트 걸린 N마리가 각자의 NetFreq(S0 30Hz~S3 2Hz)로
  `Health` 를 계속 흘린다. **§10-PIE 9 는 "도트를 다수에 건 상태"로 측정한다.**

`Performance.md:117` 은 아직 "3프로퍼티"인데 실제는 5다(VIT1 미반영) → 이 유닛에서 **6으로 정정**한다.

## 9. 데이터드리븐 경계

- **코드**: 비트 저장 · 시간축(전투시계 + 4중 폐쇄) · 배치 진행 · 조합 알고리즘 · 해석 캐시 · §6 배선 지점 ·
  효과 축 6종 · **진행 드라이버가 2개(배치 패스·보스 틱)라는 사실**.
- **데이터**: 이름 · 태그 · 슬롯 번호 · 지속시간 · 전 배율 · 조합 재료쌍 · 소모 여부 · **재발동 쿨다운** ·
  판정 순서 · 저항 2종 · 배타 그룹 · 카드 레어도 티어.

## 10. 검증 기준

**순수 함수 자동화 (`FPSRoguelite.Status.Unit`)**
1. 재적용 = 지속시간 갱신, 스택 없음 · 2. 조합 성립 → 강한 ON + 재료 2비트 OFF · 3. 재료 하나로는 미성립 ·
4. 다중 성립 시 앞선 것 하나만 · 5. 저항 0=거부 / 0.5=절반, Weak·Strong 독립 · 6. `Resolve` 축별 곱·OR ·
7. 쿨다운 미래면 부여 거부 · 8. **강한 상태 재발동이 기존 강한 비트의 만료를 갱신** ·
9. **DoT 가 활성 구간으로 클램프**(stride 구간 중간 만료) · 10. 카탈로그 `IsDataValid` 음성 검사

**월드 자동화 (신규 — 순수 함수로는 위험의 대부분을 못 잡는다, G1-P3-3)**
11. `ResetForReuse` 후 `StatusBits == 0`(풀 재사용 누수)
12. **스테이지 전환을 끼운 만료** — 전환 6초 뒤 남은 시간 불변(§6-1)
13. **드라이버 없는 액터(문)에 부여가 거부**되는가(§5-6)
14. 전원 DBNO 구간을 끼운 만료 — 상태 진행이 멈추지 않는가(§6 삽입 위치)

**기존 회귀** — `Enemy.*`(5) · **`Combat.Vitals`**(§6-1 시계 확장 파급) · `Boss.*` · **`Card.Synergy`**
(배타 필터 후 그룹 몫 계약, §5-5) · `Editor.CardCsv.*` · `Smoke.ModuleLoads`

**PIE 사용자 스모크**
1. 둔화 — 적이 눈에 띄게 느려지는가 · 2. 도트 — 사격을 멈춰도 깎이는가 · **레벨업 프리즈 중 멈추는가** ·
**스테이지 전환 뒤에도 남은 시간이 유지되는가** · 3. 방어력감소 — 데미지가 커지는가 ·
4. 공격속도저하 — 발사 간격이 벌어지는가, **차징 애니가 안 깨지는가** ·
5. 무기당 배타 — 이미 든 상태에서 다른 상태 카드가 오퍼에 안 뜨는가 ·
6. 무기 2정으로 **실명 발동** — 공격이 멈추는가, 재료 2개가 사라지는가, **다른 적의 사격이 정상인가**(토큰) ·
7. 도트 킬이 XP 를 주는가 · 8. 보스에 하드 CC 가 안 걸리고 **도트는 걸리는가** ·
9. **원격 클라(호스트 아님)에서 상태가 보이는가** — 2인 PIE ·
10. **도트를 다수에 건 상태**로 적 200+ 프레임 예산 유지

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
§3-B 13개 인용 중 12개 일치 · 해석 캐시 무효화 빈도 · 메모리 ≈12KB/240마리 · `IncomingDamageMultiplier` 가
VIT1 불변식 V1 을 안 깬다 · 배타의 **경로 커버리지 자체는 성립**(치트 벡터 아님).

### G2 머지 게이트 — *(푸시 직전 · Fable)*
