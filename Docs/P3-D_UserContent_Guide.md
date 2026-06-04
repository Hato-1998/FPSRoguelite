# P3-D 사용자 콘텐츠 작업 가이드 — 카드 UI / 공유 XP바 / 오프닝 시드 PIE 테스트

> P3-D는 **C++ 베이스/로직만 완료**(`phase/p3d-cardui` 브랜치, 빌드+스모크+Codex 통과)된 상태다.
> 실제로 화면에 XP바·카드가 뜨려면 **에디터에서 위젯 블루프린트(WBP) 4종을 만들고 BP_FPSRPlayerController에 연결**해야 한다.
> 이 문서대로 6단계만 따라 하면 PIE에서 공유 XP바·카드 선택·리롤·오프닝 시드를 검증할 수 있다.
>
> 코드 원칙(Game.MD §6-2): **에셋 경로 C++ 하드코딩 금지** → 위젯 클래스는 전부 BP_FPSRPlayerController가 참조(5단계).
> 카드 콘텐츠(GE/카드/풀)는 P3-C에서 만든 `Content/Cards/Character/`를 그대로 재사용한다(추가 작업 없음).

---

## 0. 사전 준비 / 개념

- **C++가 제공하는 것**(이미 완료): CommonUI 모듈, `UFPSRGameViewportClient`(DefaultEngine.ini에 등록됨 → 작업 불필요),
  레이어 스택 레이아웃 베이스(`UFPSRPrimaryGameLayout`), XP바/카드선택/카드엔트리 위젯 베이스, 서버 권위 RPC 배선, 디버그 커맨드.
- **사용자가 만드는 것**: 위 C++ 베이스를 **부모로 상속한 WBP 4종**(레이아웃·스타일·BindWidget 연결). 비주얼은 **플레이스홀더**(레이아웃·바인딩만, 스타일링 최소 — Game.MD §3-D).
- **CommonUI 구조**: 화면 루트 = `WBP_PrimaryGameLayout`(4개 레이어 스택). 상시 HUD(XP바)는 **Game 레이어**, 카드 선택 모달은 **Modal 레이어**에 푸시된다. 입력 모드(마우스 표시/게임입력 차단)는 카드 위젯의 C++(`GetDesiredInputConfig`)가 처리하므로 별도 설정 불필요.
- **BindWidget 규칙**(중요): WBP 안의 위젯 **이름이 C++ 멤버명과 정확히 일치**해야 바인딩된다.
  - `BindWidget`(필수) = 그 이름의 위젯이 **반드시 있어야** WBP 컴파일 통과.
  - `BindWidgetOptional` = 있으면 연결, 없어도 무방(텍스트/프로그레스바 등 표시 요소).

권장 폴더:
```
Content/UI/
├── WBP_PrimaryGameLayout
├── WBP_XPBar
├── WBP_CardSelect
└── WBP_CardEntry
```

> 만드는 순서는 **의존 역순**: CardEntry → CardSelect → XPBar → PrimaryGameLayout → PC 연결.
> (CardSelect가 CardEntry를, PC가 나머지를 참조하므로 자식부터 만들면 드롭다운에서 바로 고를 수 있다.)

---

## 1단계 — `WBP_CardEntry` (카드 1장) — 부모 `FPSRCardEntryWidget`

1. `Content/UI/` → 우클릭 → **User Interface → Widget Blueprint** → **부모 클래스 선택 창**에서 `FPSRCardEntryWidget` 검색·선택 → 이름 `WBP_CardEntry`
2. 디자이너에서 레이아웃 구성(예: 세로 박스에 텍스트들 + 버튼). 아래 이름으로 위젯 배치:

| 위젯 팔레트 | 변수명(정확히) | 바인딩 | 용도 |
|---|---|---|---|
| **Button** | `SelectButton` | **필수(BindWidget)** | 이 카드 선택. 클릭 시 C++가 서버에 인덱스 전송 |
| Text | `CardNameText` | 선택 | 카드 이름 |
| Text | `RarityText` | 선택 | 등급(Common/Rare/Epic/Legendary) |
| Text | `DescriptionText` | 선택 | 설명 |
| Text | `MagnitudeText` | 선택 | 수치(+15 등) |

