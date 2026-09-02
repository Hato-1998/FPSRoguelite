# BOSS1 — 보스 패턴 콘텐츠 저작 가이드 (사용자 작업)

> 코드(S1·S2·S3 + §14 개정)는 `feat/boss-patterns` 에 머지 대기 상태로 올라가 있다.
> **이 문서의 작업을 하기 전까지 보스는 화면에서 아무것도 하지 않는다** — 패턴 목록이 비어 있으면 선택기가 고를
> 것이 없기 때문이다.
> 명세 정본 = [`Docs/Specs/BOSS1_AbilityPatternFramework.md`](Specs/BOSS1_AbilityPatternFramework.md) (개정분 = §14).
> 선례 형식 = `Docs/VIT1_ContentGuide.md`.

---

## 0. 이 작업이 만드는 것

| 만드는 것 | 어디에 | 왜 |
|---|---|---|
| 패턴 BP 3개 | `Content/Character/Boss/Patterns/` | 수치를 저작할 자리. C++ 는 구조만 갖고 있다 |
| `BP_BossHomingOrb` | `Content/Character/Boss/` | 격추 대상의 몸통(메시·트레일) |
| `BP_Boss` 배선 | 기존 에셋 | 패턴 3개 + **트리거** + 연출 |
| 표식·빔·마크 연출 | `BP_Boss` 이벤트 그래프 | `Source/` 에는 데칼·Niagara 호출이 **0건**이다 — 연출은 전부 여기 |
| `DA_BossDefinition` 값 | 기존 에셋 | 페이즈 임계값 + 체력 |

---

## 1. 보스가 언제 공격하는가 — 트리거 (§14 신규, 여기부터 읽을 것)

**패턴은 연달아 나가지 않는다.** 한 패턴이 후딜까지 끝나면 보스는 유휴 상태로 들어가고, **트리거가 발화해야**
다음 패턴이 시작된다. `BP_Boss` 클래스 디폴트 → `FPSR|Boss|Patterns`:

| 항목 | 설명 |
|---|---|
| **`Pattern Triggers`** | 배열. 하나라도 발화하면 다음 패턴이 시작된다 |
| ↳ `Kind` | `Elapsed`(보스전 경과 초) · `Pattern Count`(수행한 패턴 수) · `Health Below`(체력 비율 이하) |
| ↳ `Threshold` | 초 / 개수 / 비율(0~1) |
| ↳ `b Repeating` | `Elapsed`·`PatternCount` 만 의미 있음. **`HealthBelow` 는 켜도 1회만 발화**한다(체력은 내려가기만 하므로 반복이면 "그 뒤로 매 틱 영원히"가 된다) |
| **`Selection Policy`** | `Sequential`(배열 순서대로 — 플레이어가 로테이션을 학습할 수 있다) / `Random` |
| `Pattern Gap Seconds` | **트리거 배열이 비었을 때만** 쓰이는 폴백 주기 |

**추천 시작값**
```
Pattern Triggers:
  [0] Elapsed      / Threshold 8   / Repeating ✔     ← 기본 리듬
  [1] Health Below / Threshold 0.66 / (1회)          ← 2페이즈 진입에 한 번 몰아친다
  [2] Health Below / Threshold 0.33 / (1회)          ← 3페이즈 진입
Selection Policy: Sequential
```

> 🔴 **배열을 비워 두지 마라.** 비면 보스가 영원히 아무것도 하지 않는다. 코드가 폴백을 걸고 로그로 경고하지만,
> 그건 안전장치이지 의도가 아니다.
> 💡 "평타 5회 뒤" 같은 트리거는 **아직 없다** — 보스에게 평타가 없어 셀 대상이 없다. `Pattern Count` 가 그 자리를
> 대신한다. 평타가 붙으면 트리거 종류만 하나 늘어난다.

---

## 2. 패턴 BP 3개

`Content/Character/Boss/Patterns/` 를 만들고, 각각 **Blueprint Class** 로 생성한다(부모 클래스 검색창에 입력).

### 공통 — 모든 패턴이 갖는 손잡이 (`FPSR|Boss|Pattern`)

| 항목 | 뜻 |
|---|---|
| **`Prep Seconds`** | 보스가 모션을 잡는 구간. **이 동안 패턴은 아무것도 스폰하지 않는다** |
| **`Recovery Seconds`** | 실행이 끝난 뒤의 유휴. 이 동안 다음 패턴이 시작되지 않는다 |
| `Cooldown Seconds` | 이 패턴 자신의 재사용 대기(프리즈 중엔 흐르지 않는다) |
| `Min Phase` | 이 페이즈부터 선택기가 고른다. 1 = 처음부터 |

