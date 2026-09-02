# BOSS1 — 보스 패턴 콘텐츠 저작 가이드 (사용자 작업)

> 코드(S1·S2·S3)는 `feat/boss-patterns` 에 머지 대기 상태로 올라가 있다. **이 문서의 작업을 하기 전까지
> 보스는 화면에서 아무것도 하지 않는다** — 패턴 목록이 비어 있으면 선택기가 고를 것이 없기 때문이다.
> 명세 정본 = [`Docs/Specs/BOSS1_AbilityPatternFramework.md`](Specs/BOSS1_AbilityPatternFramework.md).
> 선례 형식 = `Docs/VIT1_ContentGuide.md`.

---

## 0. 이 작업이 만드는 것

| 만드는 것 | 어디에 | 왜 |
|---|---|---|
| 패턴 BP 3개 | `Content/Character/Boss/Patterns/` | 수치를 저작할 자리. C++ 는 구조만 갖고 있다 |
| `BP_BossHomingOrb` | `Content/Character/Boss/` | 격추 대상의 몸통(메시·트레일) |
| `BP_Boss` 배선 | 기존 에셋 | 패턴 3개를 보스에 물린다 |
| 표식·빔 연출 | `BP_Boss` 이벤트 그래프 | `Source/` 에는 데칼·Niagara 호출이 **0건**이다 — 연출은 전부 여기 |
| `DA_BossDefinition` 값 | 기존 에셋 | 페이즈 임계값 + 체력 |

---

## 1. 패턴 BP 3개

`Content/Character/Boss/Patterns/` 를 만들고, 각각 **Blueprint Class** 로 생성한다(부모 클래스 검색창에 입력).

| BP 이름 | 부모 클래스 | 핵심 저작값 |
|---|---|---|
| `GA_Boss_Barrage` | `FPSRBossGA_Barrage` | `ShellsPerPlayer` 5 · `IntervalSeconds` 2.0 · `FuseSeconds` 1.4 · `RadiusCm` 500 · `Damage` · `CooldownSeconds` |
| `GA_Boss_SweepLaser` | `FPSRBossGA_SweepLaser` | `WarmupSeconds` 1.5 · `AngularSpeedDegPerSec` 30 · `BeamsPerPhase` 1 · `Revolutions` 2 · `Damage` · `CooldownSeconds` |
| `GA_Boss_HomingOrbs` | `FPSRBossGA_HomingOrbs` | `OrbCount` 5 · `OrbHealth` **150** · `TrackSeconds` 8 · `OrbClass` = 아래 `BP_BossHomingOrb` · `Damage` · `CooldownSeconds` |

기본값은 이미 C++ 에 들어 있으니 **그대로 두고 시작해도 된다.** 위 표는 어떤 손잡이가 있는지 보여 주는 것이다.

> 🔴 **BP 에서 하지 말 것 (둘 다 조용히 깨진다)**
> 1. **`Event ActivateAbility` 를 통째로 구현하지 마라.** C++ 부모가 거기서 `CommitAbility` 를 부르는데,
>    덮으면 쿨다운이 영영 안 찍혀 그 패턴이 무한 재사용된다.
> 2. **`Delay` · `WaitDelay` · `PlayMontageAndWait` 같은 시간 노드를 쓰지 마라.** 이것들은 월드 타이머로
>    돌아서 레벨업 프리즈를 그냥 뚫는다(카드 고르는 동안 보스가 계속 공격한다). 시간이 필요하면
>    `ServerTickPattern` 의 `DeltaSeconds` 를 쓴다.

`MinPhase` 를 올리면 그 패턴은 해당 페이즈부터 나온다. 예: 레이저를 2페이즈부터 보고 싶으면 `MinPhase = 2`.

---

## 2. `BP_BossHomingOrb`

**Blueprint Class → 부모 `FPSRBossHomingOrb`**, 이름 `BP_BossHomingOrb`, 위치 `Content/Character/Boss/`.

- `Mesh` 에 구체 메시를 넣는다(엔진 `/Engine/BasicShapes/Sphere` 로 시작해도 된다). 크기는 `Sphere` 컴포넌트
  반지름(45cm)에 대충 맞춘다.
- **`Mesh` 의 콜리전은 건드리지 마라** — `NoCollision` 이 정상이다. 피격 볼륨은 `Sphere` 다.
- 트레일/글로우 Niagara 를 붙이고 싶으면 `Mesh` 아래에 붙인다.
- `Event On Orb Destroyed Cosmetic` 에 격추 연출(파티클·사운드)을 건다. **이 이벤트는 클라에서도 불린다.**

---

## 3. `BP_Boss` 배선

`Content/Character/Boss/BP_Boss` 를 연다.

### 3-1. 패턴 물리기
클래스 디폴트 → `FPSR|Boss|Patterns` → **`Granted Abilities`** 배열에 §1 의 BP 3개를 넣는다.
**순서가 곧 라운드로빈 순서다.**

### 3-2. 표식 연출 (포격)
표식은 **매 프레임 배열을 읽어 그리는 방식**이다(이벤트가 아니다 — 불발과 폭발을 클라가 구분할 수 있어야 해서).

`Event Tick` 에서:
1. `Get Blast Marks` → 배열
2. 각 항목의 `Center`(월드 좌표) · `Radius` · `DetonateAtClock` · `TargetPawn`
3. `Get Pattern Clock Seconds` 와 `DetonateAtClock` 을 빼면 **남은 시간**이 나온다 → 링 채우기/색 보간에 쓴다
4. 남은 시간이 0 이하가 되는 프레임에 폭발 연출을 낸다

