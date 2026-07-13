# P6-A 사용자 콘텐츠 가이드 — 게임 플로우 셸 (메뉴→런→결과→메뉴)

> **✅ 구현 완료 (실제 반영본 — 2026-06-12, VibeUE MCP, 브랜치 `phase/p6a-content`).** 아래 콘텐츠는 **이미 작성·연결·검증(MCP)되어 있다.** 이 문서는 (1) 무엇이 만들어졌는지 기록 + (2) 사람이 에디터에서 재현/수정할 때의 참조용이다. C++ 베이스는 `c673d03`에서 main에 머지됨. **작업 트리 = `E:/Git_Project/FPSRoguelite`(main, 워트리는 삭제됨).** 에셋 경로 C++ 하드코딩 없음 — 전부 세팅/BP로 연결.
>
> ### ⚠️ 원안 대비 정정 2건 (실제 구현에서 발견)
> 1. **CommonButtonStyle 재사용 불가 → 신규 작성**: 원안은 "카드 UI의 CommonButtonStyle 재사용"을 전제했으나, **기존 카드 버튼은 일반 `UButton`(FButtonStyle)** 이라 재사용할 `UCommonButtonStyle` 에셋이 **없었다.** → CommonButton 인프라를 처음부터 작성: `BnW_MenuButtonStyle`(부모 `CommonButtonStyle`) + `WBP_PlayButton`/`WBP_QuitButton`/`WBP_ReturnButton`(부모 `CommonButtonBase`, 라벨 베이크). (§3 참조)
> 2. **메뉴 GM "설정할 것 없음" → PlayerControllerClass 지정 필수**: 메뉴 GM C++ 생성자는 PlayerControllerClass를 **C++ `AFPSRMenuPlayerController`(위젯 클래스 미설정)** 로 박는다. 그대로 두면 PIE에서 `[Menu] PrimaryLayoutClass is not assigned` 에러 + 검은 화면. → **`BP_FPSRMenuGameMode.PlayerControllerClass = BP_FPSRMenuPC` 로 오버라이드해야** 위젯 클래스가 지정된 PC가 스폰된다(런 GM `BP_FPSRGameMode→BP_FPSRPC`와 동일 패턴). (§1.1 참조)
>
> ### 실제 만들어진 것 (`Content/UI/Menu/` 신규 폴더 외)
> - `Content/UI/Menu/BnW_MenuButtonStyle` (CommonButtonStyle: normal/hover/pressed/disabled 회색 RoundedBox)
> - `Content/UI/Menu/WBP_PlayButton`·`WBP_QuitButton`·`WBP_ReturnButton` (CommonButtonBase + 위 스타일 + 라벨 베이크)
> - `Content/UI/Menu/WBP_MainMenu` (부모 `FPSRMainMenuWidget`, PlayButton/QuitButton 배치)
> - `Content/UI/Menu/WBP_Result` (부모 `FPSRResultWidget`, OutcomeText + ReturnButton + OnOutcomeSet 그래프)
> - `Content/Game/Core/BP_FPSRMenuGameMode` (+ PlayerControllerClass=BP_FPSRMenuPC), `Content/Character/Player/BP_FPSRMenuPC`
> - `Content/Character/Player/BP_FPSRPC` (ResultWidgetClass=WBP_Result), `Content/Maps/L_MainMenu`
> - `Config/DefaultGame.ini`[FPSRGameFlowSettings], `Config/DefaultEngine.ini`(GameDefaultMap+EditorStartupMap=L_MainMenu)
>
> ### 남은 일 (사람)
> - **최종 육안 PIE**(루프 클로즈 §8 1~6단계) — 헤드리스 불가.
> - **머지**: 육안 통과 후 `git checkout main; git merge --no-ff phase/p6a-content`.
> - **`.uproject`**: VibeUE 플러그인 활성 항목은 클린 체크아웃을 깨므로(`Plugins/VibeUE/` gitignore됨) P6-A 커밋에서 **제외함**. 영구 해결 = .uproject VibeUE 항목에 `"Optional": true`.
> - (선택) 메뉴 레이아웃 폴리시(버튼 크기/위치/타이틀/색)는 디자이너에서.

