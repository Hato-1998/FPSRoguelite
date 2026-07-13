# P7-U11a 사용자 콘텐츠 가이드 — 멀티플레이 루프(로비 허브 / 세션 / Seamless 트래블 / 로비 무기선택)

> 대상 브랜치: **`phase/p7-mp-loop-lobby`** (코드 완료·커밋·푸시됨 `0c9ea96`). 이 문서대로 **콘텐츠(맵 2 + BP 2 + WBP 1[+엔트리 1] + DA 1 + 세팅 + steam_appid.txt)** 를 만들면 루프가 동작한다.
> 코드는 전부 베이스만 있고 **에셋 경로 하드코딩 0** (맵/풀은 소프트참조 = Project Settings). 빌드/스모크는 통과 상태. C++ 수정 불필요.
> 선행 읽기: `Game.md`, `PROGRESS.md`(🌐 U11a), `Docs/P7-MultiplayerLoop_Plan.md`. 메뉴/결과 셸은 P6-A(`Docs/P6A_GameFlow_UserContent_Guide.md`)에서 이미 완성됨 — 이 가이드는 그 위에 **로비 허브**를 얹는다.

---

## 0. 전제 / 만들 것 요약
**기존 재사용 에셋**: `WBP_PrimaryGameLayout`(4레이어 스택, 재사용), `BP_FPSRGameMode`/`BP_FPSRPC`(런), `BP_FPSRMenuGameMode`/`BP_FPSRMenuPC`+`WBP_MainMenu`/`WBP_Result`(P6-A 메뉴 셸), 무기 DA들(`DA_Weapon_*`).

**이번에 만들 것**:
| # | 에셋 | 부모/타입 | 위치(권장) |
|---|---|---|---|
| 1 | `BP_FPSRLobbyGameMode` | `FPSRLobbyGameMode` | `Content/Game/Core/` |
| 2 | `BP_FPSRLobbyPC` | `FPSRLobbyPlayerController` | `Content/Character/Player/` |
| 3 | `L_Lobby` | Level | `Content/Maps/` |
| 4 | `L_Transition` | Level(빈) | `Content/Maps/` |
| 5 | `WBP_Lobby` | `FPSRLobbyWidget` | `Content/UI/Lobby/` |
| 6 | `WBP_LoadoutEntry` | UserWidget(무기 버튼 1개) | `Content/UI/Lobby/` |
| 7 | `DA_LoadoutPool` | `FPSRLoadoutPoolDataAsset` | `Content/Weapons/` |
| 8 | `steam_appid.txt` | 텍스트(=480) | 프로젝트 루트(`.uproject` 옆) |
| 9 | `BP_LobbyDisplayPawn` | `FPSRLobbyDisplayPawn` | `Content/Character/Player/` |
| - | Project Settings | LobbyMap / LoadoutPool 지정 | `Config/DefaultGame.ini` |

> **🆕 U11a 확장(2026-06-17)**: 호스트 **Start 버튼은 제거**되고 **플레이어별 Ready**(R)로 대체 — 참가자 전원이 준비하면 서버가 3초 카운트다운 후 자동 시작. **로비 코드 조인**(V), **포디움 3D 표시 캐릭터**(`BP_LobbyDisplayPawn`, 플레이스홀더 마네킹) 추가. 아래 §1.3·§2.1·§3 갱신분 참고.

> **흐름 미리보기**: 메뉴 **Play** → (HostSession) → **L_Lobby**(포디움에 캐릭터 스폰) → 각자 무기 1정 선택 → 각자 **R 준비** → **전원 준비 시 3초 후 자동 시작** → **L_Sandbox**(seamless) → 전투 → (전멸/`FPSR.EndRun defeat`) → **L_Lobby 복귀**(seamless, 런·준비 리셋) → 재선택·재준비.

---

## 1. 로비 GameMode / PC 블루프린트

