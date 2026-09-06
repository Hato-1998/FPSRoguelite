# STAT1 — 경량 상태이상 기반 (약한 4종 + 강한 2종 + 라이플 부여 카드 4장)

> 보드 행: [경량 적 상태이상 서브시스템 (비-GE)](https://app.notion.com/3b93972ddd8881c09b88ee600cc486f0) · 마일스톤 **M1** · 갈래 **코어(T1·T3·T5)**
> 상위 계약: `Docs/SSOT/Enemy.md` §2-6(시간축·상태이상) · `Docs/SSOT/Roadmap.md` §7-6 M1 · `Docs/Specs/VIT1_ShieldHealthTwoLayer.md`

---

## 1. 목표

적(스웜·엘리트·보스)에 **지속시간 있는 상태이상**을 거는 공용 기반을 만들고, 그 위에 약한 4종과 강한 2종을
얹는다. 라이플에 부여 카드 4장을 붙여 **인게임에서 눈으로 확인 가능한 상태**까지 간다.

이름·설명·수치는 **미확정**이다(사용자 2026-09-06). 이 유닛이 고정하는 것은 **구조**이고, 이름·지속시간·
배율·조합표는 전부 콘텐츠 값으로 노출한다.

| 축 | 이번 유닛의 산출물 |
|---|---|
| 약한 4종 | 둔화 · 도트데미지 · 방어력감소 · 공격속도저하 |
| 강한 2종 | 실명(공격불가) = 둔화+도트 · 속박(이동불가) = 도트+방어력감소 |
| 부여 | 라이플 `UnlockableFeatures` 에 약한 4종 카드 (무기당 1개 배타) |
| 가시성 | 상태 비트 복제 + GMS 이벤트 (플레이스홀더 수준) |

## 2. 비목표

- **피아식별 반전** — 사용자 결정(2026-09-06)으로 **2단계 별도 행**. 근거 = §3-C. 이번 유닛은 슬롯만 예약한다.
- **플레이어에게 걸리는 상태이상** — 적→플레이어 데미지는 브릿지를 우회하므로(§3-B) 별도 배선이 필요하다. 이번 유닛의 대상은 적 전용.
- **프로덕션 VFX·오디오** — M2 소관. 이번엔 "보이기만 하면 되는" 플레이스홀더.
- 라이플 외 무기의 부여 카드 — 사용자 지시("우선 라이플 먼저").

## 3. C0 조사 실측 (설계 근거 — 추측 아님)

### 3-A. 클린 슬레이트
- `Source/` 전체에 `StatusEffect|Shock|Frost|Rupture|Burn|Debuff|Ailment` 심볼 **0건**.
- `Status.Burning/Slowed/Stunned` 태그 3개는 최초 스캐폴드 커밋(`52faacf4`) 이후 **참조 0건**인 유령 태그.
- `UFPSRWeaponFragment::OnStatusKill`(`FPSRWeaponFragment.h:103-108`) = 선언만, 호출부 0, `FPSRWeaponHooks` 에 진입점 없음.
- `UCardEffect_*::GetDamageTypeTag()`(`FPSRCardEffect.h:80-81`) = 오버라이드 0건, 호출부 0건.

### 3-B. 붙일 자리는 닫혀 있다

| 사실 | 근거 |
|---|---|
| 적은 **액터 틱 0** | `FPSREnemyBase.cpp:58` `PrimaryActorTick.bCanEverTick = false` |
| 적·컴포넌트에 **`FTimerHandle` 0건** — 전부 서브시스템 소유 | `grep FTimerHandle Private/Enemy/` |
| 구동은 배치 패스 2개 | `TickServerMovement`(`FPSREnemySpawnSubsystem.cpp:645,721`) · `ServerTickAttack`(`:605,619`) |
| **프리즈 시 패스가 통째로 early-return** | `FPSREnemySpawnSubsystem.cpp:305-310, 396-399` |
| 거리 LOD 는 stride 로 스킵하되 `DeltaSeconds = DeltaTime * Stride` 로 보정 | `FPSREnemySpawnSubsystem.cpp:556-570, 601` |
| 이동속도 후크 = **1함수, 읽는 곳 2곳** | `GetEffectiveMoveSpeed()` — `FPSREnemyBase.cpp:1276, 1413` |
| "사용 시점에 곱한다" 관용구가 이미 검증돼 있음 | `FPSREnemyBase.cpp:36-54` `FPSR.Debug.EnemySpeedScale`, 헤더 주석 `.h:601-604` |
| 공격 타이밍 = 프리즈-멈춤 누산기 | `RangedChargeTime`/`RangedFireCooldown`(`.h:905-910`) + `ChargeElapsed`/`CooldownElapsed`(`.h:993-997`) |
| 데미지 브릿지 단일 | `FPSRCombat::ApplyDamage`(`FPSRCombatStatics.h:138`) → `UFPSREnemyHealthComponent::ApplyDamage`(`.h:33`) |
| 전투시계 존재 | `AFPSRGameState::GetCombatClockSeconds()`(`FPSRGameState.cpp:256-273`), 틱 0 · 엣지 누적 |
| 적 per-instance 방어 층 **없음** | `FPSREnemyHealthComponent.cpp:81-83` — *"no per-instance mitigation ATTRIBUTE layer"* |
| 보스도 같은 체력 컴포넌트를 쓴다 | `FPSRBossBase.h:243` |

### 3-C. 이번 유닛이 **하지 않는** 이유 (피아식별 반전)

| 막힌 지점 | 근거 |
|---|---|
| 타겟 후보가 `PlayerControllerIterator` 로만 채워짐, `TArray<APawn*, TInlineAllocator<4>>` | `FPSREnemySpawnSubsystem.cpp:335-341, 342-384` |
| 적격 판정이 `APlayerController*` 타입에 묶임 | `FPSRTargeting.h:36` |
| 적 팀 투사체는 `IsA(AFPSRCharacter)` 만 적대 | `FPSRProjectile.cpp:415-419` |
| 적 브랜치가 **데미지 브릿지를 안 탐** | `FPSRProjectile.cpp:499-514` |
| 보스 폭발이 **플레이어 pawn 채널만** 수집 | `FPSRCombatStatics.h:100-113` |
| **적 근접 축은 죽은 코드로 제거됨** — ADR 0013 C0 이후 전 적이 원거리 | `FPSREnemyBase.h:26-28` |

### 3-D. 반증된 전제 (사용자 승인 설계의 결함 — 이 유닛이 메운다)

사용자는 "**한 무기에는 상태이상 카드 1개**"를 락다운 방지 장치로 승인했다. **현행 인프라는 이를 강제하지 않는다.**

- 카드 family 배타는 **추첨 시점 + 같은 레어도** 한정 — *"same family at a different rarity co-presents"*(`FPSRCardSubsystem.cpp:25-28, 340-344`).
- 다음 오퍼에서 같은 family 를 또 받는 것은 **아무것도 막지 않는다**.
- 상태이상을 프래그먼트로 만들면 `MaxFragmentSlots = 3`(`FPSRWeaponDataAsset.h:392`)이라 **한 무기가 3종 동시 보유** 가능.

→ **보유 시점 배타 축을 신설한다**(§5-5). 이것이 없으면 라이플 하나로 둔화+도트를 모아 실명을 혼자 무한히 건다.

⚠️ **잔존 한계(사용자에게 보고됨)**: 배타는 **무기당**이라 무기 3슬롯(원거리2+근접1) 플레이어는 여전히
상태이상 3종을 들고 무기를 바꿔 조합할 수 있다. "협동 전용 조합"을 원하면 **플레이어당 1개**여야 한다 —
그 판정은 밸런스이므로 배타 스코프를 데이터로 노출해 나중에 뒤집을 수 있게 둔다.

## 4. 제1원리 3줄 (핵심원칙 4)

1. **제1원리 근거** — 동시 생존 천장 240, 액터당 비용 최소화가 이 게임의 제1원리다. 그래서 ① 신규 컴포넌트를
   만들지 않고 기존 `UFPSREnemyHealthComponent` 에 POD 필드로 얹는다 ② 액터 틱·타이머를 도입하지 않고
   **기존 배치 패스에 스텝 1개**를 더한다 ③ 복제는 **1바이트(비트마스크)**만 는다.
2. **엔진 기본값과의 관계 = 덮는다.** 엔진 표준은 duration/periodic `GameplayEffect` 다. 두 이유로 못 쓴다 —
   ① 스웜·보스에 ASC 가 없어(`Enemy.md:71-72`) **전 티어에 안 걸린다** ② 엔진이 duration/period 를 월드
   `FTimerManager` 로 돌려(`GameplayEffect.cpp:4409` · `:4431`) §2-2 전역 프리즈를 **뚫는다**. 대체 =
   전투시계 타임스탬프 + 배치 패스(실드 재생이 쓰는 검증된 관용구).
3. **프로젝트 정합** — `FFPSRDamageSpec` 은 *"sibling units (lightweight enemy status effects) can add fields
   without touching this signature again"*(`FPSRVitals.h:8-12`)라고 **이 유닛을 위해 열어 둔 자리**다. 저장소를
   체력 컴포넌트에 두면 스웜·엘리트·보스가 **같은 컴포넌트를 공유**하므로 전 티어가 한 번에 해결된다.

## 5. 인터페이스 (헤더 스케치)

### 5-1. 상태 정의 = 무상태 공유 DataAsset (`UFPSRWeaponFragment` 선례)

```cpp
UENUM() enum class EFPSRStatusKind    : uint8 { Weak, Strong };
UENUM() enum class EFPSRStatusRefresh : uint8 { RefreshDuration, Ignore };

UCLASS() class UFPSRStatusEffectDataAsset : public UPrimaryDataAsset
{
    UPROPERTY(EditDefaultsOnly) FGameplayTag StatusTag;              // Status.*
    UPROPERTY(EditDefaultsOnly) uint8 SlotIndex = 0;                 // 0..7 비트 위치(카탈로그 내 중복 금지)
    UPROPERTY(EditDefaultsOnly) EFPSRStatusKind Kind = Weak;
    UPROPERTY(EditDefaultsOnly) float DurationSeconds = 5.f;
    UPROPERTY(EditDefaultsOnly) EFPSRStatusRefresh Refresh = RefreshDuration;

    // 효과 축 — 전부 기본값이 무효과. 한 상태가 여러 축을 건드려도 된다.
    UPROPERTY(EditDefaultsOnly) float MoveSpeedMultiplier       = 1.f;
    UPROPERTY(EditDefaultsOnly) float AttackIntervalMultiplier  = 1.f;  // >1 = 느려짐
    UPROPERTY(EditDefaultsOnly) float IncomingDamageMultiplier  = 1.f;  // 방어력감소 = >1
    UPROPERTY(EditDefaultsOnly) float DamagePerSecond           = 0.f;
    UPROPERTY(EditDefaultsOnly) bool  bDisableAttack            = false;
    UPROPERTY(EditDefaultsOnly) bool  bDisableMovement          = false;

    // Strong 전용 (EditCondition 으로 Kind==Strong 일 때만 노출)
    UPROPERTY(EditDefaultsOnly) TArray<uint8> RequiredWeakSlots;       // 정확히 2개(IsDataValid)
    UPROPERTY(EditDefaultsOnly) bool bConsumeSources = true;           // 사용자 결정 2026-09-06
};
```

**조합표를 별도 테이블로 두지 않는 이유**: 강한 상태가 자기 재료를 스스로 선언하면 조합표와 강한상태 정의가
**한 곳**에 산다. 재료 쌍을 바꾸려면 그 DA 하나만 고치면 되고, 미사용 조합 3쌍을 나중에 채우는 것도 DA 추가뿐이다.

### 5-2. 카탈로그 (슬롯 ↔ 정의 룩업)

```cpp
UCLASS() class UFPSRStatusCatalogDataAsset : public UPrimaryDataAsset
{
    UPROPERTY(EditDefaultsOnly) TArray<TObjectPtr<UFPSRStatusEffectDataAsset>> Statuses;
    // IsDataValid: SlotIndex 중복 0 · 범위 0..7 · Strong 의 RequiredWeakSlots 는 정확히 2개이고 둘 다 Weak 슬롯 ·
    //              Strong 끼리 같은 재료쌍 금지 · Weak 이 RequiredWeakSlots 를 갖지 않을 것
};
```

경로는 **config 소프트경로**(`FPSREnemyRenderSettings.h:70` 선례) — C++ 에셋 경로 하드코딩 금지(핵심원칙 2).

### 5-3. 런타임 상태 (POD, 체력 컴포넌트 소유)

```cpp
USTRUCT() struct FFPSRStatusState
{
    UPROPERTY() uint8 ActiveBits = 0;        // 복제되는 유일한 필드
    float SlotExpiry[8] = {};                // 전투시계 타임스탬프(서버 전용)
    float DotAccumulator = 0.f;              // 서버 전용
    TWeakObjectPtr<AActor> DotInstigator;    // 킬 크레딧(§7-4)
};

USTRUCT() struct FFPSRResolvedStatus         // ActiveBits 가 바뀔 때만 재계산
{
    float MoveMult = 1.f;
    float AttackIntervalMult = 1.f;
    float IncomingDamageMult = 1.f;
    float DotPerSecond = 0.f;
    bool  bAttackDisabled = false;
    bool  bMoveDisabled   = false;
};
```

### 5-4. 규칙 = 상태 없는 순수 함수 (`FPSRVitals` 선례)

```cpp
namespace FPSRStatus
{
    /** 부여. 저항으로 거부되면 false. 조합이 성립하면 OutFired 에 강한 슬롯을 담는다. */
    bool Apply(FFPSRStatusState&, const UFPSRStatusCatalogDataAsset&, uint8 Slot,
               float NowCombat, float ResistScale, TArray<uint8>& OutFired);

    /** 만료 + 조합 판정. 비트가 바뀌면 true. */
    bool Advance(FFPSRStatusState&, const UFPSRStatusCatalogDataAsset&, float NowCombat,
                 TArray<uint8>& OutExpired, TArray<uint8>& OutFired);

    FFPSRResolvedStatus Resolve(const FFPSRStatusState&, const UFPSRStatusCatalogDataAsset&);
}
```

**왜 순수 함수인가** — 적은 컴포넌트 필드에, 플레이어(2단계)는 다른 저장소에 살 텐데 규칙은 하나여야 한다.
VIT1 이 `FPSRVitals::ApplyDamage` 로 정확히 같은 문제를 푼 선례가 있고, 테스트가 월드 없이 돈다.

### 5-5. 부여 = 프래그먼트 + 신규 배타 축

```cpp
UCLASS() class UFPSRStatusApplyFragment : public UFPSRWeaponFragment
{
    UPROPERTY(EditDefaultsOnly) TObjectPtr<UFPSRStatusEffectDataAsset> Status;
    UPROPERTY(EditDefaultsOnly) float ApplyChance = 1.f;
    virtual void OnHitActor(...) const override;   // 기존 훅 — 신규 훅면 0
};

// UFPSRWeaponFragment 에 추가:
UPROPERTY(EditDefaultsOnly) FGameplayTag ExclusionGroup;   // 같은 그룹은 무기당 1개
```

`DrawWeaponUnlockOffer` 후보 필터에 **"이 무기가 이미 같은 `ExclusionGroup` 프래그먼트를 보유하면 배제"** 를
추가한다(`MaxFragmentSlots` 검사 옆). **§3-D 의 반증을 메우는 지점이고, 이 유닛의 유일한 카드 시스템 개정이다.**

## 6. 배선 지점 (전부 기존 단일 지점)

| 축 | 지점 | 방식 |
|---|---|---|
| 둔화 · 속박 | `AFPSREnemyBase::GetEffectiveMoveSpeed()` | `× MoveMult` (속박 = 0). 기존 `EnemySpeedScale` 과 같은 "사용 시점 곱" |
| 공격속도저하 · 실명 | `AFPSREnemyBase::ServerTickAttack` | charge/cooldown 임계 `× AttackIntervalMult`; 실명 = 진입부 early-out. **`EditDefaultsOnly` 원본은 안 건드린다**(CDO 공유) |
| 방어력감소 | `UFPSREnemyHealthComponent::ApplyDamage`(`.cpp:81-89`) | `FMitigation` 합성 시 per-instance 층을 곱한다. `DirectionalArmorDR`(열려만 있고 항상 0) 선례 |
| 도트 | 배치 패스 → `ApplyDamage(Dot, DotInstigator, Spec{DamageType=Status})` | 누산기, `Ctx.DeltaSeconds` 사용(stride 보정이 이미 있음) |
| 진행·조합 | `UFPSREnemySpawnSubsystem` 배치 패스에 스텝 1개 | 프리즈 early-return 을 그대로 상속 |
| 보스 | 보스 자체 틱에서 같은 `Advance` 호출 | ⚠️ **미확인** — 보스가 스폰 서브시스템 패스에 없다면 별도 호출 필요(§11-1) |
| 저항 | `UFPSRVitalsProfileDataAsset` 에 `StatusResistScale` 신설 | 보스·엘리트 하드 CC 면역/감쇠. `DefenseByDamageType` 옆 |
| 킬 시임 | `FPSRWeaponHooks::NotifyStatusKill` 신설 | `NotifyKill` 대칭. `OnStatusKill` 최초 배선 |

## 7. 함수별 계약

1. **`Apply` — 재적용은 지속시간 갱신**(기본값; 데이터로 `Ignore` 선택 가능). 스택 없음. 저항 `ResistScale`
   은 지속시간에 곱하고, 0 이면 부여 자체가 거부된다(보스 하드 CC 면역의 구현).
2. **`Advance` — 만료를 먼저, 조합을 그 다음.** 순서를 뒤집으면 만료 직전 프레임에 조합이 성립해 재료가
   이미 사라진 강한 상태가 생긴다.
3. **조합 = 재료 소모**(사용자 결정 2026-09-06). 강한 상태가 발동하면 `RequiredWeakSlots` 2개의 비트를 끈다.
   **면역창 없음** — 사용자가 무한 유지를 상정하고 승인했다. 대신 재발동에는 재료 2개를 다시 모아야 하고,
   §3-D 의 무기당 배타가 "한 무기로는 재료 하나만"을 강제한다.
4. **DoT 킬 크레딧** — `DotInstigator`(약참조)로 `ApplyDamage` 를 호출한다. 시전자가 죽거나 나가면 약참조가
   풀리고, 그때는 **DoT 를 계속 굴리되 Instigator=null 로 넘긴다**(데미지는 유지, 킬 크레딧만 소실). DoT 를
   중단시키면 "시전자가 죽으면 적이 살아난다"는 더 나쁜 거동이 된다.
5. **다중 조합 동시 성립** — 둔화+도트+방어력감소면 실명(둔화+도트)과 속박(도트+방어력감소)이 동시에 성립한다.
   계약 = **카탈로그 배열 순서대로 판정하고, 재료가 이미 소모된 조합은 성립하지 않는다.** 즉 앞선 것 하나만
   발동한다. 순서가 곧 우선순위이고 **데이터**다(§11-2 에 사용자 확인 항목으로 남긴다).

## 8. 복제표 (§6-3 서버권위 + Push Model)

| 프로퍼티 | 소유 | 복제 | 비고 |
|---|---|---|---|
| `FFPSRStatusState::ActiveBits` | `UFPSREnemyHealthComponent` | **Push Model, 변할 때만** | 1바이트. 아래 주 참조 |
| `SlotExpiry` · `DotAccumulator` · `DotInstigator` | 동 | **복제 0** — 서버 전용 | 클라는 "무엇이 걸렸나"만 알면 되고 "언제 끝나나"는 필요 없다 |
| 부여·만료 코스메틱 | GMS 로컬 pub/sub | **복제 0** | `FFPSRCosmeticEventMessage`. 이 유닛이 GMS 의 **첫 실사용 프로듀서** |

⚠️ `Performance.md:117` 은 "적 복제 상태 = Transform + **3프로퍼티**"라고 적었는데 **이미 틀렸다** — VIT1 이
`MaxShield`·`Shield` 를 더해 5개다(`FPSREnemyHealthComponent.cpp:51-62` 실측). 이 유닛에서 **6개로 정정**한다.

## 9. 데이터드리븐 경계

- **코드(구조상 안 바뀌는 것)**: 비트 저장 · 전투시계 만료 · 배치 진행 · 조합 판정 알고리즘 · 해석 캐시 ·
  6개 배선 지점 · 효과 축 6종(이동 / 공격간격 / 피해증폭 / 도트 / 공격불가 / 이동불가).
- **데이터(나중에 바뀔 것)**: 상태 이름 · 태그 · 슬롯 번호 · 지속시간 · 전 배율 · 조합 재료쌍 · 소모 여부 ·
  판정 순서 · 저항 스케일 · 배타 그룹 · 라이플 카드 4장의 레어도 티어.

## 10. 검증 기준

**자동화 — 신규 `FPSRoguelite.Status.Unit`(월드 없이 순수 함수)**

1. 재적용 = 지속시간 갱신, 스택 없음
2. 만료가 전투시계 기준(프리즈 구간을 끼워도 남은 시간 불변)
3. 조합 성립 → 강한 비트 ON + 재료 2비트 OFF
4. 재료 하나만으로는 조합 미성립
5. 다중 성립 시 **앞선 것 하나만** 발동
6. 저항 0 = 부여 거부 / 0.5 = 지속시간 절반
7. `Resolve` 가 다중 상태에서 축별로 곱해짐
8. 카탈로그 `IsDataValid` 음성 검사(슬롯 중복 · 재료 3개 · Weak 에 재료 선언 등)

**기존 회귀** — `Enemy.*`(5) · `Combat.Vitals` · `Boss.TimeAxisGuard` · `Card.Synergy` · `Editor.CardCsv.*` · `Smoke.ModuleLoads`

**PIE 사용자 스모크**

1. 라이플에 둔화 카드 → 적이 눈에 띄게 느려지는가
2. 도트 카드 → 사격을 멈춰도 체력이 계속 깎이는가 · **레벨업 프리즈 중 멈추는가**
3. 방어력감소 → 데미지 숫자가 커지는가
4. 공격속도저하 → 적 발사 간격이 벌어지는가
5. **무기당 배타** — 라이플에 둔화를 든 상태에서 다른 상태이상 카드가 오퍼에 안 뜨는가
6. 무기 2정(라이플 둔화 + 다른 무기 도트)으로 **실명 발동** — 적이 공격을 멈추는가, 재료 2개가 사라지는가
7. 도트로 죽인 적이 XP 를 주는가(킬 크레딧)
8. 보스에 하드 CC 가 안 걸리는가(저항)
9. 적 200+ 에서 프레임 예산 유지(`Performance.md` §5 기준)

## 11. 미결정 (착수 전 확인 필요)

1. ⚠️ **보스가 스폰 서브시스템 배치 패스에 포함되는가 — 미확인.** 아니면 보스 틱에서 `Advance` 를 따로 불러야 한다. C1 착수 시 최우선 확인.
2. **다중 조합 동시 성립 시 "앞선 것 하나만"이 맞는가** — §7-5. 기본값으로 진행하되 PIE 에서 사용자 판정.
3. ⚠️ **적 머티리얼이 CPD 를 아직 아무것도 안 읽는다**(`FPSRAnimCPDParams.h:22` 자백). 가시성을 CPD 로 하려면
   머티리얼 작업이 동반돼야 하고 그건 **콘텐츠(사용자) 영역**이다. 대안 = 1차엔 적 헬스바 위젯의 아이콘.
4. **미사용 조합 3쌍**(둔화+방어력감소 / 둔화+공속저하 / 도트+공속저하) — 의도인지 확장 슬롯인지 미확인.
5. **방어력감소에 상한이 없다** — `MaxTotalReduction` 은 감산만 막고 증폭은 안 막는다(`FPSRVitalsProfile.h:67-72`).
   방어력감소 + 크리 + 약점이 겹칠 때의 상한을 둘지 결정 필요(권장: 프로파일에 증폭 상한 1개).

## 12. 레드팀 지적 원장

### G1 플랜 게이트 — *(제출 예정)*
### G2 머지 게이트 — *(푸시 직전)*
