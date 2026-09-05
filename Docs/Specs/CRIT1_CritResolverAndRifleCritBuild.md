# CRIT1 — 치명타 공용 리졸버 + 라이플 치명타 빌드 카드 세트

## 1. 메타

| 항목 | 값 |
|---|---|
| 유닛 ID / 이름 | CRIT1 / 치명타 공용 리졸버 + 라이플 치명타 빌드 |
| 브랜치 | **`main` 직접** (2026-09-05 사용자 결정 — 트렁크 기반 개발로 전환, `Workflow.md` §6-7 개정) |
| 작성 모델 | `claude-opus-5` (`Workflow.md` §6-5-2 개정 2026-08-26 — C1 설계 = Opus, Fable 은 G1/G2 게이트) |
| 작성일 / 최종 갱신 | 2026-09-05 (rev2 — G1 지적 반영 + P-C 추가) |
| 상태 | `확정`(G1 조건부 통과 → P1 2건·P2 6건·P3 반영 완료) |
| 관련 SSOT | `CombatWeaponCard.md` §2-3-1·§2-3-2·§2-3-5·§2-3-8·§2-3-9·§2-3-10 / `PlayerFeel.md` §2-14 / `Workflow.md` §6-5-2·§6-7 |
| 관련 메모리 | [[card-pool-routing]] [[production-structure-first]] [[reason-in-multiplayer-terms]] [[code-is-immutable-structure-only]] [[da-edits-are-user-work]] [[weapon-modular-evolution-scope-plan]] [[uasset-strings-name-table-only]] |

### 단계 구성

| 단계 | 내용 | 게이트 |
|---|---|---|
| **P-A** | 치명타 공용 리졸버 + 프래그먼트 5종 + 카드 5장 | G1 통과(2026-09-05) · G2 = 푸시 전 |
| **P-C** | 라이플 슬롯 상한 5 + **조준 배율 스탯 축 신설** + 스코프를 스탯 임계 진화로 이관(SniperScope 카드 은퇴) | 신규 축 = §2-3-8 쿡북 인정 패턴(코어 갈래 아님) · G2 동반 |
| (P-B) | **빌드 시너지 추첨 수렴**(같은 빌드 카드 가중치) = 사용자 지시로 이번 작업 흐름에 포함하되, 추첨 알고리즘 + 카드/CSV 스키마 확장이라 **자체 G1/G2 를 갖는 후속 유닛(CRIT2)** 으로 분리한다. 이번 유닛이 먼저 들어가야 수렴을 실측할 빌드 1개가 생긴다 | 별도 |

---

## 2. 목표 / 비목표

### 목표 (동작 기준)

1. **치명타 규칙이 한 곳에서 정해진다.** 지금 치명타 굴림은 5개 데미지 경로(Hitscan / ChargeLaser / Melee / Projectile / Explosion)에 **같은 3줄이 복붙**돼 있다. 새 규칙을 넣으면 5곳을 각각 고쳐야 하고, 하나를 빠뜨리면 **그 무기군에서만 조용히 안 나는** 결함이 된다 — 이 프로젝트는 히트마커 우선순위에서 같은 병을 앓고 `ResolveHitMarker`(`FPSRCombatStatics.h:145`)로 접은 전례가 있다. 같은 처방을 쓴다.
2. **라이플 치명타 빌드 해금 카드 5장이 동작한다** (전부 `DA_Weapon_Rifle.UnlockableFeatures` = 미션 클리어 풀):
   | # | 카드 | 동작 |
   |---|---|---|
   | 1 | 치명타 추가타 | 치명타가 **실제로 입힌 피해**의 50% 를 같은 대상에 **2차 데미지 인스턴스**로 다시 넣는다 |
   | 2 | 치명타 흡혈 | 치명타가 실제로 입힌 피해의 10% 만큼 시전자 회복 |
   | 3 | 재장전 각성 | 재장전 **완료** 시 치명타 확률 +20%p · 치명타 배수 +0.20, **5초**(DA 조정) |
   | 4 | 약점 관통 | **약점**에 맞은 타격은 굴림 없이 확정 치명타 |
   | 5 | 활강 집중 | **라이플을 든 채** 슬라이딩 진입 시 치명타 확률 +40%p, **5초**. 다시 슬라이딩하면 지속시간 **초기화** |
3. **치명타 배수 기본값이 사양과 일치한다** — `GlobalCritMultiplier` 2.0(=+100%) → **1.5**(=+50%).
4. **타임드 버프가 전역 프리즈(§2-2)에 타지 않는다** — 카드 선택 30초 프리즈 동안 5초 버프가 소멸하지 않는다.
5. **신규 복제 0.**
6. **(P-C) 라이플이 한 빌드를 온전히 담는다** — `MaxFragmentSlots` 3 → **5**. 규칙 = **한 무기 = 한 빌드**. 빌드가 더 추가돼도 이 값은 올리지 않는다(카드 총량이 느는 건 *선택지의 폭*이지 *필요 슬롯*이 아니다).
7. **(P-C) 스코프가 슬롯을 먹지 않는다** — 조준 배율(`ADSFieldOfView`)이 **카드로 강화 가능한 무기 스탯 축**이 되고, 라이플 사이트 슬롯이 그 값의 임계로 스코프 단계에 진입한다. `DA_CardModifiers_SniperScope` / `DA_Fragment_Rifle_SniperScope` 는 **은퇴**한다.

### 비목표 (일부러 하지 않는 것)

- **라이플 전용 치명타 *스탯* 카드**(무기 축 `CritChance/CritMultiplier` 신설). 치명타는 캐릭터 전역 속성으로 남는다. 빌드의 스탯 축은 기존 `DA_Card_CritChance`·`DA_Card_CritMult`·`DA_Card_Damage`·`DA_Card_FireRate_*` 로 충족한다. (보드 행 「스탯 카드 풀-출처 기반 통합」이 All/This 쌍을 재설계 예정이라, 지금 새 쌍을 만들지 않는 것이 그 행과의 충돌도 피한다.)
- **캐릭터 해금 카드 루트 신설**(`EFPSRCardRoute` 확장). 5장 전부 라이플 기능 카드다.
- **빌드 시너지 추첨 수렴** = P-B(CRIT2)로 분리. 이번 유닛에서 카드에 빌드 태그를 달지 않는다(스키마를 두 번 흔들지 않기 위해 CRIT2 가 한 번에 넣는다).
- **치명타 버프 HUD 표기**. 버프는 비복제라 클라가 모른다. 관측은 §12 의 검증 로그로만.
- **약점 컴포넌트 추가 저작**(원거리·엘리트). 콘텐츠 판단(§11).
- **기존 5경로의 굴림 *단위* 변경.** 근접 = 스윙당 1굴림, ChargeLaser = payoff 샷 한정, 나머지 = 타격당. **현행 단위를 그대로 보존**한다.
- **프래그먼트 교체 UI**(`ServerSelectCardReplacement` 의 미구현 프론트). 슬롯 상한 5 가 이번 대응이다.
- 재장전 타이머 자체의 프리즈 관통(월드 `FTimerManager`) 교정 — 선행 결함, 범위 밖.
- `FPSRoguelite.Editor.CardCsv.RoundTrip` 자동화 실패(선행 결함) 수정.

---

## 3. 제1원리 3줄 (핵심원칙 4)