> 시간이 **3층**이라는 점을 기억할 것: **준비**(보스 모션, 아무것도 없음) → **패턴 내부 유예**(표식 신관 /
> 빔 정지 / 미사일 대기) → **후딜**(유휴). 셋 다 별개 숫자다.

### 2-1. `GA_Boss_Barrage` (부모 `FPSRBossGA_Barrage`)

| 항목 | 기본값 |
|---|---|
| `Shells Per Player` | 5 (플레이어 1인당 발수) |
| `Interval Seconds` | 2.0 (볼리 간격) |
| `Fuse Seconds` | 1.4 (표식 등장 → 폭발) |
| `Radius Cm` | 500 |
| `Damage` / `Knockback Strength` / `Damage Type` | 밸런스 |

### 2-2. `GA_Boss_SweepLaser` (부모 `FPSRBossGA_SweepLaser`)

| 항목 | 기본값 | 비고 |
|---|---|---|
| **`Beam Grace Seconds`** | 1.5 | 빔이 생기고 **정지한 채 무해한** 구간. 12/3/6/9 중 랜덤으로 생기므로 누군가의 머리 위에 뜰 수 있고, **이 구간이 그 사람이 빠져나갈 유일한 창**이다 |
| **`Angular Speed By Phase`** | `[30, 45, 60]` | 인덱스 = 페이즈−1. 넘어가면 마지막 값. 양수 = 시계방향 |
| `Beams Per Phase` / `Max Beams` | 1 / 5 | 빔 수 = `clamp(페이즈 × BeamsPerPhase, 1, MaxBeams)` |
| `Beam Half Width Deg` | 1.5 | 판정 폭(플레이어 캡슐 각반경이 자동 가산된다) |
| `Revolutions` | 2.0 | 몇 바퀴 돌고 끝나는가 |
| `Beam Visual Height Cm` | 60 | 🔴 **연출 전용.** 판정은 "공중이었는가"이지 발 높이가 아니다 |
| `Inner Radius Cm` | 1250 | 빔이 시작되는 반지름(보스 몸통). 안전지대가 아니다 |

> 첫 빔만 12/3/6/9 중 랜덤이고 **나머지는 등간격**이다. 전부 방위에 고정하면 3개일 때 90/90/180 이 되어
> 180° 쪽에 넓은 안전지대가 고정된다.

### 2-3. `GA_Boss_HomingOrbs` (부모 `FPSRBossGA_HomingOrbs`)

| 항목 | 기본값 | 비고 |
|---|---|---|
| `Orb Count` / `Orb Health` | 5 / **150** | |
| **`Orb Grace Seconds`** | 0.8 | 링에서 **대기**하는 시간. 이후 추적 시작 |
| **`Spawn Ring Radius Cm`** | 2200 | 보스 주위 링 반지름 |
| `Spawn Height Cm` | 1500 | 링 높이 |
| `Spawn Interval Seconds` | 0.25 | 한 기씩 생기는 간격 |
| `Track Seconds` | 8.0 | 추적 지속. 지나면 직진 |
| **`Blast Radius Cm`** | 400 | 🔴 터질 때 **반경 안 모든 플레이어**를 때린다 |
| `Damage` / `Blast Knockback` / `Damage Type` | 밸런스 | |
| **`Orb Class`** | ← §3 의 `BP_BossHomingOrb` | 비우면 아무것도 안 나간다 |

> **대상이 죽으면** 미사일은 사라지지 않고 **마지막 위치로 가서 터진다** — 즉 부활하러 모인 팀원이
> 폭심지에 있게 된다. 이게 이 패턴이 뭉침을 벌하는 방식이다.

> 🔴 **BP 에서 하지 말 것 (둘 다 조용히 깨진다)**
> 1. **`Event ActivateAbility` 를 구현하지 마라.** C++ 부모가 거기서 `CommitAbility` 를 부르고 준비 단계를
>    시작한다. 덮으면 쿨다운이 영영 안 찍히고 준비 구간도 사라진다.
> 2. **`Delay` · `WaitDelay` · `PlayMontageAndWait` 같은 시간 노드를 쓰지 마라.** 월드 타이머로 돌아서
>    레벨업 프리즈를 그냥 뚫는다(카드 고르는 동안 보스가 계속 공격한다).

