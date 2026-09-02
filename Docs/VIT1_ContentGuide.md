# VIT1 콘텐츠 저작 가이드 (사용자 작업)

> 대상 = `Docs/Specs/VIT1_ShieldHealthTwoLayer.md` **§11-1**(콘텐츠 7항목) · **§11-2**(i-frame) · **§12-10**(PIE 10항목).
> 코드는 main 머지 완료(`29f035d7`). **이 문서의 작업을 하기 전까지 실드 시스템은 실제로 켜지지 않는다** — 프로파일 미할당 = 현행 거동(무회귀)이 의도된 설계다.
> 형제 문서 = `Docs/AnimationPass_ContentGuide.md`(같은 성격의 콘텐츠 인계 문서).
> 🔴 Claude 는 DA 값 저작·BP 노드 편집·게임 실행을 하지 않는다(상시 지시). 이 문서는 **값과 위치**만 준다.

---

## 0. 시작 전에 — 이것부터 안 하면 되돌릴 수 없다

**🔴 무회귀 기준선 = 현행 적 `MaxHealth` 전수 기록.**

`VitalsProfile` 을 적 BP 에 할당하는 **순간부터 BP 의 `MaxHealth` 는 무시된다.** 스폰 시 `InitializeVitals` 가 프로파일 값으로 덮기 때문이다(명세 §8). 그래서 **프로파일에 옮겨 적기 전에 할당해 버리면 적 체력이 조용히 바뀌고, 원래 값이 뭐였는지 알 방법이 없어진다.**

**읽는 위치**: 적 BP 열기 → **Components 탭 → `HealthComponent`** 선택 → Details 패널 → **`Max Health`**
(C++ 기본값 = **50**. BP 가 덮어썼으면 그 값이 굵게/노란 되돌리기 화살표와 함께 보인다.)

| 적 BP | 경로 | 현행 `MaxHealth` |
|---|---|---|
| `BP_EnemyMeleeBase` | `Content/Character/Enemy/` | ← 여기 적어 두기 |
| `BP_EnemyRangedBase` | `Content/Character/Enemy/` | ← |
| `BP_EnemyEliteBase` | `Content/Character/Enemy/` | ← |

> ⚠️ **명세 §11-1 은 "적 BP 2종"이라 적었지만 실제로는 3종이다**(위 표, 2026-09-02 실측 정정).
> 파생 BP 가 더 있는지는 `Content/Game/Data/DA_EnemyRoster` → `SpawnRules` 의 적 클래스 목록으로 확인할 것 — 거기 등장하는 클래스가 실제로 스폰되는 전부다.
> 일괄 확인이 편하면 UE 내장 **Property Matrix**(콘텐츠 브라우저에서 여러 에셋 선택 → 우클릭 → Asset Actions → Bulk Edit via Property Matrix).

---

## 1. `DA_Vitals_*` 프로파일 만들기 (§11-1 ①②)

**생성**: 콘텐츠 브라우저 우클릭 → **Miscellaneous → Data Asset** → 클래스 픽커에서 **`FPSRVitals Profile Data Asset`** 선택.
**권장 위치**: `Content/Game/Data/Vitals/`

**필드** (Details 패널)

| 섹션 | 필드 | 기본값 | 저작 지침 |
|---|---|---|---|
| Vitals | `Max Health` | 50 | 🔴 **§0 에서 적어 둔 현행 BP 값을 그대로.** 밸런싱은 그 다음 |
| Vitals | `Max Shield` | 0 | **0 = 실드 없음.** 처음엔 전부 0 으로 두고 **한 종만** 올려 볼 것 |
| Vitals\|Shield Regen | `Shield Regen Per Second` | 0 | ⚠️ `Max Shield > 0` 일 때만 **보인다**(0 이면 숨겨짐) |
| Vitals\|Shield Regen | `Shield Regen Delay Seconds` | 3 | **부분 손상** 후 재생까지 정지 시간 |
| Vitals\|Shield Regen | `Shield Broken Regen Delay Seconds` | 6 | **완파(0 도달)** 후의 더 긴 정지 시간 |
| Vitals\|Defense | `Defense By Damage Type` | 비어 있음 | 비면 전부 1.0. **작을수록 단단하다**(받는 데미지 배수) |
| Vitals\|Defense | `Max Total Reduction` | 0.95 | 건드릴 일 거의 없음. **1.0 은 저장 불가**(에러) |