### 1.1 `BP_FPSRLobbyGameMode`
- Content Browser > Add > Blueprint Class → 부모 검색 **`FPSRLobbyGameMode`** → 이름 `BP_FPSRLobbyGameMode`.
- **⚠️ Class Defaults > `Player Controller Class` = `BP_FPSRLobbyPC`** (필수, §1.2). C++ 생성자가 PC를 위젯 미설정 `AFPSRLobbyPlayerController`로 박으므로, BP로 오버라이드 안 하면 로비가 검은 화면 + `[Lobby] PrimaryLayoutClass / LobbyWidgetClass not assigned` 로그가 난다(메뉴 GM→PC와 동일 패턴).
- **⚠️ Class Defaults > `Default Pawn Class` = `BP_LobbyDisplayPawn`**(§1.3, U11a). C++ 기본은 `ASpectatorPawn`이라, 포디움에 표시 캐릭터를 세우려면 BP에서 오버라이드해야 한다(미지정 시 보이지 않는 스펙테이터만 스폰).
- 나머지는 C++ 기본값 그대로(GameState=`AFPSRGameState`, PlayerState=`AFPSRPlayerState`, **bUseSeamlessTravel=true**). 저장.

### 1.2 `BP_FPSRLobbyPC`
- 부모 **`FPSRLobbyPlayerController`** → 이름 `BP_FPSRLobbyPC`.
- **Class Defaults**(카테고리 `FPSR|UI`):
  - **`Primary Layout Class` = `WBP_PrimaryGameLayout`** (기존 재사용)
  - **`Lobby Widget Class` = `WBP_Lobby`** (§3에서 생성 — 먼저 §3 만들고 지정해도 됨)
- 저장. ※ 로비 PC도 Enhanced Input IMC 불필요(CommonUI Menu 입력모드).

### 1.3 `BP_LobbyDisplayPawn` (포디움 표시 캐릭터, U11a)
> 표시 전용 폰 — 게임플레이 `AFPSRCharacter`와 분리(게임플레이 폰 비오염). 입장 순서대로 PlayerStart에 1개씩 스폰되어 **각 플레이어의 바디 + 선택 무기**를 포디움에 보여준다. C++ 베이스가 복제된 PlayerState의 선택 무기를 추적해 BP 훅을 호출하고, BP는 **메시만** 담당.
- 부모 **`FPSRLobbyDisplayPawn`** → 이름 `BP_LobbyDisplayPawn`.
- 디자이너/컴포넌트: 상속된 **`BodyMesh`**(SkeletalMesh, 루트)에 **플레이스홀더 마네킹**(`SKM_Manny`/`SKM_Quinn` 등) 지정. (선택) 무기 부착용 자식 컴포넌트(SkeletalMesh 또는 StaticMesh) 추가.
- 베이스 제공 API:
  - **이벤트(BlueprintImplementableEvent)**: `On Display Weapon Changed(Weapon)` — 선택 무기가 바뀌거나 바인딩 직후 호출. 여기서 무기 DA의 1P 메시(소프트 `WeaponMesh1P`/`WeaponMeshStatic1P`, `Load Synchronous`)를 부착/교체.
  - **상태(BlueprintPure)**: `Get Displayed Weapon()` → 무기DA(null=미선택).
- 그래프(예): **Event On Display Weapon Changed** → `Weapon` Is Valid 가드 → `WeaponMesh1P`(소프트) Load → 무기 컴포넌트 Set Skeletal Mesh(또는 부착). null이면 무기 메시 비우기.
- ⚠️ **에셋 갭**: 현재 프로젝트는 1P 팔/무기만 있고 3P 풀바디·3P 무기 메시가 없다 → **마네킹 + (있으면)1P 무기 메시로 구조만 검증**. 실제 캐릭터/3P 무기 아트는 후속.
- 저장.

---

## 2. 맵 — `L_Lobby` + `L_Transition`

### 2.1 `L_Lobby` (로비 허브 맵)
1. File > New Level → **Empty**(또는 Basic) → `Content/Maps/L_Lobby` 저장.
2. **World Settings > GameMode Override = `BP_FPSRLobbyGameMode`**.
3. 액터:
   - **PlayerStart 를 포디움 수만큼**(예 4개) 배치 — 입장 순서대로 엔진 `ChoosePlayerStart`가 배정해 각 플레이어 표시 폰이 자기 포디움에 선다. (1개만 둬도 솔로는 동작.)
   - **CameraActor** 1개(룸 오버뷰 구도) — Details > **Auto Activate for Player = Player 0** 체크 → 로컬 뷰타깃이 이 카메라가 됨(포디움 전체가 보임, C++ 불필요).
   - (선택) 방/포디움 메시·라이트(스테인드글라스 룸 — 사용자 아트, 플레이스홀더 가능).
