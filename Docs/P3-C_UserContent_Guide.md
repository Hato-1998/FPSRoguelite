# P3-C 사용자 콘텐츠 작업 가이드 — 카드 시스템 PIE 테스트

> P3-C(카드 데이터/로직)는 C++ 베이스/로직만 완료된 상태다. 실제로 카드가 뜨고 적용되려면
> **에디터에서 콘텐츠 에셋(GE / 카드 DataAsset / 카드풀 DataAsset)을 만들고 BP에 연결**해야 한다.
> 이 문서대로 5단계만 따라 하면 콘솔 커맨드로 카드 추첨·적용·리롤을 검증할 수 있다.
>
> 코드 원칙(Game.MD §6-2): **에셋 경로 C++ 하드코딩 금지** → 카드/GE/풀은 전부 에디터 콘텐츠이고,
> `BP_FPSRGameMode`가 풀을 참조한다(아래 4단계).

---

## 0. 사전 준비 / 개념

- **카드 효과 = GameplayEffect(GE)**. 카드를 고르면 `UFPSRCardSubsystem::ApplyCard`가 카드의 `AppliedEffect`(GE)를
  플레이어 ASC에 적용한다. 즉 **카드 1장 = GE 1개 + 카드 DataAsset 1개**.
- **P3-C에서 적용 가능한 카드 = `Scope = Character`만**. (`ThisWeapon`/`AllWeapons` 무기 카드는 P4 — 추첨에서 자동 제외됨.)
- GE가 건드릴 수 있는 **글로벌 속성**(현재 구현된 것):

  | AttributeSet | 속성 | 기본값 | 의미 |
  |---|---|---|---|
  | `FPSRHealthSet` | `MaxHealth` | 100 | 최대 체력 (clamp ≥ 1) |
  | `FPSRHealthSet` | `Health` | 100 | 현재 체력 (clamp 0~MaxHealth) |
  | `FPSRCombatSet` | `GlobalCritChance` | 0.05 | 전역 치명타 확률(0~1 권장) |
  | `FPSRCombatSet` | `GlobalCritMultiplier` | 2.0 | 치명타 배율 |
  | `FPSRCombatSet` | `GlobalDamageMultiplier` | 1.0 | 전역 데미지 배율 |
  | `FPSRCombatSet` | `Luck` | 0 | **카드 추첨 가중**(상위 등급 확률↑) |
  | `FPSRCombatSet` | `RarityBonus` | 0 | **카드 추첨 가중**(상위 등급 확률↑) |

  > 새로운 스탯 축(이속/픽업반경/XP획득 등)은 아직 AttributeSet에 없다 → 그 카드를 만들려면 C++ 확장 필요(§2-3).
  > 지금은 위 7개 속성 범위 안에서 샘플 카드를 만든다.

- 콘텐츠 폴더 권장 위치: **`Content/Cards/`** (하위에 `GE/`). 기존 무기 DA가 `Content/Weapons/`인 것과 동일한 결.

---

## 1단계 — GameplayEffect(GE) 만들기 (카드 효과)

각 카드가 적용할 효과를 GE로 만든다. **두 가지 방식**이 있으며 둘 다 동작하고 혼용 가능하다.

### 공통 생성 절차
1. 콘텐츠 브라우저 → `Content/Cards/GE/` 폴더 생성 후 진입
2. 우클릭 → **Blueprint Class** → 검색창에 `GameplayEffect` → 부모로 선택 → 이름 지정
3. 에셋 더블클릭 → 디테일 패널에서 아래 설정
4. 공통: **Duration Policy = `Instant`** (속성 base 값을 영구 가산·스택 누적 — 뱀서식 영구 강화. "제거 가능 버프"가 필요하면 `Infinite`)

---

### ⭐ 방식 A (권장) — SetByCaller: GE 1개를 등급별 다른 값으로 재사용

**한 속성당 GE 1개**만 만들고, 수치는 **카드의 `Magnitude` 필드**(2단계)로 넣는다.
→ "최대체력 +15(Common)"·"최대체력 +100(Legendary)"가 **같은 GE 1개**를 공유하므로 GE 에셋 수가 급증하지 않는다.