**만들 것 (권장)**

| 에셋 | 용도 | 시작값 |
|---|---|---|
| `DA_Vitals_EnemyMelee` | 근접 잡몹 | `MaxHealth` = 현행값 · `MaxShield` = **0** |
| `DA_Vitals_EnemyRanged` | 원거리 잡몹 | 〃 |
| `DA_Vitals_EnemyElite` | 엘리트 | 〃 · **여기부터 실드를 켜 볼 것**(예: `MaxShield` 30 · 재생 8/s · 지연 3/6) |
| `DA_Vitals_Player` | 플레이어 | `MaxHealth` = 현행 어트리뷰트 기본 **100** · `MaxShield` = 예 50 · 재생 10/s · 지연 3/6 |

> **왜 엘리트부터인가** — 실드는 잡몹 200~300 마리에 붙는 순간 체감 난이도가 크게 흔들린다. 엘리트 1종으로 먼저 감을 잡고 잡몹으로 내리는 편이 되돌리기 쉽다.
> **저장 시 경고** — `MaxShield > 0` 인데 재생속도가 0 이면 *"이 실드는 한 번 깎이면 영원히 안 찬다"* 경고가 뜬다. 1회성 실드를 의도했으면 무시해도 된다(에러 아님).

### 할당

| 대상 | 경로 | 위치 |
|---|---|---|
| 적 BP 3종 | `Content/Character/Enemy/BP_Enemy*` | **Class Defaults** → 섹션 **`FPSR\|Enemy`** → **`Vitals Profile`** |
| 플레이어 BP | `Content/Character/Player/BP_FPSRPlayer` | **Class Defaults** → 섹션 **`FPSR\|Vitals`** → **`Vitals Profile`** |

같은 섹션의 **`Shield Regen Update Hz`**(기본 10)는 HUD 실드바가 차오르는 **표현 갱신율**이다. 게임플레이에 영향 없음 — 바가 뚝뚝 끊겨 보일 때만 올린다.

---

## 2. 로스터 덱 배수 (§11-1 ③)

`Content/Game/Data/DA_EnemyRoster` → 섹션 **`Roster`** → **`Vitals Modifier`**

| 필드 | 기본 | 의미 |
|---|---|---|
| `Max Health Scale` | 1.0 | 이 로스터가 스폰하는 **모든** 적의 체력 배수 |
| `Max Shield Scale` | 1.0 | 〃 실드량 배수 |
| `Shield Regen Scale` | 1.0 | 〃 재생속도 배수 |

**지금은 전부 1.0(항등)인지 확인만 하고 넘어가면 된다.** 난이도 등급별 로스터가 생길 때 채운다.
> 🔴 **덱은 양(量)만 스케일하고 방어계수는 안 건드린다** — 같은 몬스터가 덱에 따라 다른 속성 저항을 가지면 플레이어가 학습할 수 없기 때문(설계 확정, 명세 §5-2).

---

## 3. 무기 대실드 계수 — **이게 요구의 본체다** (§11-1 ④)

`Content/Weapons/DataTable/DA_Weapon_*` → 섹션 **`무기|기본`** → **`기본 스탯`** 펼치기 → ⚠️ **`Damage` 하위 섹션** → **`Shield Damage Multiplier`**

> ⚠️ **찾기 함정**: `Damage`(데미지 본체)는 `Weapon` 카테고리인데 `Shield Damage Multiplier` 는 `Weapon|Damage` 라 **접힘 섹션이 다르다.** 데미지 옆에 없다고 없는 게 아니다.