1. **제1원리 근거** — 이 게임의 제약은 *적 200~300 을 싸게*다. 치명타 판정은 **적이 아니라 플레이어(≤4)의 사격**에 붙는 비용이라 스웜 예산과 직교한다. 그래서 아껴야 할 것은 CPU 가 아니라 **경로 수**다: 규칙이 5곳에 복제돼 있으면 규칙이 하나 늘 때마다 5배로 틀릴 수 있다. 2차 데미지 인스턴스는 **치명타가 난 타격에서만** 추가 `ApplyDamage` 1회(기본 확률 5%)이므로 스웜 틱·복제·액터당 비용에 붙는 항목이 아니다.
2. **엔진 기본값·기존 인프라와의 관계** — ① **GAS 를 덮지 않는다**: 치명타는 지금도 GE 가 아니라 `ASC->GetNumericAttribute` 직독 + 순수 산술이다(비-GE 데미지 = Game.md §1 의 명시적 선택). 값을 struct 하나로 묶기만 한다. ② **타임드 버프를 GAS 지속 GE 로 만들지 않는다** — 엔진 GE 지속시간은 월드 시간이라 §2-2 전역 프리즈를 관통한다(`CombatWeaponCard.md` §2-3-5 🔴 미해결, WorkLog "화상 GE 5초가 카드 프리즈 중에도 실시간으로 탄다"). 5초 버프에 쓰면 레벨업 프리즈 한 번에 증발한다. 대신 VIT1 이 만든 **프리즈-멈춤 전투시계**(`AFPSRGameState::GetCombatClockSeconds()`, 틱 0)를 만료 기준으로 **재사용**한다. ③ **프래그먼트 계약을 따른다**: 무상태 공유 에셋 + `FFPSRFireContext` 훅. 상태(잔여시간)는 프래그먼트가 아니라 **무기 인스턴스**에 둔다. ④ **(P-C) 새 stat 축은 데이터화하지 않는다** — `CombatWeaponCard.md` §2-3-8 쿡북이 "새 무기 stat **축** = enum + `RecomputeResolved` switch case(**컴파일체크 유지**, 데이터화 금지)"로 못박아 둔 그대로 따른다.
3. **프로젝트 제약과의 정합** — 4인 협동 기준선: 치명타 굴림·버프·회복·2차 인스턴스는 **전부 서버에서만** 일어나고 클라는 결과만 본다. 버프는 무기 인스턴스에 붙으므로 무기를 바꾸면 따라오지 않는다. 확장 축: 새 치명타 규칙 = `FFPSRCritContext` 필드 1개 + 프래그먼트 서브클래스 1개(5경로 무수정) — §2-3-1 의 OCP directive 와 같은 방향.

---

## 4. 파일 목록

### P-A 코드

| 경로 | 신규/수정 | 한 줄 설명 |
|---|---|---|
| `Public/Combat/FPSRCritTypes.h` | **신규** | `FFPSRCritContext` USTRUCT. 별도 헤더인 이유 = `FPSRProjectileTypes.h`·`FPSRCombatStatics.h`·`FPSRWeaponFragment.h` 셋이 모두 쓰는데 셋 사이엔 의존이 없다 |
| `Public/Combat/FPSRCombatStatics.h` / `Private/Combat/FPSRCombatStatics.cpp` | 수정 | `RollCrit`·`ComputeCritRiderMagnitudes`·`ApplyCritRiders` + `ApplyExplosion` 시그니처 전환 + 폭발 루프 라이더 |
| `Public/Weapon/FPSRWeaponFragment.h` / `Private/Weapon/FPSRWeaponFragment.cpp` | 수정 | 훅 3종 + 브릿지 3종 + 프래그먼트 5종. **`UFPSRFragment_ExplosiveRounds::OnImpact` 의 `ApplyExplosion` 호출부**를 기본 컨텍스트(`FFPSRCritContext{}`)로 전환 = "스플래시는 치명타 없음" 현행 보존 |
| `Public/Weapon/FPSRWeaponInstance.h` / `Private/Weapon/FPSRWeaponInstance.cpp` | 수정 | 서버 전용 타임드 치명타 버프 슬롯 + 3함수 |
| `Public/Weapon/FPSRProjectileTypes.h` | 수정 | `CritChance`+`CritMultiplier` 2필드 → `FFPSRCritContext Crit` 1필드 |
| `Private/Weapon/FPSRProjectile.cpp` | 수정 | 굴림 → `RollCrit`, 치명타 직후 → `ApplyCritRiders`, `ApplyExplosion` 호출부 전환 |
| `Private/Enemy/FPSREnemyBase.cpp` | 수정 | **(G1 P2-1)** `:1107-1108` 이 `Params.CritChance/CritMultiplier` 를 직접 세팅한다 → 두 줄 삭제. 기본값 `Crit{}`(Chance 0)가 "적 발사체는 치명타 없음" 계약을 그대로 보존한다 |
| `Private/AbilitySystem/Abilities/FPSRGA_WeaponFire_Hitscan.cpp` | 수정 | 컨텍스트 빌드 + 굴림/라이더 치환 |
| `Private/AbilitySystem/Abilities/FPSRGA_WeaponFire_ChargeLaser.cpp` | 수정 | 〃 (payoff 샷 한정 현행 유지) |
| `Private/AbilitySystem/Abilities/FPSRGA_WeaponMelee.cpp` | 수정 | 〃 (스윙 1굴림 단위 보존) |
| `Private/AbilitySystem/Attributes/FPSRCombatSet.cpp` | 수정 | `InitGlobalCritMultiplier(2.0f)` → `1.5f` |
| `Private/Weapon/FPSRWeaponInventoryComponent.cpp` | 수정 | `FinishReload()` 끝에 `NotifyReloadFinished` |
| `Private/Hero/FPSRCharacterMovementComponent.cpp` | 수정 | 권위 슬라이드 상승 에지(`:214-222`)에 `NotifySlideStarted` |
| `Private/Tests/FPSRCritResolverTest.cpp` | **신규** | 순수함수 자동화(굴림·확정크리·라이더 산식) |

### P-C 코드

| 경로 | 신규/수정 | 한 줄 설명 |
|---|---|---|
| `Public/Weapon/FPSRWeaponTypes.h` | 수정 | `EFPSRWeaponStat::ADSFieldOfView` 항목 + `GetAxisValue` case (필드 `ADSFieldOfView` 는 이미 `FFPSRWeaponStatBlock:136` 에 있다) |
| `Private/Weapon/FPSRWeaponInstance.cpp` | 수정 | `RecomputeResolved` switch 에 case 1개 (**하한 클램프 필수** — FOV 0/음수 금지) |

### 콘텐츠 (Claude = 에셋 생성까지 / 수치·DA 저작 = 사용자)

| 대상 | 신규/수정 | 한 줄 설명 |
|---|---|---|
| `Content/Cards/Weapons/Modifiers/DA_Fragment_Rifle_CritOverkill` | 신규 | 카드 1 (`BonusRatio=0.5`) |
| `…/DA_Fragment_Rifle_CritLifesteal` | 신규 | 카드 2 (`HealRatio=0.10`, `HealEffect=GE_Card_LifestealHeal` **기존 재사용**) |
| `…/DA_Fragment_Rifle_CritOnReload` | 신규 | 카드 3 (`+0.20 / +0.20 / 5s`) |
| `…/DA_Fragment_Rifle_WeakpointCrit` | 신규 | 카드 4 (수치 없음) |
| `…/DA_Fragment_Rifle_CritOnSlide` | 신규 | 카드 5 (`+0.40 / 5s`) |
| `Content/Weapons/DataTable/DA_Weapon_Rifle` | 수정 | **(P-C, 사용자)** ① `최대 프래그먼트 슬롯` 3→5 ② 사이트 파츠 슬롯의 진화 단계를 `프래그먼트 스택` → **`스탯 임계`**(축=조준 배율 FOV, 비교=이하, 기준값=사용자) ③ `EvolutionFragment` 참조 해제 |
| `…/DA_CardModifiers_SniperScope` · `DA_Fragment_Rifle_SniperScope` | **은퇴** | 시트 행 삭제 → 임포터가 풀 멤버십에서 제거. 에셋 삭제는 별건(참조 0 확인 후) |
| 구글 시트 **CardCatalog** 6행 | 신규 | `weapon.frag.crit*` 5행 + `weapon.adsfov` 1행 |
| 구글 시트 **Cards** 6행 | 신규 | 기능 카드 5행 + **조준 배율 강화** 스탯 카드 1행. SniperScope 행 삭제 |