GE 설정:
- **Modifiers** 1개 추가:
  - **Attribute**: 대상 속성 (예: `FPSRHealthSet.MaxHealth`)
  - **Modifier Op**: `Add`
  - **Magnitude Calculation Type**: **`Set By Caller`**
  - **Set By Caller → Data Tag**: **`SetByCaller.CardMagnitude`** (반드시 이 태그)
- 카드의 `Magnitude`가 런타임에 이 자리에 주입된다(코드 `ApplyCard`가 처리).

방식 A 샘플 GE (속성당 1개):

| GE 에셋명 | 대상 속성 | Op | Magnitude 타입 | SetByCaller 태그 |
|---|---|---|---|---|
| `GE_Card_MaxHealth` | `FPSRHealthSet.MaxHealth` | Add | Set By Caller | `SetByCaller.CardMagnitude` |
| `GE_Card_CritChance` | `FPSRCombatSet.GlobalCritChance` | Add | Set By Caller | `SetByCaller.CardMagnitude` |
| `GE_Card_Damage` | `FPSRCombatSet.GlobalDamageMultiplier` | Add | Set By Caller | `SetByCaller.CardMagnitude` |
| `GE_Card_CritMult` | `FPSRCombatSet.GlobalCritMultiplier` | Add | Set By Caller | `SetByCaller.CardMagnitude` |
| `GE_Card_Luck` | `FPSRCombatSet.Luck` | Add | Set By Caller | `SetByCaller.CardMagnitude` |
| `GE_Card_Rarity` | `FPSRCombatSet.RarityBonus` | Add | Set By Caller | `SetByCaller.CardMagnitude` |

> ⚠️ **속성 오타 주의**: 모디파이어 Attribute를 **카드 의도와 일치**시킬 것. (예: 치명타 배율 카드는 `GlobalCritMultiplier`여야 하며 `GlobalDamageMultiplier`가 아님 — 흔한 실수.)

---

### 방식 B (구식, 호환) — 고정 수치 GE

수치를 GE에 직접 박는다(Magnitude=`Scalable Float`). 등급별로 값이 다르면 **GE를 등급 수만큼** 만들어야 한다.
SetByCaller를 안 쓰므로 카드의 `Magnitude` 필드는 무시된다. 소량·일회성 카드엔 무방.

> **검증 팁**: GE 적용 여부는 PIE 중 콘솔 `showdebug abilitysystem`으로 플레이어 ASC 속성값 변화를 눈으로 확인.

---

## 2단계 — 카드 DataAsset 만들기 (`UFPSRCardDataAsset`)

각 카드 1장 = DataAsset 1개. 위 GE를 참조한다.

### 생성 절차
1. `Content/Cards/` 진입 → 우클릭 → **Miscellaneous → Data Asset**
2. 클래스 선택 창에서 **`FPSRCardDataAsset`** 선택 → 이름 지정 (예: `Card_CritChance`)
3. 에셋 더블클릭 → 필드 입력

### 필드 설명

| 필드 | 설명 | 예시 |
|---|---|---|
| **Display Name** | UI 표시명(P3-D 위젯용) | `정밀 조준` |
| **Description** | 설명문(멀티라인) | `치명타 확률 +5%` |
| **Scope** | **`Character`** 로 둘 것 (P3-C는 Character만 적용) | `Character` |
| **Rarity** | `Common`/`Rare`/`Epic`/`Legendary` | `Common` |
| **Applied Effect** | 1단계에서 만든 GE 클래스 선택 | `GE_Card_MaxHealth` |
| **Weight** | 같은 등급 내 상대 추첨 가중치(기본 1.0) | `1.0` |
| **Magnitude** | **방식 A 전용** — GE에 주입할 수치(SetByCaller). 방식 B(고정 GE)면 비워둠/무시 | `15` |
| **Card Family** | (선택) GameplayTag. 같은 family 카드는 한 번에 1장만 제안. **비우면 Applied Effect GE 클래스로 자동 그룹핑** | (보통 비움) |

