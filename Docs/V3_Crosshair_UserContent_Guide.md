# V3 — 기본 크로스헤어 HUD 유저 콘텐츠 작업 가이드

> **유닛**: V3 (기본 크로스헤어 HUD / 정적 레티클) — `Docs/TaskPrompts_Master.md` §C V3
> **브랜치**: `phase/p4d-crosshair` (main 분기, §6-7)
> **성격**: **순수 콘텐츠(WBP)**. C++ 변경 없음.
> **읽은 SSOT**: PlayerFeel §2-14(게임필/HUD), CombatWeaponCard §2-4-2(확산/ADS), 메모리 vibeue-mcp-capabilities.

## 핵심 확정 (조사 결과)
- **C++ 변경 불필요** — `IsAiming()`이 이미 `BlueprintPure`(`Source/FPSRoguelite/Public/Weapon/FPSRWeaponFireComponent.h:38-39`). 위젯에서 `Get Component By Class`로 FireComponent를 얻어 바로 호출 가능.
- HUD 인프라: `WBP_GameHUD`(Game 레이어, 중앙)가 PlayerController `EnsurePrimaryLayout`에서 `UI.Layer.Game`에 push. `WBP_HitMarker`도 같은 중앙. 크로스헤어는 그 캔버스에 중앙 자식으로 추가, **Z는 히트마커보다 아래**.
- FireComponent는 `AFPSRCharacter`의 컴포넌트(`WeaponFire`). 위젯에서 `Owning Player Pawn → Get Component By Class(FPSRWeaponFireComponent)`로 접근.

---

## STEP 0 — 브랜치 (작업 격리) — ✅ 완료됨

병렬 세션이 없으므로 워크트리 없이 **단일 작업 폴더에서 브랜치 체크아웃**으로 진행한다. 이미 셋업 완료:

```powershell
# (이미 적용됨) main 기준 V3 브랜치 생성 + 체크아웃
git checkout -b phase/p4d-crosshair main   # 또는 이미 있으면: git checkout phase/p4d-crosshair
```

현재 작업 폴더 `E:/Git_Project/FPSRoguelite`가 `phase/p4d-crosshair` 브랜치다. **에디터를 `FPSRoguelite.uproject`로 그대로 열어** 작업하면 저장되는 `.uasset`이 이 브랜치에 들어간다.
> ⚠️ V3는 콘텐츠 전용(C++ 리빌드 없음)이라 브랜치 체크아웃이 가벼움. 나중에 병렬 세션이 생기면 그때 워크트리로 분리.

---

## STEP 1 — `WBP_BasicCrosshair` 위젯 신설

`Content/UI/HUD/` 우클릭 → **User Interface → Widget Blueprint** → 부모 `UserWidget` → 이름 `WBP_BasicCrosshair`.
> 별도 위젯으로 두는 이유: U12(동적 스프레드)가 이 위젯만 확장하면 되고 `WBP_GameHUD` 레이아웃을 안 건드림(캡슐화).

### 위젯 트리 (디자이너)
```
[Root]
└─ Overlay  "CrosshairRoot"   ← Is Variable ✔ (ADS 바인딩 대상)
   ├─ Image  "Bar_Top"
   ├─ Image  "Bar_Bottom"
   ├─ Image  "Bar_Left"
   ├─ Image  "Bar_Right"
   └─ Image  "Dot_Center"
```

- 루트에 **Overlay** 하나, 이름 `CrosshairRoot`, **Is Variable** 체크.
- Overlay 안에 Image 5개. 각 Image 슬롯(Overlay Slot) **Horizontal/Vertical Alignment = Center**.

### 각 Image 속성 (플레이스홀더 — 흰 십자 + 점)
브러시는 텍스처 없이 단색: 각 Image → **Appearance → Brush → Draw As = `Image`**, **Tint = 흰색(1,1,1,1)**.