> ⚠️ **시트가 마스터다**(`Localization.md` L-5). `Content/Authoring/*.csv` 직접 편집 금지 — 다음 `Scripts/sync-authoring-csv.ps1` 이 덮는다.

---

## 5. 인터페이스 선언 (헤더 스케치)

```cpp
// ── Public/Combat/FPSRCritTypes.h (신규) ──────────────────────────────────────
#pragma once
#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "FPSRCritTypes.generated.h"

class UGameplayEffect;

/**
 * 한 발사(활성화)의 치명타 규칙 전체. 종전에 경로마다 따로 들고 다니던 (CritChance, CritMultiplier) 두 float 을
 * 대체한다 — 규칙이 하나 늘 때마다 5경로 시그니처가 다시 흔들리는 것을 막는 것이 이 struct 의 존재 이유다.
 *
 * USTRUCT 인 이유는 하나뿐: FFPSRProjectileParams(USTRUCT) 의 UPROPERTY 멤버로 실려 발사체 수명 동안 유지돼야 하고,
 * HealEffect(UClass*) 가 그 동안 GC 되지 않아야 한다.
 * BlueprintReadOnly: 채우는 주체는 언제나 서버 C++ 다(BP 가 쓰라고 노출한 것이 아니라 파라미터 구조체가 이미
 * BlueprintType 이라 따라온 것).
 *
 * ⚠️ 이 값은 **발사 시점에 한 번 확정(bake)** 된다. 활성화 도중 ASC 속성이나 버프가 바뀌어도 그 활성화에는 반영되지
 * 않는다 — 종전 5경로가 이미 그렇게 동작했고, 이 유닛은 그 성질을 보존한다.
 */
USTRUCT(BlueprintType)
struct FPSROGUELITE_API FFPSRCritContext
{
    GENERATED_BODY()

    /** 치명타 굴림 확률 [0,1]. = ASC GlobalCritChance + 무기 인스턴스의 활성 타임드 버프 합.
     *  0 = 절대 안 터짐(적이 쏜 발사체가 이 값을 0 으로 남겨 두는 현행 계약 유지). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crit")
    float Chance = 0.0f;

    /** 치명타 성립 시 피해에 곱하는 배수. = ASC GlobalCritMultiplier + 활성 타임드 버프 합. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crit")
    float Multiplier = 1.0f;

    /** 카드 4 — 약점에 맞은 타격은 굴림 없이 확정 치명타. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crit")
    bool bWeakpointAlwaysCrit = false;

    /** 카드 1 — 치명타가 **실제로 입힌 피해**의 이 비율만큼 같은 대상에 2차 데미지 인스턴스. 0 = 없음. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crit", meta = (ClampMin = "0.0"))
    float BonusInstanceRatio = 0.0f;

    /** 카드 2 — 치명타가 실제로 입힌 피해의 이 비율만큼 시전자 회복. 0 = 없음. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crit", meta = (ClampMin = "0.0"))
    float HealRatio = 0.0f;

    /** HealRatio>0 일 때 쓰는 즉발 힐 GE(SetByCaller 태그 `SetByCaller.CardMagnitude`). 에셋 경로 C++ 하드코딩
     *  금지(§6-2) — 프래그먼트 DA 가 저작해 넘긴다. null 이면 회복은 조용히 no-op. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crit")
    TSubclassOf<UGameplayEffect> HealEffect;

    /** 라이더(2차 인스턴스/회복)가 하나라도 걸려 있는가 — 치명타 직후 호출부의 조기 반환용. */
    bool HasRiders() const { return BonusInstanceRatio > 0.0f || (HealRatio > 0.0f && HealEffect != nullptr); }
};


// ── Public/Combat/FPSRCombatStatics.h (namespace FPSRCombat 에 추가) ──────────
namespace FPSRCombat
{
    /** 한 타격의 치명타 판정. **순수 함수**(RNG 소비 외 부작용 없음) — 굴림 단위(타격당/스윙당)는 호출자가 정한다.
     *  WeakpointMult > 1 = 이 타격이 약점에 맞았다. bWeakpointAlwaysCrit 이면 **굴림을 건너뛰고** 참을 돌려준다
     *  (RNG 스트림을 소비하지 않는다). Chance <= 0 이면 즉시 거짓(현행 `CritChance > 0.0f &&` 단축평가 보존). */
    FPSROGUELITE_API bool RollCrit(const FFPSRCritContext& Crit, float WeakpointMult);

    /** 라이더 **산식만** 뗀 순수 함수 (G1 P1-2 — 자동화가 겨냥할 표면). 액터·월드·GE 를 만지지 않는다.
     *  OutBonusDamage = DamageDealt × BonusInstanceRatio · OutHealAmount = DamageDealt × HealRatio.
     *  DamageDealt <= 0 이면 둘 다 0. */
    FPSROGUELITE_API void ComputeCritRiderMagnitudes(const FFPSRCritContext& Crit, float DamageDealt,
        float& OutBonusDamage, float& OutHealAmount);

    /** 치명타가 **실제 피해를 입힌 직후** 호출한다. 위 산식으로 카드 라이더 2종을 적용한다.
     *   ① BonusDamage > 0 → 같은 대상에 2차 `ApplyDamage`. 2차는 치명타 굴림도, 라이더 재적용도 하지 않는다
     *      (재귀 없음 — 이 함수가 `ApplyDamage` 를 직접 부르고 자기 자신을 다시 부르지 않는 것이 그 보증이다).
     *   ② HealAmount > 0 && HealEffect → 시전자 ASC 에 즉발 힐 GE(SetByCaller = HealAmount).
     *
     *  기준값이 **CritHitResult.DamageDealt(실제 소진분)** 인 이유: 오버킬·코프스 재타격이 0 이라 흡혈과 같은
     *  안티-파밍 성질을 갖는다(§2-3-5 흡혈 계약과 동일).
     *  **적에게 입힌 치명타에만** 발동한다(`bWasEnemy && !bTargetIsPlayer`) — 아군 오사·자폭이 회복 펌프가 되는
     *  것을 막는다(§2-3-5 "FF/자가 흡혈은 계속 닫혀 있다", 사용자 결정 2026-09-01).
     *
     *  OutBonus = 2차 인스턴스의 결과(없으면 기본값). **호출 경로가 §6 의 OR 표대로 집계에 반영해야 한다** —
     *  2차 인스턴스가 적을 죽이거나 실드를 깰 수 있기 때문이다.
     *  서버 전용: 호출자가 이미 권위 스코프 안이라고 가정한다. */
    FPSROGUELITE_API void ApplyCritRiders(
        const FFPSRCritContext& Crit, AActor* Instigator, AActor* Target,
        const FDamageResult& CritHitResult, const FFPSRDamageSpec& Spec, FDamageResult& OutBonus);

    /** 🔁 시그니처 전환: (float CritChance, float CritMultiplier) → (const FFPSRCritContext&).
     *  **기본값 인자를 두지 않는다** — 실호출자 2곳(`FPSRProjectile.cpp:326`, `FPSRWeaponFragment.cpp:75`)이
     *  컴파일 에러로 전부 드러나야 "빠뜨린 경로"가 생기지 않는다.
     *  폭발도 대상별로 굴리므로 라이더가 성립한다 — 루프 안에서 `ApplyCritRiders` 를 부르고, 그 결과를 §6 의
     *  OR 표대로 `FExplosionResult`·마커 집계·넉백 제외 판정에 반영한다. */
    FPSROGUELITE_API FExplosionResult ApplyExplosion(UWorld* World, const FVector& Center, float Radius, float Damage,
        const FFPSRCritContext& Crit, AActor* Instigator, bool bAllowSelf, float KnockbackStrength,
        const FFPSRDamageSpec& Spec = FFPSRDamageSpec());
}


// ── Public/Weapon/FPSRWeaponInstance.h (추가) ────────────────────────────────
/**
 * 서버 전용 타임드 치명타 버프 1건(카드 3·5). 프래그먼트는 무상태 공유 에셋이라 잔여시간을 들 수 없으므로 상태는
 * **무기 인스턴스**에 산다 — 그래서 무기를 바꾸면 따라오지 않는다.
 * 만료 시각은 **프리즈-멈춤 전투시계**(`AFPSRGameState::GetCombatClockSeconds`, VIT1)로 잰다.
 */
struct FFPSRTimedCritBuff
{
    /** 갱신 키 = **버프를 건 프래그먼트 에셋 포인터** (G1 P1-1). 태그가 아니다 —
     *  이 리포의 프래그먼트 정체성 규약이 이미 "identity = asset pointer"(`UFPSRWeaponInstance::HasFragment`)라
     *  저작이 필요 없고 두 카드가 서로를 덮어쓸 수 없다. 같은 프래그먼트가 다시 걸면 **덮어쓴다**
     *  (= 카드 5 "슬라이딩 시 초기화"). 서로 다른 프래그먼트는 **합산**된다. */
    const UFPSRWeaponFragment* Source = nullptr;
    float ChanceAdd = 0.0f;
    float MultiplierAdd = 0.0f;
    /** 전투시계 기준 만료 시각(초). */
    float ExpiryCombatTime = 0.0f;
};

// class UFPSRWeaponInstance 안:
public:
    /** 서버 전용. 같은 Source 가 이미 있으면 수치·만료를 덮어쓰고, 없으면 추가한다.
     *  `Source == nullptr` · `Duration <= 0` · 권위 아님 → no-op.
     *  슬롯이 가득 차면(4) 가장 먼저 만료되는 항목을 밀어낸다.
     *  권위 판정 = `GetTypedOuter<AActor>()` 의 `HasAuthority()`(인스턴스는 폰 인벤토리 소유 UObject). */
    void ApplyTimedCritBuff(const UFPSRWeaponFragment* Source, float ChanceAdd, float MultiplierAdd, float Duration);

    /** 현재 살아 있는 버프의 합. 호출 시점에 만료분을 정리(lazy prune)하므로 별도 틱이 필요 없다 —
     *  **이 유닛은 액터당 틱을 하나도 늘리지 않는다**. 권위 무관(클라에선 배열이 비어 0,0 을 돌려준다).
     *  GameState 를 못 찾으면(로비·비런 월드) 전투시계 0 으로 보고 **전부 만료 취급**한다. */
    void SumActiveCritBuffs(float& OutChanceAdd, float& OutMultiplierAdd);

    /** 버프를 비운다. 호출 지점 = **무기 인스턴스 재초기화/스탯 재해결 시점과 런 종료 리셋** 두 곳.
     *  홀스터(무기 교체)로는 지우지 않는다 — 버프가 인스턴스에 귀속된다는 §3-③ 의미가 그것이다. */
    void ClearTimedCritBuffs();

private:
    /** 서버 전용·**비복제**. UObject 참조는 `Source`(raw const 포인터) 하나뿐이며, 이는 항상-로드되는 프래그먼트
     *  DA 를 가리키고 **역참조하지 않는다**(동등비교 키로만 쓴다) → UPROPERTY 가 아니어도 안전.
     *  인라인 4 = 핫패스 무할당(현실적 동시 버프 = 2). */
    TArray<FFPSRTimedCritBuff, TInlineAllocator<4>> ActiveCritBuffs;


// ── Public/Weapon/FPSRWeaponFragment.h (훅 추가) ─────────────────────────────
// class UFPSRWeaponFragment 안:
    /** 치명타 규칙 훅: 발사 1회의 치명타 컨텍스트가 굳기 **전**에 프래그먼트가 규칙을 얹는다(카드 1·2·4).
     *  `ModifyFireMode` 와 같은 계급의 "해결 단계" 훅이며 무상태다 — 활성화당 1회, 타격당이 아니다.
     *  ⚠️ **스택 합성 규칙(고정, G1 P2-5)** — `ActiveFragments` 는 스택당 중복 원소를 갖는다(`MultiShot` 의
     *  스택당 가산 규약과 같은 계급). 따라서 구현은: 비율·가산치는 `+=`, bool 은 `|=`,
     *  `HealEffect` 는 **비어 있을 때만 대입**(먼저 온 것이 이긴다). */
    virtual void ModifyCrit(const FFPSRFireContext& Context, FFPSRCritContext& CritInOut) const {}

    /** 재장전 **완료** 훅(취소 경로 `CancelReload` 는 발화하지 않는다). 서버 권위. */
    virtual void OnReloadFinished(const FFPSRFireContext& Context) const {}

    /** 슬라이딩 **진입** 훅(상승 에지 1회). 서버 권위. */
    virtual void OnSlideStarted(const FFPSRFireContext& Context) const {}

// namespace FPSRWeaponHooks 에 추가:
    /** ASC 속성 + 무기 타임드 버프를 합쳐 컨텍스트를 만들고, 활성 프래그먼트의 `ModifyCrit` 을 돌린다.
     *  치명타를 쓰는 경로의 **유일한 컨텍스트 생성 지점**. ASC 가 null 이면 `{Chance 0, Multiplier 1}`
     *  (적 발사체·비-GAS 시전자 = 절대 치명타 없음, 현행 계약 보존). */
    FPSROGUELITE_API FFPSRCritContext BuildCritContext(const FFPSRFireContext& Context, const UAbilitySystemComponent* ASC);

    /** 현재 무기의 프래그먼트에 재장전 완료를 통지. 라이브 활성화가 없으므로 컨텍스트를 합성한다
     *  (전례 = `AFPSRCharacter::ServerSetAiming_Implementation` 의 OnAim 합성). */
    FPSROGUELITE_API void NotifyReloadFinished(APawn* Avatar, UFPSRWeaponInstance* Instance);

    /** 슬라이딩 진입 통지. 캐릭터 → 인벤토리 → **현재 장착** 인스턴스를 이 함수가 직접 해석한다
     *  (CMC 가 무기 계층을 알지 않게 하려는 것 — 호출부는 1줄). 권위가 아니면 no-op.
     *  ⚠️ 의도된 경계: 버프는 **슬라이딩 시점에 들고 있던 무기**에만 붙는다. SMG 를 들고 슬라이딩하면
     *  라이플은 아무것도 받지 않는다(카드 문구가 "라이플을 든 채"라고 말하는 이유). */
    FPSROGUELITE_API void NotifySlideStarted(APawn* Avatar);


// ── Public/Weapon/FPSRWeaponFragment.h (프래그먼트 5종) ──────────────────────
/** 카드 1 — 치명타 추가타. 2차는 치명타가 아니며 라이더를 재발동하지 않는다. 흡혈·OnHitActor 계열과는
 *  의도적으로 상호작용한다(사용자 결정 2026-09-05). */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_CritBonusInstance : public UFPSRWeaponFragment
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "0.0"))
    float BonusRatio = 0.5f;
    virtual void ModifyCrit(const FFPSRFireContext& Context, FFPSRCritContext& CritInOut) const override;
};

/** 카드 2 — 치명타 흡혈. 힐 GE 는 기존 흡혈 카드의 것을 재사용할 수 있다(SetByCaller 계약 동일). */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_CritLifesteal : public UFPSRWeaponFragment
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "0.0"))
    float HealRatio = 0.10f;
    /** 즉발 힐 GE. null = 회복 no-op(콘텐츠 미저작이어도 빌드·스모크가 깨지지 않는다). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment")
    TSubclassOf<UGameplayEffect> HealEffect;
    virtual void ModifyCrit(const FFPSRFireContext& Context, FFPSRCritContext& CritInOut) const override;
};

/** 카드 3 — 재장전 각성. */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_CritOnReload : public UFPSRWeaponFragment
{
    GENERATED_BODY()
public:
    /** 치명타 **확률**에 더하는 절대치(0.20 = +20%p — 기존 CritChance 카드와 같은 단위). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "0.0"))
    float CritChanceAdd = 0.20f;
    /** 치명타 **배수**에 더하는 절대치(0.20 = 1.5 → 1.7 — 기존 CritMult 카드와 같은 단위). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "0.0"))
    float CritMultiplierAdd = 0.20f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "0.0"))
    float Duration = 5.0f;
    virtual void OnReloadFinished(const FFPSRFireContext& Context) const override;
};

/** 카드 4 — 약점 관통. 수치 없음(마커성 규칙). */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_WeakpointAlwaysCrit : public UFPSRWeaponFragment
{
    GENERATED_BODY()
public:
    virtual void ModifyCrit(const FFPSRFireContext& Context, FFPSRCritContext& CritInOut) const override;
};

/** 카드 5 — 활강 집중. */
UCLASS()
class FPSROGUELITE_API UFPSRFragment_CritOnSlide : public UFPSRWeaponFragment
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "0.0"))
    float CritChanceAdd = 0.40f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (ClampMin = "0.0"))
    float Duration = 5.0f;
    virtual void OnSlideStarted(const FFPSRFireContext& Context) const override;
};


// ── P-C: Public/Weapon/FPSRWeaponTypes.h ─────────────────────────────────────
enum class EFPSRWeaponStat : uint8
{
    // ... 기존 7항 ...
    /** 조준 시 카메라 FOV(도). **값이 작을수록 배율이 높다** — 강화 카드는 음수 퍼센트를 쓴다
     *  (기존 RecoilVertical 카드가 감소를 음수로 저작하는 것과 같은 규약).
     *  파츠 스테이지의 `StatThreshold` 트리거가 이 축을 읽어 스코프 단계로 진화한다(W-U1b). */
    ADSFieldOfView UMETA(DisplayName = "ADS Field of View")
};
// GetAxisValue: case EFPSRWeaponStat::ADSFieldOfView: return ADSFieldOfView;
// RecomputeResolved: case ... : CachedResolved.ADSFieldOfView =
//     FMath::Clamp((CachedResolved.ADSFieldOfView + Add) * Mult, 5.0f, 170.0f); break;
//     ⚠️ 클램프 필수 — 카드가 쌓여 FOV 가 0/음수가 되면 카메라가 깨진다. 하한 5도 = 20배율 상당.
```