4. 저장.

### 2.2 `L_Transition` (빈 전환 맵 — seamless 필수)
1. File > New Level → **Empty** → `Content/Maps/L_Transition` 저장. **아무것도 안 둬도 됨**(PlayerStart도 불필요).
2. ⚠️ 이 맵이 **없으면 seamless ServerTravel이 깨진다**. 경로는 `DefaultEngine.ini`에 이미 `TransitionMap=/Game/Maps/L_Transition.L_Transition`로 지정돼 있으니 **이름/경로를 정확히** 맞출 것.

> **L_Sandbox(게임플레이 맵)는 편집 금지** — 트래블 대상으로만 쓴다(보스 U4가 소유). 이미 `Run Map`으로 지정돼 있음.

---

## 3. `WBP_Lobby` — 로비 위젯 (핵심)

> 메뉴 위젯과 달리 **BindWidget이 없다**(C++ 베이스는 액션/게터만 제공). 버튼·리스트는 디자이너가 배치하고 **그래프에서 C++ 함수에 연결**한다. 베이스 제공 API(🆕=U11a):
> - **액션(BlueprintCallable)**: `Select Loadout Weapon(Pool Index)` · 🆕`Toggle Ready()` · `Request Show Invite()` · 🆕`Join Lobby By Code(Code)`
> - **상태(BlueprintPure)**: `Get Loadout Pool()` → DA · `Get Selected Weapon()` → 무기DA · 🆕`Is Local Player Ready()` → bool · `Is Local Player Host()` → bool(UI 힌트만) · 🆕`Get Lobby Code()` → String
> - **이벤트(BlueprintImplementableEvent)**: `On Loadout Refreshed()`(내 선택 변경 시) · 🆕`On Ready Refreshed()`(내 준비상태 변경 시)
> - ⚠️ **`Request Start Run()` 제거**됨 — 호스트 Start 대신 **전원 Ready**가 시작 트리거(서버가 집계·카운트다운·트래블).

- Add > Widget Blueprint → **부모 `FPSRLobbyWidget`** → 이름 `WBP_Lobby` (`Content/UI/Lobby/`).

### 3.1 디자이너 — 레이아웃(예시)
루트 Canvas/VerticalBox에:
- **무기 선택 영역**(상단 중앙): `Wrap Box`(또는 Horizontal Box) 변수명 `WeaponListBox` (Is Variable ✔) — 무기 버튼들이 여기 채워진다.
- **플레이어 목록 영역**: `Vertical Box` 변수명 `PlayerListBox` (Is Variable ✔) — 입장 플레이어 + 각자 선택 무기 + **준비 상태** 표시. (포디움 캐릭터가 있으면 보조용.)
- 🆕 **Ready 버튼**(하단 좌): 변수명 `ReadyButton` (Is Variable ✔), 라벨 "Ready (R)" — `Is Local Player Ready`로 색 토글.
- 🆕 **로비 코드 표시**(하단 우): TextBlock `LobbyCodeText` — `Get Lobby Code` 표시.
- 🆕 **코드 조인**(하단 우): `EditableTextBox` 변수명 `JoinCodeInput` + `Button` 변수명 `JoinButton` 라벨 "Join (V)".
- **Invite 버튼**(중앙 상): 변수명 `InviteButton` (Is Variable ✔), 라벨 "Invite Friends (F)".
- (선택) 내 현재 선택 표시 TextBlock `SelectedText`.

### 3.2 `WBP_LoadoutEntry` — 무기 1칸 (자식 위젯)
무기 리스트를 동적으로 만들기 위한 작은 엔트리 위젯:
- Add > Widget Blueprint → 부모 **UserWidget** → `WBP_LoadoutEntry`.
- 디자이너: `Button`(변수 `SelectButton`) 안에 `TextBlock`(변수 `NameText`).
- 변수 추가(Instance Editable ✔):
  - `WeaponIndex` (Integer)
  - `OwnerLobby` (`FPSRLobbyWidget` 오브젝트 레퍼런스)
- 그래프:
  - **Event Construct** → (선택) `NameText` SetText = 표시명(부모가 세팅하므로 생략 가능).
  - **`SelectButton` OnClicked** → `OwnerLobby` → **Select Loadout Weapon**(`WeaponIndex`).