| 무기 | 권장 시작값 | 근거 |
|---|---|---|
| `DA_Weapon_Sniper` | **2.0** | 🔴 **저격이 실드에 강하다 = 이 유닛 요구의 본체.** 이 값이 없으면 저격 메리트가 산술적으로 존재하지 않는다 |
| `DA_Weapon_Bazooka` | 1.0~1.5 | 판단 |
| `DA_Weapon_Rifle` / `SMG` / `LMG` / `Shotgun` / `Knife` / `ChargeLaser` | **1.0**(기본) | 평범 |

**의미**: 1 = 평범 · >1 = 실드에 강함 · **0 = 실드를 아예 못 깎음**(전량이 체력으로 이월 = "실드 무시" 무기).
**체력 데미지에는 영향이 없다** — 실드 층에서만 곱해진다.

> 🔴 **이월(초과분 관통)만으로는 저격이 유리해지지 않는다.** 실드 30 에 100 한 방과 50 두 방의 총 체력피해는 **같다**(청킹 중립, 불변식 V3). 이월은 얇은 실드가 큰 한 방을 통째로 삼키는 **페널티를 없앨 뿐**이다. 저격 메리트는 **오직 이 계수**가 만든다. PIE 2번이 이걸 본다.
> 이 축은 **카드로도 조정된다**(`EFPSRWeaponStat::ShieldDamageMultiplier`) — 나중에 "대실드 +50%" 카드를 만들 수 있다.

---

## 4. 힐팩 배치 (§11-1 ⑤)

> 🔴 **주의**: 힐팩은 C3 에서 잡은 P1(틱 미활성) 때문에 **이번이 실동작 최초 확인**이다. 다른 항목보다 먼저 한 개만 놓고 PIE 로 확인할 것.

**권장 절차 — 인스턴스마다 메시를 붙이지 말고 BP 를 하나 만든다**
1. 콘텐츠 브라우저 우클릭 → Blueprint Class → **All Classes** 에서 `FPSRHealthPickup` 검색 → `BP_HealthPickup` 생성 (`Content/Pickups/` 권장)
2. **Components → `Mesh`** 선택 → Details → **`Static Mesh`** 에 눈에 띄는 메시 지정 (C++ 은 메시를 안 잡는다 — 안 넣으면 **투명**하다)
3. Class Defaults → 섹션 **`FPSR|Pickup`** 값 저작
4. `Content/Maps/L_Map1_City` 에 드래그해서 배치

| 필드 | 기본 | 의미 |
|---|---|---|
| `Heal Flat` | 0 | 고정 회복량 |
| `Heal Max Health Fraction` | 0.25 | 최대체력 비율 회복. **둘 다 저작 가능**(합산). 둘 다 0 이면 아예 소모되지 않는다 |
| `Collect Radius` | 120 | 자석 없음 — **일부러 찾아가야 하는 자원**이다 |
| `Respawn Seconds` | 45 | **0 = 1회용**. >0 이면 전투시계 기준(카드 프리즈 중엔 안 흐른다) |
| `Require Missing Health` | true | 체력 만땅인 팀원이 지나가다 삼키는 것 방지 |

> **수집자만 회복한다**(파티 전체 아님) — 4인 협동에서 "다친 사람이 가서 먹는다"가 협동 판단을 만든다.
> **DBNO/사망 플레이어는 못 먹는다.**

---

## 5. HUD 3종 (§11-1 ⑥) — **PIE 1·5번의 선행조건**

### ⑥-① 적 실드바 — `Content/UI/HUD/WBP_EnemyHealthBar`

이 위젯의 부모(`UFPSREnemyHealthBarWidget`)는 **`BindHealthComponent(HealthComp)`** 라는 BlueprintImplementableEvent 하나만 준다. 그 이벤트 안에서:

1. 🔴 **즉시 1회 읽어서 그린다** — `HealthComp` 에서 `Get Shield` / `Get Max Shield` 를 바로 호출해 바를 세팅. **이걸 빠뜨리면 위젯이 늦게 붙는 경우를 못 덮는다**(이벤트만 구독하면 첫 그림이 디자이너 기본값으로 샌다).
2. `HealthComp` 의 **`On Health Changed`** 에 바인드 → 이제 **실드 전용 피격에도 발화한다**(체력이 안 변한 히트도 리페인트 신호가 온다). 핸들러에서 `Get Shield`/`Get Max Shield` 재조회.
3. `HealthComp` 의 **`On Shield Broken Cosmetic`**(파라미터 없음)에 바인드 → 실드 깨지는 이펙트.