---

## 6. 함수별 계약

| 함수 | 권위 | 호출자 | 전제조건 | 실패 시 동작 |
|---|---|---|---|---|
| `RollCrit` | 순수 | 5경로 굴림 지점 | 없음 | `Chance<=0 && !확정` → false |
| `ComputeCritRiderMagnitudes` | 순수 | `ApplyCritRiders`, 자동화 | 없음 | `DamageDealt<=0` → 0,0 |
| `ApplyCritRiders` | 서버 전용 | 5경로, 치명타 타격이 실피해를 낸 직후 | 호출자가 권위 스코프 | `Instigator`/`Target` null · `!HasRiders()` · `DamageDealt<=0` · `!bWasEnemy` · `bTargetIsPlayer` → 조기 반환 |
| `ApplyExplosion` | 서버 전용 | `FPSRProjectile.cpp:326`, `FPSRWeaponFragment.cpp:75` (**2곳**) | 현행과 동일 | 현행과 동일 |
| `BuildCritContext` | 권위 무관(읽기만) | 5경로 활성화 시작 | `Context.Instance` null 가능 | ASC null → `{0,1}`; Instance null → 버프 합 0 |
| `NotifyReloadFinished` | 서버 전용 | `UFPSRWeaponInventoryComponent::FinishReload()` | 호출부가 이미 권위 게이트 | Instance null → no-op |
| `NotifySlideStarted` | 서버 전용 | CMC 권위 슬라이드 상승 에지 | `Avatar` 유효 | 권위 아님·인벤토리/인스턴스 없음 → no-op |
| `ApplyTimedCritBuff` | 서버 전용 | 프래그먼트 훅 2종 | `Source != nullptr && Duration>0` | 권위 아님/전제 위반 → no-op |
| `SumActiveCritBuffs` | 권위 무관 | `BuildCritContext` | 없음 | 버프 없음/GameState 없음 → 0,0 |