### 3.3 `WBP_Lobby` 그래프 — 무기 리스트 채우기
**함수 `RebuildWeaponList`**(직접 만들기):
1. `WeaponListBox` → **Clear Children**.
2. **Get Loadout Pool** → (Is Valid 가드) → `Selectable Weapons` **For Each Loop** (Index 사용):
   - `Create Widget`(Class=`WBP_LoadoutEntry`, Owning Player=Get Owning Player) →
   - Set `WeaponIndex` = Index, `OwnerLobby` = self,
   - 엔트리의 `NameText` SetText = `Array Element`(무기DA) → **Display Name**,
   - `WeaponListBox` **Add Child**.
3. (선택) **Get Selected Weapon** 과 같은 무기면 엔트리를 하이라이트(색/테두리).

호출 지점:
- **Event Construct** → `RebuildWeaponList`.
- **Event On Loadout Refreshed**(BIE 오버라이드) → `RebuildWeaponList`(+ `SelectedText` 갱신). ← 내 선택이 서버에서 확정되면 자동 호출됨.

### 3.4 `WBP_Lobby` 그래프 — Ready / 코드 / 초대 / 플레이어 목록
- **Event Construct**:
  - 플레이어 목록·접속 변화를 위해 **Set Timer by Function Name**(예 `RefreshPlayerList`, 0.5s, Looping) 시작.
  - **`LobbyCodeText`** SetText = **Get Lobby Code**(초기 1회; 클라는 세션 복제 후 채워지므로 폴에서도 갱신 권장).
- 🆕 **`ReadyButton` OnClicked** → **Toggle Ready**. (무기 미선택이면 서버가 거부 — 버튼은 눌리되 상태 안 바뀜. 안내 텍스트 권장.)
- 🆕 **Event On Ready Refreshed**(BIE 오버라이드) → `ReadyButton` 색/라벨 = **Is Local Player Ready** 기준(예 준비=초록 "Ready ✔" / 미준비=회색 "Ready (R)").
- 🆕 **`JoinButton` OnClicked** → **Join Lobby By Code**(`JoinCodeInput` GetText→ToString). (PIE/NULL OSS에선 검색 불가 — 2-PC Steam에서 동작.)
- **`InviteButton` OnClicked** → **Request Show Invite**. (Steam 오버레이 — 패키지+Steam에서만 실제 동작)
- **함수 `RefreshPlayerList`**: `PlayerListBox` Clear → **Get Game State** → **Player Array** For Each → `Cast to FPSRPlayerState` → 텍스트 = `Get Player Name` + 각자 `Get Selected Weapon`→Display Name(null이면 "선택 중") + **`Is Ready`**(✔/…) → TextBlock 만들어 Add Child. (+ `LobbyCodeText` 재갱신으로 클라 코드 표시 보정.)
- 저장 + 컴파일.

> ※ 입력모드(마우스 커서)는 C++ `GetDesiredInputConfig`(Menu/NoCapture)가 처리 — 그래프 불필요.
> ※ **R/V/F 키 바인딩**(선택): WBP에서 `OnKeyDown` 오버라이드 또는 버튼 단축키로 R=Toggle Ready, V=JoinButton 포커스/실행, F=Invite, Q/E=무기 네비를 연결. 마우스 클릭만으로도 전체 동작.

---

## 4. `DA_LoadoutPool` — 선택 가능한 무기 풀
- Content Browser > Add > **Miscellaneous > Data Asset** → 클래스 **`FPSRLoadoutPoolDataAsset`** → 이름 `DA_LoadoutPool` (`Content/Weapons/`).
- **`Selectable Weapons`** 배열에 로비에서 고르게 할 무기 DA를 순서대로 추가(예: `DA_Weapon_Rifle`, `DA_Weapon_Sniper`, `DA_Weapon_Shotgun`, `DA_Weapon_Bazooka`, `DA_Weapon_Grenade`, `DA_Weapon_ChargeLaser`, `DA_Weapon_LMG`, `DA_Weapon_BurstRifle`).
- 저장. (각 무기 DA의 `Display Name`이 로비 버튼 라벨로 쓰인다 — 비어 있으면 채워두면 좋다.)