| 위젯 | 크기(px) | 위치(Overlay Slot Padding) |
|---|---|---|
| `Bar_Top` | 3 × 12 | 위로 (Padding Bottom 16) |
| `Bar_Bottom` | 3 × 12 | 아래로 (Padding Top 16) |
| `Bar_Left` | 12 × 3 | 왼쪽 (Padding Right 16) |
| `Bar_Right` | 12 × 3 | 오른쪽 (Padding Left 16) |
| `Dot_Center` | 3 × 3 | 중앙(패딩 0) |

> 가독성 팁(선택): 어두운 외곽선이 필요하면 각 바 뒤에 1px 큰 검정 Image를 깔거나 폴리시(후속)로 미룬다. **지금은 플레이스홀더면 충분.**
> 갭(16)·두께(3)는 PIE에서 보며 미세조정 — **처음엔 살짝 크게 잡고 줄이는 게 편함**(작으면 안 보임).

### ⚠️ 크기 조절축 = `CrosshairRoot` RenderTransform Scale (설정 유닛 연동 대비)
- 전체 크기는 **`CrosshairRoot`(Overlay)의 RenderTransform → Scale**(X=Y 균일)로 조절한다. 바 픽셀값은 "Scale 1.0에서 보기 좋은 기본형"으로만 잡고, 확대/축소는 **Scale 스칼라 1개**로.
- 인게임 크기 설정 유닛(별도 — `Docs/TaskPrompts_Master.md` §B 신규 유닛)이 **바로 이 `CrosshairRoot`의 Scale을 덮어쓴다**. 그래서 `CrosshairRoot`를 단일 Is-Variable 루트로 유지하는 게 중요(설정값이 한 곳만 건드림).
- "너무 작다" 즉시 해결: 위 픽셀값을 키우거나 `CrosshairRoot` RenderTransform Scale 기본값을 `1.5`~`2.0`으로 올려 V3 마감.

### (권장) 바 속성을 변수 3개로 묶기 — `Event PreConstruct`
4개 바에 길이/두께/갭을 일일이 입력하지 말고 **변수 3개 → PreConstruct에서 일괄 적용**. 변수 하나만 고치면 5개 전부 반영(디자이너 프리뷰도 실시간). **U12 동적 스프레드가 이 `Gap` 변수를 애니메이트** → 프로덕션 구조로도 정답.

1. 변수 추가(Details → Variable, **Instance Editable ✔**, Category `Crosshair`):
   - `Thickness` (float) = 3
   - `Length` (float) = 12
   - `Gap` (float) = 16
2. **`Event PreConstruct`** 그래프에서 각 바에 적용:
   - 크기: 각 Image → **`Set Desired Size Override`**
     - `Bar_Top` / `Bar_Bottom` → `(Thickness, Length)`
     - `Bar_Left` / `Bar_Right` → `(Length, Thickness)`  ← 가로바라 전치
     - `Dot_Center` → `(Thickness, Thickness)`
   - 갭(중앙에서 밀어내기): 각 바의 `Slot` → **Cast To `Overlay Slot`** → **`Set Padding`**(Make Margin, 해당 변만 `Gap`)
     - `Bar_Top` → Bottom = `Gap` / `Bar_Bottom` → Top = `Gap` / `Bar_Left` → Right = `Gap` / `Bar_Right` → Left = `Gap`
     - (Overlay 슬롯 정렬이 Center여야 패딩이 중앙 기준으로 밀어냄)
3. 컴파일 → 디자이너에서 `Thickness`/`Length`/`Gap` 기본값을 바꾸면 **5개 바가 한 번에** 갱신.

> 2층 구조 요약: **형태=PreConstruct 변수 3개**(Thickness/Length/Gap), **전체 배율=CrosshairRoot Scale 1개**(U17 설정/U12 확장이 덮어씀). 둘 다 "한 값"으로 전부 적용.
> 그래프가 부담되면 대안: 십자 1장을 **단일 텍스처 Image**로 두면 크기=Image Size 한 값(단 갭 독립 조절·U12 스프레드는 어려움 → PreConstruct 권장).