### 5경로 적용 순서 (무회귀 계약 — **순서를 바꾸지 않는다**)

```
Hitscan / ChargeLaser / Projectile:
    FinalDamage = Damage × DamageMultiplier
 →  bCrit = RollCrit(Crit, WeakpointMult);  if (bCrit) FinalDamage *= Crit.Multiplier
 →  프래그먼트 OnHitActor (기존)
 →  FinalDamage *= WeakpointMult (기존)
 →  ResolveDamage → ApplyDamage
 →  [신규] if (bCrit) ApplyCritRiders(...)   ← 기준값이 "실제로 입힌 피해"라 ApplyDamage **뒤**여야 한다
 →  히트마커·킬 집계 (기존) + 아래 OR 표
Melee:      스윙 시작에서 1회 RollCrit(Crit, 1.0f) — 단위 보존(약점 확정크리는 근접에 미적용)
Explosion:  대상 루프 안에서 RollCrit(Crit, 1.0f) — 폭발은 약점을 수집하지 않는다(현행 유지)
```

### 2차 인스턴스 결과(`OutBonus`) 반영 표 — **경로마다 다르게 하지 말 것** (G1 P2-2·P2-6)

| 반영 대상 | 규칙 |
|---|---|
| 킬 집계 (`bServerKill` 등) | `\|= OutBonus.bKilled` |
| `NotifyKill` | `OutBonus.bKilled` 면 **호출**(2차 인스턴스가 죽인 적도 처치-트리거 카드의 대상이다) |
| 실드 파괴 (`bServerShieldBroke`) | `\|= OutBonus.bShieldBroke` |
| 히트마커 우선순위 입력 | 위 두 값이 반영된 뒤의 `bKill`/`bShieldBreak` 로 `ResolveHitMarker` 호출 |
| 폭발 `FExplosionResult` | `KilledEnemies` 에 2차 킬 **추가**, `bAnyEnemyHit` 유지 |
| 폭발 넉백 제외 판정 | 2차 킬을 본 뒤의 `bKilled` 로 판정(`FPSRCombatStatics.cpp:403` — 시체를 날리지 않기 위해) |

---

## 7. 복제표