---

## 5. 프로젝트 세팅 — 맵/풀 참조
- Edit > Project Settings → **Game > "FPSR Game Flow"**:
  - **Lobby Map = `L_Lobby`**
  - **Loadout Pool = `DA_LoadoutPool`**
  - (이미 설정됨) Main Menu Map = `L_MainMenu`, Run Map = `L_Sandbox`.
- 저장(→ `Config/DefaultGame.ini` 기록). ※ `LobbyMap`은 ini에 `/Game/Maps/L_Lobby.L_Lobby`로 이미 적혀 있으니, 맵 이름만 맞으면 자동 잡힌다. `LoadoutPool`은 여기서 지정해야 한다.

---

## 6. `steam_appid.txt`
- 프로젝트 루트(`FPSRoguelite.uproject` 옆)에 **`steam_appid.txt`** 파일 생성, 내용 **`480`** 한 줄.
- 에디터/패키지 실행 시 Steam 클라이언트가 이 app id(스페이스워, 공용 테스트)로 붙는다.
- ⚠️ **480은 공용** — 동시에 여러 세션이 떠 충돌할 수 있으니 **소인원 테스트 한정**. 출시 시 전용 app id로 교체.
- (Steam OSS·NetDriver 설정은 `DefaultEngine.ini`에 코드 단계에서 이미 들어가 있음 — 손댈 것 없음.)

---

## 7. 검증

### 7.1 PIE 솔로 — **세션 없이 전체 루프 검증 가능(핵심)**
> Steam 클라이언트 없이도 OSS가 NULL로 폴백되어 `HostSession`→로컬 세션→트래블이 동작한다. **친구 초대/조인만** 빠지고 나머지 루프는 PIE에서 전부 확인된다.
1. **`L_MainMenu`를 열고** Play (Standalone, Players=1).
2. **Play 버튼** → `L_Lobby`로 전환 → `WBP_Lobby` 표시(마우스 커서), 포디움에 표시 캐릭터 스폰, 룸 카메라 구도, 로비 코드 표시(NULL OSS면 비거나 짧은 코드).
3. **무기 선택** → 무기 버튼 클릭 → 그 무기 하이라이트/`On Loadout Refreshed` 동작 + 포디움 캐릭터 무기 메시 갱신(`On Display Weapon Changed`).
4. **준비/시작** → **무기 미선택 상태로 Ready** → 거부(상태 안 바뀜) 확인 → 무기 선택 후 **R 준비** → (솔로=1인) **3초 카운트다운 후 자동 시작** → `L_Transition` 거쳐 `L_Sandbox`로 **seamless** 전환 → FPS 폰이 **선택한 무기로 스폰**. (준비 중 다시 R로 해제 시 카운트다운 취소되는지도 확인.)
5. **패배** → 콘솔(`~`) `FPSR.EndRun defeat` → 결과창 잠깐 → ~3s 후 **L_Lobby 복귀**(seamless).
6. **런 리셋 확인** → 로비에서 다시 선택 가능, **준비 상태도 해제**됨, XP/카드/AllWeapons 모디파이어 초기화(이전 런 흔적 없음).
7. **재시작** → 무기 재선택 → R 준비 → 2회차도 깨끗.
8. **승리 경로(임시)** → 전투 중 `FPSR.EndRun victory` → 마찬가지로 로비 복귀(보스 자동복귀는 U11b).

**트래블 골격만 빠르게**(로비/UI 없이): 어느 맵에서든 `FPSR.TravelLobby` / `FPSR.TravelGame`로 seamless 왕복만 선검증.

### 7.2 2-PC Steam E2E (멀티 — 패키지 빌드)
> 친구 초대/조인은 **Steam 클라이언트 실행 + 패키지 빌드 2-PC(2계정)** 필요. PIE로는 NULL OSS라 초대 불가.
1. 양쪽 PC에서 Steam 로그인 + 패키지 실행(각자 `steam_appid.txt` 동봉).
2. 호스트: 메뉴 Play → 로비 → **Invite Friends**(Steam 오버레이) → 친구 초대.
3. 클라: 초대 수락 → `OnSessionUserInviteAccepted` → 자동 조인 → 같은 로비 입장(플레이어 목록 2명, 포디움 2개).
   - 🆕 **코드 조인 대안**: 클라가 호스트의 표시 코드를 `Join (V)` 입력→조인(같은 결과). 잘못된 코드면 `OnJoinByCodeComplete(false)` 로그.
