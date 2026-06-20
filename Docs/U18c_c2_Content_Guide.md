# U18c c2 콘텐츠 가이드 — 캐릭터 GAS-native 회복 카드 (흡혈 + 체력 재생)

> c2 **코드는 완료**(브랜치 `phase/u18c-behavior-hooks`). 이 문서는 **PIE에 필요한 콘텐츠**(GE/GA BP + 카드 + 풀 등록)를 에디터/VibeUE로 저작하는 절차다. c3 기능해금 콘텐츠는 **헤드리스 저작 완료**(`Scripts/u18c_content.py`).
> 코드는 콘텐츠 무의존이라 콘텐츠 null이면 안전 no-op(빌드/스모크 통과). 아래는 PIE용.

## 0. 만들 것 (6)
| # | 자산 | 위치 | 종류 |
|---|---|---|---|
| 1 | `GE_Card_LifestealHeal` | `/Game/Cards/Character/GameplayEffect/` | GameplayEffect BP |
| 2 | `GE_Card_HealthRegen` | `/Game/Cards/Character/GameplayEffect/` | GameplayEffect BP |
| 3 | `GA_Lifesteal` | `/Game/Cards/Character/` (또는 Abilities 폴더) | BP : `UFPSRPassiveAbility_Lifesteal` |
| 4 | `DA_Card_Lifesteal` | `/Game/Cards/Character/Data/` | `UFPSRCardDataAsset` |
| 5 | `DA_Card_HealthRegen` | `/Game/Cards/Character/Data/` | `UFPSRCardDataAsset` |
| 6 | (수정) `DA_Character_CardPool` | `/Game/Cards/Character/` | `Cards` 배열에 4·5 추가 |

> **템플릿 복제 권장**: 기존 `BP_Card_MaxHealth`(GE)·`DA_Card_MaxHealth`(카드)가 SetByCaller 패턴의 레퍼런스. 복제 후 수정이 가장 빠름.

## 1. GE_Card_LifestealHeal (흡혈 즉시 회복)
- **Duration Policy** = `Instant`
- **Modifiers** 1개:
  - Attribute = `UFPSRHealthSet → Health`
  - Modifier Op = `Add`
  - Magnitude Calculation Type = `Set By Caller`
  - SetByCaller **Data Tag** = `SetByCaller.CardMagnitude`
- (흡혈 GA가 `힐량 = DamageDealt × HealRatio`를 이 태그로 주입한다.)

## 2. GE_Card_HealthRegen (초당 체력 재생)
- **Duration Policy** = `Infinite`
- **Period** = `1.0` (초당 1회 적용) · Execute Periodic Effect on Application = 취향(첫 틱 즉시 여부)
- **Modifiers** 1개:
  - Attribute = `Health` / Op = `Add` / Magnitude = **Set By Caller**, Data Tag = `SetByCaller.CardMagnitude`
  - (per-rarity 초당 회복량을 카드 RarityTiers가 주입 — §5. 고정값이면 ScalableFloat로 두고 카드 RarityTiers 생략 가능.)

## 3. GA_Lifesteal (패시브 GA Blueprint)
- 부모 클래스 = **`UFPSRPassiveAbility_Lifesteal`** (C++).
- 클래스 디폴트:
  - `HealEffect` = `GE_Card_LifestealHeal` (#1)
  - `HealRatio` = `0.05` (5% — 취향 조정)
- ⚠️ **AbilityTrigger / NetExecutionPolicy / InstancingPolicy는 C++ 생성자에서 이미 설정**(GameplayEvent.Player.DealtDamage 트리거·ServerOnly). BP에서 건들 필요 없음.

## 4. DA_Card_Lifesteal (흡혈 카드)
- `Group` = **Character**
- `DisplayName` = "Lifesteal"(또는 한글)
- `Effects` = **[ `Character: Passive Ability` ]** (= `UCardEffect_CharacterPassive`)
  - `PassiveAbility` = `GA_Lifesteal` (#3)
- RarityTiers 불요(패시브=매그니튜드 없음). 등급 라벨 원하면 단일 티어 추가(거동 무관).

## 5. DA_Card_HealthRegen (재생 카드)
- `Group` = **Character**
- `DisplayName` = "Health Regen"
- `Effects` = **[ `Character: Gameplay Effect` ]** (= `UCardEffect_CharacterGE`)
  - `Effect` = `GE_Card_HealthRegen` (#2)
  - `RarityTiers` = 등급별 초당 회복량(예: Common 1.0 / Rare 2.0 / Epic 3.0 / Legendary 5.0). **OfferRarities는 이 티어들에서 자동 파생**(PostLoad). ⚠️ `IsDataValid`가 모든 등급 커버리지를 강제하니, OfferRarities 등급 전부에 티어를 둘 것.
- (GE_HealthRegen이 고정 ScalableFloat면 RarityTiers 생략 가능 → 단일 오퍼.)

## 6. 풀 등록
- `DA_Character_CardPool` 열기 → **`Cards`** 배열에 `DA_Card_Lifesteal`·`DA_Card_HealthRegen` 추가(저장). (`Cards`=레벨업 중앙 풀 / `WeaponUnlockCards`=무기해금.)

## 7. PIE 검증 (c2)
1. 런 진입 → 레벨업 프리즈에서 **흡혈/재생 카드가 오퍼에 등장**.
2. **흡혈** 픽 → 적에게 데미지 시 비율만큼 HP 회복. **오버킬/시체 타격은 무회복**(이벤트=실측 DamageDealt).
3. **재생** 픽 → 데미지 안 줘도 **초당 HP 회복**(MaxHealth 상한).
4. **런 종료**(로비 복귀) → 재시작 시 **흡혈/재생 효과 소거**(ResetRunState가 카드 grant 패시브 ClearAbility + 재생 GE RemoveActiveEffects). 다음 런 깨끗.

## 참고 — 코드 계약
- 흡혈 트리거: `FPSRCombat::ApplyDamage`(적 브랜치, 서버) → instigator ASC에 `GameplayEvent.Player.DealtDamage`(페이로드=실측 DamageDealt) → 리스너 카운트>0(흡혈 grant 시 set)일 때만 송신(적500 0비용).
- 핸들 추적: `UCardEffect_CharacterPassive::Apply`가 GiveAbility 핸들을 `AFPSRPlayerState.CardGrantedAbilityHandles`에 기록 → `ResetRunState`가 `ClearAbility` + 리스너 카운트 0.
- 재생: 신규 코드 0(기존 `UCardEffect_CharacterGE` + periodic GE). 카드 grant=GE 적용, 런 종료=RemoveActiveEffects가 정리.