### 샘플 — 방식 A로 "최대체력" 카드를 등급별로 (GE 1개 공유)

| 카드 DataAsset | Rarity | Applied Effect | Magnitude | Card Family |
|---|---|---|---|---|
| `Card_MaxHealth_C` | Common | `GE_Card_MaxHealth` | `15` | (비움) |
| `Card_MaxHealth_R` | Rare | `GE_Card_MaxHealth` | `35` | (비움) |
| `Card_MaxHealth_E` | Epic | `GE_Card_MaxHealth` | `60` | (비움) |
| `Card_MaxHealth_L` | Legendary | `GE_Card_MaxHealth` | `100` | (비움) |
| `Card_CritChance_C` | Common | `GE_Card_CritChance` | `0.05` | (비움) |
| `Card_Damage_R` | Rare | `GE_Card_Damage` | `0.20` | (비움) |
| `Card_Luck_E` | Epic | `GE_Card_Luck` | `1` | (비움) |
| `Card_Rarity_L` | Legendary | `GE_Card_Rarity` | `1` | (비움) |

> **위 4장의 MaxHealth 카드는 GE 1개(`GE_Card_MaxHealth`)를 공유**하고 Magnitude만 다르다 → 한 번의 추첨엔 이 중 **1장만** 뜬다(family 자동 그룹핑, GE 클래스 동일). 등급별로 다양하게 만들고 싶으면 이 패턴을 반복.
> ⚠️ 다른 속성인데 같은 family로 묶고 싶거나, 다른 GE인데 같은 계열로 묶고 싶으면 그때만 `Card Family` 태그를 직접 지정.
> 등급별로 1장 이상 있어야 추첨이 다양해진다. 최소 6~8장 권장.

---

## 3단계 — 카드풀 DataAsset 만들기 (`UFPSRCardPoolDataAsset`)

추첨 후보가 되는 마스터 풀. 위 카드들을 담는다.

### 생성 절차
1. `Content/Cards/` → 우클릭 → **Miscellaneous → Data Asset** → 클래스 **`FPSRCardPoolDataAsset`** → 이름 `DA_CardPool`
2. 더블클릭 → 필드 입력

### 필드 설명

| 필드 | 설명 | 기본값 |
|---|---|---|
| **Cards** | 2단계에서 만든 카드 DataAsset들을 전부 추가 | (배열) |
| **Common Weight** | Common 등급 기본 가중치 | 1.0 |
| **Rare Weight** | Rare 기본 가중치 | 0.5 |
| **Epic Weight** | Epic 기본 가중치 | 0.2 |
| **Legendary Weight** | Legendary 기본 가중치 | 0.05 |
| **Rarity Bonus Scale** | RarityBonus 속성이 상위등급 확률을 끌어올리는 계수 | 1.0 |
| **Luck Scale** | Luck 속성이 상위등급 확률을 끌어올리는 계수 | 0.1 |

- **추첨 공식**(참고): `유효가중치 = Card.Weight × (등급 기본가중치) × (1 + RarityBonus×RarityBonusScale + Luck×LuckScale) × 등급tier`
  - 등급tier: Common=0, Rare=1, Epic=2, Legendary=3 → Luck/RarityBonus가 0이면 등급 기본가중치만 작용,
    높아질수록 상위 등급 카드의 가중치가 증가.
- **Cards 배열에 추가하는 카드는 `Scope=Character`만 실제 추첨된다**(무기 스코프는 자동 제외).

---

## 4단계 — `BP_FPSRGameMode`에 풀 연결

서브시스템에 풀을 주입하는 유일한 데이터 연결점(코드 하드코딩 없음).

1. `Content/Core/BP_FPSRGameMode` 더블클릭 (부모 = `FPSRGameMode`)
2. 디테일 패널 → 카테고리 **`FPSR | Cards`** → **`Card Pool`** 프로퍼티에 `DA_CardPool` 할당
3. 컴파일 · 저장