> `SelectButton`만 필수. 텍스트는 선택이지만 검증하려면 최소 `CardNameText`/`RarityText`는 넣는 걸 권장.
> 버튼 안에 텍스트를 넣는 구조여도 되고, 버튼 옆에 별도 텍스트를 두어도 된다(이름만 맞으면 됨).
> **클릭 핸들러를 BP에서 만들 필요 없음** — C++가 `SelectButton->OnClicked`를 자동 바인딩한다.

---

## 2단계 — `WBP_CardSelect` (3카드 모달) — 부모 `FPSRCardSelectWidget`

1. `Content/UI/` → Widget Blueprint → 부모 `FPSRCardSelectWidget` → 이름 `WBP_CardSelect`
2. 루트에 가로 박스(Horizontal Box) 등을 두고 **`WBP_CardEntry`를 3개 배치**한 뒤, 각각 이름을 아래로 지정:

| 위젯 | 변수명(정확히) | 바인딩 | 용도 |
|---|---|---|---|
| **WBP_CardEntry** | `CardEntry_0` | **필수(BindWidget)** | 첫 번째 카드 |
| **WBP_CardEntry** | `CardEntry_1` | **필수(BindWidget)** | 두 번째 카드 |
| **WBP_CardEntry** | `CardEntry_2` | **필수(BindWidget)** | 세 번째 카드 |
| Button | `RerollButton` | 선택 | 리롤(차지 소비) |
| Text | `RerollChargesText` | 선택 | 남은 리롤 횟수 |

> 팔레트에서 `WBP_CardEntry`를 끌어다 놓고 이름을 `CardEntry_0/1/2`로 바꾼다(타입이 `UFPSRCardEntryWidget`라 BindWidget이 인식).
> 카드가 2장만 발급되면 3번째(`CardEntry_2`)는 C++가 자동으로 숨긴다(Collapsed).
> 모달 배경(어둡게)·정렬은 자유. **입력/포커스/리롤 클릭 핸들러는 C++가 처리** — BP 작업 불필요.
>
> **📍 위치(정중앙)**: 위젯은 Modal 레이어 스택에 푸시되며 **스택이 자식을 화면 전체로 늘린다**. 즉 위치는 **이 WBP 안에서** 정한다.
> 가장 수정하기 쉬운 방법 → **루트를 `Canvas Panel`로** 두고, 카드 묶음(Horizontal Box 등)을 선택해 **Anchor 프리셋 = Center(중앙)** + Alignment `(0.5, 0.5)`, Position `(0,0)`. 디자이너에서 드래그로 미세조정 가능.
> (Overlay 루트도 가능: 카드 박스 슬롯의 HAlign=Center / VAlign=Center.)

---

## 3단계 — `WBP_XPBar` (공유 XP바 HUD) — 부모 `FPSRXPBarWidget`

1. `Content/UI/` → Widget Blueprint → 부모 `FPSRXPBarWidget` → 이름 `WBP_XPBar`
2. 보통 화면 하단에 배치할 요소들. 아래 이름으로:

| 위젯 | 변수명(정확히) | 바인딩 | 용도 |
|---|---|---|---|
| **Progress Bar** | `XPBar` | 선택 | 현재/필요 XP 비율(0~1) |
| Text | `LevelText` | 선택 | 파티 레벨 |
| Text | `StackText` | 선택 | 본인 보류 픽 수(레벨업 스택) |

> 전부 선택이지만 검증하려면 3개 다 넣는 걸 권장. **값 갱신은 C++가 이벤트(OnRep 델리게이트)로 처리** — BP 바인딩 함수 불필요.
> 표시 형식(예: "Lv 3", "XP 40/150", "Stack 2")은 텍스트 블록 자체 스타일로. C++는 숫자만 SetText 한다.
>
> **📍 위치(화면 맨 아래)**: XP바는 Game 레이어 스택에 푸시되며 **스택이 자식을 화면 전체로 늘린다** → 위치는 이 WBP 안에서 정한다.
> 가장 쉬운 방법 → **루트를 `Canvas Panel`로** 두고, XP바 묶음을 선택해 **Anchor 프리셋 = Bottom(아래 가로 stretch)** 으로 하고 Offset(아래 여백)만 조정. 또는 **루트를 `Vertical Box`로** 두고 **Vertical Alignment = Bottom**. 디자이너에서 바로 보며 조정 가능.