---

## STEP 2 — ADS 시 숨김 (핵심 배선)

`WBP_BasicCrosshair` 그래프에서 `CrosshairRoot`의 **Visibility를 바인딩**한다.

1. 디자이너 `CrosshairRoot` 선택 → Details **Behavior → Visibility** 옆 **Bind → Create Binding**.
2. 생성된 함수(`Get_CrosshairRoot_Visibility`) 그래프:

```
Get Owning Player Pawn
      └─► Get Component By Class  (Component Class = FPSRWeaponFireComponent)
                  └─► [Is Valid?]  ──(Valid)──► IsAiming()  ──► Branch
                          │                                        │ True  → Return: Hidden (또는 Collapsed)
                          │                                        │ False → Return: Visible
                          └──(Not Valid)──────────────────────────► Return: Visible
```

- `Get Component By Class`의 **Component Class** = `FPSRWeaponFireComponent`.
- `IsAiming`은 `BlueprintPure`라 바로 사용(추가 C++ 없음).
- 반환 타입 **ESlateVisibility**: 조준 중 `Hidden`, 평상시 `Visible`.

> Visibility 바인딩은 매 프레임 평가되나 위젯 1개 룩업이라 비용 무시 가능. (원하면 `Event Construct`에서 FireComponent를 변수로 캐싱 — 선택.)

**컴파일 + 저장.**

---

## STEP 3 — `WBP_GameHUD`에 배치

`WBP_GameHUD` 열기:

1. `WBP_HitMarker`가 들어있는 같은 **Canvas Panel**에 `WBP_BasicCrosshair`를 자식으로 추가.
2. 슬롯 **Anchors = 중앙(0.5,0.5)**, **Alignment = (0.5,0.5)**, **Position X/Y = 0** → 화면 정중앙.
3. **Z-Order**: `WBP_BasicCrosshair`를 `WBP_HitMarker`보다 **아래**(트리에서 히트마커 위에 배치 → 나중 그려지는 히트마커가 위로).
4. 컴파일 + 저장.

---

## STEP 3-B — 히트마커 비주얼 교체 (거대 네모 → 대각선 X 4선) + 색상 구분

> ⚠️ **로직은 손대지 말 것 — 이미 완성됨.** 사용자 그래프 확인 결과 바인딩은 이미 정상:
> `Event Construct → Set Timer by Event(0.2, Looping) → SET Bind Retry Handle`(리트라이 패턴) / `TryBind → Get Owning Player Pawn → Get Component by Class(FPSRPlayerFeedbackComponent) → Is Valid → Bind Event to On Hit Marker → Clear & Invalidate Timer` / `OnHitMarker_Event(Marker Type) → Setting Hit Marker`. **이 부분 그대로 둔다.**
> 문제는 **비주얼이 거대한 네모 한 장**이라는 것. 이 단계는 ① 그 네모를 **대각선 X 4선 플레이스홀더**로 교체하고 ② `Setting Hit Marker` 이벤트에 **MarkerType별 틴트**를 넣는다. 순수 콘텐츠, **C++ 변경 없음**.

### 이미 배선된 것 (확인됨)
- enum `EFPSRHitMarkerType { Hit, Crit, Kill }` / 전 무기 경로 서버권위 → `FPSRPlayerController.cpp:391` `NotifyHitConfirmed(MarkerType)` → `OnHitMarker(MarkerType)` 브로드캐스트 → 위 그래프가 수신 중. **Kill 타입까지 이미 도달**.

### ① 비주얼 교체 — 디자이너 탭 (`WBP_HitMarker` Designer)
기존 **거대한 네모 Image를 삭제**하고, **Canvas Panel** 아래에 대각선 바 4개로 X를 만든다(중앙 갭).