## 0. 전제 / 폴더
- main 트리(`E:/Git_Project/FPSRoguelite`) 에디터로 작업(워트리는 머지 후 삭제됨). 기존 에셋: `WBP_PrimaryGameLayout`(`Content/UI/Widget/`, 4레이어 스택 보유 — 재사용), 런 PC `BP_FPSRPC`(`Content/Character/Player/`), 런 GameMode `BP_FPSRGameMode`(`Content/Game/Core/`).
- 권장 신규 폴더: `Content/UI/Menu/`(WBP_MainMenu·WBP_Result), `Content/Game/Core/`(메뉴 GM/PC BP), `Content/Maps/`(L_MainMenu).
- 만들 것 요약: **맵 1 + BP 2(+런PC 1필드) + WBP 2 + 세팅 2(프로젝트세팅·ini)**.

---

## 1. 메뉴 GameMode/PC 블루프린트

### 1.1 `BP_FPSRMenuGameMode`
- Content Browser > Add > Blueprint Class → 부모 검색 **`FPSRMenuGameMode`** → 이름 `BP_FPSRMenuGameMode` (`Content/Game/Core/`).
- **⚠️ Class Defaults > `Player Controller Class` = `BP_FPSRMenuPC`** 로 지정(**필수**). C++ 생성자가 PlayerControllerClass를 C++ `AFPSRMenuPlayerController`(위젯 클래스 미설정)로 박기 때문에, BP에서 위젯 클래스를 지정한 `BP_FPSRMenuPC`로 오버라이드하지 않으면 PIE에서 `[Menu] PrimaryLayoutClass is not assigned` + 검은 화면이 된다(런 GM `BP_FPSRGameMode→BP_FPSRPC`와 동일 패턴).
- Pawn은 C++가 `ASpectatorPawn`으로 지정함(그대로 둠). 저장.

### 1.2 `BP_FPSRMenuPC`
- 부모 **`FPSRMenuPlayerController`** → 이름 `BP_FPSRMenuPC` (`Content/Character/Player/` 또는 `Content/Game/Core/`).
- **Class Defaults** 디테일(카테고리 `FPSR|UI`):
  - **`Primary Layout Class` = `WBP_PrimaryGameLayout`** (기존 것 재사용)
  - **`Main Menu Widget Class` = `WBP_MainMenu`** (§3에서 생성 — 먼저 §3을 만들고 와서 지정해도 됨)
- 저장. ※ 메뉴 PC는 Enhanced Input IMC 불필요(마우스 입력은 CommonUI 입력모드로 처리).

---

## 2. 메인메뉴 맵 — `L_MainMenu`
1. File > New Level → **Empty Level**(또는 Basic) → `Content/Maps/L_MainMenu`로 저장.
2. **World Settings**(Window > World Settings) → **GameMode Override = `BP_FPSRMenuGameMode`**.
3. 액터 배치(최소):
   - **PlayerStart** 1개(SpectatorPawn이 여기 스폰 — 없으면 원점 스폰, 있으면 깔끔).
   - (선택) Directional Light + Sky Atmosphere/배경, 또는 카메라 한 대 — 메뉴 배경용. 없어도 동작(검은 배경 + 메뉴 위젯).
4. 저장.

> 메뉴는 SpectatorPawn이라 이동/사격 없음 + CommonUI가 마우스 커서를 띄운다(아래 위젯의 `GetDesiredInputConfig`가 `Menu/NoCapture`).

---

## 3. `WBP_MainMenu` — 메인메뉴 위젯
- Add > Widget Blueprint → **부모 클래스 지정 필요**: 생성 시 "User Widget" 대신 **`FPSRMainMenuWidget`**를 부모로 선택(위젯BP 생성 다이얼로그에서 부모 클래스 검색). 이름 `WBP_MainMenu` (`Content/UI/Menu/`).
  - ※ 이미 만든 뒤 부모가 UserWidget이면: File > Reparent Blueprint → `FPSRMainMenuWidget`.

### 3.1 디자이너 — 위젯 배치
- 루트에 **Vertical Box**(또는 Canvas) → 안에:
  - (선택) Title `TextBlock` "FPSRoguelite"
  - **CommonButton 2개** — ⚠️ 일반 `Button`이 아니라 **CommonUI 버튼**이어야 C++ `UCommonButtonBase* PlayButton/QuitButton` BindWidget이 잡힌다.
    - **⚠️ 정정(실제)**: `CommonButtonBase`는 추상 클래스라 팔레트에 직접 못 놓는다. **CommonButtonBase를 부모로 한 WBP 서브클래스**를 만들어 그걸 배치해야 한다. 또 **기존 카드 UI는 일반 `UButton`**이라 재사용할 `CommonButtonStyle`이 없었음 → 신규 작성함.
    - **실제 반영**: `WBP_PlayButton`/`WBP_QuitButton`(부모 `CommonButtonBase`, 루트 `TextBlock` 라벨 베이크, Style=`BnW_MenuButtonStyle`)을 `WBP_MainMenu`의 VerticalBox에 배치. 배치 시 컴포넌트 이름을 **`PlayButton`/`QuitButton`** 으로(= BindWidget 변수명) 지정.