| 프로퍼티 / RPC | 종류 | 비고 |
|---|---|---|
| — | — | **신규 복제 0.** 치명타 굴림·버프·라이더는 전부 서버 전용 |
| `UFPSRWeaponInstance::ActiveCritBuffs` | **비복제** | 클라는 버프를 모른다(HUD 표기 = 비목표) |
| `FFPSRProjectileParams::Crit` | **비복제** | Params 는 `GetLifetimeReplicatedProps` 미등록(서버 전용). 이 성질이 깨지면 `WeaponInstance` 약참조와 함께 재검토 |
| 힐 GE(카드 2) | 기존 GAS 경로 | 흡혈 카드와 동일 |
| (P-C) `ADSFieldOfView` 해결값 | 비복제 | 해결 스탯은 각 머신이 로컬 재계산(기존 규약). ADS FOV 는 오너-로컬 카메라 |

> 패키지 빌드에서 Push Model 이 꺼져도 영향 없다 — **복제를 추가하지 않기 때문**.

---

## 8. 수명주기 · 소유권

- **생성/등록**: 새 서브시스템·컴포넌트·델리게이트 **없음**.
- **해제**: `ClearTimedCritBuffs()` 호출 지점 = **① 무기 인스턴스 재초기화/스탯 재해결 ② 런 종료 리셋** 두 곳(§5 주석과 동일 표현). **홀스터로는 지우지 않는다.**
- **"안 불러도 안전"의 전제**: 만료 시각이 **절대값**이라, 전투시계가 리셋되면(새 런) 과거 버프가 "미래"로 보일 수 있다. 이것이 문제가 안 되는 이유는 **무기 인스턴스와 GameState 가 같은 맵 수명을 공유**하기 때문이다(런 = `ServerTravel` 단위, 인스턴스는 폰 인벤토리에 `NewObject`). 이 전제가 깨지면(런 중 시계 리셋) 위 ② 가 정확성 조건이 된다.
- **GC 소유**: `HealEffect`(UClass\*)는 ① 프래그먼트 DA 의 `UPROPERTY` ② USTRUCT 로 실린 `FFPSRProjectileParams` — 양쪽 리플렉션 경유라 발사체 비행 중에도 안전. **이것이 `FFPSRCritContext` 를 USTRUCT 로 만든 유일한 이유다.** `FFPSRTimedCritBuff::Source` 는 **역참조하지 않는 비교 키**라 GC 참조가 아니어도 된다.
- **델리게이트**: 신규 구독 0.

---

## 9. 데이터드리븐 경계

| 값 | 나가는 곳 | 기본값 |
|---|---|---|
| 2차 인스턴스 비율 | `DA_Fragment_Rifle_CritOverkill` | 0.5 |
| 치명타 흡혈 비율 / 힐 GE | `DA_Fragment_Rifle_CritLifesteal` | 0.10 / `GE_Card_LifestealHeal` 재사용 |
| 재장전 버프 확률·배수·지속 | `DA_Fragment_Rifle_CritOnReload` | 0.20 / 0.20 / 5.0 |
| 슬라이딩 버프 확률·지속 | `DA_Fragment_Rifle_CritOnSlide` | 0.40 / 5.0 |
| **(P-C) 라이플 프래그먼트 슬롯 상한** | `DA_Weapon_Rifle` | **5** |
| **(P-C) 스코프 진화 임계 FOV** | `DA_Weapon_Rifle` 사이트 슬롯 스테이지 | 사용자 저작 |
| 카드 등급·가중치·표시문구 | 구글 시트 Cards 행 | `L:0.0`, Weight 1 |
| 버프 슬롯 상한(4) / ADS FOV 클램프(5~170도) | **C++ 상수** | — (구조값, 조정 대상 아님) |
| 치명타 배수 기본 1.5 / 확률 0.05 | **C++** `FPSRCombatSet` Init | 1.5 / 0.05 |

---

## 10. 성능 예산

- **신규 틱 0.** 버프 만료는 타이머가 아니라 판독 시점 비교(lazy prune).
- **적(200~300)에 붙는 구조가 아니다** — 전부 플레이어(≤4) 사격 경로. 적 측 코드·메모리 무변경.
- **활성화당**: ASC 속성 3회 판독(현행과 동일) + 버프 배열 ≤4 순회 + 프래그먼트 `ModifyCrit` 가상호출(현행 `ModifyFireMode`·`PreFire` 와 동급). 타격당이 아니다.
- **타격당**: `RollCrit` = 비교 2회 + `FRand` 1회(현행과 동일). 치명타 시에만 `ApplyCritRiders` — 2차 `ApplyDamage` 1회 + 힐 GE 1회. 기본 확률 5% 기준 히트당 기대비용 ≈ 0.05 × ApplyDamage.
- **복제 대역 0 증가.**
- **완화**: `HasRiders()` 조기 반환 → 카드 미보유 플레이어는 종전 경로 + 분기 1회.

---

## 11. 미결정 항목 · 명세 갭 처리

**결정됨 (2026-09-05 사용자)**: 치명타 배수 1.5 · 카드 1 = 2차 인스턴스 · 5장 전부 라이플 기능 카드 · 재장전 버프 = 완료 후 5초 · 슬롯 상한 **5**(한 무기 = 한 빌드) · 스코프 = 스탯 임계 진화로 이관.

**미결정 (콘텐츠 — 코드는 어느 쪽이든 무수정)**

1. **약점 저작 범위** — `UFPSRWeakpointComponent` 가 `BP_EnemyMeleeBase`·`BP_Boss` 에만 있다. 카드 4 는 원거리·엘리트에겐 발동하지 않는다. ✅ **보스에는 닿는다**(BOSS1 이 `UFPSREnemyHealthComponent` 를 재사용 — `FPSRBossBase.cpp:79`).
2. **스코프 진화 임계 FOV 값** + 배율 카드의 등급별 수치.
3. **카드 5장의 등급 노출** — 초안은 `L:0.0`(레전더리 단독, 기존 기능 카드 전례).
4. **카드 이름·설명 3언어** — §14 는 초안.
5. **프래그먼트 DA 5종의 최종 수치** — §9 기본값은 초안.

**의도된 경계 (구현자가 "고치지" 말 것)**

- **재장전이 프리즈 중 완료되면** 버프가 걸린다(재장전 타이머는 월드 시간이라 프리즈를 관통한다). **의도다** — 버프의 시계는 전투시계라 프리즈 동안 줄지 않으므로 이득도 손해도 없고, `ServerSetAiming` 처럼 훅을 막으면 프리즈가 보상을 삼킨다.
- **카드 2 의 회복 기준값에 카드 1 의 2차 인스턴스 피해는 포함되지 않는다**(2차는 치명타가 아니다). 기존 흡혈 카드와는 **둘 다 발동**한다(중복 회복 = 의도).
- **카드 5 는 슬라이딩 시점에 들고 있던 무기에만 붙는다**(§5 `NotifySlideStarted` 주석).

**갭 처리 규칙(고정)**: 명세에 없는 판단이 필요해지면 **추측하지 말고 멈추고 "명세 갭"으로 보고**한다. 특히 ① 5경로 중 어느 하나에서 적용 순서를 바꿔야 할 것 같을 때 ② §6 OR 표를 경로별로 다르게 하고 싶을 때 ③ 버프를 무기 인스턴스가 아닌 곳에 두고 싶어질 때.

---

## 12. 검증 기준

