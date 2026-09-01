# VIT1 — 실드/체력 2층 바이탈 시스템 (플레이어 + 몬스터 공용)

## 1. 메타

| 항목 | 값 |
|---|---|
| 유닛 ID / 이름 | **VIT1** — Shield/Health Two-Layer Vitals |
| 브랜치 | `phase/m1-shield-2layer` |
| 작성 모델 | `claude-opus-5` (§6-5-2 개정 2026-08-26 — C1 설계 담당은 Opus, Fable은 G1/G2 게이트) |
| 작성일 / 최종 갱신 | 2026-09-01 / 2026-09-01 |
| 상태 | **`검증완료` — C3 전체 diff 대조 통과**(2026-09-02). 대조가 P1 1건·P2 2건을 잡아 전부 수정·재검증했다. 남은 것 = **G2 머지 게이트 → `--no-ff` 머지**. 원장 = §13-0(G1) · §13-2(C3 표적) · **§13-3(C3 전수)** |
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
5. **저격/관통 무기가 실드 상대로 손해를 보지 않고, 데이터로 유리해진다.** 두 기제가 **역할이 다르다**:
   - **초과분 이월 = 페널티 제거(필수)**. 이월이 없으면 얇은 실드가 큰 한 방을 통째로 삼켜(실드 게이트) 저격이 구조적으로 **불리**해진다 — 요청과 정반대. ⚠️ **이월 산술 자체는 청킹-중립이다**(G1 지적): 실드 30·체력 100에 `100 한 방` = 체력 −70, `50 두 방` = 체력 −70. **즉 이월은 메리트를 만들지 않는다. 낭비를 없앨 뿐이다.**
   - **무기별 대실드 계수(`ShieldDamageMultiplier`) = 메리트 본체**. 저격이 실드에 강한 것은 **저작된 데이터**이지 산술의 부산물이 아니다. 여기에 재생 지연(느린 무기가 재생 창을 준다)이 반대 방향으로 작용하므로, 최종 밸런스는 이 계수와 재생속도의 저작으로 잡는다.
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
| `Public/Hero/FPSRCharacter.h` | 수정 | 서버전용 재생 상태 2 + 드라이버 + **실드파손 경고 핸들러/핸들**(C3 추가, §5-5-1) |
| `Private/Hero/FPSRCharacter.cpp` | 수정 | `ApplyContactDamage` 2층화 + Tick 재생 드라이버 + **경고 발행 배선**(C3 추가, §5-5-1) |
| `Public/Core/FPSRGameState.h` | 수정 | **프리즈-멈춤 전투 시계**(틱 0) |
| `Private/Core/FPSRGameState.cpp` | 수정 | `SetRunPaused` 엣지에서 동결시간 누적 |
| `Public/Hero/FPSRFeedbackTypes.h` | 수정 | `EFPSRHitMarkerType::ShieldBreak` **말미 추가** |
| `Public/Weapon/FPSRWeaponTypes.h` | 수정 | `FFPSRWeaponStatBlock::ShieldDamageMultiplier` + `EFPSRWeaponStat` 축 **말미 추가** + `GetAxisValue` case |
| `Private/Weapon/FPSRWeaponInstance.cpp` | 수정 | `RecomputeResolved` switch case 1개 |
| `Public/Weapon/FPSRProjectileTypes.h` | 수정 | `FFPSRProjectileParams::DamageSpec` — 발사·폭발 시점까지 계수를 나른다(비복제 서버 상태라 안전) |
| `Public/Pickup/FPSRHealthPickup.h` | **신규** | 맵 배치형 힐팩(`AFPSRXPPickup` 형제) |
| `Private/Pickup/FPSRHealthPickup.cpp` | **신규** | 수집·회복·전투시계 기반 리스폰 |
| `Private/Run/Mission/FPSRMission_CarryNoHit.cpp` | 수정 | 피격 판정을 "실드 포함 총 피해"로 |
| `Private/Hero/FPSRReviveComponent.cpp` | 수정 | `PerformRevive`(`:129`)에서 실드 앵커를 완파 상태로 재설정 — 🔴 회귀함정 6 |
| `Public/UI/FPSREnemyHealthBarWidget.h` · `Private/…cpp` | 수정 | `BindHealthComponent` 를 실드로 확장 + **초기 1회 동기화** [[umg-event-widget-initial-sync]] |
| `Private/AbilitySystem/Abilities/FPSRGA_WeaponFire_Hitscan.cpp` | 수정 | **`FFPSRDamageSpec` 구성**(§5-9) + 마커 집계 일원화 + 실드파손 전달 |
| `Private/AbilitySystem/Abilities/FPSRGA_WeaponFire_ChargeLaser.cpp` | 수정 | 〃 |
| `Private/AbilitySystem/Abilities/FPSRGA_WeaponMelee.cpp` | 수정 | 〃 |
| `Private/Weapon/FPSRProjectile.cpp` | 수정 | 〃 (`Params.DamageSpec` 를 직격·폭발 양쪽에 전달) |
| `Private/Weapon/FPSRWeaponFragment.cpp` | 수정 | Fragment AOE(`:70`)도 `Params.DamageSpec` 전달 |
| `Config/DefaultGameplayTags.ini` | 수정 | `Message.Player.ShieldBroken` 1줄 |
| **`Docs/SSOT/CombatWeaponCard.md`** §2-3-5 | **문서** | "초당 체력 재생 = periodic GE" 지정에 **은퇴 정정** + 흡혈이 실드 데미지로도 발동함을 등재 |
| **`Docs/SSOT/PlayerFeel.md`** §2-13·§2-14 | **문서** | 회복 경로 4종 확정 · 실드 규칙(DBNO/부활/i-frame) · 실드 파손 피드백 |
| **`Docs/SSOT/Enemy.md`** §2-6·§2-10 | **문서** | 적 `MaxHealth` 저작처를 "BP 에디터 기본값" → 프로파일 DA 로 정정 · 2층 데미지 계약 |
| **`Docs/Architecture/0014-…`** | **문서** | 덱이 「비율」에 더해 **개체 강도 배수**도 갖게 됨(불변식 1 "수의 소유자=디렉터"는 무변) |

> 🔴 **문서가 먼저다** — CLAUDE.md 핵심원칙 3(*"설계 변경은 해당 `Docs/SSOT/` 도메인 파일 먼저"*). 위 4개 문서 갱신은 C2 구현과 **같은 커밋 묶음**에 들어가며, 코드보다 뒤에 오지 않는다.

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

> ⚠️ `ShieldKeep` 의 `MinKeep` 하한은 **`ShieldDamageMultiplier == 0`(실드 무시) 일 때는 적용하지 않는다.** 하한의 목적은 "완화 중첩으로 무적이 되는 것"을 막는 것이지, 설계자가 의도적으로 연 우회로를 막는 게 아니다. 이 경우 데미지는 전량 체력으로 간다.
> 🔴 **불변식 V1 — "무적 불가". 이 명세에서 V1 의 정의는 여기 하나뿐이다**(§11-5 가 인용하는 것도 이것).
>   **V1**: `Incoming > 0` 이고 `(Shield > 0 || Health > 0)` 이면 `Result.TotalSpent() > 0`.
>   *유일한 예외* — `SDM == 0 && Health == 0 && Shield > 0`. 실드 무시 무기가 체력이 이미 0 인 대상을 때리는 경우이고, **게임 상태로 도달 불가**하다(적은 `Health <= 0` 에 `bDead` 로 조기 반환, 플레이어는 `IsIncapacitatedLocal()` 로 조기 반환).
>   보조 형태(테스트가 실제로 거는 단언):
>   - **V1a** `Incoming > 0 ∧ Shield > 0 ∧ SDM > 0` → `ShieldSpent > 0`
>   - **V1b** `Incoming > 0 ∧ Health > 0 ∧ Overflow > 0` → `HealthSpent > 0`
>   증명(경우 분해): `MaxTotalReduction ≤ 0.99` 이므로 `MinKeep ≥ 0.01 > 0`, 따라서 `HealthKeep > 0` 이고 `SDM > 0` 이면 `ShieldKeep > 0`. ⓐ`SDM > 0 ∧ Shield > 0` → `WantShield > 0` → `ShieldSpent > 0`. ⓑ`SDM > 0 ∧ Shield == 0` → `Consumed = 0` → `Overflow = Incoming` → `Health > 0` 이면 `HealthSpent > 0`. ⓒ`SDM == 0` → `WantShield = 0` → `Consumed = 0`(가드) → `Overflow = Incoming` → `Health > 0` 이면 `HealthSpent > 0`, `Health == 0` 이면 위 예외.
>   - ⚠️ **`HealthSpent > 0` 을 무조건 보장한다고 쓰면 거짓이다** — 실드가 타격을 전량 흡수하는 것(`Shield 100 · Health 50 · Incoming 10` → `HealthSpent = 0`)은 **정상 거동이지 무적이 아니다**(`ShieldSpent = 10`). G1 2차가 잡은 오류.
>   - **`MaxTotalReduction < 1.0` 이 V1 의 유일한 안전판**이다 → §5-2 에서 데이터로 강제한다(`ClampMax = 0.99` + `IsDataValid` 에러).
> 🔴 **불변식 V2**: `MaxShield == 0` → `ShieldSpent == 0` 이고 `Overflow == Incoming` (실드 없는 개체는 현행 `Health = Clamp(Health - Damage, 0, MaxHealth)` 와 산술적으로 동일). **요구 1의 유일한 구현 수단.**
> ⚠️ **불변식 V3 (청킹 중립 — 설계자가 알고 있어야 하는 성질)**: 재생이 개입하지 않는 구간에서 이 산술은 **분할에 중립**이다 — 같은 총 데미지를 한 방으로 넣든 나눠 넣든 총 체력 피해가 같다. 이월은 **낭비를 없앨 뿐 보너스를 주지 않는다.** 저격 메리트의 실제 원천은 `ShieldDamageMultiplier` 데이터축이다(§2 목표 5).

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

    /** 🔴 **불변식 V1 의 안전판**(G1 P2-3). 이 개체에 대한 한 타격의 총 감쇠 상한.
     *  M4 방향성 아머 DR × 층 계수가 곱해져 0 뎀이 되는 것을 산술로 막는다 —
     *  `Enemy.md` §2-6 방패 아키타입의 "하드블록(0뎀) 금지"(0뎀이면 히트마커·흡혈·킬크레딧이
     *  전부 침묵한다)를 데이터 층에서 강제하는 자리다. **1.0 을 저작할 수 없다**(ClampMax). */
    UPROPERTY(EditAnywhere, Category = "Vitals|Defense", meta = (ClampMin = "0.0", ClampMax = "0.99"))
    float MaxTotalReduction = 0.95f;

    /** 서버. DamageType 에 맞는 층 계수를 채운다(정확일치 → 기본엔트리 → 1.0 순). */
    void ResolveDefense(const FGameplayTag& DamageType, float& OutShieldDefense, float& OutHealthDefense) const;