> GameMode가 BeginPlay에서 `UFPSRCardSubsystem::SetActivePool(CardPool)`를 호출한다. 풀이 비어 있으면
> `FPSR.DrawCards` 시 로그에 `ActivePool is null` 경고가 뜬다 → 이 단계 누락 신호.

---

## 5단계 — PIE 콘솔 테스트 (검증 시나리오)

PIE 실행 후 콘솔(`~`)에서 순서대로:

| # | 콘솔 입력 | 기대 결과 (Output Log, 카테고리 `LogFPSR`) |
|---|---|---|
| 1 | `FPSR.RerollCharges 3` | `RerollCharges set to 3` |
| 2 | `FPSR.DrawCards 3` | `[Card] [0] Card_xxx (Common)` … 3줄(등급 분포는 가중 랜덤) |
| 3 | `FPSR.AddXP 120` | (P3-A) 레벨업 → PendingLevelUps 큐 증가 |
| 4 | `FPSR.ApplyCard 0` | `ApplyCard index 0 -> applied` + 해당 GE가 ASC에 적용 |
| 5 | `FPSR.ApplyCard 0` | (레벨업 큐 소진 시) `-> rejected` ← **무료 지급 방지 게이트 정상** |
| 6 | `FPSR.Reroll` | 차지 1 차감 + 3장 재추첨 로그 |
| 7 | `FPSR.Reroll` (3회 초과) | `not enough reroll charges` ← **리롤 3회 제한 정상** |

### 속성 적용 눈으로 확인
- 콘솔 `showdebug abilitysystem` → 화면에 플레이어 ASC 속성 목록 표시.
- `Card_CritChance` 적용 전후 `GlobalCritChance` 가 `0.05 → 0.10` 으로 증가하는지 확인.
- `Card_MaxHealth` 적용 시 `MaxHealth` `100 → 125`.

---

## 트러블슈팅

| 증상 | 원인 / 해결 |
|---|---|
| `DrawCards: ActivePool is null` 경고 | 4단계 누락 — `BP_FPSRGameMode.Card Pool` 미할당 |
| 카드가 0장 추첨됨 | 풀 Cards가 비었거나, 전부 `Scope≠Character`(무기 카드만 들어있음) |
| `ApplyCard -> rejected` 계속 | 레벨업 큐 없음 — 먼저 `FPSR.AddXP 120` (오프닝 시드 흐름은 P3-D에서 `bConsumeLevelUp=false`로 별도 처리) |
| GE 적용해도 속성 변화 없음 | GE의 Modifier Attribute/Op/Magnitude 미설정, 또는 Duration Policy가 Instant가 아님 |
| 방식 A인데 값이 0으로 적용됨 | GE SetByCaller Data Tag가 `SetByCaller.CardMagnitude`와 불일치, 또는 카드 `Magnitude`=0 |
| 카드가 의도와 다른 스탯을 올림 | GE Modifier의 **Attribute 오타**(예: 치명타배율 카드인데 `GlobalDamageMultiplier` 지정) |
| 특정 등급만 안 나옴 | 그 등급 카드가 풀에 없음 / 등급 기본가중치가 너무 낮음(Legendary 0.05) — Luck/RarityBonus 카드 적용 후 재추첨해 비교 |

---

## 작업 체크리스트

- [ ] 1단계: GE 6종 생성 (`Content/Cards/GE/`, Duration=Instant, Modifier 설정)
- [ ] 2단계: 카드 DataAsset 6장 (`Scope=Character`, Applied Effect 연결, Rarity 지정)
- [ ] 3단계: `DA_CardPool` 생성 + Cards 배열에 6장 추가
- [ ] 4단계: `BP_FPSRGameMode.Card Pool = DA_CardPool` + 컴파일
- [ ] 5단계: PIE 콘솔 7단계 시나리오 + `showdebug abilitysystem` 속성 확인
- [ ] 결과 보고 → 이상 없으면 P3-C → main 머지, P3-D(카드 UI) 착수

> 콘텐츠는 P3-D 머지 시점에 함께 커밋할지 사용자에게 확인한다(메모리 규칙: Phase 종료 시 사용자 콘텐츠 동반 커밋 여부 확인).