쓸 수 있는 노드(전부 `HealthComp` 대상, BlueprintPure): **`Get Shield`** · **`Get Max Shield`** · **`Is Shield Broken`** · `Get Health` · `Get Max Health`
> `Max Shield` 가 0 이면 실드바를 **숨길 것**(실드 없는 개체가 정상이다).

### ⑥-② `ShieldBreak` 히트마커 — `Content/UI/HUD/WBP_HitMarker`

`UFPSRPlayerFeedbackComponent::OnHitMarker`(파라미터 `EFPSRHitMarkerType`)를 이미 구독하고 있을 것이다. **enum 에 5번째 값 `Shield Break` 가 추가됐다.**

🔴 **`Switch on EFPSRHitMarkerType` 노드에 새 출력 핀 `Shield Break` 가 생겼는데 연결이 비어 있다** — 연결하지 않으면 실드를 깼을 때 마커가 **아무것도 안 뜬다**(다른 종류로 폴백하지 않는다). 전용 연출을 붙일 것.

우선순위는 C++ 이 정한다: **Kill > ShieldBreak > Weak > Crit > Hit**. 한 번의 발사에 여러 결과가 겹치면 가장 위 하나만 온다.
> enum 은 **말미에 추가**했으므로 기존에 저작된 마커 값들은 그대로다(값 밀림 없음).

### ⑥-③ 본인 실드바 / 팀원 실드바

플레이어 실드는 GAS 어트리뷰트(PlayerState ASC)라 **이미 전원에게 복제된다 — 바인딩만 하면 된다.**

BP 경로: **`Get Float Attribute`**(GAS 라이브러리 노드)
- `Actor` = 플레이어 캐릭터 또는 그 PlayerState (둘 다 `IAbilitySystemInterface` 구현)
- `Attribute` = 픽커에서 **`FPSRHealthSet.Shield`** / **`FPSRHealthSet.MaxShield`**
- 체력도 같은 방식(`Health` / `MaxHealth`)

> ⚠️ **C++ 편의 접근자가 없다.** 적 실드바에는 `Get Shield` 같은 BlueprintPure 가 있지만 **플레이어 쪽은 아무것도 없어서** 위젯 Tick 에서 `Get Float Attribute` 를 폴링하는 형태가 된다. 원하면 Claude 가 `UFPSRRunHUDWidget` 에 BlueprintPure 접근자(`GetShield01` 등)를 추가할 수 있다 — **말해 줄 것**(코드 작업).
> ⚠️ **숨겨진 위젯은 Tick 이 안 돈다**(UMG 의 Tick 은 Paint 안에서 불린다). 실드바를 `Collapsed` 로 토글하는 구조면 루트는 항상 페인트 상태로 두고 안쪽만 숨길 것.

### ⑥-④ 실드 파손 위험 경고 — 🔴 **지금은 코드가 막고 있다**

명세 §11-1 은 이 위젯이 GMS 채널 `Message.Player.ShieldBroken` 을 구독하라고 적었지만, **`UFPSRGameplayMessageSubsystem` 에는 `UFUNCTION` 이 하나도 없다** — `Get`·`RegisterListener` 전부 C++ 템플릿이라 **WBP 에서 구독할 방법이 없다**(2026-09-02 실측).

**→ 이 항목만 보류하고 나머지를 먼저 하라.** 처리 방안은 아래 §8 에 있다.

---

## 6. 카드풀에서 `HealthRegen` 제거 (§11-1 ⑦)

`Content/Cards/Character/DA_Character_CardPool` → 섹션 **`Card Pool`** → **`Cards`** 배열에서
**`DA_Card_Character_HealthRegen`** 항목 삭제.