#if WITH_EDITOR
    /** ①중복 태그(에러) ②`DamageType.*` 아닌 태그(에러) ③`MaxShield>0` 인데 재생속도 0 — 영구 파손,
     *  대개 저작 실수(경고) ④엔트리 8개 초과 — 피격당 선형 스캔이라 상한을 경고 ⑤`MaxTotalReduction >= 1.0`
     *  — **에러**(불변식 V1 붕괴. `ClampMax` 는 에디터 입력만 막지 임포트/스크립트 저작을 못 막는다). */
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

`PostAttributeChange` 추가 규칙 **3개**:
- **`MaxShield` 증가 → `Shield` 도 같은 양만큼 증가.** §2-13 의 `MaxHealth` 규칙(`FPSRHealthSet.cpp:79-84`)과 **대칭** — 권위 전용 가드도 동일(클라 이중적용 방지).
- **`MaxShield` 가 0 으로 떨어지면 `Shield` 도 0.** "실드 포기하고 체력↑" 카드가 실드를 남기지 않게. (기존 `Shield` 클램프가 다음 변경 때 처리하지만, 그때까지 HUD 에 유령 실드가 남는다.)
- 🔴 **`Shield` 가 재생 드라이버 **밖에서** 바뀌면 앵커를 재기저(rebase)한다** — 아래 §5-4-1.

#### 5-4-1. 🔴 앵커 재기저 규칙 (G1 P2-1)

**문제**: 재생 앵커(`ShieldAtLastDamage`, `LastDamageCombatTime`)에서 파생한 값을 드라이버가 `SetShield` 로 밀어넣으므로, **앵커를 모르는 쓰기는 다음 틱(≤0.1s)에 되덮인다.** 반례 — 최근 피격(앵커 40, 지연 중)에 레벨업 프리즈에서 "+MaxShield 50" 카드를 고르면 `Shield` 가 90 이 됐다가 드라이버가 40 으로 지운다. **가장 흔한 흐름(카드 선택)에서 카드 효과가 눈앞에서 증발한다.**

**규칙 (하나)**: `Shield` 에 일어난 변화 중 **바이탈 시스템이 만들지 않은 것**은 그 델타를 앵커에도 그대로 더한다.

```cpp
    /** 바이탈 시스템 자신이 Shield 를 쓰는 동안 켜지는 스코프 가드. 켜져 있으면 PostAttributeChange
     *  가 앵커를 건드리지 않는다(드라이버 값은 이미 앵커의 파생물이고, 데미지 경로는 앵커를 직접 세운다). */
    struct FScopedVitalsWrite { /* RAII: bVitalsWriting 을 true 로 */ };
    bool bVitalsWriting = false;
```

| 쓰기 주체 | 가드 | 앵커 처리 |
|---|---|---|
| 재생 드라이버(§5-5) | **ON** | 손대지 않는다 — 드라이버 값이 앵커의 파생물이다 |
| `ApplyContactDamage` | **ON** | 명시 세팅: `ShieldAtLastDamage = 새 Shield`, `LastDamageCombatTime = CombatNow` (지연 재시작 = 이 시스템의 목적) |
| 초기화 / 부활 | **ON** | 명시 세팅(§5-5) |
| **그 밖 전부** — 카드 GE, `MaxShield` 연동 증가, 향후 "실드 즉시 충전" 효과 | OFF | `ShieldAtLastDamage = Clamp(ShieldAtLastDamage + Δ, 0, MaxShield)` — **시각은 안 건드린다** |

> **권위**: 이 규칙은 **서버 전용**이다(규칙 1 의 "권위 전용 가드"와 같은 문구 — `ASC->IsOwnerActorAuthoritative()`). 복제된 어트리뷰트 변경은 클라 `PostAttributeChange` 도 발화하지만 클라 앵커는 아무도 읽지 않으므로 무해하다. 그래도 명시적으로 막아 진실을 하나로 둔다.
> **가드의 소유 위치** = `AFPSRCharacter`(앵커 두 멤버가 사는 곳). `UFPSRHealthSet::PostAttributeChange` 가 소유 캐릭터를 얻어 질의한다 — 어트리뷰트 셋은 PlayerState ASC 에 살고 폰 교체를 견뎌야 하므로 상태를 들지 않는다.
> 🔴 **캐릭터에 닿는 법 (함정)** — **`GetOwningActor()` 는 캐릭터가 아니라 `AFPSRPlayerState` 를 돌려준다**(ASC 소유자가 PlayerState 다). 이걸 바로 `AFPSRCharacter` 로 캐스트하면 **조용히 nullptr** 이 되고 → 재기저가 영영 안 돌아 **P2-1 이 그대로 되살아난다**(컴파일도 되고 단위테스트도 통과하는 형태로). 두 경로 중 하나를 쓸 것:
>   - **권장** `Cast<AFPSRCharacter>(GetOwningAbilitySystemComponent()->GetAvatarActor())` — `AFPSRCharacter::…::InitAbilityActorInfo(PS, this)`(`FPSRCharacter.cpp:506`)가 아바타를 캐릭터로 세운다(G1 실측). 한 홉.
>   - 선례 `UFPSRCombatSet::ApplyMoveSpeedToOwner`(`FPSRCombatSet.cpp:81-97`)의 `GetOwningActor()` → `APlayerState::GetPawn()` → `Cast<AFPSRCharacter>`. 같은 리포에서 이미 도는 패턴이라 안전하지만 두 홉이다.
>   어느 쪽이든 **널 체크 필수** — 폰 교체 창에서 아바타가 잠시 없다(그때는 no-op).
> **왜 시각을 안 건드리나**: 카드를 먹은 것은 피격이 아니다. 남은 재생 지연은 그대로 흘러야 한다.
> ⚠️ **후속 저작 주의(G1 2차 P3-c)** — 이 규칙은 **실드를 늘리는** 델타를 전제한다. 나중에 **실드를 깎는** 효과(디버프·"실드 흡수" 적 등)를 만들면, 마지막 피격이 오래전일 때 앵커만 낮아지고 시각은 그대로라 **다음 드라이버 틱에 즉시 만충 복귀**한다. 그런 효과는 **시각도 함께 재기저**해야 한다(= 피격으로 취급). 이 유닛의 카드에는 하향 효과가 없어 지금은 무해하다.
> **완파 판정과의 정합**: 파손 여부는 `ShieldAtLastDamage <= 0` 으로 읽는다. 완파(앵커 0) 상태에서 카드가 +50 을 주면 앵커가 50 이 되어 다음 재생이 **짧은 지연**을 쓴다 — "실드를 보충받았으니 완파가 아니다"로 읽히므로 의도된 거동이다.
> **이 규칙이 GAS 를 살린다**: `Shield` 를 어트리뷰트로 둔 값어치가 "GE 가 만질 수 있다"인데, 재기저 없이는 그 GE 가 전부 무력화된다.

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
    if (New > Shield + KINDA_SMALL_NUMBER)        // ← 단조 증가만. 아래 주석 참조
    {
        FScopedVitalsWrite Guard; SetShield(New)
    }