---

## 4단계 — `WBP_PrimaryGameLayout` (레이어 루트) — 부모 `FPSRPrimaryGameLayout`

> `FPSRPrimaryGameLayout`은 **Abstract**라 C++ 직접 사용 불가 → 반드시 이 WBP로 상속해 쓴다.

1. `Content/UI/` → Widget Blueprint → 부모 `FPSRPrimaryGameLayout` → 이름 `WBP_PrimaryGameLayout`
2. 루트를 **Overlay**(또는 Canvas)로 두고, 그 안에 **`Common Activatable Widget Stack` 4개**를 겹쳐 배치. 각각 이름을 정확히:

| 위젯(팔레트: CommonUI) | 변수명(정확히) | 바인딩 | 레이어 |
|---|---|---|---|
| **Common Activatable Widget Stack** | `Layer_Game` | **필수** | 상시 HUD(XP바) — 맨 아래 |
| **Common Activatable Widget Stack** | `Layer_GameMenu` | **필수** | (예약) |
| **Common Activatable Widget Stack** | `Layer_Menu` | **필수** | (예약) |
| **Common Activatable Widget Stack** | `Layer_Modal` | **필수** | 카드 선택 — 맨 위 |

> 4개 모두 필수(BindWidget). Overlay에서 **Modal을 가장 마지막(위)**, Game을 가장 처음(아래)에 두면 카드 모달이 HUD 위에 뜬다.
> 각 스택은 화면 전체(Anchors 채움)로. **스택에 위젯을 미리 넣지 말 것** — C++가 런타임에 푸시한다.
> `UI.Layer.Game/GameMenu/Menu/Modal` 게임플레이태그는 이미 존재하므로 추가 작업 없음.

---

## 5단계 — `BP_FPSRPlayerController`에 위젯 클래스 연결

위젯을 띄우는 유일한 데이터 연결점(코드 하드코딩 없음).

1. `Content/Core/BP_FPSRPlayerController` 더블클릭(부모 = `FPSRPlayerController`)
2. 디테일 패널 → 카테고리 **`FPSR | UI`** → 3개 프로퍼티 할당:

| 프로퍼티 | 할당 |
|---|---|
| **Primary Layout Class** | `WBP_PrimaryGameLayout` |
| **XP Bar Widget Class** | `WBP_XPBar` |
| **Card Select Widget Class** | `WBP_CardSelect` |

3. 컴파일 · 저장

> PC가 BeginPlay에서 `WBP_PrimaryGameLayout`을 뷰포트에 추가하고 `WBP_XPBar`를 Game 레이어에 푸시한다.
> 미할당 시 Output Log(`LogFPSR`)에 `PrimaryLayoutClass not assigned` / `CardSelectWidgetClass not assigned` 경고가 뜬다 → 이 단계 누락 신호.

---

## 6단계 — PIE 테스트 (검증 시나리오)

> 디버그 커맨드는 **호스트/스탠드얼론(권한 보유)** 에서 실행. (단일 PIE는 자동으로 권한 보유.)
> 카드 콘텐츠가 없으면 추첨이 0장이니 **P3-C 가이드의 카드/풀이 `BP_FPSRGameMode.Card Pool`에 연결되어 있어야 한다.**

### A. 공유 XP바
| # | 콘솔 입력 | 기대 결과 |
|---|---|---|
| 1 | (PIE 시작) | 하단에 XP바 표시(Lv 1, XP 0/100, Stack 0) |
| 2 | `FPSR.AddXP 120` | 레벨업 → `LevelText` 갱신, `StackText`(보류 픽) 증가, `XPBar` 진행도 변화 |

### B. 정비시간 카드 선택(자동 발급)
| # | 콘솔 입력 | 기대 결과 |
|---|---|---|
| 3 | `FPSR.AddXP 300` | 픽 여러 개 누적(Stack ↑) |
| 4 | `FPSR.SetPhase breather` | **카드 모달 자동 표시**(`WBP_CardSelect`, 3카드) + 마우스 커서 표시 |
| 5 | 카드 클릭 | 카드 적용 → 본인 픽 1 소진 → 픽 남으면 **다음 카드 자동 발급**, 0이면 모달 닫힘 |
| 6 | `RerollButton` 클릭 | 차지 1 차감 + 3장 재추첨(`RerollChargesText` 감소) |
| — | `FPSR.SetPhase combat` | 정비시간 종료(다시 전투) |