4. 각자 무기 선택 → 각자 포디움에 상대 무기 표시 확인 → **둘 다 R 준비** → **전원 준비 시 3초 후 자동 시작** → 둘 다 인게임(각자 선택 무기) — 클라도 seamless 동반.
5. 전투 → 1명 사망=게임 계속, 2명 사망=패배 → 둘 다 **로비 복귀** → 리셋(준비 해제) → 재준비.
6. 데미지/FF/넉백이 서버권위로 정상(회귀 없음).

---

## 8. 흔한 함정 체크
- 로비가 **검은 화면 + 로그 `[Lobby] ... not assigned`** → `BP_FPSRLobbyGameMode.PlayerControllerClass`가 `BP_FPSRLobbyPC`인지, 그 PC에 `Primary Layout Class`/`Lobby Widget Class`를 지정했는지(§1).
- **준비해도 시작 안 됨** → 참가자 **전원**이 준비해야 카운트다운 시작. 한 명이라도 미준비/무기 미선택이면 대기. 카운트다운 중 누군가 해제하면 취소(정상). 로그 `[Lobby] All N participant(s) ready — starting run in 3.0s` 확인.
- **Ready가 안 눌림(상태 안 바뀜)** → **무기 미선택 가드**. 무기 먼저 고르면 준비 가능(서버가 빈손 준비 거부). 무기를 해제하면 준비도 자동 해제.
- **포디움 캐릭터/무기 안 보임** → `BP_FPSRLobbyGameMode.DefaultPawnClass`가 `BP_LobbyDisplayPawn`인지(§1.1), 그 폰의 `BodyMesh`에 마네킹·`On Display Weapon Changed` 그래프가 있는지(§1.3). 룸이 안 잡히면 `CameraActor`의 Auto Activate for Player 0(§2.1).
- **코드 조인 실패** → PIE/NULL OSS는 세션 검색 불가(2-PC Steam 전용). app id 480 공용이라 코드 충돌 가능 — 소인원 한정. 클라측 코드 정밀 대조 폴백이 있어 느슨한 필터는 보정됨.
- **seamless 전환이 깨짐/멈춤** → `L_Transition` 맵이 존재하고 경로가 `DefaultEngine.ini`의 `TransitionMap`과 정확히 일치하는지.
- **무기 리스트가 빔** → Project Settings의 `Loadout Pool`이 `DA_LoadoutPool`인지, 그 안 `Selectable Weapons`가 채워졌는지. (소프트참조라 로비 진입 시 동기 로드)
- **인게임에서 무기가 기본 로드아웃으로 나옴** → 로비에서 선택 안 했거나(미선택=Default 폴백), `FPSR.TravelGame`로 로비 우회 직행한 경우(정상 폴백).
- **Play가 로비로 안 감** → `Lobby Map` 미지정 또는 `L_Lobby` 미생성(로그 `Hosted but LobbyMap is null/invalid`). 맵 만들고 세팅 지정.
- **Steam 초대 안 뜸** → 패키지+Steam 실행 환경에서만 동작(PIE/에디터·NULL OSS는 오버레이 없음). app id 480 공용 충돌 가능.
- 로그의 `Failed to create the web browser window`, `Unable to create OnlineSubsystem instance Steam`(헤드리스/Steam 미실행) 등은 **무해**.

---

## 9. 완료 후
- PIE 솔로(§7.1) + 2-PC Steam E2E(§7.2) 통과 → 사용자 알림.
- 콘텐츠 동반 커밋(맵/BP/WBP/DA + steam_appid.txt) → `codex-review.ps1 -Base main` 재확인 → PROGRESS ✅ → **`--no-ff` main 머지**.
- 병렬 클론 A(U3/U4 보스)와 EndRun 본문·L_Sandbox·RunDirector 무충돌 — 독립 머지 가능.
- 후속(U11b): 보스 OnDeath→EndRun(Victory) 자동 복귀가 이 위에 얹힘. 결과창 ReturnButton(현재 메인메뉴행)→"로비로" 정리도 폴리시로 가능.