```
> 🔴 **드라이버는 실드를 절대 깎지 않는다**(`New > Shield` 비교, `|New - Shield|` 아님). 재기저(§5-4-1)가 정상 경로를 이미 닫지만, 이 단조성은 **어떤 재기저 누락이 있어도 카드/GE 가 준 실드를 드라이버가 지울 수 없게** 만드는 두 번째 안전망이다. 실드를 줄이는 주체는 데미지 하나뿐이다.
> **프리즈 게이트가 따로 필요 없다** — `CombatNow` 가 프리즈 중 안 흐르므로 `New == Shield` 가 되어 쓰기 자체가 안 일어난다. 게이트를 코드로 또 두면 진실이 둘이 된다.
> 🔴 **회귀함정 6**: `IsAlive()` 가 DBNO 를 false 로 본다(§2-13) → **DBNO 중 실드 재생 정지**.

**앵커 초기화 (G1 P2-1 후반 — 이걸 안 하면 스폰 첫 틱이 만충 실드를 지운다)**

| 시점 | 앵커 세팅 | 이유 |
|---|---|---|
| `InitAbilityActorInfo`(프로파일 → 어트리뷰트 초기값 직후) | `ShieldAtLastDamage = MaxShield`, `LastDamageCombatTime = CombatNow` | 만충에서 시작. ⚠️ 기본 멤버값 `(0, -1e9)` 로 두면 `RegenPerSecond == 0` 저작(`IsDataValid` 가 **경고**만 낸다) 시 `ComputeRegeneratedShield` 가 0 을 돌려주고 드라이버가 실드를 지운다 — 단조성 안전망이 이것도 막지만, 앵커가 틀린 채 굴러가면 첫 피격 후 재생이 어긋난다 |
| `PerformRevive`(`FPSRReviveComponent.cpp:129`, 체력 50% 세팅 직후) | `ShieldAtLastDamage = 0`, `LastDamageCombatTime = CombatNow` | **완파 상태에서 부활**. `PostReviveInvuln` 5초 동안 완파지연 6초가 거의 흘러, 무적이 풀린 직후 실드가 차기 시작한다 |
| 런 리셋 / 리스폰 | `InitAbilityActorInfo` 와 동일 | |

> 적 쪽 대응 = `ResetForReuse` 가 `ShieldAtLastDamage = MaxShield` 로 되돌린다(§8). **두 저장소가 같은 규칙**이다.

플레이어 재생 수치의 저작 자리 = **`UFPSRHealthSet` 기본값이 아니라 캐릭터 BP 의 `EditDefaultsOnly`**:
```cpp
    /** 플레이어 기준 프로파일 1개(사용자 결정 2026-09-01 — 캐릭터 종류가 1개라 전원 공유,
     *  개인차는 전부 카드가 만든다). 적과 **같은 DataAsset 타입**을 쓴다 = 저작 UI·검증기 공유. */
    UPROPERTY(EditDefaultsOnly, Category = "FPSR|Vitals")
    TObjectPtr<UFPSRVitalsProfileDataAsset> VitalsProfile;
```
`PossessedBy`/`InitAbilityActorInfo` 시점에 프로파일 → 어트리뷰트 초기값(`SetMaxHealth`/`SetMaxShield`/…) + 재생 3값을 캐릭터 멤버로 굽는다. 프로파일 null = 현행 기본값(`MaxShield = 0` → 실드 없음, 무회귀).

#### 5-5-1. 🔴 요구 5 「본인 실드 파손 경고」의 발행 배선 (C3 전체대조 신설, 2026-09-02)

**갭**: C2 까지의 명세는 `UFPSRHealthSet::OnShieldBroken`(§5-4)을 "요구 5 의 플레이어 경고 원천"이라 선언만 하고, **그것을 구독해 `Message.Player.ShieldBroken` 을 발행하는 자리를 §4 파일 목록에 넣지 않았다.** 결과 = 코드에 그 델리게이트의 **구독자가 0개**였고, `DefaultGameplayTags.ini` 의 태그는 아무도 발행하지 않는 죽은 태그였다. §11-1 사용자 작업 6-③(경고 위젯)은 GMS 구독인데 **UMG 는 C++ 비-다이내믹 델리게이트를 구독할 수 없어 콘텐츠로 메울 수 없다.** 갭 처리 = §11-6 → C1(Opus) 명세 수정 후 구현. 사용자 결정(2026-09-02): **이 유닛에서 배선한다.**

**확정 배선**:
```cpp
// AFPSRCharacter::InitAbilitySystem — 권위 게이트 없음(로컬 코스메틱은 그릴 기계에서 나야 한다)
ShieldWarningDelegateHandle = ASC->GetGameplayAttributeValueChangeDelegate(UFPSRHealthSet::GetShieldAttribute())
    .AddUObject(this, &AFPSRCharacter::HandleShieldValueChangedForWarning);