---

## 3. `BP_BossHomingOrb`

**Blueprint Class → 부모 `FPSRBossHomingOrb`**, 이름 `BP_BossHomingOrb`, 위치 `Content/Character/Boss/`.

- `Mesh` 에 구체 메시(`/Engine/BasicShapes/Sphere` 로 시작해도 된다). `Sphere` 컴포넌트 반지름 45cm 에 맞춘다.
- **`Mesh` 콜리전은 건드리지 마라** — `NoCollision` 이 정상이다. 피격 볼륨은 `Sphere` 다.
- 이벤트 **두 개를 다르게** 만들 것:

| 이벤트 | 언제 | 어떻게 보여야 하나 |
|---|---|---|
| `On Orb Destroyed Cosmetic` | **플레이어가 격추함** | 깨지는 연출. **폭발 없음** — 데미지도 없다 |
| `On Orb Detonated Cosmetic` | 플레이어 접촉 · 벽 충돌 · 사망 지점 도착 | 폭발. `Blast Radius Cm` 만큼 |

> 둘을 같은 연출로 만들면 **플레이어가 이겼을 때와 보스가 맞혔을 때가 구분되지 않는다.**

---

## 4. `BP_Boss` 배선

### 4-1. 패턴 물리기
클래스 디폴트 → `FPSR|Boss|Patterns` → **`Granted Abilities`** 에 §2 의 BP 3개. 순서가 `Sequential` 순서다.
그리고 §1 의 **`Pattern Triggers`** 를 채운다.

### 4-2. 준비 / 후딜 모션 (§14 신규)
`Event On Pattern Stage Changed Cosmetic (New Stage)`:

| Stage | 연출 |
|---|---|
| `Prep` | 보스가 힘을 모으는 모션·발광. **여기서 플레이어에게 "온다"를 알린다** |
| `Execute` | 패턴 자체 연출 |
| `Recovery` | 지친/식는 모션. 이 구간엔 보스가 아무 데미지도 주지 않는다 |
| `Finished` | 평시로 |

### 4-3. 지목 공지 (§14 신규)
`Event On Marked Player Changed Cosmetic (New Marked)` — 미사일 패턴이 대상을 고르면 **전원에게** 불린다.
`New Marked` 가 내 폰이면 다르게(경고음·화면 테두리), 남이면 그 사람 위에 마커. `null` 이면 해제.

> 이게 없으면 지목당한 사람만 상황을 알고 나머지 셋은 왜 미사일이 저기로 가는지 모른다.

### 4-4. 표식 연출 (포격)
표식은 **매 프레임 배열을 읽어 그리는 방식**이다(이벤트가 아니다 — 불발과 폭발을 클라가 구분할 수 있어야 해서).

`Event Tick` 에서:
1. `Get Blast Marks` → 배열
2. 각 항목의 `Center`(월드) · `Radius` · `DetonateAtClock` · `TargetPawn`
3. `Get Pattern Clock Seconds` 와 `DetonateAtClock` 의 차 = **남은 시간** → 링 채우기
4. 남은 시간이 0 이하가 되는 프레임에 폭발 연출

> 💡 `TargetPawn` 이 내 폰이면 다른 색으로. 4인이면 표식이 여럿 뜬다.
> 💡 배열에서 사라졌는데 `DetonateAtClock` 이 아직 안 지났으면 **불발**이다 — 폭발 연출을 내지 마라.

### 4-5. 빔 연출 (레이저)
`Event Tick` 에서 `Get Beam State` → `OutBeamCount` · `OutBaseAngleDeg` · `bOutWarmup` · 반환값(활성 여부).

- 빔 `i` 의 각도 = `OutBaseAngleDeg + i * (360 / OutBeamCount)`
- 보스 중심에서 그 각도로 뻗는 판/메시. 길이는 아레나를 덮을 만큼(160m 아레나면 120m 이상)
- 🔴 **`bOutWarmup == true` 인 동안은 확실히 다른 색**(예: 붉은 예고선). 그 구간엔 빔이 **정지해 있고 무해**하다.
  이 구분이 규격 요구사항이다(`Enemy.md §2-6` 부조리 탄막 금지)
