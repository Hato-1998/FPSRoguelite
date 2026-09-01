# VIT1 — 실드/체력 2층 바이탈 시스템 (플레이어 + 몬스터 공용)

## 1. 메타

| 항목 | 값 |
|---|---|
| 유닛 ID / 이름 | **VIT1** — Shield/Health Two-Layer Vitals |
| 브랜치 | `phase/m1-shield-2layer` |
| 작성 모델 | `claude-opus-5` (§6-5-2 개정 2026-08-26 — C1 설계 담당은 Opus, Fable은 G1/G2 게이트) |
| 작성일 / 최종 갱신 | 2026-09-01 / 2026-09-01 |
| 상태 | `초안` (G1 플랜게이트 대기) |
| 관련 SSOT | `CombatWeaponCard.md` §2-3-1·§2-3-5·§2-3-7·§2-3-8·§2-3-9 · `Enemy.md` §2-6·§2-10 · `PlayerFeel.md` §2-13·§2-14 · `RunFlow.md` §2-2·§2-8 · `Architecture/0013`·`0014` |
| 관련 메모리 | `[[reason-in-multiplayer-terms]]` `[[production-structure-first]]` `[[code-is-immutable-structure-only]]` `[[push-model-off-in-packaged-build]]` `[[cpp-uproperty-name-collides-with-bp]]` `[[extensibility-first-designer-tooling]]` `[[da-edits-are-user-work]]` `[[do-not-launch-game]]` |
| 보드 행 | [실드/체력 2층 데미지 구조](https://app.notion.com/3be3972ddd88813bb054d5c8ac0a3ee2) — M1 · 미듐 · M |

---

## 2. 목표 / 비목표

### 목표 (동작 기준)

1. **플레이어와 모든 몬스터가 같은 2층 규칙으로 피해를 받는다** — 실드가 먼저 닳고, 초과분이 체력으로 이월된다. 층마다 데미지 타입별 방어 계수를 따로 갖는다.
2. **실드가 없는 개체가 자연스럽게 표현된다** — `MaxShield = 0` 이면 전량이 체력으로 간다. 보스·구조물·"실드 포기" 카드를 쓴 플레이어가 전부 이 한 값으로 표현된다(분기 없음).
3. **실드는 피해를 안 입은 뒤 일정 시간이 지나면 스스로 찬다.** 지연은 **부분 손상 / 완파** 두 단계다. 시계는 **전역 프리즈(§2-2) 동안 멈춘다.**
4. **체력은 스스로 차지 않는다.** 회복 경로는 ①맵 힐팩 ②흡혈 카드 ③부활(50%) ④`MaxHealth` 증가분 즉시 회복(§2-13) 넷뿐이다.
5. **한 방이 클수록 실드 상대로 유리하다** — 초과분 이월 + 무기별 대실드 계수. 저격/관통 무기의 구조적 메리트.
6. **실드 파손이 양쪽에 전달된다** — 공격자에게 히트마커, 적에게는 클라 코스메틱, 플레이어 본인에게는 위험 경고. **신규 복제 프로퍼티 0으로.**
7. **수치가 전부 데이터다** — 방어계수·최대량·재생속도·재생지연·파손 여부를 다른 시스템(카드·상태이상·디렉터)이 읽고 쓸 수 있다.

### 비목표 (일부러 안 하는 것)

- **적 실드 재생을 클라이언트가 실시간으로 보는 것.** 적 재생은 지연계산(무틱·무복제)이라 클라는 다음 피격 결과로만 안다(사용자 결정 2026-09-01).
- **방향성 아머(`DirectionalArmor`) 구현.** M4 별도 행. 이 유닛은 **결합 규칙과 그 자리(hook)만** 못박는다.
- **상태이상 본체.** M1 자매 행 2개 소관. 이 유닛은 **상호작용 규칙과 질의 API**만 제공한다.
- **보스 실드.** 보스는 `MaxShield = 0`(요구 1의 "보스는 실드 없이 체력만 많거나"). 확장점만 남긴다.
- **힐팩 드롭(적 사망 시).** 이번엔 **맵 배치형**만. 드롭 경로는 후속.
- **`HealthRegen` 카드 삭제.** 카드 **풀에서 빼는 것은 콘텐츠 작업**(사용자). 코드는 안 건드린다 — `UCardEffect_CharacterGE`는 다른 카드가 계속 쓴다.
- **periodic GE × 전역 프리즈 문제의 전면 해결.** 이 유닛에서 **발견**했지만(§11-3) 스코프 밖 — 별도 행.
- **`ECardScope`/오퍼 파이프라인 변경.** 카드는 기존 효과 타입만 쓴다(신규 `UFPSRCardEffect` 서브클래스 0개).

---

## 3. 제1원리 3줄 (핵심원칙 4)

1. **제1원리 근거 — 적 200~300을 싸게.** 그래서 (a) **신규 컴포넌트 0**: 실드는 이미 모든 적·보스·구조물이 공유하는 `UFPSREnemyHealthComponent`의 필드로 들어간다(컴포넌트를 하나 더 붙이면 액터당 UObject 1개 + 등록/틱 후보 1개가 늘고, 그건 이 게임에서 가장 비싼 축이다). (b) **적 재생은 무틱·지연계산**: 200~300개가 매 프레임 도는 재생 루프는 예산이 없고, 배치패스로 돌리면 200~300 × 복제 dirty가 매 주기 발생한다. 피격 시점에 `f(경과시간)`으로 한 번 계산하면 비용이 **피격 횟수에 비례**하지 그 자리에 서 있는 적 수에 비례하지 않는다. (c) **`MaxShield = 0`이 곧 "실드 없음"**: 분기·플래그·서브클래스 없이 산술로 흡수되고, Push Model이라 값이 안 변해 영원히 dirty가 안 된다.
2. **엔진 기본값·기존 인프라와의 관계.**
   - **그대로 쓰는 것**: `UAttributeSet` + `ATTRIBUTE_ACCESSORS_BASIC`(엔진 `AttributeSet.h:466`, UE 5.7 실측 — `PROPERTY_GETTER`/`VALUE_GETTER`/`SETTER`/`INITTER` 4종 묶음) · Push Model `DOREPLIFETIME_WITH_PARAMS_FAST` · `OnRep_*` 코스메틱 브로드캐스트(U20 `OnRep_bDead → OnDeathCosmetic` 패턴) · `UFPSRPickupSubsystem`/`AFPSRXPPickup` 픽업 패턴.
   - **덮는 것 = 시간축.** 엔진의 시간 기제(`FTimerManager` 기반 duration/periodic GE, `WaitDelay` AbilityTask, `World->GetTimeSeconds()`)를 **재생에 쓰지 않는다.** 근거: `Enemy.md` §2-6이 엘리트 ASC에 대해 못박은 것과 **같은 이유** — §2-2 전역 프리즈는 `TimeDilation`이 아니라 **상태 게이트**인데 엔진은 이들을 월드 타이머로 돌린다(엔진 `GameplayEffect.cpp:4409` duration · `:4431` period `bLoop=true`). ⚠️ 그 런타임 가드(`GameplayEffectApplicationQueries`)는 **엘리트 ASC에만** 걸려 있고 **플레이어 ASC(`Mixed`)에는 없다** — 즉 플레이어 실드를 periodic GE나 생 타이머로 재생시키면 4인 협동에서 한 명이 카드 고르는 30초 동안 **전원 실드가 공짜로 충전된다.** 대체 = **프리즈-멈춤 전투 시계**(§5-1). `RunClock`은 못 쓴다 — 보스 진입 시 핀되고(`FPSRRunDirectorSubsystem.cpp:405`) `TimeScale`이 곱해지는(`:385`) 생존/HUD 시계다.
3. **프로젝트 제약과의 정합.** 데미지 종착지가 정확히 2곳(`UFPSREnemyHealthComponent::ApplyDamage` / `AFPSRCharacter::ApplyContactDamage`)이고 **우회 호출자 0건**(2026-09-01 전수 grep)이라 수술 부위가 닫혀 있다. 공유 규칙을 **상태 없는 순수 함수 하나**로 두면 두 저장소가 서로를 몰라도 같은 답을 내고, `FGameplayTag DamageType` 자리를 `FFPSRDamageSpec` 구조체로 바꾸면 **자매 행 2개(경량 적 상태이상 · 범용 상태이상)가 시그니처를 다시 안 건드리고** 필드만 추가하면 된다. 다음 단계 확장(방향성 아머 M4, 원소 저항 D3)은 각각 §5-1의 `FMitigation` 필드 하나와 프로파일 엔트리 하나로 들어온다.

---

## 4. 파일 목록

| 경로 | 신규/수정 | 한 줄 설명 |
|---|---|---|
| `Public/Combat/FPSRVitals.h` | **신규** | 공유 규칙 = 순수 함수 네임스페이스(상태·복제 없음) + `FFPSRDamageSpec` |
| `Private/Combat/FPSRVitals.cpp` | **신규** | 2층 배분·계수·이월·재생 공식 |
| `Public/Combat/FPSRVitalsProfile.h` | **신규** | `UFPSRVitalsProfileDataAsset` + `FFPSRVitalsDeckModifier` + `FFPSRResolvedVitals` |
| `Private/Combat/FPSRVitalsProfile.cpp` | **신규** | 계수 조회 · 덱 합성 · `IsDataValid` |
| `Public/Combat/FPSRCombatStatics.h` | 수정 | `FDamageResult` 확장 · `FFPSRDamageSpec` 인자화 · `ResolveHitMarker` 신설 |
| `Private/Combat/FPSRCombatStatics.cpp` | 수정 | `DamageDealt` 재정의 · 실드파손 전파 · 마커 집계 일원화 |
| `Public/Enemy/FPSREnemyHealthComponent.h` | 수정 | `Shield`/`MaxShield` 복제 + 해석값 5 + 서버전용 2 + `InitializeVitals` |
| `Private/Enemy/FPSREnemyHealthComponent.cpp` | 수정 | 지연재생 + 2층 적용 + `OnRep_Shield` 코스메틱 |
| `Public/Enemy/FPSREnemyBase.h` | 수정 | `VitalsProfile` 저작 필드 1개 |
| `Public/Enemy/FPSREnemyRosterDataAsset.h` | 수정 | 덱 배수 `VitalsModifier` 1개 |
| `Private/Enemy/FPSREnemySpawnSubsystem.cpp` | 수정 | 스폰 시 프로파일×덱 해석 → `InitializeVitals` |
| `Public/AbilitySystem/Attributes/FPSRHealthSet.h` | 수정 | 어트리뷰트 4개 추가 |
| `Private/AbilitySystem/Attributes/FPSRHealthSet.cpp` | 수정 | 클램프·`OnRep_Shield`·`MaxShield` 증가분 즉시 충전 |
| `Public/Hero/FPSRCharacter.h` | 수정 | 서버전용 재생 상태 2 + 드라이버 |
| `Private/Hero/FPSRCharacter.cpp` | 수정 | `ApplyContactDamage` 2층화 + Tick 재생 드라이버 |
| `Public/Core/FPSRGameState.h` | 수정 | **프리즈-멈춤 전투 시계**(틱 0) |
| `Private/Core/FPSRGameState.cpp` | 수정 | `SetRunPaused` 엣지에서 동결시간 누적 |
| `Public/Hero/FPSRFeedbackTypes.h` | 수정 | `EFPSRHitMarkerType::ShieldBreak` **말미 추가** |
| `Public/Weapon/FPSRWeaponTypes.h` | 수정 | `ShieldDamageMultiplier` + `EFPSRWeaponStat` 축 1개 |
| `Private/Weapon/FPSRWeaponInstance.cpp` | 수정 | `RecomputeResolved` switch case 1개 |
| `Public/Pickup/FPSRHealthPickup.h` | **신규** | 맵 배치형 힐팩(`AFPSRXPPickup` 형제) |
| `Private/Pickup/FPSRHealthPickup.cpp` | **신규** | 수집·회복·전투시계 기반 리스폰 |
| `Private/Run/Mission/FPSRMission_CarryNoHit.cpp` | 수정 | 피격 판정을 "실드 포함 총 피해"로 |
| `Private/AbilitySystem/Abilities/FPSRGA_WeaponFire_Hitscan.cpp` | 수정 | 마커 집계 일원화 + 실드파손 전달 |
| `Private/AbilitySystem/Abilities/FPSRGA_WeaponFire_ChargeLaser.cpp` | 수정 | 〃 |
| `Private/AbilitySystem/Abilities/FPSRGA_WeaponMelee.cpp` | 수정 | 〃 |
| `Private/Weapon/FPSRProjectile.cpp` | 수정 | 〃 |
| `Config/DefaultGameplayTags.ini` | 수정 | `Message.Player.ShieldBroken` 1줄 |

---

## 5. 인터페이스 선언 (헤더 스케치)

### 5-1. `FPSRVitals` — 공유 규칙 (상태 0 · 복제 0)

```cpp
// Public/Combat/FPSRVitals.h
#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

/** 한 타격의 "무엇으로 때리는가". U18a 가 넣은 trailing `FGameplayTag DamageType` 자리를 대체한다 —
 *  자매 행(경량 적 상태이상 · 범용 상태이상)이 필드만 추가하면 되도록 구조체로 연다.
 *  전 필드 기본값 = 현행 거동(빈 태그 = Physical, 대실드 배수 1.0). USTRUCT 아님 —
 *  복제·BP 노출 대상이 아니고(서버 내부 전달값) UHT 비용을 살 이유가 없다. */
struct FFPSRDamageSpec
{
    /** 빈 = Physical. `DamageType.*`(DefaultGameplayTags.ini:29-33). 층별 방어계수 조회 키. */
    FGameplayTag DamageType;

    /** 대실드 배수(관통 메리트, 사용자 결정 2026-09-01). 1 = 평범 · >1 = 실드에 강함 ·
     *  0 = 실드를 아예 못 깎음(전량이 체력으로 이월 = "실드 무시"도 데이터로 표현된다). */
    float ShieldDamageMultiplier = 1.0f;
};

namespace FPSRVitals
{
    /** 층 하나가 얼마나 잘 버티는가. 값이 작을수록 단단하다(받는 데미지 배수).
     *  ⚠️ 이름 주의 — "Defense" 지만 뺄셈 방어력이 아니라 **곱셈 배수**다. 0.5 = 절반만 받음. */
    struct FMitigation
    {
        float ShieldDefense  = 1.0f;   // 프로파일의 DamageType 별 값
        float HealthDefense  = 1.0f;
        /** M4 방향성 아머 DR [0,1). 이 유닛은 항상 0을 넘긴다(자리만 확보).
         *  결합 규칙 = 아래 `MaxTotalReduction` — 곱해서 무적이 되는 것을 산술로 막는다. */
        float DirectionalArmorDR = 0.0f;
        /** 🔴 불변식: 어떤 조합으로도 한 타격의 총 감쇠가 이 값을 넘지 않는다.
         *  근거 = `Enemy.md` §2-6 방패 아키타입이 못박은 "하드블록(0뎀) 금지" —
         *  DamageDealt=0 이면 히트마커·흡혈·킬크레딧이 전부 침묵한다. */
        float MaxTotalReduction = 0.95f;
    };

    /** 2층 풀의 현재 값. 호출자가 소유한다(이 네임스페이스는 아무것도 저장하지 않는다). */
    struct FPool
    {
        float Shield = 0.0f;
        float MaxShield = 0.0f;
        float Health = 0.0f;
        float MaxHealth = 0.0f;
    };

    struct FResult
    {
        float ShieldSpent = 0.0f;   // 실드에서 실제로 깎인 양
        float HealthSpent = 0.0f;   // 체력에서 실제로 깎인 양
        bool  bShieldBroke = false; // 이번 타격이 실드를 (>0 → 0) 으로 만들었다
        bool  bLethal = false;      // 체력이 0 에 도달했다

        /** 히트마커·흡혈·디렉터·미션이 읽는 "진짜 입힌 피해". 🔴 회귀함정 1의 새 정의. */
        float TotalSpent() const { return ShieldSpent + HealthSpent; }
    };

    /** 순수 함수. InOutPool 을 제자리에서 깎고 결과를 돌려준다. 권위·복제·시간 개념 없음.
     *  Incoming <= 0 이면 아무것도 안 하고 빈 결과. */
    FPSROGUELITE_API FResult ApplyDamage(FPool& InOutPool, float Incoming,
        const FFPSRDamageSpec& Spec, const FMitigation& Mit);

    /** 지연재생 공식 — **멱등**하다(같은 인자로 몇 번 불러도 같은 값).
     *  그래서 호출자는 "마지막 피격 시점의 실드값 + 그때의 시각" 두 개만 들고 있으면 되고,
     *  적은 이걸 피격 시점에만 부르고(무틱) 플레이어는 HUD 를 위해 주기적으로 부른다 — **공식은 하나**.
     *  @param ShieldAtLastDamage  마지막 피격 직후의 실드값 (0 이면 완파였다는 뜻 = 긴 지연 적용)
     *  @param ElapsedSinceDamage  프리즈-멈춤 전투시계 기준 경과 초 (§5-6) */
    FPSROGUELITE_API float ComputeRegeneratedShield(float ShieldAtLastDamage, float MaxShield,
        float ElapsedSinceDamage, float RegenPerSecond,
        float PartialDelaySeconds, float BrokenDelaySeconds);

    /** 요구 4의 "파손 여부" — 저장하지 않고 파생한다(클라도 복제된 Shield/MaxShield 로 같은 답을 낸다). */
    FORCEINLINE bool IsShieldBroken(float Shield, float MaxShield)
    {
        return MaxShield > 0.0f && Shield <= 0.0f;
    }
}
```

**`ApplyDamage` 산술 (구현자가 그대로 따를 것):**

```
ArmorKeep   = 1 - clamp(DirectionalArmorDR, 0, 1)
MinKeep     = 1 - clamp(MaxTotalReduction, 0, 1)          // 무적 방지 하한

ShieldKeep  = max(ShieldDamageMultiplier * ShieldDefense * ArmorKeep, MinKeep)  // 단, Spec.ShieldDamageMultiplier == 0 이면 0 그대로 (실드 무시는 의도된 데이터)
HealthKeep  = max(HealthDefense * ArmorKeep, MinKeep)

WantShield  = Incoming * ShieldKeep
ShieldSpent = min(Pool.Shield, WantShield)
Consumed    = (WantShield > 0) ? (ShieldSpent / WantShield) : 0     // 원본 데미지 중 실드가 먹은 비율
Overflow    = Incoming * (1 - Consumed)                             // ← 이월. 한 방이 클수록 여기가 커진다
HealthSpent = min(Pool.Health, Overflow * HealthKeep)

Pool.Shield -= ShieldSpent;  Pool.Health -= HealthSpent
bShieldBroke = (진입 시 Shield > 0) && (Pool.Shield <= 0)
bLethal      = (Pool.Health <= 0)
```

> ⚠️ `ShieldKeep` 의 `MinKeep` 하한은 **`ShieldDamageMultiplier == 0`(실드 무시) 일 때는 적용하지 않는다.** 하한의 목적은 "완화 중첩으로 무적이 되는 것"을 막는 것이지, 설계자가 의도적으로 연 우회로를 막는 게 아니다. 그리고 이 경우 데미지는 전량 체력으로 가므로 `TotalSpent() > 0` 은 여전히 성립한다.
> 🔴 **불변식 V1**: `Incoming > 0` 이고 `(Shield > 0 || Health > 0)` 이면 `Result.TotalSpent() > 0`. 어떤 데이터 조합으로도 무적을 만들 수 없다.
> 🔴 **불변식 V2**: `MaxShield == 0` → `ShieldSpent == 0` 이고 `Overflow == Incoming` (실드 없는 개체는 현행과 산술적으로 동일). **요구 1의 유일한 구현 수단.**

### 5-2. 저작 데이터 — `UFPSRVitalsProfileDataAsset`

```cpp
// Public/Combat/FPSRVitalsProfile.h

/** 데미지 타입 하나에 대한 층별 방어 배수. 빈 DamageType = 그 프로파일의 기본값(미지정 타입 전부). */
USTRUCT(BlueprintType)
struct FFPSRVitalsDefenseEntry
{
    GENERATED_BODY()

    /** 빈 = 기본 엔트리. `DamageType.*` 만 유효(IsDataValid 가 검사). */
    UPROPERTY(EditAnywhere, meta = (Categories = "DamageType"))
    FGameplayTag DamageType;

    UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", UIMax = "3.0"))
    float ShieldDefense = 1.0f;

    UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", UIMax = "3.0"))
    float HealthDefense = 1.0f;
};

/** 한 개체 종류의 생존 규격. 같은 몬스터(아키타입)끼리 공유한다 — 요구 2.
 *  ⚠️ 적 `MaxHealth` 는 사용자 결정(2026-09-01)으로 BP 기본값에서 **여기로 전면 이관**된다. */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRVitalsProfileDataAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category = "Vitals", meta = (ClampMin = "1.0"))
    float MaxHealth = 50.0f;

    /** 0 = 이 개체는 실드가 없다(요구 1). 아래 재생 필드는 그때 전부 무의미해지므로 숨긴다. */
    UPROPERTY(EditAnywhere, Category = "Vitals", meta = (ClampMin = "0.0"))
    float MaxShield = 0.0f;

    // [[dataasset-conditional-field-visibility]] — 실드 없는 프로파일에서 재생 3필드를 숨긴다
    UPROPERTY(EditAnywhere, Category = "Vitals|Shield Regen",
        meta = (ClampMin = "0.0", EditCondition = "MaxShield > 0", EditConditionHides))
    float ShieldRegenPerSecond = 0.0f;

    /** 부분 손상 후 재생까지의 정지 시간(전투시계 기준). */
    UPROPERTY(EditAnywhere, Category = "Vitals|Shield Regen",
        meta = (ClampMin = "0.0", EditCondition = "MaxShield > 0", EditConditionHides))
    float ShieldRegenDelaySeconds = 3.0f;

    /** 완파(0 도달) 후의 더 긴 정지 시간 — 헤일로식 이원 지연(사용자 결정 2026-09-01). */
    UPROPERTY(EditAnywhere, Category = "Vitals|Shield Regen",
        meta = (ClampMin = "0.0", EditCondition = "MaxShield > 0", EditConditionHides))
    float ShieldBrokenRegenDelaySeconds = 6.0f;

    /** 데미지 타입별 층 계수. 비어 있으면 전부 1.0. 빈 태그 엔트리 = 기본값. */
    UPROPERTY(EditAnywhere, Category = "Vitals|Defense")
    TArray<FFPSRVitalsDefenseEntry> DefenseByDamageType;

    /** 서버. DamageType 에 맞는 층 계수를 채운다(정확일치 → 기본엔트리 → 1.0 순). */
    void ResolveDefense(const FGameplayTag& DamageType, float& OutShieldDefense, float& OutHealthDefense) const;

#if WITH_EDITOR
    /** 중복 태그 · `DamageType.*` 아닌 태그 · MaxShield>0 인데 재생속도 0(영구 파손 = 대개 저작 실수) ·
     *  엔트리 8개 초과(피격당 선형 스캔이라 상한을 경고) 를 잡는다. */
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

/** ADR 0014 난이도 등급별 「덱」 층 배수 — 사용자 결정(2026-09-01) "같은 덱은 공유".
 *  🔴 덱은 **양(量)만 스케일하고 계수는 안 건드린다.** 계수를 덱에도 두면 같은 몬스터가 덱에 따라
 *  다른 속성 저항을 갖게 되어 플레이어가 학습할 수 없다(뱀서류 리텐션의 핵심은 학습 가능성).
 *  ⚠️ ADR 0014 불변식 1("적 「수」의 단일 소유자는 디렉터")과 충돌 없음 — 이건 수가 아니라 개체 강도다. */
USTRUCT(BlueprintType)
struct FFPSRVitalsDeckModifier
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, meta = (ClampMin = "0.01", UIMax = "5.0"))
    float MaxHealthScale = 1.0f;

    UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", UIMax = "5.0"))
    float MaxShieldScale = 1.0f;

    UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", UIMax = "5.0"))
    float ShieldRegenScale = 1.0f;
};

/** 프로파일 × 덱을 스폰 시 1회 접어 만든 값. 컴포넌트가 이걸 그대로 굽는다(런타임 재조회 없음). */
struct FFPSRResolvedVitals
{
    float MaxHealth = 50.0f;
    float MaxShield = 0.0f;
    float ShieldRegenPerSecond = 0.0f;
    float ShieldRegenDelaySeconds = 3.0f;
    float ShieldBrokenRegenDelaySeconds = 6.0f;
    /** 계수 조회용. 덱은 계수를 안 건드리므로 프로파일 포인터를 그대로 들고 간다(액터당 8바이트). */
    TObjectPtr<const UFPSRVitalsProfileDataAsset> Profile = nullptr;

    /** 프로파일 null = 실드 없는 현행 거동(호출자가 넘긴 FallbackMaxHealth 사용). 무회귀 경로. */
    static FFPSRResolvedVitals Resolve(const UFPSRVitalsProfileDataAsset* Profile,
        const FFPSRVitalsDeckModifier& Deck, float FallbackMaxHealth);
};
```

### 5-3. 적 — `UFPSREnemyHealthComponent` (신규 컴포넌트 0)

```cpp
public:
    /** 서버. 실드 2층을 통과시킨다. 반환 = 이 타격의 실제 결과(호출자가 마커·흡혈·디렉터에 쓴다).
     *  ⚠️ 시그니처 변경: 종전 `FGameplayTag DamageType` → `const FFPSRDamageSpec&`. 반환 void → FResult. */
    FPSRVitals::FResult ApplyDamage(float DamageAmount, AActor* DamageInstigator,
        const FFPSRDamageSpec& Spec = FFPSRDamageSpec());

    /** 서버. 프로파일×덱 해석값을 굽고 풀을 가득 채운다. 스폰 서브시스템이 Activate 직후 호출. */
    void InitializeVitals(const FFPSRResolvedVitals& Resolved);

    /** 서버. 종전 API 유지 — 실드 없는 개체(보스·구조물)를 위한 얇은 래퍼.
     *  🔴 회귀함정 7: MaxShield 를 **명시적으로 0** 으로 두어 AFPSRDestructible 이 실드를 딸려받지 않게 한다. */
    void InitializeMaxHealth(float NewMaxHealth);

    UFUNCTION(BlueprintPure, Category = "FPSR|Enemy") float GetShield() const { return Shield; }
    UFUNCTION(BlueprintPure, Category = "FPSR|Enemy") float GetMaxShield() const { return MaxShield; }

    /** 요구 4 · 자매 행("실드 있으면 상태이상 저항")이 쓰는 질의. 서버/클라 양쪽에서 같은 답. */
    UFUNCTION(BlueprintPure, Category = "FPSR|Enemy")
    bool IsShieldBroken() const { return FPSRVitals::IsShieldBroken(Shield, MaxShield); }

    /** 서버. 지연재생을 지금 시점으로 확정한다. ApplyDamage 진입부가 자동으로 부르므로
     *  게임플레이 코드는 부를 필요가 없다 — 외부 질의(HUD 디버그, 상태이상 판정)가 정확한 값을
     *  원할 때만. 멱등하다. */
    void CatchUpShieldRegen();

    /** 클라 코스메틱: 실드가 (>0 → 0) 으로 복제돼 내려온 엣지에서 1회. U20 OnDeathCosmetic 과 같은 패턴.
     *  🔴 신규 복제 0 — 이미 복제되는 Shield 의 RepNotify 일 뿐이다. */
    UPROPERTY(BlueprintAssignable, Category = "FPSR|Enemy")
    FFPSREnemyShieldBrokenSignature OnShieldBrokenCosmetic;

protected:
    UFUNCTION() void OnRep_Shield();

    /** 클라 실드바 퍼센트용(MaxHealth 가 B12 에서 복제되는 것과 같은 이유). OnRep_Shield 공유. */
    UPROPERTY(ReplicatedUsing = OnRep_Shield) float MaxShield = 0.0f;
    UPROPERTY(ReplicatedUsing = OnRep_Shield) float Shield = 0.0f;

    // --- 스폰 시 1회 구워지는 해석값 (서버 전용, 비복제) ---
    float ShieldRegenPerSecond = 0.0f;
    float ShieldRegenDelaySeconds = 3.0f;
    float ShieldBrokenRegenDelaySeconds = 6.0f;
    UPROPERTY() TObjectPtr<const UFPSRVitalsProfileDataAsset> VitalsProfile = nullptr;

    // --- 지연재생 상태 (서버 전용, 비복제) — 이 둘만 있으면 공식이 멱등해진다 ---
    float ShieldAtLastDamage = 0.0f;
    float LastDamageCombatTime = -1.0e9f;

    /** 클라가 파손 엣지를 검출하기 위한 직전값(비복제, 클라 로컬). */
    float LastKnownShieldForCosmetic = 0.0f;
```

> ⚠️ `AFPSREnemyBase` 에 추가하는 저작 필드는 `VitalsProfile` **하나뿐**이다(`EditDefaultsOnly`). [[cpp-uproperty-name-collides-with-bp]] — 콘텐츠 BP 컴포넌트 이름과 충돌하면 C++ 빌드는 통과하고 `FPSRoguelite.Enemy.BlueprintParent` 자동화만 잡는다. **그 테스트를 반드시 돌린다**(§12-3).

### 5-4. 플레이어 — `UFPSRHealthSet` 어트리뷰트 4개

```cpp
    ATTRIBUTE_ACCESSORS_BASIC(UFPSRHealthSet, Shield)
    ATTRIBUTE_ACCESSORS_BASIC(UFPSRHealthSet, MaxShield)
    ATTRIBUTE_ACCESSORS_BASIC(UFPSRHealthSet, ShieldDefense)   // 곱셈 배수. 1 = 기본
    ATTRIBUTE_ACCESSORS_BASIC(UFPSRHealthSet, HealthDefense)

    /** 실드가 (>0 → 0) 이 된 순간 1회(서버). 요구 5의 플레이어 경고 원천. */
    mutable FFPSRShieldBrokenSignature OnShieldBroken;

private:
    UPROPERTY(BlueprintReadOnly, Category="Health", ReplicatedUsing=OnRep_Shield,        meta=(AllowPrivateAccess=true)) FGameplayAttributeData Shield;
    UPROPERTY(BlueprintReadOnly, Category="Health", ReplicatedUsing=OnRep_MaxShield,     meta=(AllowPrivateAccess=true)) FGameplayAttributeData MaxShield;
    UPROPERTY(BlueprintReadOnly, Category="Health", ReplicatedUsing=OnRep_ShieldDefense, meta=(AllowPrivateAccess=true)) FGameplayAttributeData ShieldDefense;
    UPROPERTY(BlueprintReadOnly, Category="Health", ReplicatedUsing=OnRep_HealthDefense, meta=(AllowPrivateAccess=true)) FGameplayAttributeData HealthDefense;
```

`PreAttributeChange`/`PreAttributeBaseChange`: `Shield` 를 `[0, MaxShield]`, `MaxShield`/`ShieldDefense`/`HealthDefense` 를 `>= 0` 으로 클램프(기존 `ClampAttribute` 확장).

`PostAttributeChange` 추가 규칙 **2개**:
- **`MaxShield` 증가 → `Shield` 도 같은 양만큼 증가.** §2-13 의 `MaxHealth` 규칙과 **대칭**(권위 전용 가드도 동일 — 클라 이중적용 방지).
- **`MaxShield` 가 0 으로 떨어지면 `Shield` 도 0.** "실드 포기하고 체력↑" 카드가 실드를 남기지 않게. (기존 `Shield` 클램프가 다음 변경 때 처리하지만, 그때까지 HUD 에 유령 실드가 남는다.)

> **"실드 포기" 카드 = 신규 효과 클래스 0개.** `UCardEffect_CharacterGE` + `GE_Card_ForgoShield`(`MaxShield` Override 0 · `MaxHealth` Additive +N) 하나면 된다. §2-3-8 컨벤션 쿡북의 "기존 Attribute 범위 내 새 카드 = GE + DataAsset (코드 0)" 경로 그대로.

### 5-5. `AFPSRCharacter` — 2층 적용 + 재생 드라이버

```cpp
public:
    /** 서버. FFPSRDamageSpec 로 시그니처 변경(종전 trailing FGameplayTag). 반환 = 이 타격의 실제 결과. */
    FPSRVitals::FResult ApplyContactDamage(float DamageAmount, AActor* DamageInstigator,
        const FFPSRDamageSpec& Spec = FFPSRDamageSpec());

    /** 서버. 체력만 회복한다(실드는 스스로 찬다). 힐팩·카드·부활이 쓰는 단일 진입점.
     *  DBNO/Dead 는 no-op(부활은 자기 경로로 체력을 세팅한다). MaxHealth 로 클램프. */
    void ApplyHealing(float Amount, AActor* HealInstigator);

protected:
    /** 서버 전용 재생 상태. 적 컴포넌트와 **같은 두 값** — 공식이 하나이기 때문. */
    float ShieldAtLastDamage = 0.0f;
    float LastDamageCombatTime = -1.0e9f;
    /** HUD 바가 차오르는 것을 보이게 하는 드라이버의 누산기(서버). */
    float ShieldRegenDriverAccum = 0.0f;

    /** 서버 재생 드라이버 갱신 빈도. 적은 무틱 지연계산이지만 플레이어는 HUD 바가 실시간으로
     *  차야 하므로 주기적으로 같은 공식을 호출해 어트리뷰트에 밀어넣는다(플레이어는 최대 4명). */
    UPROPERTY(EditDefaultsOnly, Category = "FPSR|Vitals", meta = (ClampMin = "1.0", ClampMax = "60.0"))
    float ShieldRegenUpdateHz = 10.0f;
```

`Tick`(서버 권위 구간)에서:
```
ShieldRegenDriverAccum += DeltaSeconds
if (ShieldRegenDriverAccum >= 1/ShieldRegenUpdateHz && IsAlive() && MaxShield > 0 && Shield < MaxShield)
    ShieldRegenDriverAccum = 0
    New = FPSRVitals::ComputeRegeneratedShield(ShieldAtLastDamage, MaxShield,
              CombatNow - LastDamageCombatTime, RegenRate, PartialDelay, BrokenDelay)
    if (|New - Shield| > KINDA_SMALL_NUMBER) SetShield(New)
```
> **프리즈 게이트가 따로 필요 없다** — `CombatNow` 가 프리즈 중 안 흐르므로 `New == Shield` 가 되어 쓰기 자체가 안 일어난다. 게이트를 코드로 또 두면 진실이 둘이 된다.
> 🔴 **회귀함정 6**: `IsAlive()` 가 DBNO 를 false 로 본다(§2-13) → **DBNO 중 실드 재생 정지**. 부활(`PerformRevive`)은 체력 50% 세팅 뒤 `ShieldAtLastDamage = 0; LastDamageCombatTime = CombatNow` 로 **완파 상태에서 시작**한다(부활 직후 `PostReviveInvuln` 5초 동안 완파지연 6초가 거의 흘러, 무적이 풀린 직후 실드가 차기 시작한다).

플레이어 재생 수치의 저작 자리 = **`UFPSRHealthSet` 기본값이 아니라 캐릭터 BP 의 `EditDefaultsOnly`**:
```cpp
    /** 플레이어 기준 프로파일 1개(사용자 결정 2026-09-01 — 캐릭터 종류가 1개라 전원 공유,
     *  개인차는 전부 카드가 만든다). 적과 **같은 DataAsset 타입**을 쓴다 = 저작 UI·검증기 공유. */
    UPROPERTY(EditDefaultsOnly, Category = "FPSR|Vitals")
    TObjectPtr<UFPSRVitalsProfileDataAsset> VitalsProfile;
```
`PossessedBy`/`InitAbilityActorInfo` 시점에 프로파일 → 어트리뷰트 초기값(`SetMaxHealth`/`SetMaxShield`/…) + 재생 3값을 캐릭터 멤버로 굽는다. 프로파일 null = 현행 기본값(`MaxShield = 0` → 실드 없음, 무회귀).

### 5-6. `AFPSRGameState` — 프리즈-멈춤 전투 시계 (틱 0)

```cpp
public:
    /** 서버 전용. §2-2 전역 프리즈 동안 **멈추는** 단조 증가 시계(초).
     *  🔴 왜 새로 만드나 — `World->GetTimeSeconds()` 는 프리즈 중에도 흐르고(4인 협동에서 한 명이
     *  카드 고르는 사이 전원/전 적 실드가 공짜로 충전된다), `UFPSRRunDirectorSubsystem::RunClock` 은
     *  보스 진입 시 핀되고(`FPSRRunDirectorSubsystem.cpp:405`) TimeScale 이 곱해지는(`:385`)
     *  생존/HUD 시계라 재생 시간축으로 못 쓴다.
     *  🔴 왜 틱이 없나 — `SetRunPaused` 가 **권위 게이트 + 엣지 가드가 걸린 단일 전이 지점**이라
     *  거기서 동결 구간만 누적하면 된다. 프레임당 비용 0.
     *  클라에서는 0 을 반환한다(바이탈 계산은 전부 서버 권위 — 클라가 이 값을 쓸 일이 없다). */
    float GetCombatClockSeconds() const;

private:
    /** 지금까지 프리즈로 흘려보낸 총 시간(서버 전용, 비복제). */
    float AccumulatedFrozenSeconds = 0.0f;
    /** 현재 프리즈가 시작된 월드 시각(bRunPaused 일 때만 유효). */
    float FreezeStartedAtWorldTime = 0.0f;
```

`SetRunPaused` 의 엣지 가드 **뒤**(값을 바꾸기 전후)에서:
```
if (bPaused) FreezeStartedAtWorldTime = World->GetTimeSeconds();
else         AccumulatedFrozenSeconds += World->GetTimeSeconds() - FreezeStartedAtWorldTime;
```
`GetCombatClockSeconds()`:
```
if (!HasAuthority()) return 0.f
Now = World->GetTimeSeconds()
return Now - AccumulatedFrozenSeconds - (bRunPaused ? (Now - FreezeStartedAtWorldTime) : 0.f)
```
> `EndRunFreeze` 는 `SetRunPaused(true)` 를 경유하므로 런 종료 후 시계가 영구히 멈춘다 — 결과 화면 뒤에서 실드가 차지 않는다. 의도된 거동.

### 5-7. `FPSRCombatStatics` — 데미지 결과와 마커 일원화

```cpp
    struct FDamageResult
    {
        bool  bApplied = false;
        bool  bKilled = false;
        bool  bWasEnemy = false;
        /** 🔴 회귀함정 1 — 재정의: 종전 `HealthBefore - HealthAfter` → **실드소진 + 체력소진**.
         *  실드가 흡수해도 0 이 되지 않는다(히트마커·흡혈·관통 판정이 침묵하지 않는다). */
        float DamageDealt = 0.0f;
        /** 이번 타격이 대상의 실드를 깼다(요구 5). */
        bool  bShieldBroke = false;
    };

    FPSROGUELITE_API FDamageResult ApplyDamage(AActor* Target, float FinalDamage, AActor* Instigator,
        const FFPSRDamageSpec& Spec = FFPSRDamageSpec());

    FPSROGUELITE_API FExplosionResult ApplyExplosion(UWorld* World, const FVector& Center, float Radius,
        float Damage, float CritChance, float CritMultiplier, AActor* Instigator, bool bAllowSelf,
        float KnockbackStrength, const FFPSRDamageSpec& Spec = FFPSRDamageSpec());

    /** 히트마커 집계 **단일 소유자**. 종전에는 같은 3중 삼항 사슬이 5곳(Hitscan:411 / ChargeLaser /
     *  Melee / Projectile / NotifyHitMarker)에 복붙돼 있었고, ShieldBreak 를 넣으려면 5곳을 똑같이
     *  고쳐야 했다 — `CombatWeaponCard.md` §2-3-5 가 경고한 "5경로 분산" 그 자체.
     *  우선순위 = **Kill > ShieldBreak > Weak > Crit > Hit**. */
    FPSROGUELITE_API EFPSRHitMarkerType ResolveHitMarker(bool bKill, bool bShieldBreak, bool bWeak, bool bCrit);
```

`EFPSRHitMarkerType` 은 **말미에 `ShieldBreak` 를 추가**한다(중간 삽입 금지 — `uint8` 값이 밀려 저작된 BP/에셋의 저장값이 조용히 다른 의미가 된다):
```cpp
enum class EFPSRHitMarkerType : uint8
{
    Hit, Crit, Weak, Kill,
    ShieldBreak UMETA(DisplayName = "Shield Break")   // ← 말미. 우선순위는 ResolveHitMarker 가 정한다
};
```

### 5-8. 힐팩 — `AFPSRHealthPickup`

```cpp
/** 맵 배치형 회복 픽업(요구 3 · 🔴 회귀함정 5 "하강 나선" 대응). AFPSRXPPickup 의 형제 —
 *  서버권위 · 비-GAS · 자석 없음(체력 회복은 의도적으로 찾아가야 하는 자원). */
UCLASS()
class FPSROGUELITE_API AFPSRHealthPickup : public AActor
{
public:
    AFPSRHealthPickup();
protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;   // 서버만: 반경 검사 + 리스폰 타이머

    /** 회복량. 절대값 vs 최대체력 비율은 밸런싱 손잡이가 달라 둘 다 둔다(0 이면 그 축 미사용). */
    UPROPERTY(EditAnywhere, Category="FPSR|Pickup", meta=(ClampMin="0.0")) float HealFlat = 0.0f;
    UPROPERTY(EditAnywhere, Category="FPSR|Pickup", meta=(ClampMin="0.0", ClampMax="1.0")) float HealMaxHealthFraction = 0.25f;

    UPROPERTY(EditAnywhere, Category="FPSR|Pickup", meta=(ClampMin="0.0")) float CollectRadius = 120.0f;
    /** 0 = 1회용(수집 후 소멸). >0 = 그만큼 지난 뒤 재활성 — **전투시계 기준**(프리즈 중 안 흐른다). */
    UPROPERTY(EditAnywhere, Category="FPSR|Pickup", meta=(ClampMin="0.0")) float RespawnSeconds = 45.0f;
    /** 이미 체력이 가득한 플레이어에게 소모되지 않게(4인 협동에서 지나가던 만렙이 삼키는 것 방지). */
    UPROPERTY(EditAnywhere, Category="FPSR|Pickup") bool bRequireMissingHealth = true;

    UPROPERTY(ReplicatedUsing=OnRep_Available) bool bAvailable = true;
    UFUNCTION() void OnRep_Available();   // 클라: 메시/이펙트 표시 토글
};
```
> **수집자만 회복한다**(파티 전체 아님) — 4인 협동에서 "다친 사람이 가서 먹는다"가 협동 판단을 만든다. 전체 회복이면 아무나 밟는 자원이 되어 위치 선택이 무의미해진다.
> DBNO/Dead 플레이어는 수집 불가(`IsAlive()` 게이트).

---

## 6. 함수별 계약

| 함수 | 권위 | 호출자 | 전제조건 | 실패 시 동작 |
|---|---|---|---|---|
| `FPSRVitals::ApplyDamage` | 무관(순수) | 두 저장소 | 없음 | `Incoming<=0` → 빈 `FResult`, 풀 무변 |
| `FPSRVitals::ComputeRegeneratedShield` | 무관(순수·**멱등**) | 두 저장소 | `MaxShield>=0` | `MaxShield<=0` → 0 반환 |
| `UFPSREnemyHealthComponent::ApplyDamage` | 서버 전용 | `FPSRCombat::ApplyDamage` 단 1곳 | `Owner` 유효·`!bDead` | 조기 반환, 빈 `FResult` |
| `UFPSREnemyHealthComponent::InitializeVitals` | 서버 전용 | 스폰 서브시스템 `AcquireEnemy` | `MaxHealth>0` | 조기 반환(현행 값 유지) |
| `UFPSREnemyHealthComponent::CatchUpShieldRegen` | 서버 전용 | `ApplyDamage` 진입부 · 외부 질의 | — | 오프-권위 no-op |
| `AFPSRCharacter::ApplyContactDamage` | 서버 전용 | `FPSRCombat::ApplyDamage` · `FPSRProjectile:490` | 살아있음·i-frame/grace 통과 | 조기 반환, 빈 `FResult` |
| `AFPSRCharacter::ApplyHealing` | 서버 전용 | 힐팩 · (향후 카드) | `IsAlive()` | no-op |
| `AFPSRGameState::GetCombatClockSeconds` | 서버(클라 0) | 두 저장소 · 힐팩 | `GetWorld()` 유효 | 0 반환 |
| `FPSRCombat::ResolveHitMarker` | 무관(순수) | 5 데미지 경로 | — | 전부 false → `Hit` |
| `UFPSRVitalsProfileDataAsset::ResolveDefense` | 무관 | 두 저장소 | — | 미일치 → 1.0/1.0 |

**`FPSRCombat::ApplyDamage` 의 두 분기가 이제 대칭이 된다** — 종전에는 플레이어 분기가 `bApplied=true` 만 세우고 `DamageDealt` 를 0 으로 남겼다(`FPSRCombatStatics.cpp:190-194`). 그래서 **FF 흡혈이 이미 침묵하고 있었다.** 실드가 만든 문제가 아니라 기존 비대칭이며, `ApplyContactDamage` 가 `FResult` 를 반환하게 되는 이 유닛에서 **함께 해소된다**(`DamageDealt = Result.TotalSpent()`). 거동 변화 = FF 를 켠 세션에서 아군 오사에 흡혈이 발동하게 됨 — `Enemy.md` §2-10 의 "협동 카드 시임" 의도와 같은 방향이므로 회귀가 아니라 결함 수정으로 본다. **§13 에 명시하고 G2 에 올린다.**

---

## 7. 복제표 (§6-3 서버권위 + Push Model)

| 프로퍼티 / RPC | 종류 | Push Model | 조건 | 비고 |
|---|---|---|---|---|
| `UFPSREnemyHealthComponent::Shield` | `ReplicatedUsing=OnRep_Shield` | `MARK_PROPERTY_DIRTY` @ `ApplyDamage`·`InitializeVitals`·`ResetForReuse`·`CatchUpShieldRegen`(값 변화 시에만) | `COND_None` | `MaxShield==0` → 영원히 dirty 안 됨 = 대역 0 |
| `UFPSREnemyHealthComponent::MaxShield` | `ReplicatedUsing=OnRep_Shield` | `InitializeVitals` 1회 | `COND_None` | `MaxHealth` 가 B12 에서 복제되는 것과 같은 이유(클라 실드바 퍼센트) |
| `UFPSRHealthSet::Shield` / `MaxShield` / `ShieldDefense` / `HealthDefense` | GAS 어트리뷰트 `ReplicatedUsing` | GAS 소관 | `COND_None` | PlayerState ASC — 팀원 HUD 가 그대로 읽는다(신규 배선 0) |
| `AFPSRHealthPickup::bAvailable` | `ReplicatedUsing=OnRep_Available` | `MARK_PROPERTY_DIRTY` @ 수집·리스폰 | `COND_None` | 맵당 소수 |
| `AFPSRGameState::AccumulatedFrozenSeconds` | **비복제** | — | — | 서버 전용. 바이탈 계산은 전부 서버 권위 |
| 신규 RPC | **0개** | — | — | 요구 5 전부가 기존 복제의 `OnRep` 파생 |

> ⚠️ **패키지 빌드에서 Push Model 이 컴파일 아웃된다**([[push-model-off-in-packaged-build]]). 그 상태에서도 정합이 성립한다: 두 신규 프로퍼티는 `MARK_PROPERTY_DIRTY` **없이도** 엔진의 shadow-state 비교로 걸러지므로 값이 안 변하면 대역 0 이고, 늘어나는 것은 액터당 float 비교 2회뿐(300 마리 = 600 비교/복제주기). **따라서 복제 실측은 에디터에서만 유효하다** — 패키지 수치를 근거로 삼지 말 것.
> 🔴 **보드 행 ② 와의 관계**: 원 스코프("스웜 적 복제 프로퍼티를 늘리지 않는다")는 *구조물 전용 옵트인* 전제였다. 사용자 결정(2026-08-31)으로 **적도 실드를 갖는** 스코프가 되었으므로 3개 → **5개**가 된다. 위 대역 논거로 상쇄되지만 **전제가 바뀐 것이지 지켜진 것이 아니다** — G1 판단 대상.

---

## 8. 수명주기 · 소유권

- **적 생성**: 풀 취득(`AcquireEnemy`) → `Activate(Location)` → `HealthComponent->ResetForReuse()`(기존) → **`InitializeVitals(Resolved)`**. 순서 중요 — `ResetForReuse` 가 체력을 옛 `MaxHealth` 로 채운 뒤 `InitializeVitals` 가 새 규격으로 덮는다. 해석(`FFPSRResolvedVitals::Resolve`)은 **스폰 서브시스템이** 한다(덱을 아는 유일한 주체).
- **적 재사용**: `ResetForReuse` 가 `Shield = MaxShield`, `ShieldAtLastDamage = MaxShield`, `LastDamageCombatTime = -1e9` 로 되돌린다. 🔴 이월 스테이지 캐리(`ServerResetEliteForStageCarry`)는 **바이탈을 건드리지 않는다**(쌓아온 압박을 가져가는 것이 이월의 의도 — ADR 0013 과 같은 논리).
- **적 해제**: `Deactivate` — 바이탈은 아무것도 되돌릴 필요 없음(다음 `Activate` 가 전부 재설정). 델리게이트 `OnShieldBrokenCosmetic` 은 컴포넌트 소유라 액터와 함께 산다.
- **플레이어**: `InitAbilityActorInfo`(PlayerState ASC 준비 시점)에서 프로파일 → 어트리뷰트 초기값 + 재생 3값 굽기. 리스폰/런 리셋 시 재적용. **부활**은 §5-5 참조.
- **GC 소유**: `VitalsProfile` 은 `UPROPERTY(TObjectPtr<const ...>)` 로 잡는다(적 컴포넌트 · 캐릭터 · `FFPSRResolvedVitals` 는 스택 임시값이라 무관). 프로파일 자체는 적 BP CDO / 캐릭터 BP CDO 가 하드 레퍼런스로 살려둔다.
- **델리게이트 대칭**: `OnShieldBrokenCosmetic` 구독자 = 적 BP(코스메틱) 1개. `AFPSREnemyBase` 가 `BeginPlay` 에서 구독하고 `EndPlay` 에서 해제 — ❌ **per-enemy 바인딩은 금지**(§2-3-5 "적500 dispatch 예산"). 대신 **BP 가 자기 컴포넌트의 델리게이트에만 바인딩**한다(액터 내부, 전역 디스패치 아님) — `OnHealthChanged`/`OnDeathCosmetic` 과 정확히 같은 패턴.
- **초기 동기화**: 클라 실드바는 `OnRep_Shield` 구독만으로는 부족하다(위젯이 나중에 붙을 수 있다) → `BindHealthComponent` 가 현재 `Shield`/`MaxShield` 를 즉시 1회 반영한다(`UFPSREnemyHealthBarWidget` 의 기존 패턴 확장). [[umg-event-widget-initial-sync]]

---

## 9. 데이터드리븐 경계 (핵심원칙 2)

| 값 | 나가는 곳 | 기본값 | 비고 |
|---|---|---|---|
| 적 `MaxHealth` | **`UFPSRVitalsProfileDataAsset`** | 50 | 🔴 BP 기본값에서 **전면 이관**(사용자 결정) — §11-1 마이그레이션 |
| 적/플레이어 `MaxShield` | 〃 | 0 | 0 = 실드 없음 |
| 재생 속도 · 부분지연 · 완파지연 | 〃 | 0 / 3s / 6s | |
| 데미지 타입별 층 계수 | 〃 `DefenseByDamageType[]` | 전부 1.0 | 원소 저항(D3)이 여기로 들어온다 |
| 덱 배수 3종 | `UFPSREnemyRosterDataAsset::VitalsModifier` | 전부 1.0 | ADR 0014 등급별 덱 |
| 무기 대실드 계수 | `FFPSRWeaponStats::ShieldDamageMultiplier` | 1.0 | 저격 = >1 저작. `EFPSRWeaponStat` 축이라 **카드로 조정 가능** |
| 완화 상한 `MaxTotalReduction` | `UFPSRVitalsProfileDataAsset` (개체별) | 0.95 | M4 아머와의 결합 안전판 |
| 플레이어 재생 갱신 Hz | `AFPSRCharacter` `EditDefaultsOnly` | 10 | 순수 표현 갱신율 |
| 힐팩 회복량 · 리스폰 · 반경 | `AFPSRHealthPickup` `EditAnywhere`(인스턴스별) | 25% / 45s / 120 | 맵마다 다르게 배치 |
| i-frame `DamageInvulnerabilityDuration` | 기존 `AFPSRCharacter` | **0.25 (재조정 필요)** | 🔴 회귀함정 4 — §11-2 |

> C++ 에 남는 것 = **2층 배분 산술 · 이월 규칙 · 완화 상한 불변식 · 재생 공식의 형태**. 전부 "구조상 안 바뀌는 것"이다. 숫자는 하나도 코드에 없다. [[code-is-immutable-structure-only]]
> 에셋 경로 하드코딩 0 — 프로파일은 BP/DA 참조로만 들어온다.

---

## 10. 성능 예산 (핵심원칙 1)

**적(동시 200~300, 캡 500)**
- **틱: 0.** 실드 재생은 `ApplyDamage` 진입부의 지연계산뿐 — 비용이 **피격 횟수**에 비례하고, 서 있기만 하는 적은 0 이다.
- **액터당 메모리**: 복제 float 2 + 해석 float 3 + 서버전용 float 2 + `TObjectPtr` 1 ≈ **40 바이트** → 300 마리 = **12 KB**. 신규 UObject/컴포넌트 **0개**.
- **피격당 추가 비용**: `GetGameState()` 1회 · `ResolveDefense` 선형 스캔(저작 엔트리 0~2개, `IsDataValid` 가 8 초과를 경고) · 부동소수 산술 ~15회. 기존 `ApplyDamage` 대비 무시할 수준.
- **복제 대역**: `MaxShield==0` 인 적 = **0**(Push Model 이 dirty 를 안 만듦). 실드가 있는 적 = 피격당 float 1개. 재생은 지연계산이라 **재생 중 복제가 전혀 없다**(사용자 결정 2 의 직접적 이득).

**플레이어(≤4)**
- 서버 드라이버 = 10 Hz × 최대 4명 = 40 호출/초, 각 호출은 부동소수 산술 5회 + 조건부 어트리뷰트 쓰기.
- 복제: 실드가 차는 동안만 초당 최대 10회 × 4명 × float 1 → 4 클라 기준 대략 **~1 KB/s 상한**, 재생 사이클(수 초) 동안만. 값이 안 변하면 쓰기 자체를 안 한다.

**완화 수단**: 별도 필요 없음(배치·Significance·주기분할 전부 불필요). 이 설계의 성능 논거는 "완화"가 아니라 **애초에 도는 것이 없다**는 데 있다.

---

## 11. 미결정 항목 · 명세 갭 처리

### 11-1. 적 `MaxHealth` 전면 이관의 실행 범위 — **사용자 작업 구간**
사용자가 "(나) 전면 이관"을 선택했다. 코드는 **양쪽을 다 받는다**(프로파일이 있으면 프로파일, 없으면 기존 BP `MaxHealth`) — 그래서 **코드 머지 시점에 회귀 0** 이고, 이관은 콘텐츠 작업으로 뒤따른다. [[da-edits-are-user-work]]
- Claude 가 하는 것: 프로파일 클래스 · 해석 경로 · `IsDataValid` · **"프로파일 미할당 적" 경고 자동화**(§12-7).
- 사용자가 하는 것: 적 BP 2종(`BP_EnemyMeleeBase`·나머지)에 `DA_Vitals_*` 저작·할당. 값은 현행 BP `MaxHealth` 를 그대로 옮기는 것부터(무회귀 기준선).
- ⚠️ 이관이 **끝나기 전까지 BP `MaxHealth` 가 진실**이다. 두 자리가 공존하는 이 구간을 짧게 가져갈 것.

### 11-2. i-frame 재조정 — **사용자 판정 필요(런타임 체감)** 🔴 회귀함정 4
`DamageInvulnerabilityDuration = 0.25s` 위에 실드가 얹히면 스웜 근접 압박이 사라질 수 있다. 코드는 값을 안 바꾼다. 사용자가 PIE 로 체감 후 결정할 것 — 권장 탐색 범위 **0.10~0.25s**(실드가 이미 "연속 피격 흡수" 역할을 하므로 i-frame 의 몫이 줄었다). 값 변경은 BP 저작.

### 11-3. periodic GE × 전역 프리즈 — **이 유닛에서 발견, 스코프 밖** 🔴
`CombatWeaponCard.md` §2-3-5 가 "초당 체력 재생 = periodic infinite GE" 로 지정했고 플레이어 ASC 에는 `Enemy.md` §2-6 의 시간형-GE 가드가 **없다**. 즉 **기존 `HealthRegen` 카드는 지금도 프리즈 중에 회복된다.** 이 유닛은 그 카드를 풀에서 빼는 것으로 우회하지만, **`UCardEffect_CharacterGE` + periodic GE 조합 자체가 남아 있다.** → **보드 신규 행**으로 올린다(§12-8).

### 11-4. 자매 행과의 상호작용 규칙 — **여기서 정하고 그쪽이 따른다**
1. **"Shock 가 실드를 2배로 깎는다"** = `DefenseByDamageType[DamageType.Lightning].ShieldDefense = 2.0`. **신규 기제 0** — 이미 있는 데이터축이다.
2. **"실드 있으면 상태이상 저항"** = 상태이상 서브시스템이 `IsShieldBroken()` / `GetShield()` 를 질의. **API 이미 제공**(§5-3).
3. **상태이상 부여 판정은 `FPSRVitals::FResult` 를 본다** — `HealthSpent > 0` 인지(실드를 뚫었는지)로 "실드에 막힌 타격은 상태이상도 막힌다" 같은 규칙을 표현할 수 있다. `FFPSRDamageSpec` 에 필드를 추가하는 것도 이 유닛이 연 자리다.
4. **저장소는 합치지 않는다** — 실드 = float 2개(복제 필요) / 상태이상 = 비트+타이머(복제 정책이 다름). 억지 통합은 둘 다 나쁘게 만든다.

### 11-5. M4 방향성 아머 결합 — **규칙만 확정, 구현은 M4**
`FMitigation::DirectionalArmorDR` 자리를 이 유닛이 만든다. M4 는 그 값을 채우기만 하면 된다.
🔴 **결합 규칙(확정)**: 아머 DR 과 층 계수는 **곱해지되 `MaxTotalReduction`(기본 0.95)으로 클램프**되고, `Incoming > 0` 이면 `TotalSpent() > 0` 이 **항상** 보장된다(불변식 V1). "실드 계수 × 아머 DR = 무적"은 산술적으로 불가능하다.

### 11-6. 갭 처리 규칙 (고정)
C2(Sonnet 구현) 중 명세에 없는 판단이 필요해지면 **추측해서 채우지 말고 멈추고 "명세 갭"으로 보고**한다. 갭은 C1 으로 돌아가 Opus 가 명세를 고친 뒤 재개하며, **갭이 구조를 바꾸면 G1 을 다시 태운다**(§6-5-2).

---

## 12. 검증 기준 (무엇을 통과라 부르는가)

| # | 검사 | 통과 조건 |
|---|---|---|
| 1 | 명세 대조 | §5·§6·§7 의 선언·시그니처·복제 설정이 코드와 1:1 일치. **특히 `ApplyDamage` 산술 블록을 그대로 구현했는가** |
| 2 | 빌드 | 로그의 **`Result: Succeeded`** 로 판정([[build-exit-code-lies-grep-result]]). **2회 돌린다** — ①헤더 신규 3개라 누락 검출용 `-DisableUnity`([[nonunity-build-is-67-seconds]]) ②머지 전 1회 `-DisableAdaptiveUnity -ForceUnity`(§6-6 — Adaptive Unity 가 방금 고친 파일을 블롭에서 빼므로 일반 초록은 유니티 동명충돌을 구조적으로 못 잡는다. 자동화 테스트를 추가하므로 **필수**). 빌드 전 에디터 종료([[ue-editor-file-locks-block-git]]), 라이브코딩 금지([[no-live-coding]]) |
| 3 | 헤드리스 스모크 | `FPSRoguelite.Smoke.ModuleLoads` **+ `FPSRoguelite.Enemy.BlueprintParent`**(적 BP 에 UPROPERTY 를 추가하므로 — [[cpp-uproperty-name-collides-with-bp]]) |
| 4 | 순수함수 단위 검증 | `FPSRVitals` 자동화 테스트 신규: ①`MaxShield=0` → 현행과 산술 동일(V2) ②완화 최대치에서도 `TotalSpent()>0`(V1) ③`ComputeRegeneratedShield` **멱등**(같은 인자 2회 = 같은 값) ④이월: 큰 한 방이 작은 여러 방보다 총 체력피해가 크거나 같다 ⑤`ShieldDamageMultiplier=0` → 전량 체력 |
| 5 | 회귀 — 데미지 | 실드 없는 적(`MaxShield=0`)에 대해 `DamageDealt`·`bKilled`·히트마커·흡혈·관통이 **변화 0** |
| 6 | 회귀 — 미션·디렉터 | `FPSRMission_CarryNoHit` 가 실드만 깎인 피격도 스트릭을 끊는다 / `NotifyPlayerDamageTaken` 이 **실드 포함 총 피해**를 먹는다 |
| 7 | 데이터 검증 | 프로파일 미할당 적 BP 를 **경고**로 리포트(이관 진척 추적용, 실패는 아님) + `UFPSRVitalsProfileDataAsset::IsDataValid` 4항목 |
| 8 | 레드팀 게이트 (G2) | §6-6-1 · **P1 잔존 시 머지 금지**. 지적과 처리 결과를 §13 에 남긴다. 프리즈 시계·복제 프로퍼티 증가·FF 흡혈 거동 변화 3건을 **명시적으로 올린다** |
| 9 | PIE / 사용자 스모크 | 아래 목록 — **Claude 는 게임을 실행하지 않는다**([[do-not-launch-game]]). 보드는 `검증중` |

**사용자 PIE 확인 목록 (2인 이상, `L_Lobby` 시작 — [[pie-2player-test-recipe]])**
1. 실드 있는 적을 쏜다 → 실드바가 먼저 닳고, **다 깎이는 순간 전용 히트마커**가 뜬다. 그 뒤 체력이 닳는다.
2. 저격(대실드 계수 >1)과 연사총으로 같은 총 데미지를 넣는다 → **저격이 체력을 더 많이 깎는다**(이월 효과).
3. 적을 실드만 깎고 물러난다 → **완파 지연**이 지나야 다시 찬다. 부분만 깎고 물러나면 **더 빨리** 찬다.
4. 🔴 **프리즈 검증**: 실드를 깎은 직후 레벨업 프리즈를 띄우고 **30초 이상 카드를 안 고른다** → 재개 직후 적/본인 실드가 **프리즈 전과 같은 수준**이어야 한다(공짜 충전 금지).
5. 본인이 맞아 실드가 깨진다 → 화면에 위험 경고. 팀원 HUD 에도 그 사람 실드가 0 으로 보인다.
6. DBNO 로 쓰러진다 → **다운 중 실드가 차지 않는다**. 부활하면 체력 50% + **실드 0** 에서 시작.
7. 힐팩을 먹는다 → 체력만 오른다(실드는 무관). 체력이 가득하면 소모되지 않는다.
8. 문/구조물을 부순다 → **현행과 완전히 동일**(실드 없음).
9. FF 를 켜고 아군을 쏜다 → 흡혈이 발동한다(§6 의 의도된 결함 수정).

---

## 13. 레드팀 지적 원장 (C3 에서 채운다)

> `Workflow.md` §6-6-1. **기각엔 근거가 필요하다** — 제1원리 조항 / 코드 인용 / 실측치 중 하나.

| 심각도 | 지적 (요약 + 파일:줄) | 처리 | 근거 |
|---|---|---|---|
| P1 | | 수용 / 기각 / 보류 | |
| P2 | | | |
| P3 | | | |

- **레드팀에 무엇을 줬나**: (C3 에서 기입)
- **G1↔G2 사이에 들어간 "플랜에 없던 구조 결정"**: (C3 에서 세어 기입 — §6-5-2 의 명시 요구)