> 💡 `TargetPawn` 이 내 폰이면 다른 색으로 그린다. 4인이면 표식이 여럿 뜨는데, 내 것을 구분 못 하면
> 판독이 어렵다(명세가 이걸 유일한 미결 밸런스 항목으로 남겨 뒀다).
> 💡 배열에서 사라졌는데 `DetonateAtClock` 이 아직 안 지났으면 **불발**이다(보스 사망 등) — 폭발 연출을 내지 마라.

### 3-3. 빔 연출 (레이저)
`Event Tick` 에서 `Get Beam State` → `OutBeamCount` · `OutBaseAngleDeg` · `bOutWarmup` · 반환값(활성 여부).

- 빔 `i` 의 각도 = `OutBaseAngleDeg + i * (360 / OutBeamCount)`
- 각 빔을 보스 중심에서 그 각도로 뻗는 판/메시로 그린다. 길이는 아레나를 덮을 만큼(160m 아레나 기준 120m 이상)
- **`bOutWarmup == true` 인 동안은 다른 색**(예: 붉은 예고선)으로 그린다 — 그 구간엔 데미지가 없다.
  이 예고가 규격 요구사항이라(`Enemy.md §2-6` 부조리 탄막 금지) 시각적으로 확실히 구분되게 할 것.
- 높이는 발목 근처(60cm 부근). ⚠️ **이건 순수 연출이다** — 실제 판정은 "공중이었는가"로 하지 발 높이로 하지 않는다.
  그래도 **보이는 높이와 판정이 어긋나면 플레이어가 배신감을 느끼므로** 점프로 넘는 것처럼 보이게 맞춰라.

### 3-4. 페이즈 연출
`Event On Phase Changed Cosmetic (NewPhase)` — 색 변화·이펙트·사운드. 클라·호스트 양쪽에서 불린다.

---

## 4. `DA_BossDefinition`

`Content/Character/Boss/DA_BossDefinition`:

| 항목 | 값 | 비고 |
|---|---|---|
| `Phase Health Thresholds` | `[0.66, 0.33]` | **배열 길이가 곧 페이즈 수다.** 2개 = 3페이즈. 내림차순 · 0과 1 사이 |
| `Max Health` | 재조정 | 25m 짜리 표적이라 맞히기 쉽다. 패턴이 생겼으니 다시 볼 것 |

에셋을 저장하면 **데이터 검증이 자동으로 돈다** — 임계값이 내림차순이 아니거나 0/1 밖이면 그 자리에서 잡아 준다.

---

## 5. PIE 검증 (11항목)

`FPSR.SkipToBoss` 로 보스전에 진입한 뒤, 아래 콘솔로 패턴을 **강제 발동**한다(쿨다운을 기다리지 않아도 된다):

```
FPSR.BossPattern 0     (Granted Abilities 배열의 인덱스)
FPSR.BossPhase 2       (페이즈 강제 — 되돌릴 수는 없다)
```

| # | 확인할 것 |
|---|---|
| 1 | 포격 착탄점이 **각자 발밑**에 뜬다. 공중에 떠 있어도 표식은 지면에 눕는다 |
| 2 | 레이저 **웜업 동안 데미지가 없고** 경고 인디케이터가 떴다가 **꺼진다** |
| 3 | 점프로 빔을 넘을 수 있다 / **빔 안으로 뛰어들면 맞는다** |
| 4 | **2인 PIE 클라 화면**에서 보이는 빔과 맞는 순간이 일치한다 |
| 5 | 오브가 총에 부서지고 **격추 연출이 클라에도 보인다** · XP 가 안 나온다 |
| 6 | **오브가 프롭 위를 지날 때도 데미지가 들어간다** (폰이었으면 무적이 됐을 지점) |
| 7 | **패턴 진행 중 레벨업 프리즈 30초** — 신관·빔 각도·오브가 전부 멈춘다 |
| 8 | 페이즈 2·3 에서 빔이 2줄·3줄 |
| 9 | 보스 처치 후 **시체가 공격하지 않고** 표식·오브가 남지 않는다 |
| 10 | **리슨서버 호스트 화면**에서 페이즈·빔·격추 연출이 전부 난다 (호스트는 OnRep 을 못 받는다) |
| 11 | 런 재시작 후 이전 런의 표식·오브가 월드에 없다 |

7번과 10번이 가장 잘 놓치는 항목이다. 7번은 이 유닛 설계의 본체이고, 10번은 직전 유닛(VIT1)이
실제로 놓쳐서 결함이 됐던 형태다(2인 PIE 는 클라 화면만 보면 정상으로 보인다).

---

## 6. 알려진 한계 (이번 범위에서 감수한 것)

- **보스는 여전히 움직이지 않는다.** 이동·StateTree 는 비목표였고, 세 패턴 모두 정지한 중앙 타워 전제에서 성립한다.
- **패턴은 한 번에 하나만 나간다.** 선택기가 단일 활성이다. 레이저+포격 동시는 후속 확장.
- **레이저는 지형을 관통한다**(사용자 결정). 엄폐로는 못 피하고 점프만이 회피 수단이다.
- **클라 빔은 편도 지연만큼의 오차가 남는다.** 코스메틱은 앞당겨 그리고 판정은 늦게 결제해 양쪽에서
  열어 뒀지만, 서버 권위인 이상 완전히 없앨 수는 없다.