- 높이는 `Beam Visual Height Cm` 부근. ⚠️ 판정은 "공중이었는가"지만, **보이는 높이와 판정이 어긋나면
  플레이어가 배신감을 느끼므로** 점프로 넘는 것처럼 보이게 맞춰라

### 4-6. 페이즈 연출
`Event On Phase Changed Cosmetic (New Phase)` — 색·이펙트·사운드.

---

## 5. `DA_BossDefinition`

| 항목 | 값 | 비고 |
|---|---|---|
| `Phase Health Thresholds` | `[0.66, 0.33]` | **배열 길이가 곧 페이즈 수다.** 2개 = 3페이즈. 내림차순 · 0과 1 사이 |
| `Max Health` | 재조정 | 25m 표적이라 맞히기 쉽다. 패턴이 생겼으니 다시 볼 것 |

저장하면 데이터 검증이 자동으로 돈다 — 내림차순이 아니거나 0/1 밖이면 그 자리에서 잡아 준다.

---

## 6. PIE 검증

`FPSR.SkipToBoss` 로 진입한 뒤, 콘솔로 패턴을 **강제 발동**한다(쿨다운·트리거를 기다리지 않아도 된다):

```
FPSR.BossPattern 0     (Granted Abilities 배열의 인덱스)
FPSR.BossPhase 2       (페이즈 강제 — 되돌릴 수는 없다)
```

| # | 확인할 것 |
|---|---|
| 1 | **준비 모션이 보이고**, 그 동안 아무것도 스폰되지 않는다 |
| 2 | **후딜 동안 보스가 정말 아무 데미지도 주지 않는다** |
| 3 | 패턴이 연달아 나가지 않는다 — 트리거가 발화할 때만 시작된다 |
| 4 | 포격 착탄점이 **각자 발밑**. 공중에 떠 있어도 표식은 지면에 |
| 5 | 레이저가 12/3/6/9 중 한 곳에서 생기고, **유예 동안 정지 + 무피해**, 그 뒤 회전 |
| 6 | 페이즈가 오르면 빔이 늘고 **회전이 빨라진다** |
| 7 | 점프로 빔을 넘을 수 있다 / **빔 안으로 뛰어들면 맞는다** |
| 8 | **2인 PIE 클라 화면**에서 보이는 빔과 맞는 순간이 일치한다 |
| 9 | 미사일 지목이 **전원에게** 보인다 |
| 10 | 미사일이 링으로 생겨 **대기했다가** 추적을 시작한다 |
| 11 | **지목당한 사람이 죽으면 미사일이 그 자리로 가서 터진다**(부활하러 가면 맞는다) |
| 12 | 오브가 총에 부서지고 **격추 연출이 클라에도 보인다** · XP 가 안 나온다 · **폭발하지 않는다** |
| 13 | **오브가 프롭 위를 지날 때도 데미지가 들어간다** |
| 14 | **패턴 중 레벨업 프리즈 30초** — 신관·빔 각도·미사일이 전부 멈춘다 |
| 15 | 보스 처치 후 **시체가 공격하지 않고** 표식·오브가 남지 않는다 |
| 16 | **리슨서버 호스트 화면**에서 페이즈·빔·격추·지목 연출이 전부 난다 |
| 17 | 런 재시작 후 이전 런의 표식·오브가 월드에 없다 |

14번과 16번이 가장 잘 놓친다. 14번은 이 유닛 설계의 본체이고, 16번은 직전 유닛(VIT1)이 실제로 놓쳐서
결함이 됐던 형태다(2인 PIE 는 클라 화면만 보면 정상으로 보인다).

---

## 7. 알려진 한계 (이번 범위에서 감수한 것)

- **보스는 여전히 움직이지 않는다.** 이동·StateTree 는 비목표였고, 세 패턴 모두 정지한 중앙 타워 전제에서 성립한다.
- **평타가 없다.** 유휴·후딜 구간에 보스가 완전히 무해하다 — 그 시간이 길수록 지루해진다. 평타는 별도 슬라이스.
- **패턴은 한 번에 하나만.** 선택기가 단일 활성이다. 레이저+포격 동시는 후속.
- **레이저는 지형을 관통한다**(사용자 결정). 엄폐로는 못 피하고 점프만이 회피 수단이다.
- **클라 빔은 편도 지연만큼의 오차가 남는다.** 코스메틱은 앞당겨 그리고 판정은 늦게 결제해 양쪽에서 열어 뒀지만,
  서버 권위인 이상 완전히 없앨 수는 없다.