### C. 오프닝 시드(2장)
| # | 콘솔 입력 | 기대 결과 |
|---|---|---|
| 7 | `FPSR.OpeningSeed` | 카드 모달 표시 → **2장 연속 선택**(레벨업 픽 소비 안 함) 후 닫힘 |
| 8 | `FPSR.OpeningSeed 3` | 3장 연속(개수 인자) |

### D. 엔진 에러 해소 확인
- Output Log에서 기존 **`LogUIActionRouter` 에러가 사라졌는지** 확인(DefaultEngine.ini ViewportClient 효과).

### E. (선택) 협동 — 2-client PIE
- Play 설정에서 Number of Players = 2 → 각 플레이어가 **본인 화면에서 독립적으로** 픽을 소비하는지(한 명이 다른 명 픽을 못 가져감) 확인.

---

## 트러블슈팅

| 증상 | 원인 / 해결 |
|---|---|
| WBP 컴파일 에러 `… is marked BindWidget … not found` | 필수 위젯 이름 불일치 — `SelectButton` / `CardEntry_0~2` / `Layer_Game/GameMenu/Menu/Modal` 철자·대소문자 확인 |
| XP바 안 보임 | 5단계 `XP Bar Widget Class` 미할당, 또는 `WBP_PrimaryGameLayout`의 `Layer_Game` 스택 누락 |
| `FPSR.SetPhase breather` 했는데 카드 안 뜸 | 보류 픽 0(먼저 `FPSR.AddXP`로 레벨업) / `Card Select Widget Class` 미할당 / `BP_FPSRGameMode.Card Pool` 미연결(P3-C) |
| 카드 클릭해도 반응 없음 | `WBP_CardEntry`의 `SelectButton`(필수) 누락 또는 이름 오타 |
| 마우스 커서가 안 나옴 | 카드 위젯이 Modal 레이어에 푸시되는지(=`Layer_Modal` 존재) 확인. 입력모드는 C++가 처리하므로 BP에서 SetInputMode 하지 말 것 |
| 카드 0장 / 특정 등급만 | P3-C 풀/카드 문제 → `Docs/P3-C_UserContent_Guide.md` 트러블슈팅 참조 |
| `LogFPSR: … not assigned` 경고 | 5단계 PC 프로퍼티 3개 할당 누락 |
| `Cannot present card offer … abandoning offer` | `PrimaryLayoutClass` 또는 `CardSelectWidgetClass` 미할당(설정 오류) — 5단계 확인 |

---

## 작업 체크리스트

- [ ] 1단계: `WBP_CardEntry`(부모 `FPSRCardEntryWidget`) — `SelectButton`(필수) + 텍스트들
- [ ] 2단계: `WBP_CardSelect`(부모 `FPSRCardSelectWidget`) — `CardEntry_0/1/2`(필수) + Reroll(선택)
- [ ] 3단계: `WBP_XPBar`(부모 `FPSRXPBarWidget`) — `XPBar`/`LevelText`/`StackText`
- [ ] 4단계: `WBP_PrimaryGameLayout`(부모 `FPSRPrimaryGameLayout`) — 4개 스택 정확한 이름
- [ ] 5단계: `BP_FPSRPlayerController` → `FPSR|UI`의 3개 클래스 할당 + 컴파일
- [ ] 6단계: PIE 시나리오 A~D(+선택 E) + `LogUIActionRouter` 소멸 확인
- [ ] 결과 보고 → 이상 없으면 P3-D → `--no-ff` main 머지

> 콘텐츠는 P3-D 머지 시점에 함께 커밋할지 사용자에게 확인한다(메모리 규칙: Phase 종료 시 사용자 콘텐츠 동반 커밋 여부 확인).
> 코드 베이스는 이미 `phase/p3d-cardui`에 커밋·푸시됨(커밋 7573964).