| # | 검사 | 통과 조건 |
|---|---|---|
| 1 | 명세 대조 | §5·§6·§7 선언·시그니처가 코드와 1:1. **`ApplyExplosion` 에 기본값 인자를 만들지 않았을 것** |
| 2 | 빌드 | `Build.bat FPSRogueliteEditor Win64 Development` → 로그의 **`Result: Succeeded`** (종료코드 아님). 헤더 신설이 있으므로 **`-DisableUnity` 필수** |
| 3 | 헤드리스 스모크 | `FPSRoguelite.Smoke.ModuleLoads` 통과 |
| 4 | 신규 자동화 `FPSRoguelite.Combat.CritResolver` | ⓐ `Chance=0` → 항상 false ⓑ `Chance=1` → 항상 true ⓒ `bWeakpointAlwaysCrit && WeakpointMult>1` → `Chance=0` 이어도 true ⓓ `bWeakpointAlwaysCrit && WeakpointMult==1` → 굴림대로 ⓔ `ComputeCritRiderMagnitudes` 산식(0·양수·`DamageDealt<=0`) |
| 5 | 회귀 | 카드 미보유 시 5경로의 피해·히트마커·킬 집계 종전과 동일. ⚠️ **의도된 변화 = 치명타 배수 2.0→1.5 뿐** |
| 6 | 기존 자동화 | 신규 실패 0. 선행 실패 `Editor.CardCsv.RoundTrip` 은 그대로 남아도 통과로 본다 |
| 7 | 레드팀 게이트 (G2) | **푸시 전** `origin/main..HEAD` diff 를 Fable 레드팀에 제출(`Workflow.md` §6-6-1). **P1 잔존 시 푸시 금지** |
| 8 | PIE / 사용자 스모크 | 아래 |

**검증용 관측 수단 (G1 P2-4 — 없으면 8-6·8-7 이 검증 항목이 아니라 희망사항이 된다)**
`ApplyTimedCritBuff` 와 만료 prune 지점에 `UE_LOG(LogFPSR, Verbose, ...)` 1줄씩 — 건 시각·만료 시각·합산 결과. Verbose 라 기본 출력 0.

**PIE 사용자 스모크** (Claude 는 게임을 켜지 않는다 — 메모리 `do-not-launch-game`)

1. 카드 미보유 라이플로 적 사살 — 치명타 피해가 종전보다 **약한가**(×2.0 → ×1.5).
2. 카드 4 보유 후 **근접 적(`BP_EnemyMeleeBase`)의 약점**을 쏘면 **피해 수치**가 매번 치명타 값인가.
   ⚠️ **히트마커로는 판정 불가** — 마커 우선순위가 `Kill > ShieldBreak > Weak > Crit` 라 약점 타격은 치명타 여부와 무관하게 항상 **Weak** 마커다.
3. 카드 1 보유 후 치명타 1발에 **피해가 두 번** 들어가는가(총 피해량 증가).
4. 카드 2 보유 후 치명타 시 체력이 차는가. **아군 오사·자폭으로는 절대 차지 않는가**(FF 켠 2인).
5. 카드 3 보유 후 재장전 완료 직후 치명타가 눈에 띄게 잦은가. 재장전 **취소**로는 안 걸리는가.
6. 카드 5 보유 후 슬라이딩마다 5초 버프가 **초기화**되는가(위 Verbose 로그로 만료 시각 확인).
7. **프리즈 검사** — 슬라이딩 직후 카드 선택 프리즈를 30초 끌고 재개했을 때 잔여 시간이 프리즈 전과 같은가(로그의 만료 시각이 프리즈 동안 안 흐름).
8. **협동 2인** — 클라이언트가 쏜 치명타에도 1~6 이 동일한가.
9. **(P-C)** 라이플에 기능 카드 **5장까지** 붙는가(종전 3장에서 막히던 것).
10. **(P-C)** 배율 강화 카드를 쌓아 임계를 넘으면 사이트가 **스코프로 진화**하고, SniperScope 카드는 **더 이상 제시되지 않는가**.

---

## 13. 레드팀 지적 원장 (C3/G2 에서 채운다)

### G1 플랜 게이트 (2026-09-05, `claude-fable-5`) — **조건부 통과**

| 심각도 | 지적 | 처리 | 근거 |
|---|---|---|---|
| P1-1 | 버프 갱신 키 `SourceTag` 미정의 → 카드 3·5 가 서로를 덮어씀 | **수용** | 키를 프래그먼트 에셋 포인터로(§5). 리포 규약 "identity = asset pointer" |
| P1-2 | §12-4 ⓔ 라이더 산식이 비순수 함수를 겨냥 → 구현 불가 | **수용** | 순수 헬퍼 `ComputeCritRiderMagnitudes` 신설(§5) |
| P2-1 | §4 에 `FPSREnemyBase.cpp:1107-1108` 누락 = 컴파일 에러 / `ApplyExplosion` 호출자 "5곳"은 오기(실 2곳) | **수용** | §4·§6 정정 |
| P2-2 | 폭발 경로 라이더 계약이 §5 와 §6 에서 다르게 읽힘 | **수용** | §6 OR 표 신설(넉백 제외·`KilledEnemies` 포함) |
| P2-3 | 라이플 `MaxFragmentSlots`=3 → 5장 빌드 조립 불가 | **수용** | P-C 로 승격(상한 5 + 스코프 이관, 사용자 결정) |
| P2-4 | PIE 2·7 관측 불가(약점=Weak 마커 / 버프 무관측) | **수용** | §12 PIE 2 를 피해 수치 기준으로, Verbose 로그 2줄 명시 |
| P2-5 | `ModifyCrit` 스택 합성 의미 미정의 | **수용** | §5 훅 주석에 고정 규칙(`+=` / `\|=` / set-if-empty) |
| P2-6 | OR 목록에 `bShieldBroke`·`NotifyKill` 누락 | **수용** | §6 OR 표 |
| P3 | §8 문구 모순 · `SumActiveCritBuffs` 권위 계약 · 프리즈/재장전 · 카드 2 기준값 · 카드 5 무기 귀속 · 보스 커버리지 · `BlueprintReadWrite` | **수용** | §5·§8·§11 반영 |

### C3 검증 (2026-09-05, Opus) — 통과

| 검사 | 결과 |
|---|---|
| 빌드 `-DisableUnity` | **`Result: Succeeded`** |
| `FPSRoguelite.Combat.CritResolver`(신규) | **`Result={Success}`** |
| `FPSRoguelite.Smoke.ModuleLoads` | **`Result={Success}`** |
| `FPSRoguelite.Combat.Vitals`(데미지 경로 인접 회귀) | **`Result={Success}`** |
| `FPSRoguelite.Boss.TickEnabled`(보스 include 수정분 회귀) | **`Result={Success}`** |

> 🚨 **판정 도구 함정** — `-abslog` 파일은 `-TestExit` 종료 시 잘려서 판정 줄이 안 남는다(대조군 스모크도 동일하게 잘림). **stdout 을 받아야** `Result={...}` 가 보인다. 기동 구간의 `LogAutomationTest: Error: Condition failed` 4줄은 모든 실행에 있는 노이즈다(대조군 확인). `Scripts/run_crit1_tests.bat` 주석에 못박음. 메모리 [[automation-abslog-truncated-read-stdout]].

### 🔴 플랜에 없던 결정 — **G2 프롬프트에 그대로 올릴 것** (`Workflow.md` §6-5-2 요구)

