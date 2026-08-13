# Phase A 에디터 작업 가이드 (사용자 수행, 2026-08-13)

> 대상 프로젝트: **`E:\Git_Project\FPSRoguelite2-wtA\FPSRoguelite.uproject`** (지금 열려 있는 그 에디터 — 브랜치 `phase/loc-ui-migration`).
> 원본 인벤토리(위젯별 근거) = `Docs/Review/WBP_TextInventory_20260813.md`.
> ⚠️ **전 작업 공통**: 작업 중 **PIE 실행 금지** — 모든 편집·저장이 끝난 뒤 **에디터를 재시작**하고 나서 PIE로 검증한다(§5). 중간 확인은 UMG 디자이너 미리보기로만.

## 0. 시작 전 1회 — StringTable 리로드
에디터 시작 후 CSV에 키가 추가됐으므로(A-2 커밋), 픽커에 새 키가 보이게 리로드한다:
**메뉴 Tools → FPSR → "StringTable CSV 리로드"** 클릭. (Output Log에 "reloaded 3/3" 확인)

## 1. 텍스트 → StringTable 키 바인딩 (WBP 15개, 텍스트 20곳)

각 항목 공통 절차:
1. 해당 WBP 열기 → 디자이너에서 **위젯 선택**(아래 표의 위젯 이름) → Details 패널의 **Text 프로퍼티**.
2. Text 입력칸 **왼쪽의 ▾(콤보 화살표)** 클릭 → **String Table** 선택.
3. String Table 콤보 = **UI** 선택 → Key 콤보 = 아래 표의 키 선택.
4. 텍스트가 한국어(소스 문자열)로 바뀌는지 확인 → **Compile + Save**.

| WBP (경로: Content/UI/...) | 위젯 | 키 |
|---|---|---|
| HUD/WBP_BossHUDBar | BossNameText | `HUD.Boss.NameLabel` |
| HUD/WBP_RunHUD | FrozenBanner | `HUD.Run.FrozenBanner` |
| HUD/WBP_RunHUD | LevelLabel | `HUD.Run.LevelLabel` |
| Menu/WBP_BackButton | Label | `Menu.Button.Back` (문구도 "뒤로/Back"로 교정됨 — 기존 "Button" 라벨 오류 해소) |
| Menu/WBP_PlayButton | Label | `Menu.Button.Play` |
| Menu/WBP_QuitButton | Label | `Menu.Button.Quit` |
| Menu/WBP_ReturnButton | Label | `Menu.Button.Return` |
| Menu/WBP_SettingsButton | Label | `Menu.Button.Settings` |
| WBP_Settings | TitleText | `Settings.MasterVolume.Title` |
| Widget/WBP_LoadoutEntry | NameText | `Widget.LoadoutEntry.NameLabel` ⚠️ 이 위젯은 배선 누락 의심(인벤토리 §3-4) — 키 바인딩은 하되, 로드아웃별 무기명이 나와야 하는 자리면 알려줄 것(후속 배선) |
| Widget/WBP_Lobby | WpnTxt0~7 (8개) | `Widget.Lobby.WeaponSlot0` ~ `WeaponSlot7` (순서대로) |
| Widget/WBP_Lobby | InviteText | `Widget.Lobby.InviteButton` |
| Widget/WBP_Lobby | JoinText | `Widget.Lobby.JoinButton` |

> 로비 무기 라벨 8개는 폐지된 무기(유탄발사기·점사소총)도 포함된 하드코딩 목록입니다 — 일단 키로 이관만 하고, 목록 자체의 정리는 별도 기획 판단.

## 2. WBP_CardEntry — 출처 풀 라벨 TextBlock 추가
1. `Content/UI/Widget/WBP_CardEntry` 열기.
2. 팔레트에서 **Text Block**을 카드 상단(카드 이름/레어도 근처, 기존 `TargetWeaponText` 옆 권장)에 배치.
3. 이름을 정확히 **`SourcePoolText`** 로 변경(디테일 상단 이름칸 — C++ BindWidgetOptional 이름 매칭이라 오타 금지) + **Is Variable 체크**.
4. 텍스트 내용은 아무거나(런타임에 C++가 무기 이름/"캐릭터"로 덮어씀). Compile + Save.
5. (선택 정리) 기존 `TargetWeaponText`가 같은 정보를 표시하고 있었다면 중복 — 어느 쪽을 남길지 화면 보고 판단, 지우진 말고 알려주면 코드에서 정리.

## 3. 카드/프래그먼트 에셋 리네임 (BounsShot 정정 후속)
Content Browser에서 (콘텐츠 브라우저 검색: "Bouns"):
1. `Content/Cards/Weapons/Modifiers/DA_CardModifiers_BounsDamage` → 우클릭 Rename → **`DA_CardModifiers_BonusShot`**
2. `Content/Cards/Weapons/Modifiers/DA_Fragment_BounsDamage` → **`DA_Fragment_BonusShot`** (실제 경로가 다르면 검색 결과의 Fragment 에셋)
3. 리네임 후 **Content 폴더 우클릭 → Fix Up Redirectors** → 저장(Save All).
4. ⚠️ 이후 **시트 갱신은 사용자**: Cards 시트 해당 행 CardId/AssetName = `DA_CardModifiers_BonusShot`, CardCatalog 시트 `weapon.frag.bounsdamage` 행의 AttrId를 `weapon.frag.bonusshot`으로 + Payload 경로를 새 Fragment 경로로. (완료 후 Claude가 sync→임포트로 DA와 family 파생까지 정합 처리)

## 4. 마무리 저장 → 에디터 재시작
- **Save All** → 에디터 **완전 종료 후 재실행** (MCP/스크립트로 위젯을 만진 세션에서 PIE 금지 갓차 — 수동 편집도 동일 원칙 적용).

## 5. 재시작 후 PIE 검증 (사용자 스모크)
1. 에디터 개인설정 → 지역 및 언어 → **미리보기 게임 언어 = 한국어** → PIE: 메뉴/로비/HUD/설정 문구가 한국어인지.
2. 미리보기 언어 = English → 동일 화면 영어 확인. = 日本語 → 일본어 확인(□ 두부 글자 나오면 폰트 이슈 — 보고).
3. 카드 제시 화면: 카드 이름/설명 3개국어 전환 + **출처 풀 라벨**(무기 이름 or "캐릭터") 표시 + 레어도 라벨(커먼/레어/...) 번역 확인.
4. 이상 발견 항목은 위젯 이름과 함께 메모 → Claude에게 전달.

## 6. 완료 후 Claude가 이어서 할 일 (참고)
wtA 변경분 커밋 분리(WBP 바인딩/카드 리네임) → gather(en/ja LocRes) → Phase A 머지 게이트(빌드+테스트+diff) → main 머지·push → 시트 sync(리네임 반영) → 보드/WorkLog. BP 그래프 핀 리터럴 3곳(RunHUD Select·Result Switch·DownedOverlay)은 후속 스윕으로 등재.