```
[Root]
└─ Canvas Panel
   ├─ Image  "Seg_TL"   ← Is Variable ✔
   ├─ Image  "Seg_TR"   ← Is Variable ✔
   ├─ Image  "Seg_BL"   ← Is Variable ✔
   └─ Image  "Seg_BR"   ← Is Variable ✔
```

4개 모두 공통: **Brush 단색 흰색(1,1,1,1)**, Canvas Slot **Anchors = 중앙(0.5,0.5)**, **Alignment(0.5,0.5)**, **Size = 16 × 3**.
세그먼트별 **Position**(중앙 기준 오프셋)과 **RenderTransform → Angle**(자기중심 회전, Pivot 0.5):

| 위젯 | Position (X,Y) | Angle | 모양 |
|---|---|---|---|
| `Seg_TL` | (-8, -8) | **45** | `\` (좌상 팔) |
| `Seg_BR` | ( 8,  8) | **45** | `\` (우하 팔) |
| `Seg_TR` | ( 8, -8) | **-45** | `/` (우상 팔) |
| `Seg_BL` | (-8,  8) | **-45** | `/` (좌하 팔) |

→ 중앙에 점 없는 X(┌가 아니라 ✕ 모양 4선). 갭/길이/두께(8/16/3)는 PIE 보며 미세조정. **히트마커는 정적 크로스헤어보다 약간 크게**(레퍼런스 사진 비율).

> 가독성(선택): 각 바 뒤에 1px 큰 검정 바를 깔면 밝은 배경에서도 보임 — 폴리시(후속)로 미뤄도 됨.

### ①-B (권장) 세그먼트 속성 공통화 — `Event PreConstruct`
크로스헤어와 동일 패턴. 4개 세그먼트의 차이는 **Position(±Offset)·Angle(±45)** 뿐, Size는 공통이라 변수 3개로 묶는다. 변수 하나만 고치면 4선 전부 반영(디자이너 실시간).

1. 변수 추가(Details → Variable, **Instance Editable ✔**, Category `HitMarker`):
   - `MarkerLength` (float) = 16
   - `MarkerThickness` (float) = 3
   - `MarkerOffset` (float) = 8   ← 중앙에서 각 세그먼트 중심까지 거리(=갭 조절)
   - (선택) `MarkerAngle` (float) = 45   ← X 기울기 튜닝용
2. **`Event PreConstruct`** 에서 각 세그먼트에 적용:
   - 크기: 4개 모두 → **`Set Desired Size Override`**( `(MarkerLength, MarkerThickness)` ) — 회전 전 가로바 기준이라 전부 동일.
   - 위치: 각 `Slot` → **Cast To `Canvas Panel Slot`** → **`Set Position`**( 아래 부호 × `MarkerOffset` )
   - 각도: 각 세그먼트 → **`Set Render Transform Angle`**( 아래 부호 × `MarkerAngle` )

   | 세그먼트 | Position | Angle |
   |---|---|---|
   | `Seg_TL` | (−Offset, −Offset) | **+**Angle |
   | `Seg_BR` | (+Offset, +Offset) | **+**Angle |
   | `Seg_TR` | (+Offset, −Offset) | **−**Angle |
   | `Seg_BL` | (−Offset, +Offset) | **−**Angle |

   - Canvas Slot은 **Anchors 중앙(0.5,0.5)·Alignment(0.5,0.5)** 여야 Position이 중앙 기준 오프셋이 됨.
   - 음수는 `MarkerOffset`/`MarkerAngle`에 `-1` 곱(또는 리터럴 `-`).
3. 컴파일 → 디자이너에서 `MarkerLength`/`MarkerThickness`/`MarkerOffset`만 바꾸면 **4선 한 번에** 갱신.

> PreConstruct(정적 레이아웃)는 ②펄스(RenderTransform Scale/Opacity)·③틴트(ColorAndOpacity)와 **트랙이 안 겹쳐** 충돌 없음.
> 전체 배율을 한 값으로 더 주고 싶으면: Canvas를 루트 컨테이너로 감싸 그 **RenderTransform Scale** 사용(크로스헤어 `CrosshairRoot` Scale과 동일 개념 — 선택).

### ② 펄스 애니메이션 (기존 것 재사용/확인)
짧은 펄스(예: 0.12s)로 **Render Transform Scale 1.3→1.0 + Render Opacity 1→0**. 색(ColorAndOpacity) 트랙은 넣지 말 것(③ 틴트와 충돌 방지). 기존 펄스 애님이 색을 건드리면 **Render Opacity 기반으로 교체**.

### ③ 색상 구분 — `Setting Hit Marker` 이벤트 그래프
이미 있는 `Setting Hit Marker(MarkerType)` 커스텀 이벤트 본문에:
1. `Switch on EFPSRHitMarkerType`(MarkerType) →
   - **Hit / Crit** → 흰색 `(1,1,1,1)`
   - **Kill** → 빨강 `(1,0,0,1)`
   (Crit은 지금 흰색 권장 — 노랑은 폴리시 선택.)
2. 그 색을 **`Set Color and Opacity` (Target = `self`)** 에 연결 → 위젯 전체(4선) 일괄 틴트.
   - ⚠️ 펄스 애님이 self의 Color를 애니메이트하면 self 대신 **Seg_TL~BR 각각에 `Set Color and Opacity`** 4회(또는 4선을 Overlay로 묶고 그 위 처리)로 우회.
3. 틴트 적용 → **`Play Animation`(펄스)** 호출.

### ④ 컴파일 + 저장.

> 설계 메모(§2-14): "히트마커 최종 연출은 크로스헤어/발사체 작업 후 재확인" = 바로 이 시점. **비주얼 형태(X) + 틴트만** 확정(로직·바인딩은 기존 유지). 약점(헤드샷) 전용 마커는 U3a/U12 범위 — 여기선 Hit/Kill 색만.

---

## STEP 4 — 검증 (PIE)

- [ ] 크로스헤어가 화면 **정중앙** 표시.
- [ ] 발사해도 위치 정합(정적이라 중앙 고정이 정상).
- [ ] **우클릭 ADS 중 숨김**, 떼면 다시 표시.
- [ ] 히트마커가 크로스헤어 **위에** 겹쳐 보임.
- [ ] **적 명중 시 흰색 대각선** 마커 펄스.
- [ ] **적 처치 시 빨간색** 마커 펄스(Kill 색 분기 동작).
- [ ] **메뉴(Menu 레이어)에선 미표시** — Game 레이어 전용이라 자동 충족(확인만).

---

## STEP 5 — 커밋 / 머지 (§B-3)

PIE 통과 후:
```powershell
git add Content/UI/HUD/WBP_BasicCrosshair.uasset Content/UI/HUD/WBP_GameHUD.uasset Content/UI/HUD/WBP_HitMarker.uasset
git commit -m "content(V3): 기본 정적 크로스헤어 + ADS 숨김 + 히트마커 색상 구분(명중 흰/처치 빨강)"
git push -u origin phase/p4d-crosshair
```
- 코드 변경 없음 → Codex 머지게이트는 콘텐츠 바이너리라 diff 무관(스킵 가능).
- 사용자 검토(PIE) 승인 → `--no-ff` main 머지 → PROGRESS 갱신 + TaskPrompts §B에 V3 ✅.

---

## 하지 말 것 (범위 경계)
- ❌ 동적 스프레드(원 확대/축소) — **U12** 범위. `GetCurrentBloom()`/`SpreadDegrees` 읽지 말 것.
- ❌ 무기별 다른 크로스헤어(샷건 원 등) — U12/폴리시.
- ❌ 에셋 경로 C++ 하드코딩 — 해당 없음(순수 WBP).