| # | 무엇 | C3 판정 |
|---|---|---|
| 1 | **`FPSRGA_WeaponFire_Projectile.cpp` 를 §4 표 밖에서 수정.** 이 GA 가 `Params.CritChance/Multiplier` 를 직접 채우고 있었고, **모든 플레이어 무기가 Projectile 아키타입**이다(Rifle·Sniper·Shotgun·Bazooka 4종 DA 를 바이너리 대조 — `FPSRGA_WeaponFire_Projectile` 1건 / Hitscan 0건). ASC 직독 shim 으로 두면 **카드 5장이 라이플에서 전부 무동작**이 된다 | **유지** — 명세 §4 표의 누락이지 재량이 아니다. §6 이 "`BuildCritContext` = 유일한 컨텍스트 생성 지점"이라고 못박은 것과 일치 |
| 2 | `FPSRCritTypes.h` 가 `GameplayEffect.h` 를 include(명세는 전방선언) — `TSubclassOf::operator*()` 가 `T::StaticClass()` 를 호출해서(`SubclassOf.h:110`, 엔진 소스 대조) 인라인 `HasRiders()` 가 완전한 타입을 요구 | **C3 에서 되돌림** — 무거운 GAS 헤더가 combat 모듈 대부분에 실리므로, 전방선언을 복원하고 `HasRiders()` 본문만 신규 `Private/Combat/FPSRCritTypes.cpp` 로 뺐다(호출처 1곳, 전부 .cpp) |
| 3 | `FPSRBossHomingOrb.h` 에 `#include "Boss/FPSRBossTypes.h"` 추가 — **CRIT1 무관 선행 결함**(유니티 블롭에 가려져 있던 것) | **유지 + 확대**: C3 의 헤더 정리가 전이 include 사슬을 끊자 `FPSRBossHomingOrb.cpp` 의 `MARK_PROPERTY_DIRTY_FROM_NAME` 도 깨졌다 → `Net/Core/PushModel/PushModel.h` 추가. 같은 매크로를 쓰는 8파일 중 6파일이 이미 명시 include 를 갖고 있다(대조군) — 보스 2파일만 예외였다. ⚠️ **`FPSRBossBase.cpp` 도 같은 누락이 남아 있다**(지금은 다른 경로로 얻어 컴파일됨) = 후속 |
| 4 | `ApplyTimedCritBuff` 정의부 매개변수명 `Source` → `BuffSource`(클래스 멤버 `Source` 와 겹침) | **유지** — 시그니처 무영향 |
| 5 | `ClearTimedCritBuffs()` 호출을 `InitializeWithSource()` 한 곳에만 배치(§8 의 "런 종료 리셋"은 인스턴스가 매 런 새로 생성되므로 자동 충족) | **유지** — §8 이 조건부로 적어 둔 전제와 일치 |

### G2 머지 게이트 — *(푸시 직전에 채운다)*

---

## 14. 시트 저작 초안 (사용자 붙여넣기용)

> ⚠️ **구글 시트에 넣는다.** 리포 `Content/Authoring/*.csv` 직접 편집 금지(`Localization.md` L-5).

### CardCatalog 시트 — 6행 추가 / 1행 삭제

```
weapon.frag.critbonus,WeaponBehavior,/Game/Cards/Weapons/Modifiers/DA_Fragment_Rifle_CritOverkill.DA_Fragment_Rifle_CritOverkill,,,,
weapon.frag.critlifesteal,WeaponBehavior,/Game/Cards/Weapons/Modifiers/DA_Fragment_Rifle_CritLifesteal.DA_Fragment_Rifle_CritLifesteal,,,,
weapon.frag.critonreload,WeaponBehavior,/Game/Cards/Weapons/Modifiers/DA_Fragment_Rifle_CritOnReload.DA_Fragment_Rifle_CritOnReload,,,,
weapon.frag.weakpointcrit,WeaponBehavior,/Game/Cards/Weapons/Modifiers/DA_Fragment_Rifle_WeakpointCrit.DA_Fragment_Rifle_WeakpointCrit,,,,
weapon.frag.critonslide,WeaponBehavior,/Game/Cards/Weapons/Modifiers/DA_Fragment_Rifle_CritOnSlide.DA_Fragment_Rifle_CritOnSlide,,,,
weapon.adsfov,WeaponStat,ADSFieldOfView,PercentMultiply,TRUE,,값이 작을수록 배율↑ — 강화 카드는 음수 저작(RecoilVertical 규약과 동일)
```
삭제: `weapon.frag.riflesniperscope` 행(스코프가 스탯 임계 진화로 이관되어 프래그먼트가 필요 없다)

### Cards 시트 — 6행 추가 / 1행 삭제

컬럼 = `CardId, AssetName, Group, Route, OwnerWeapon, Weight, Family, DisplayName_ko/en/ja, Description_ko/en/ja, E1_Attr, E1_Override, E1_Tiers, E2_*, E3_*`

```
DA_CardModifiers_CritOverkill,DA_CardModifiers_CritOverkill,Weapon,MissionClearWeaponFeature,DA_Weapon_Rifle,1,,치명타 추가타,Critical Overkill,クリティカル追撃,치명타가 입힌 피해의 50%를 한 번 더 입힌다,Crits deal an extra 50% of the damage they dealt,クリティカルで与えたダメージの50%を追加で与える,weapon.frag.critbonus,,L:0.0,,,,,,
DA_CardModifiers_CritLifesteal,DA_CardModifiers_CritLifesteal,Weapon,MissionClearWeaponFeature,DA_Weapon_Rifle,1,,치명타 흡혈,Critical Leech,クリティカル吸血,치명타가 입힌 피해의 10%만큼 회복한다,Heal for 10% of the damage your crits deal,クリティカルで与えたダメージの10%を回復する,weapon.frag.critlifesteal,,L:0.0,,,,,,
DA_CardModifiers_CritOnReload,DA_CardModifiers_CritOnReload,Weapon,MissionClearWeaponFeature,DA_Weapon_Rifle,1,,재장전 각성,Reload Rush,リロード覚醒,재장전을 마치면 5초간 치명타 확률 +20%·치명타 피해 +20%,Finishing a reload grants +20% crit chance and +20% crit damage for 5s,リロード完了後5秒間 クリティカル率+20%・クリティカルダメージ+20%,weapon.frag.critonreload,,L:0.0,,,,,,
DA_CardModifiers_WeakpointCrit,DA_CardModifiers_WeakpointCrit,Weapon,MissionClearWeaponFeature,DA_Weapon_Rifle,1,,약점 관통,Weakpoint Precision,弱点貫通,약점에 맞힌 사격은 반드시 치명타가 된다,Shots that hit a weakpoint always crit,弱点に当てた射撃は必ずクリティカルになる,weapon.frag.weakpointcrit,,L:0.0,,,,,,
DA_CardModifiers_CritOnSlide,DA_CardModifiers_CritOnSlide,Weapon,MissionClearWeaponFeature,DA_Weapon_Rifle,1,,활강 집중,Slide Focus,滑走集中,이 무기를 든 채 슬라이딩하면 5초간 치명타 확률 +40% (슬라이딩할 때마다 초기화),Sliding while holding this weapon grants +40% crit chance for 5s (refreshed each slide),この武器を持ってスライディングすると5秒間 クリティカル率+40%（スライディングのたびに更新）,weapon.frag.critonslide,,L:0.0,,,,,,
DA_Card_ADSZoom_ThisWeapon,DA_Card_ADSZoom_ThisWeapon,Weapon,LevelUpWeapon,DA_Weapon_Rifle,1,,조준 배율,Aim Magnification,照準倍率,이 무기의 조준 배율 증가,Aim magnification up for this weapon,この武器の照準倍率アップ,weapon.adsfov,,C:-0.05;R:-0.08;E:-0.12;L:-0.15,,,,,,
```
삭제: `DA_CardModifiers_SniperScope` 행

> `Family` 공란 = 임포터가 `E1_Attr` 에서 자동 파생(§2-3-2 v3) → 기능 5장이 서로 다른 family 라 한 제시에 공존 가능.
> 라이플 미션 풀 = SniperScope 1장 → **치명타 5장**. 라이플 레벨업 풀 = 연사·탄창 2장 → **+조준 배율 = 3장**.