- **변수명(중요, 정확히 일치)**: 두 버튼 컴포넌트 이름이 정확히 **`PlayButton`**, **`QuitButton`** + **Is Variable**. (이름이 다르면 BindWidget 컴파일 에러)
  - 라벨 텍스트는 각 버튼 WBP 안 `Label` TextBlock에 "Play" / "Quit"로 베이크됨(per-WBP).

### 3.2 ⚠️ CommonButton 스타일 (안 보이거나 클릭 안 되는 흔한 원인)
- CommonButton은 **Style(UCommonButtonStyle)이 비면 렌더/클릭이 안 된다**. **실제 반영**: `BnW_MenuButtonStyle`(부모 `CommonButtonStyle`, normal/hover/pressed/disabled = 회색 RoundedBox 브러시)을 신규 작성해 각 버튼 WBP의 Class Defaults `Style`에 지정함. (재사용할 기존 스타일이 없어 신규 — 원안 정정)
- 라벨은 일반 `TextBlock`(흰색)을 사용 — CommonTextBlock은 또 다른 스타일(CommonTextStyle) 에셋이 필요해 의존성을 줄이려 일반 TextBlock 채택.

### 3.3 동작
- 클릭 핸들러는 **C++에 이미 배선**됨(`NativeOnInitialized`에서 `PlayButton/QuitButton->OnClicked()` → `HandlePlayClicked`=StartRun / `HandleQuitClicked`=QuitGame). **이벤트 그래프에 아무것도 안 짜도 된다.** 버튼 이름만 맞추면 끝.
- 저장 + 컴파일.

---

## 4. `WBP_Result` — 결과(승/패) 위젯
- Add > Widget Blueprint → 부모 **`FPSRResultWidget`** → 이름 `WBP_Result` (`Content/UI/Menu/`).

### 4.1 디자이너
- 루트에 Vertical Box →
  - **Outcome `TextBlock`** — 변수로 만들고 이름 예 `OutcomeText`(이름 자유, BindWidget 아님 — §4.2에서 직접 세팅).
  - **CommonButton 1개** → 변수명 **`ReturnButton`**(정확히), 라벨 "Return to Menu". 스타일 §3.2처럼 지정.

### 4.2 이벤트 그래프 — `On Outcome Set` 구현 (승/패 텍스트 스왑)
- 그래프에서 **Event `On Outcome Set`**(C++ BlueprintImplementableEvent, 입력 `Outcome` = EFPSRRunOutcome) 추가 →
  - `Outcome` **Switch on EFPSRRunOutcome**:
    - `Victory` → `OutcomeText`에 SetText "VICTORY"(원하면 초록색)
    - `Defeat` → SetText "DEFEAT"(빨강)
    - `None` → (무시 또는 빈 문자열)
- **Return 클릭은 C++ 배선**(`HandleReturnClicked` → 호스트면 `ReturnToMenu`, 클라면 서버 RPC). 그래프 작업 불필요. 버튼 이름만 `ReturnButton`.
- 저장 + 컴파일.

---

## 5. 런 PC에 결과 위젯 연결 — `BP_FPSRPC`
- 기존 `BP_FPSRPC`(부모 `FPSRPlayerController`) 열기 → Class Defaults > 카테고리 `FPSR|UI`:
  - **`Result Widget Class` = `WBP_Result`**
- 저장. (런이 끝나면 `ClientShowRunResult`가 이 클래스를 Menu 레이어에 띄운다.)

---

## 6. 프로젝트 세팅 — 맵 참조 (FPSR Game Flow)
- Edit > Project Settings → 좌측 **Game** 카테고리 그룹 안 **"FPSR Game Flow"**:
  - **Main Menu Map = `L_MainMenu`**
  - **Run Map = `L_Sandbox`**
  - **Run Travel Options = (빈칸)** ※ 리슨서버 협동(P5) 때 `listen` 등 추가. 지금은 빈칸=싱글/호스트.