// 핸들러: (Old > 0 && New <= 0) 엣지 + IsLocallyControlled() → GMS BroadcastMessage(Message.Player.ShieldBroken, FFPSRCosmeticEventMessage{WorldLocation})
// EndPlay 에서 Remove — ASC/어트리뷰트셋은 PlayerState 에 살아 폰보다 오래 산다(리스폰마다 죽은 엔트리가 쌓인다)
```

> 🔴 **왜 `OnShieldBroken` 이 아니라 GAS 값변경 델리게이트인가 (엔진 실측)** — `OnShieldBroken` 은 `PostAttributeChange` 에서 올라오는데, **클라에서는 그 클라가 해당 어트리뷰트의 애그리게이터를 들고 있을 때만** 그 경로가 돈다: 엔진 `GameplayEffect.cpp:3682` `SetBaseAttributeValueFromReplication` 은 애그리게이터가 없으면 **else 분기**로 빠져 `AttributeValueChangeDelegates` 만 브로드캐스트하고 `PostAttributeChange` 를 부르지 않는다(애그리게이터가 있을 때만 `:3702` → `SetNumericAttribute_Internal` → `AttributeSet.cpp:84` 로 도달). 즉 `OnShieldBroken` 에 붙이면 **원격 협동 플레이어는 경고를 못 받는다.** `AttributeValueChangeDelegates` 는 두 경로 모두에서 발화한다(`:3911` · `:3721`) — 유일하게 클라 신뢰 가능한 훅이다.
> ⚠️ 부수 정정: §5-4-1 의 괄호 서술("복제된 어트리뷰트 변경은 클라 `PostAttributeChange` 도 발화하지만")은 **조건부로만 참**이다. 앵커 재기저는 권위 게이트가 걸려 있어 **영향 없음**(진술만 부정확했다).
> **신규 복제 0 · 신규 RPC 0 유지** — 이미 복제되는 `Shield` 어트리뷰트에서 클라가 스스로 엣지를 읽는다. §7 의 "신규 RPC 0개"가 문자 그대로 유지된다.
> `OnShieldBroken` 은 **폐기하지 않는다** — 서버측 훅으로 남는다(§5-4 선언 유지, 현재 구독자는 없다).

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
        /** 🔴 **C3 전체대조 추가 (2026-09-02)** — 받은 쪽이 다른 `AFPSRCharacter` 였다(FF 아군 **또는 자폭**).
         *  히트마커 5경로는 전부 `DamageDealt > 0` 으로 마커를 게이트하는데, "플레이어엔 마커 없음" 규칙이
         *  종전에는 *암묵적*이었다 — 플레이어 분기가 `DamageDealt` 를 0 으로 남겼기 때문이다.
         *  §6 이 그 값을 **채우기로 하면서 게이트가 조용히 열렸고**, 자폭은 FF 설정과 무관하므로
         *  (`ResolveDamage`: `Target == Instigator → bAllowSelf ? BaseDamage : 0`)
         *  **로켓 점프마다 자기 화면에 마커가 뜨는** 회귀가 됐다. 이 플래그로 5곳을 명시 게이트한다.
         *  게임플레이 축은 무변 — 흡혈·킬크레딧은 `bWasEnemy` 가 그대로 막는다. */
        bool  bTargetIsPlayer = false;
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

### 5-9. 🔴 대실드 계수의 소스 → `FFPSRDamageSpec` 배선 (G1 P2-2)

이것이 빠지면 **전 무기가 `ShieldDamageMultiplier = 1.0` 으로 고정된 채 컴파일되고**, §12-4 단위테스트는 전부 통과한다(순수함수는 인자를 받은 대로 계산하므로). 발각 시점이 사용자 PIE 뿐이다. C2 는 축자 구현이고 시그니처·필드 추가가 금지되므로 **여기서 전부 못박는다.**

**① 저작 자리** — `FFPSRWeaponStatBlock`(`Weapon/FPSRWeaponTypes.h`)에 필드 1개:
```cpp
    /** 대실드 배수. 1 = 평범 · >1 = 실드에 강함(저격) · 0 = 실드 무시(전량 체력으로 이월).
     *  체력 데미지에는 영향이 없다 — 실드 층에서만 곱해진다(§5-1). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon|Damage", meta = (ClampMin = "0.0", UIMax = "4.0"))
    float ShieldDamageMultiplier = 1.0f;
```

**② 카드 축** — `EFPSRWeaponStat` 에 `ShieldDamageMultiplier` 를 **말미에 추가**(P3-3).
> ⚠️ **중간 삽입 금지** — `uint8` 이고 카드 DA(`UCardEffect_WeaponStat`)가 이 값을 **에셋에 저장**한다. 중간에 끼우면 기존 카드가 조용히 다른 축을 가리킨다. `EFPSRHitMarkerType`(§5-7)과 같은 규칙.
> `FFPSRWeaponStatBlock::GetAxisValue` switch 와 `UFPSRWeaponInstance::RecomputeResolved` switch 에 case 각 1개(§2-3-8 컨벤션 쿡북의 "새 무기 stat 축 = enum + switch case, 컴파일체크 유지" 경로 그대로).

**③ 5개 데미지 경로의 Spec 구성 지점** — 각 경로가 **이미 `ResolvedStats` 를 읽는 그 자리**에서 만든다. 신규 조회 0.

| 경로 | Spec 을 만드는 곳 | 전달 |
|---|---|---|
| 히트스캔 | `FPSRGA_WeaponFire_Hitscan.cpp` — 이미 `Instance->GetResolvedStats()` 를 잡아 두는 지점(`:93`) | 펠릿 루프의 `FPSRCombat::ApplyDamage(HitActor, Resolved, Avatar, Spec)`(`:280`) |
| 차지레이저 | `FPSRGA_WeaponFire_ChargeLaser.cpp` 동일 패턴 | `:293` |
| 근접 | `FPSRGA_WeaponMelee.cpp` 동일 패턴 | `:203` |
| 발사체(직격) | **발사 시점**에 `FFPSRProjectileParams::DamageSpec` 에 담아 보낸다 | `FPSRProjectile.cpp:463` 이 `Params.DamageSpec` 을 그대로 전달 |
| 발사체·Fragment 폭발 | 〃 | `FPSRProjectile.cpp:324` · `FPSRWeaponFragment.cpp:70` 의 `ApplyExplosion(..., Params.DamageSpec)` |

**④ 발사체가 계수를 나르는 이유** — 발사체는 발사자와 시간·공간이 분리돼 있어 착탄 시점에 무기 상태를 다시 읽으면 **그 사이 무기를 바꾸거나 카드를 먹은 값**을 쓰게 된다. `FFPSRProjectileParams` 는 이미 **서버 전용 비복제 상태**(`FPSRProjectileTypes.h:75` 주석)라 `FFPSRDamageSpec` 을 넣어도 와이어를 안 탄다 — `Pierce`·`GravityScale` 이 발사 시점 값을 나르는 것과 정확히 같은 이유·같은 자리다.

**⑤ Fragment 훅과의 관계** — `UFPSRWeaponFragment::OnProjectileSpawn(Ctx, ParamsInOut)`(`FPSRWeaponFragment.h:74`)이 이미 `Params` 를 **수정 가능하게** 받는다 → **"이 프래그먼트를 달면 실드를 무시한다" 같은 무기 행동이 신규 배선 0으로 붙는다.** 이 유닛은 그런 Fragment 를 만들지 않는다(비목표) — 자리만 열린다.

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

**🔴 플레이어 분기의 `DamageDealt` — 채우되, 흡혈은 열지 않는다 (G1 P2-4, 초안 오진 정정)**

종전 초안은 *"플레이어 분기가 `DamageDealt` 를 0 으로 남기는 것은 기존 결함이니 함께 고친다"* 고 썼다. **그 판단이 틀렸다.** 현행 게이트는 결함이 아니라 **의도된 반-파밍 장치**다 — 코드 주석이 명시한다: *"Gated on bWasEnemy too, so shooting a door … can't feed lifesteal / heal-on-damage (no farming health off a high-HP destructible)"* (`FPSRCombatStatics.cpp:177-181`). **유한 체력인 문짝조차 흡혈원으로 금지**돼 있는데, 이 유닛은 **스스로 재생되는** 실드를 만든다.

열었을 때 실제로 생기는 것:
- FF ON 세션에서 아군 실드를 쏘면 **아군 손실 0의 영구 회복 펌프**가 된다(실드는 알아서 다시 찬다).
- 더 나쁜 것 — **자폭은 FF 플래그와 독립**이다(`ResolveDamage`: `Target == Instigator → bAllowSelf ? BaseDamage : 0`). 즉 **솔로에서도** 자기 발밑 로켓 → 자기 실드 흡혈 → 실드 재생 루프가 성립한다.
- `CombatWeaponCard.md` §2-3-5 의 명문 경계와 정면 충돌: *"지속 '힐건'化 금지 = '아군을 쏴서 힐하는 게임' 붕괴 방지"*. 그리고 초안이 원용한 `Enemy.md` §2-10 협동 카드 시임은 **"FF 데미지 → 맞은 아군 회복"**(지원)이지 **"쏜 사람 회복"**(흡혈)이 아니다 — 방향이 반대다.

**확정 규칙 — 두 가지를 분리한다:**

| | 이 유닛에서 | 근거 |
|---|---|---|
| `FDamageResult::DamageDealt` (플레이어 대상) | **채운다** = `Result.TotalSpent()` | 미션 무피격 판정 · 디렉터 센서 · 관통 판정이 쓰는 "실제 입힌 피해" 축. 흡혈과 무관 |
| 흡혈 이벤트(`GameplayEvent.Player.DealtDamage`) | **`bWasEnemy` 게이트 유지 — 플레이어 대상엔 안 보낸다** | 위 파밍 루프. 현행과 거동 변화 **0** |

FF 흡혈을 열지 여부는 **사용자 결정 항목으로 승격**한다(§11-7). 이 유닛은 기본값(닫힘)으로 간다.

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

**🔴 플레이어 층 계수의 합성 규칙 (G1 P3-4 — 두 소스가 있으므로 순서를 못박는다)**
플레이어만 유일하게 계수 소스가 둘이다: 프로파일의 데미지타입별 값 + 어트리뷰트(`ShieldDefense`/`HealthDefense`).
```
FMitigation.ShieldDefense = Profile->ResolveDefense(DamageType).Shield  ×  GetShieldDefense()
FMitigation.HealthDefense = Profile->ResolveDefense(DamageType).Health  ×  GetHealthDefense()
```
- **프로파일 = 이 캐릭터가 원래 어떤 속성에 강한가**(저작 · 런 중 불변).
- **어트리뷰트 = 카드가 런 중에 얹는 배수**(base 1.0). "실드 방어 +20%" 카드 = `ShieldDefense` 를 0.8 로 만드는 GE 하나 — 프로파일을 안 건드리고, **모든 데미지 타입에 균일하게** 걸린다.
- 적은 어트리뷰트가 없으므로 프로파일 값이 곧 최종값이다(곱할 것이 없다).
- ⚠️ 값이 작을수록 단단하다(§5-1 `FMitigation` 주석) — 카드 저작 시 방향을 헷갈리지 않도록 `GetDescription` 에서 백분율로 뒤집어 보여줄 것.

> C++ 에 남는 것 = **2층 배분 산술 · 이월 규칙 · 완화 상한 불변식 · 재생 공식의 형태 · 계수 합성 순서**. 전부 "구조상 안 바뀌는 것"이다. 숫자는 하나도 코드에 없다. [[code-is-immutable-structure-only]]
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
- 사용자가 하는 것 **(전체 목록 — G1 P3-1)**:
  1. 적 BP 2종(`BP_EnemyMeleeBase`·나머지)에 `DA_Vitals_*` 저작·할당. 값은 현행 BP `MaxHealth` 를 그대로 옮기는 것부터(무회귀 기준선).
  2. 플레이어 캐릭터 BP 에 `DA_Vitals_Player` 저작·할당(실드 최대량·재생속도·이원 지연).
  3. 적 로스터 DA 에 `VitalsModifier` 저작(등급별 덱이 생기기 전까지는 항등 1.0 이어도 무방).
  4. 무기 DA 에 `ShieldDamageMultiplier` 저작 — **저격 >1**(이게 요구의 본체다, §2 목표 5).
  5. 힐팩 액터를 아레나에 배치 + 회복량/리스폰 저작.
  6. **HUD 콘텐츠 3종** — ①플레이어 본인·팀원 실드바(어트리뷰트 바인딩만) ②`ShieldBreak` 히트마커 비주얼 ③실드 파손 위험 경고 위젯(GMS `Message.Player.ShieldBroken` 구독). **§12-10 PIE 1·5번의 선행조건**이다.
  7. `DA_CardPool` 에서 `DA_Card_Character_HealthRegen` 제거(요구 3).
- ⚠️ 이관이 **끝나기 전까지 BP `MaxHealth` 가 진실**이다. 두 자리가 공존하는 이 구간을 짧게 가져갈 것.
- [[da-edits-are-user-work]] — Claude 는 값과 위치만 알려주고 에셋에 손대지 않는다.

### 11-2. i-frame 재조정 — **사용자 판정 필요(런타임 체감)** 🔴 회귀함정 4
`DamageInvulnerabilityDuration = 0.25s` 위에 실드가 얹히면 스웜 근접 압박이 사라질 수 있다. 코드는 값을 안 바꾼다. 사용자가 PIE 로 체감 후 결정할 것 — 권장 탐색 범위 **0.10~0.25s**(실드가 이미 "연속 피격 흡수" 역할을 하므로 i-frame 의 몫이 줄었다). 값 변경은 BP 저작.

### 11-3. periodic GE × 전역 프리즈 — **이 유닛에서 발견, 스코프 밖** 🔴
`CombatWeaponCard.md` §2-3-5 가 "초당 체력 재생 = periodic infinite GE" 로 지정했고 플레이어 ASC 에는 `Enemy.md` §2-6 의 시간형-GE 가드가 **없다**. 즉 **기존 `HealthRegen` 카드는 지금도 프리즈 중에 회복된다** — 이 유닛이 만든 버그가 아니라 오늘도 존재하는 것이다. 이 유닛은 그 카드를 풀에서 빼는 것으로 우회하지만, **`UCardEffect_CharacterGE` + periodic GE 조합 자체가 남아 있다.** → **보드 신규 행**으로 올린다.

### 11-7. FF/자가 흡혈 — ✅ **사용자 결정 완료 (2026-09-01): 닫은 채로 둔다.** 미결정 아님
흡혈 이벤트는 `bWasEnemy` 게이트를 유지한다 = **현행 거동 변화 0**. 열었을 때 생기는 것은 §6 참조(아군 실드 = 손실 0 회복 펌프 / 자폭은 FF 설정과 독립이라 **솔로에서도** 무한 루프). `CombatWeaponCard.md` §2-3-5 *"지속 '힐건'化 금지"* 와도 충돌한다.
> 향후 협동 회복을 원하면 그건 **"FF 데미지 → 맞은 아군 회복"**(`Enemy.md` §2-10 협동 카드 시임 — 방향이 반대다) 쪽 별도 설계다. 이 유닛의 비목표.

### 11-8. `DamageDealt` 의 단위가 섞인다 — **명시하고 G2 에 올린다** (G1 P3-2)
1. `TotalSpent()` 의 `ShieldSpent` 는 **`ShieldDamageMultiplier`·`ShieldDefense` 로 스케일된 실드 포인트**다. 대실드 계수 2.0 저격이 100 을 쏘면 실드 200 이 깎이고 `DamageDealt = 200` 이 된다 → **흡혈이 명목 데미지의 2배로 발동**한다(사용자 결정 "흡혈 = 실드 포함 전체 데미지"의 직접 귀결). 이것을 시너지로 볼지 이중 이득으로 볼지는 밸런스 판정이다. **현 설계는 "풀에서 실제로 사라진 포인트"라는 하나의 정직한 정의를 유지**하고, 예외를 만들지 않는다.
2. `NotifyPlayerDamageTaken` 이 먹는 값이 **원시 `DamageAmount`**(현행 `FPSRCharacter.cpp:1633`)에서 **완화·클램프 후 총 피해**로 바뀐다 → 폐루프 디렉터의 `IncomingDamageRate` 기준선이 이동한다. 방향은 옳다(디렉터는 "얼마나 아팠나"를 봐야 한다)지만 **기존 튜닝값이 그대로면 디렉터가 플레이어를 덜 위험하다고 읽는다**. 디렉터 임계값 재보정이 후속으로 필요.

### 11-4. 자매 행과의 상호작용 규칙 — **여기서 정하고 그쪽이 따른다**
1. **"Shock 가 실드를 2배로 깎는다"** = `DefenseByDamageType[DamageType.Lightning].ShieldDefense = 2.0`. **신규 기제 0** — 이미 있는 데이터축이다.
2. **"실드 있으면 상태이상 저항"** = 상태이상 서브시스템이 `IsShieldBroken()` / `GetShield()` 를 질의. **API 이미 제공**(§5-3).
3. **상태이상 부여 판정은 `FPSRVitals::FResult` 를 본다** — `HealthSpent > 0` 인지(실드를 뚫었는지)로 "실드에 막힌 타격은 상태이상도 막힌다" 같은 규칙을 표현할 수 있다. `FFPSRDamageSpec` 에 필드를 추가하는 것도 이 유닛이 연 자리다.
4. **저장소는 합치지 않는다** — 실드 = float 2개(복제 필요) / 상태이상 = 비트+타이머(복제 정책이 다름). 억지 통합은 둘 다 나쁘게 만든다.

### 11-5. M4 방향성 아머 결합 — **규칙만 확정, 구현은 M4**
`FMitigation::DirectionalArmorDR` 자리를 이 유닛이 만든다. M4 는 그 값을 채우기만 하면 된다.
🔴 **결합 규칙(확정)**: 아머 DR 과 층 계수는 **곱해지되 `MaxTotalReduction`(기본 0.95, 저작 상한 0.99)으로 클램프**된다 → **불변식 V1**(정의는 §5-1 한 곳 — `Incoming > 0` 이고 살아 있는 대상이면 `TotalSpent() > 0`)이 성립한다. "실드 계수 × 아머 DR = 무적"은 산술적으로 불가능하다.

### 11-6. 갭 처리 규칙 (고정)
C2(Sonnet 구현) 중 명세에 없는 판단이 필요해지면 **추측해서 채우지 말고 멈추고 "명세 갭"으로 보고**한다. 갭은 C1 으로 돌아가 Opus 가 명세를 고친 뒤 재개하며, **갭이 구조를 바꾸면 G1 을 다시 태운다**(§6-5-2).

---

## 12. 검증 기준 (무엇을 통과라 부르는가)

| # | 검사 | 통과 조건 |
|---|---|---|
| 1 | 명세 대조 | §5·§6·§7 의 선언·시그니처·복제 설정이 코드와 1:1 일치. **특히 `ApplyDamage` 산술 블록을 그대로 구현했는가** |
| 2 | 빌드 | 로그의 **`Result: Succeeded`** 로 판정([[build-exit-code-lies-grep-result]]). **2회 돌린다** — ①헤더 신규 3개라 누락 검출용 `-DisableUnity`([[nonunity-build-is-67-seconds]]) ②머지 전 1회 `-DisableAdaptiveUnity -ForceUnity`(§6-6 — Adaptive Unity 가 방금 고친 파일을 블롭에서 빼므로 일반 초록은 유니티 동명충돌을 구조적으로 못 잡는다. 자동화 테스트를 추가하므로 **필수**). 빌드 전 에디터 종료([[ue-editor-file-locks-block-git]]), 라이브코딩 금지([[no-live-coding]]) |
| 3 | 헤드리스 스모크 | `FPSRoguelite.Smoke.ModuleLoads` **+ `FPSRoguelite.Enemy.BlueprintParent`**(적 BP 에 UPROPERTY 를 추가하므로 — [[cpp-uproperty-name-collides-with-bp]]) |
| 4 | 순수함수 단위 검증 | `FPSRVitals` 자동화 테스트 신규: ①`MaxShield=0` → 현행 `Clamp(Health-Dmg)` 와 산술 동일(**V2**) ②**V1** — `MaxTotalReduction=0.99` + 아머 DR 1.0 + 층 계수 0 의 최악 조합에서 **두 픽스처**로 건다: ⓐ`Shield>0` → `TotalSpent()>0`(**`HealthSpent>0` 을 걸면 안 된다** — 실드 전량 흡수는 정상이다) ⓑ`Shield==0 ∧ Health>0` → `HealthSpent>0`. ⓑ가 없으면 ⓐ만으로는 공허 통과가 가능하다 ③`ComputeRegeneratedShield` **멱등**(같은 인자 2회 = 같은 값) ④**청킹 중립**(**V3**): 실드 30·체력 100 에 `100×1` 과 `50×2` 의 총 체력피해가 **같다** — ⚠️ "큰 한 방이 유리"를 검사하는 게 아니다(그건 산술이 아니라 SDM 데이터가 만든다). 검사 의도 = 이월이 낭비를 만들지 않음 ⑤`SDM=0` → `ShieldSpent==0` 이고 전량 체력 ⑥`SDM=2` 로 같은 명목 데미지를 넣으면 실드 소진이 2배 — ⚠️ 이건 **산술만** 고정한다. 무기 저작값이 실제로 여기까지 도달하는지(§5-9 배선)는 순수함수 테스트로 증명할 수 없다(테스트가 Spec 을 직접 만들므로). **배선 검증 = §12-10 PIE 2번** |
| 5 | 회귀 — 데미지 | 실드 없는 적(`MaxShield=0`)에 대해 `DamageDealt`·`bKilled`·히트마커·흡혈·관통이 **변화 0** |
| 6 | 회귀 — 미션·디렉터·흡혈 | `FPSRMission_CarryNoHit` 가 실드만 깎인 피격도 스트릭을 끊는다 / `NotifyPlayerDamageTaken` 이 **실드 포함 총 피해**를 먹는다 / 🔴 **흡혈 이벤트가 플레이어 대상에는 여전히 안 나간다**(§6 · §11-7 — 파밍 루프 방지) |
| 7 | 데이터 검증 | 프로파일 미할당 적 BP 를 **경고**로 리포트(이관 진척 추적용, 실패는 아님) + `UFPSRVitalsProfileDataAsset::IsDataValid` **5항목**(특히 `MaxTotalReduction >= 1.0` = 에러) |
| 8 | **SSOT 갱신** | §4 의 문서 4개(`CombatWeaponCard` §2-3-5 · `PlayerFeel` §2-13·§2-14 · `Enemy` §2-6·§2-10 · `ADR 0014`)가 **코드와 같은 커밋 묶음**에 갱신돼 있다. CLAUDE.md 핵심원칙 3 |
| 9 | 레드팀 게이트 (G2) | §6-6-1 · **P1 잔존 시 머지 금지**. 지적과 처리 결과를 §13 에 남긴다. 아래 4건을 **명시적으로 올린다**: ①프리즈 전투시계 ②적 복제 프로퍼티 3→5 ③`DamageDealt` 단위 혼합(§11-8) ④디렉터 센서 기준선 이동(§11-8) |
| 10 | PIE / 사용자 스모크 | 아래 목록 — **Claude 는 게임을 실행하지 않는다**([[do-not-launch-game]]). 보드는 `검증중` |

**사용자 PIE 확인 목록 (2인 이상, `L_Lobby` 시작 — [[pie-2player-test-recipe]])**
1. 실드 있는 적을 쏜다 → 실드바가 먼저 닳고, **다 깎이는 순간 전용 히트마커**가 뜬다. 그 뒤 체력이 닳는다.
2. 저격(대실드 계수 >1)과 연사총으로 같은 총 데미지를 넣는다 → **저격이 실드를 더 빨리 걷어낸다**. ⚠️ 이건 **계수 저작이 도달했는지**를 보는 것이다(§5-9 배선) — 이월만으로는 차이가 안 난다(V3).
2-1. 저격 한 방을 **실드보다 큰 데미지**로 넣는다 → 남은 만큼이 **체력까지 들어간다**(실드 게이트가 아니다). 이게 이월의 관찰 가능한 효과다.
3. 적을 실드만 깎고 물러난다 → **완파 지연**이 지나야 다시 찬다. 부분만 깎고 물러나면 **더 빨리** 찬다.
4. 🔴 **프리즈 검증**: 실드를 깎은 직후 레벨업 프리즈를 띄우고 **30초 이상 카드를 안 고른다** → 재개 직후 적/본인 실드가 **프리즈 전과 같은 수준**이어야 한다(공짜 충전 금지).
5. 본인이 맞아 실드가 깨진다 → 화면에 위험 경고. 팀원 HUD 에도 그 사람 실드가 0 으로 보인다.
6. DBNO 로 쓰러진다 → **다운 중 실드가 차지 않는다**. 부활하면 체력 50% + **실드 0** 에서 시작.
7. 힐팩을 먹는다 → 체력만 오른다(실드는 무관). 체력이 가득하면 소모되지 않는다.
8. 문/구조물을 부순다 → **현행과 완전히 동일**(실드 없음).
9. 🔴 **앵커 재기저 검증**(§5-4-1): 실드를 **일부만** 깎인 직후(재생 지연이 아직 안 끝난 상태) 레벨업 프리즈에서 **`+MaxShield` 카드**를 고른다 → 늘어난 실드가 **그대로 남는다**(0.1초 뒤에 옛 값으로 되돌아가면 실패).
10. FF 를 켜고 아군을 쏜다 → **흡혈이 발동하지 않는다**(§11-7 — 의도된 유지). 자기 발밑 로켓도 마찬가지.

---

## 13. 게이트 원장

### 13-0. G1 플랜 게이트 원장 (2026-09-01, Fable) — **1차 반려 → 보강 완료**

> 통과 판정: 구조 골격(순수함수 공유규칙 + 2저장소 + 프리즈-멈춤 전투시계 + 프로파일 DA + 적 무틱·무복제 지연재생 + 복제 3→5 상쇄 논거 + 자매 행 비차단 + 비목표 범위)은 **전부 검증 통과**. 반려 사유는 골격이 아니라 C2 가 즉시 부딪히는 명세 구멍.

| 심각도 | 지적 | 처리 | 반영 위치 |
|---|---|---|---|
| P2-1 | 플레이어 재생 앵커 미폐쇄 — 드라이버가 카드/GE 의 실드 쓰기를 다음 틱에 되덮음. 앵커 초기화도 미명세 | **수용** | §5-4-1 재기저 규칙 신설 + 드라이버 **단조증가** 안전망 + §5-5 앵커 초기화 3경로 표 |
| P2-2 | 대실드 계수의 소스→`FFPSRDamageSpec` 배선이 어디에도 없음 → 전 무기 1.0 고정으로 컴파일, 단위테스트 전부 통과 | **수용** | §5-9 신설(저작 자리·카드 축·5경로 구성 지점·발사체가 나르는 이유·Fragment 훅) + §4 파일 3개 추가 + §12-4 ⑥ |
| P2-3 | `MaxTotalReduction` 이 §9 표에만 있고 DataAsset 선언에 없음 → 1.0 저작 시 V1 붕괴 | **수용** | §5-2 UPROPERTY(`ClampMax 0.99`) + `IsDataValid` 5번째(에러) |
| P2-4 | FF/자가 흡혈을 "결함 수정"으로 분류한 것은 오진 — 현행은 의도된 반-파밍 게이트이고, 열면 **솔로 자폭 무한 회복 루프** | **수용(오진 정정)** | §6 전면 재작성 — `DamageDealt` 는 채우되 흡혈 이벤트는 `bWasEnemy` 게이트 유지. §11-7 로 사용자 결정 승격. §12-6·PIE 10 |
| P2-5 | 설계 변경인데 SSOT/ADR 갱신이 파일 목록에 0건 | **수용** | §4 에 문서 4개 등재 + "문서가 먼저다" 못박음 + §12-8 검증항목 |
| P3-1 | 파일 목록·사용자 작업 목록 불완전(`FPSRReviveComponent` · 체력바 위젯 · HUD 콘텐츠 3종) | **수용** | §4 · §11-1 사용자 작업 7항목 |
| P3-2 | `DamageDealt` 단위 혼합(SDM 스케일된 실드 포인트) · 디렉터 센서 기준선 이동 | **수용** | §11-8 신설 + §12-9 로 G2 에 올림 |
| P3-3 | `EFPSRWeaponStat` 말미 추가 규칙 미명시 | **수용** | §5-9 ② |
| P3-4 | 플레이어 `FMitigation` 구성(프로파일 × 어트리뷰트) 모호 | **수용** | §9 합성 규칙 |
| P3-5 | V1 각주가 `Health==0 && Shield>0` 에서 거짓 · `(§12-8)` 유령 참조 · 보드 행 원 스코프 문구 | **수용** | §5-1 V1 정밀 서술 + V3 신설 · §11-3 참조 삭제 · 보드 행은 2026-09-01 로그로 이미 갱신 |
| P3-6 | (긍정 확인) 10Hz 드라이버 전제 성립(`FPSRPlayerState.cpp:28` NetUpdateFrequency 100) · 힐팩 per-actor Tick 은 기존 패턴 준수 | 지적 아님 | — |

**추가 발견 (G1 이 실측으로 확인해 준 것, 기각 아님)**: 이월 산술은 **청킹 중립**이다 — 초안이 "한 방이 클수록 유리"라 쓴 것은 부정확했다. 이월은 실드 게이트가 만드는 **페널티를 제거**할 뿐이고, 저격 메리트의 본체는 `ShieldDamageMultiplier` 데이터축이다. §2 목표 5 · §5-1 V3 · §12-4 ④ 를 그에 맞게 고쳤다.

**미검증으로 남은 것(G1 1차 자기 보고)**: Notion 보드 행 원문(프롬프트 요약만 근거) · `DA_Card_Character_Lifesteal` 에셋 내부값 · GAS `Mixed` 모드 어트리뷰트 복제 세부.

#### 13-0-2. G1 2차 (2026-09-01, `4637cb14` 델타 재판정)

**1차 지적 10건 폐쇄 = 전건 확인.** 재검토 요청 4지점도 **전부 실측 통과**:
- **앵커 재기저가 닫힌다** — `Shield` 기록자 전수 = 드라이버·데미지·초기화(가드 ON) / 카드·GE·`MaxShield` 연동(가드 OFF), 그 밖의 기록자 코드에 없음(흡혈·힐팩·부활 체력은 전부 `Health` 기록자). `+MaxShield` 카드의 **재기저는 정확히 1회**(앵커가 어트리뷰트가 아닌 평범한 멤버라 재진입 연쇄 없음), 순서도 안전(연동 증가 시점에 `MaxShield` 가 이미 신값이라 클램프 상한이 옳다).
- **완파 판정 `ShieldAtLastDamage <= 0` 성립** — 지연 클래스는 사이클 단위 성질이라 재생 중 현재 실드가 0 을 벗어나도 앵커 0 유지가 옳고, 질의 API(현재값)와 지연 클래스(앵커)는 용도가 달라 충돌 없음.
- **§5-9 5경로 배선 누락 0** — 브릿지 외부 호출자 전수 grep 이 명세 표와 정확히 일치, 인용 줄번호 6개 전부 실측 일치. **차지레이저 warm-up**: 그 파일의 브릿지 호출이 단일 지점이라 warm-up 칩뎀과 payoff 를 Spec 구성 1곳이 함께 덮는다. **보스 폭발**은 브릿지 미경유 별도 팀-경로이고 적→플레이어 공격이라 SDM 기본값 1.0 이 옳다 — 미등재가 맞다.

| 심각도 | 지적 | 처리 | 반영 |
|---|---|---|---|
| P2 | **개정이 새로 만든 결함** — 1차 P3-5 를 고치면서 V1 을 `HealthSpent > 0` 형태로 재서술했는데 **통상 실드 흡수에서 거짓**(`Shield 100·Health 50·Incoming 10` → `HealthSpent = 0`). "무적 불가"를 "체력 관통 보장"으로 바꿔치기. §11-5 는 구 형태를 유지해 **한 명세에 V1 이 둘**. §12-4 ② 는 실드 있는 픽스처면 적색·없으면 공허 통과 | **수용** | §5-1 V1 을 `TotalSpent()` 형태로 복원 + 보조형태 V1a/V1b + **경우분해 증명** + 도달불가 예외 1개 명시 · §12-4 ② 를 두 픽스처(ⓐ`TotalSpent>0` ⓑ`Shield==0` 에서 `HealthSpent>0`)로 · §11-5 를 §5-1 단일정의 참조로 |
| P3-a | §12-4 ⑥ 이 "배선 도달을 순수함수 층에서 고정"한다고 주장 — 테스트가 Spec 을 직접 만들므로 증명 불가 | **수용** | 주장 삭제, 배선 검증은 §12-10 PIE 2 소관으로 명시 |
| P3-b | §5-4-1 에 권위 가드 명시 없음 · `bVitalsWriting` 소유 위치 미지정 | **수용** | 서버 전용 명문화 + 가드 소유 = `AFPSRCharacter`(앵커가 사는 곳), 어트리뷰트 셋은 상태를 안 든다 |
| P3-c | 향후 **실드를 깎는** 효과가 생기면 앵커만 낮아지고 시각은 그대로라 다음 틱에 즉시 만충 복귀 | **수용** | §5-4-1 후속 저작 주의 |

**미검증(G1 2차 자기 보고)**: 보드 행 갱신 주장(보드 미조회 — 메인 세션이 `4637cb14` 이전에 실행·확인) · ChargeLaser/Melee 의 `GetResolvedStats` 확보 줄(브릿지 호출 줄은 실측, 스탯 조회 줄은 Hitscan 만 실측).

#### 13-0-3. G1 3차 (2026-09-01, `16e810e6`) — ✅ **통과**

2차 지적 4건 폐쇄 전건 확인 · **신규 결함 0** · 델타(+34/−8)가 주장한 절 밖을 안 건드림. 판정 근거로 G1 이 **직접 재검산·실측한 것**:
- **V1 경우분해 증명이 참**(ⓐⓑⓒ 각각 재유도). 계수 0·아머 DR 1.0 조합에서도 `MinKeep` 하한이 받치고, `SDM==0` 우회가 **SDM 에만** 걸려 있어 증명 전제와 정합.
- **예외의 도달 불가가 양쪽 코드로 실측됨** — 적: `FPSREnemyHealthComponent.cpp:40` `bDead` 조기반환 + `:54` 에서 체력 0 도달과 `bDead=true` 가 **같은 호출 안**이라 "0 체력·생존" 상태가 존재할 틈이 없다. 플레이어: `IsIncapacitatedLocal()` 조기반환 + DBNO 전이가 같은 타격 안에서 동기 완료.
- **V1 정의 단일성** — 파일 전체 V1 언급 9곳 전수 확인, 형태가 다른 재진술 0. §11-5 의 인라인 요약("살아 있는 대상이면")은 `alive ⇒ Health>0 ⇒ 예외 비적용` 이라 **참인 따름정리**.
- **§12-4 ② 두 픽스처가 비공허**하고, "`HealthSpent>0` 을 걸면 안 된다"를 테스트 문구에 못박은 것이 2차 오류의 재발을 테스트 층에서 차단한다.
- **가드 소유 경로 성립** — `FPSRCharacter.cpp:506` `InitAbilityActorInfo(PS, this)` 로 아바타 = 캐릭터 실측.
- **초기화 중 `SetMaxShield` 재진입 순서 무해** — 연동 증가가 먼저 실드를 올려도 `[0, MaxShield]` 클램프로 수렴하고 초기화 경로가 마지막에 앵커를 명시 세팅하므로 최종 상태가 같다(오늘 도는 `MaxHealth` 초기화와 동일 역학).

**G1 통과 후 메인 세션이 추가한 것**(구조 무변경, 함정 방지 1건): §5-4-1 에 "`GetOwningActor()` 는 PlayerState 를 돌려준다" 경고 + 도달 경로 2안(`GetAvatarActor()` 권장 / `UFPSRCombatSet::ApplyMoveSpeedToOwner` 선례). 이걸 안 적으면 구현자가 바로 캐스트해 nullptr 을 받고 **재기저가 영영 안 돈다** — 컴파일·단위테스트를 통과하는 형태로 P2-1 이 되살아난다.

### 13-1. G2 레드팀 지적 원장 (C3 에서 채운다)

> `Workflow.md` §6-6-1. **기각엔 근거가 필요하다** — 제1원리 조항 / 코드 인용 / 실측치 중 하나.

| 심각도 | 지적 (요약 + 파일:줄) | 처리 | 근거 |
|---|---|---|---|
| P1 | | 수용 / 기각 / 보류 | |
| P2 | | | |
| P3 | | | |

- **레드팀에 무엇을 줬나**: (C3 에서 기입)
- **G1↔G2 사이에 들어간 "플랜에 없던 구조 결정"**: (C3 에서 세어 기입 — §6-5-2 의 명시 요구)

---

## 13-2. C3 검증 원장 (2026-09-01, Opus · 커밋 `3815285f`)

### 통과한 것 (도구 결과 원문)

| # | 검사 | 결과 |
|---|---|---|
| 2 | 빌드 `-DisableUnity` | **`Result: Succeeded`** (35.69s) |
| 2 | 빌드 `-DisableAdaptiveUnity -ForceUnity` | **`Result: Succeeded`** (14.29s). `[Adaptive Build] Excluded from … unity file:` 줄 **없음**, `C4459`/`C2084` **없음** |
| 3 | `FPSRoguelite.Smoke.ModuleLoads` | `Result={Success}` |
| 3 | `FPSRoguelite.Enemy.BlueprintParent` | `Result={Success}` — 적 BP 에 `VitalsProfile` UPROPERTY 를 추가했으므로 필수 가드([[cpp-uproperty-name-collides-with-bp]]) |
| 4 | `FPSRoguelite.Combat.Vitals` | `Result={Success}` |

### 표적 명세 대조 (전수 아님 — 아래 「남은 것」 참조)

- ✅ **§5-1 산술** — `FPSRVitals.cpp` 가 의사코드와 1:1(`Consumed` 0-나눗셈 가드 · `MinKeep` 하한이 `SDM==0` 에 미적용 · `bShieldBroke` 를 진입 시 `Shield>0` 과의 전이로 판정).
- ✅ **§5-4-1 함정 회피** — `FPSRHealthSet.cpp` 가 `GetOwningActor()` 가 아니라 **`ASC->GetAvatarActor()`** 를 쓰고, 왜 그런지(PlayerState 를 캐스트하면 조용히 nullptr → 재기저가 영영 안 돌아 P2-1 재발)를 주석에 남겼다. 서버 권위 게이트도 있다.
- ✅ **enum 말미 추가** — `EFPSRHitMarkerType::ShieldBreak` · `EFPSRWeaponStat::ShieldDamageMultiplier` 둘 다 마지막 항목으로 들어갔다(중간 삽입 0 = 저작 에셋의 `uint8` 저장값 무이동).
- ✅ **§12-4 6항목** — 테스트 파일이 6개를 전부 구현했고, ②는 **두 픽스처**이며 *"여기서 `HealthSpent>0` 을 걸면 안 된다"* 를 G1 2차 경위와 함께 주석으로 못박았다. ⑥의 한계(배선 도달은 순수함수로 증명 불가 → §12-10 PIE 2 소관)도 주석에 있다.
- ✅ **`UFPSREnemyHealthBarWidget`** — `BlueprintImplementableEvent` 라 C++ 구현체가 없다. 헤더에 계약만 확장한 것이 **올바른 처리**이고 누락이 아니다(실드바 배선 = §11-1 사용자 콘텐츠 작업).

---

## 13-3. C3 전체 diff 대조 원장 (2026-09-02, Opus · `6886fbfc..3815285f` 35파일 전수)

### 명세 일치 확인 (§13-2 가 「미확인」으로 남긴 것 포함, 전건 통과)
- **§7 `MARK_PROPERTY_DIRTY` 전수** — 표가 지정한 4지점(`ApplyDamage`·`InitializeVitals`·`ResetForReuse`·`CatchUpShieldRegen` 값변화시) 전부 존재. Push Model 파라미터도 `FDoRepLifetimeParams{bIsPushBased=true}` + `DOREPLIFETIME_WITH_PARAMS_FAST` 로 기존 3개와 동일.
- **§8 수명주기** — `AcquireEnemy` 가 `Activate()`(내부 `ResetForReuse`) **뒤에** `InitializeVitals` 를 부른다(순서 요구 충족). 이월 캐리 `ServerResetEliteForStageCarry` 는 **무접촉**(diff 미포함, 호출 경로도 바이탈 미경유).
- **§5-9 5경로가 실제로 `ResolvedStats` 를 읽는다** — Hitscan(`:93` `&Instance->GetResolvedStats()`) · ChargeLaser · Melee · **발사체 GA 발사시점**(`FPSRGA_WeaponFire_Projectile.cpp:255`) · Fragment(`Context.Instance->GetResolvedStats()`). 즉 **카드로 조정된 값**이 도달한다(원본 DA 값이 아니다).
- **힐팩** — 수집 게이트 = `AFPSRPlayerState::IsAlive()`(`LifeState == Alive` → DBNO·Dead 제외, §5-8 요구 충족) · 리스폰/수집 전부 `GetCombatClockSeconds()` 기준.
- 그 밖 §5-1~§5-8·§6·§12-4 전항 일치(§13-2 표적검사분 포함).

### 🔴 전체대조가 새로 잡은 결함 3건

| 심각도 | 지적 | 처리 |
|---|---|---|
| **P1** | **`AFPSRHealthPickup` 이 아예 틱하지 않는다.** 생성자가 `TickInterval` 만 세팅하고 `PrimaryActorTick.bCanEverTick = true` 를 빠뜨렸다. 엔진 기본값은 `false`(`Actor.cpp:276` 실측). C2 가 근거로 든 `AFPSRMission_CarryNoHit` 는 베이스 `AFPSRMissionActor.cpp:22` 에서 상속받는 것이고, 실제 형제 `AFPSRXPPickup.cpp:19` 는 직접 켠다. → 수집·리스폰 전무 = **요구 3(맵 힐팩) 전면 무동작**. 빌드·자동화 3종 전부 통과하므로 **PIE 아니면 안 잡힌다** | **수정**(1줄 + 함정 주석) |
| **P2** | **FF 아군·자폭에 히트마커가 새로 뜬다(회귀).** §6 이 지시한 "플레이어 분기 `DamageDealt` 채우기"의 부작용. 5경로 마커 게이트가 전부 `DamageDealt > 0` 인데, 그 게이트 주석이 *"friendly players leave DamageDealt 0"* 이라는 **이제는 거짓인 전제**를 명시하고 있었다. 자폭은 FF 설정과 무관하므로 **로켓 점프마다 자기 마커**가 뜬다 | **수정** — `FDamageResult::bTargetIsPlayer` 신설(§5-7) + 5곳 명시 게이트 + 거짓 주석 5개 정정. 사용자 결정 2026-09-02 |
| **P2** | **요구 5(본인 실드 파손 경고) 경로가 코드에서 끊겨 있다.** `OnShieldBroken` 구독자 0 · `Message.Player.ShieldBroken` 발행자 0. §4 파일 목록에 배선 자리가 없던 **명세 갭**(§11-6) — C2 는 명세를 충실히 따랐다 | **수정** — §5-5-1 신설 + 구현. 사용자 결정 2026-09-02. 엔진 실측으로 훅을 `PostAttributeChange` 가 아닌 GAS 값변경 델리게이트로 확정(원격 클라 도달성) |

### C2 가 명세 없이 판단한 것 (§6-5-2 가 요구하는 「플랜에 없던 구조 결정」 목록 — 전부 **유지**)
1. `AFPSRProjectile::TryDamageActor` 에 `bOutShieldBroke` out-param · `NotifyInstigatorHitMarker` 에 `bShieldBreak` 인자 — 없으면 발사체만 ShieldBreak 마커를 못 낸다. C2 가 코드 주석에 스스로 "added beyond VIT1 §4's file list" 라 명시.
2. `FPSRCombat::NotifyHitMarker` 에 `bShieldBreak = false` 인자 — §5-7 은 `ResolveHitMarker` 신설만 명세. 폭발 경로가 ShieldBreak 를 내려면 필요.
3. **`OnRep_Shield` 가 `OnHealthChanged` 를 재발화** — §8 은 "초기 1회 동기화"만 명세. 실드 전용 피격이 클라 체력바에 안 닿는 문제를 푼다. ⚠️ 부수효과 = 기존 구독자도 재호출된다. 실측: `AFPSRDestructible` 은 `MaxShield=0` 이라 도달 불가, `AFPSREnemyBase::HandleHealthChangedForHitFlash` 는 **감소-엣지 게이트**라 무해(대신 **실드 전용 피격에는 히트플래시가 안 뜬다** — 폴리시 항목, P3).
4. `MARK_PROPERTY_DIRTY(Shield/MaxShield)` 를 §7 표보다 2곳 더 찍는다(`BeginPlay`·`InitializeMaxHealth`) — 각각 값 정의·회귀함정 7. 정당.
5. `ResetForReuse` 의 `LastDamageCombatTime` 을 §8 지정 `-1e9` 가 아니라 **현재 전투시각**으로 — `Shield == MaxShield` 라 재생 경로가 조기 반환, 동작 동일하고 더 안전.
6. `OnShieldBroken` 에 `bShieldBrokenBroadcast` 1회성 가드 — 기존 `bOutOfHealthBroadcast` 패턴 답습.
7. `GetCombatClockSeconds` 에 `UFUNCTION(BlueprintPure)` 부여.
8. `FFPSRResolvedVitals::Resolve` 가 `MaxHealth` 에 `Max(1.0f, …)` 하한(프로파일 `ClampMin=1.0` 과 정합).
9. `ResolveDefense` 가 조기이탈 없이 전수 스캔(중복 정확일치 시 **마지막** 것이 이긴다) — `IsDataValid` 가 중복을 에러로 막으므로 무해.
10. `ApplyVitalsProfile` / `ResetShieldToBroken` 함수 신설 — §5-5 앵커 초기화 3경로 표의 구현 형태.
11. 힐팩이 `IsRunPaused()`/`IsStageTransitionActive()` 를 직접 게이트(`AFPSRXPPickup` 패턴 답습) · 플레이스홀더 메시를 잡지 않음(콘텐츠 할당 전제).
12. §4 목록에 없는 파일 3개 — `FPSRGA_WeaponFire_Projectile.cpp`(§5-9 ④가 요구하나 §4 누락) · `FPSRVitalsTest.cpp`(§12-4 가 요구하나 §4 누락) · `FPSRProjectile.h`(위 1번).

### C3 검증 재실행 (수정 3건 반영 후)
| # | 검사 | 결과 |
|---|---|---|
| 2 | 빌드 `-DisableUnity` | **`Result: Succeeded`** |
| 2 | 빌드 `-DisableAdaptiveUnity -ForceUnity` | **`Result: Succeeded`** · `[Adaptive Build] Excluded` 줄 없음 · `C4459`/`C2084` 없음 |
| 3·4 | `Combat.Vitals` · `Enemy.BlueprintParent` · `Smoke.ModuleLoads` | 3건 전부 `Result={Success}` (`3 tests performed`) |
| 6 | 흡혈 게이트 회귀 | `SendDealtDamageEvent` 는 여전히 `bWasEnemy && DamageDealt > 0` 안에 있고 **적 분기에서 return** 하므로 플레이어 분기는 도달 불가 — §6·§11-7 유지 확인 |

### 남은 것
1. **G2 머지 게이트**(Fable, 코어 갈래 남은 1회) — §12-9 지정 4건 + 위 P1/P2 3건을 함께 올린다.
2. **머지**(§6-7 `--no-ff`) → 보드 완료 마킹 + `Docs/WorkLog.md` 이관.
3. **사용자 런타임 검증**(§12-10 PIE 10항목) + **사용자 콘텐츠 작업 7항목**(§11-1) — 그 전까지 보드는 `검증중`.