**왜** — VIT1 확정 규칙: **체력은 스스로 차지 않는다.** 회복 경로는 ①맵 힐팩 ②흡혈 카드 ③부활(50%) ④`MaxHealth` 증가분 즉시 회복, 넷뿐이다.
> 🔴 **에셋 자체는 지우지 말 것**(`DA_Card_Character_HealthRegen` 파일은 남긴다). 풀에서 빼기만 한다 — 그 카드가 쓰는 `UCardEffect_CharacterGE` 는 다른 카드들이 계속 쓴다.
> ⚠️ 관련 미해결 건: 이 카드가 쓰던 **periodic GE 는 전역 프리즈 중에도 회복된다**(오늘도 그렇다). 카드를 빼는 건 우회일 뿐 근본 해결이 아니라 보드에 별도 행으로 올라가 있다.

---

## 7. i-frame 재조정 — **런타임 체감 판정** (§11-2)

`Content/Character/Player/BP_FPSRPlayer` → Class Defaults → 섹션 **`FPSR|Combat`** → **`Damage Invulnerability Duration`**

현재 **0.25초**. 실드가 얹히면서 "연속 피격 흡수" 역할을 실드가 나눠 갖게 됐으므로 i-frame 의 몫이 줄었다 — 그대로 두면 스웜 근접 압박이 사라질 수 있다.
**권장 탐색 범위 0.10 ~ 0.25초.** 정답은 없고 **PIE 체감으로 사용자가 정한다.**

---

## 8. 순서 · 그리고 막힌 것

**권장 순서** (앞이 뒤의 선행조건)

1. **§0 현행 `MaxHealth` 전수 기록** ← 이거 먼저. 안 하면 되돌릴 수 없다
2. **§1 프로파일 생성**(전부 `MaxShield` = 0) → **적·플레이어 BP 할당**
   → 여기서 PIE **5·8번**(실드 없는 적·문/구조물이 현행과 완전히 동일) 확인 = **무회귀 증명**
3. **§4 힐팩** 한 개 배치 → PIE **7번**(C3 P1 때문에 실동작 최초 확인)
4. **§1 엘리트 프로파일에만 `MaxShield` > 0** → **§5 HUD ①②③** → PIE **1·3·4·5·6·9번**
5. **§3 무기 SDM**(저격 2.0) → PIE **2·2-1번**
6. **§2 로스터**(항등 확인) · **§6 카드풀 제거** · **§7 i-frame**

**PIE 10항목 전문 = 명세 §12-10.** 2인 이상 · `L_Lobby` 에서 시작(메뉴에서 시작하면 안 된다).
가장 놓치기 쉬운 것 = **4번(프리즈 30초 동안 공짜 충전 없음)** 과 **9번(부분 손상 직후 `+MaxShield` 카드가 살아남는가)**.

### 🔴 코드가 막고 있는 것 — 결정 필요

| 항목 | 상태 |
|---|---|
| §5 ⑥-④ **실드 파손 경고 위젯** | **불가.** GMS 가 BP 에 노출돼 있지 않다 |
| §5 ⑥-③ 본인 실드바 | 가능하지만 **원시 GAS 폴링**. C++ 접근자를 추가하면 훨씬 깔끔 |

**권장 처리** — 경고를 `UFPSRPlayerFeedbackComponent` 로 옮긴다. 그 컴포넌트는 **이미 플레이어 로컬 HUD 코스메틱의 창구**이고(`OnHitMarker` · `OnDamageDirection` · `OnRangedTargetWarning` 전부 `BlueprintAssignable`), 자기 헤더 주석에 *"single-consumer local HUD, local, cosmetic → a pawn component + delegates is the minimal correct structure (no replication, no message bus). GameplayMessageSubsystem 은 소비자가 여럿이 될 때의 업그레이드 경로"* 라고 적어 뒀다. 실드 파손 경고는 정확히 전자다.
→ `OnShieldBroken` 을 `BlueprintAssignable` 로 추가하면 WBP 가 `OnHitMarker` 와 **똑같은 방식**으로 바인드한다.