- 저장(→ `Config/DefaultGame.ini`에 기록).

## 7. 부팅 맵 — DefaultEngine.ini
- `Config/DefaultEngine.ini` `[/Script/EngineSettings.GameMapsSettings]`:
  - **`GameDefaultMap=/Game/Maps/L_MainMenu.L_MainMenu`** 로 변경(패키지/스탠드얼론이 메뉴로 부팅).
  - (선택) 에디터 편의: Project Settings > Maps & Modes > **Editor Startup Map = L_MainMenu**.
- ※ `GlobalDefaultGameMode`(런 GM)·`GameInstanceClass`는 **변경 금지**. 메뉴 GM은 L_MainMenu의 World Settings override로 적용된다.

---

## 8. PIE 검증 (1인, 플랜 9단계)
> ⚠️ PIE는 **현재 열린 레벨**로 Play한다. **`L_MainMenu`를 열고** Play. (Editor Startup Map을 L_MainMenu로 두면 편함.) Players=1, Net Mode=Standalone(기본).

1. **부팅→메뉴**: L_MainMenu에서 Play → `WBP_MainMenu` 표시, **마우스 커서 보이고 클릭됨**, 폰 이동 없음.
2. **Play→런**: Play 버튼 클릭 → `L_Sandbox`로 전환 → FPS 폰 + XP바 + (레벨업 시)카드 플로우 정상(회귀 확인).
3. **승리**: 콘솔(`~`) `FPSR.EndRun victory` → `WBP_Result` **VICTORY** 표시 + 커서 복귀.
4. **복귀**: Return 클릭 → `L_MainMenu`로 전환(루프 닫힘). 
5. **패배**: 다시 Play → `FPSR.EndRun defeat` → **DEFEAT** → Return 동작.
6. **단축**: 런 중 `FPSR.ReturnToMenu`(UI 없이 바로 메뉴로).
7. **가드**: `FPSR.EndRun victory` 두 번 → 2번째 무반응(`bRunEnded` 원샷).
8. **루프 무결성**: 메뉴→Play→EndRun→Return→Play 2회차도 깨끗(결과 초기화, 디렉터 새 월드 재init).
9. **(선택) 넷 PIE**: Net Mode=Play As Listen Server, Players=2 → `FPSR.EndRun victory`가 두 클라 모두 결과 표시 / 호스트 Return만 전체 travel / 클라 Return은 서버 RPC 경유.

### 흔한 함정 체크
- 메뉴 버튼이 **안 보이거나 클릭 안 됨** → CommonButton **Style 미지정**(§3.2) 또는 일반 `Button` 사용. CommonButton + 스타일이어야 함.
- BindWidget **컴파일 에러** → 버튼 변수명이 `PlayButton`/`QuitButton`/`ReturnButton`과 불일치, 또는 타입이 CommonButton 계열 아님.
- 메뉴에서 **커서 안 보임** → World Settings GameMode가 `BP_FPSRMenuGameMode`인지, 위젯 부모가 `FPSRMainMenuWidget`인지 확인(입력모드는 위젯 C++이 처리).
- `FPSR.EndRun` **무반응** → 권한(호스트/스탠드얼론)에서만 동작. 클라 PIE 창에서 치면 안 됨.
- 로그의 `Failed to create the web browser window` / `aqProf.dll` 등은 **무해**(헤드리스/플러그인 경고).

---

## 9. 완료 후
- PIE 9단계 통과 → 코드+콘텐츠 함께 커밋(사용자 요청대로 콘텐츠 작업 후 일괄). 
- 머지게이트: `Scripts/codex-review.ps1 -Base main`(워트리에서) → `--no-ff`로 main 머지 → 워트리/브랜치 정리.
- **루프 진짜 닫기(후속)**: 지금은 디버그 커맨드가 종료 트리거. 실제 승리=보스 처치(P6), 패배=전원 사망 검출 → 각각 `AFPSRGameMode::EndRun(Victory/Defeat)` 한 줄 호출만 연결하면 완성(C++에 TODO 시임 표시됨).
- **P5 보안 조임 메모**: `ServerRequestReturnToMenu`가 클라 echo outcome 신뢰 + 리슨서버 클라 Return이 전원 복귀 → 협동 세션 시 호스트 한정 + 서버 보관 outcome으로.
