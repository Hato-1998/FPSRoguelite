# PROGRESS — 진행 현황 (라이브 핸드오프 문서)

> 다른 세션/다른 AI가 **즉시 이어받기** 위한 단일 진행 현황 문서.
> **작업 단계를 끝낼 때마다, 그리고 중단 전 반드시 이 파일을 갱신하고 커밋한다.**
> 확정 설계·기획·코드구조·규칙은 `Game.MD`(SSOT), **완료 작업 상세는 `git log --oneline`**. 여기엔 *무엇을 했는지*만 요약한다.

**최종 갱신: 2026-06-02**

## 한 줄 요약
**P0~P2 + P3-A·B·C → main 머지 완료. P3-D(카드 UI/공유XP바/오프닝시드) C++ 베이스 완료 = `phase/p3d-cardui` 브랜치**(빌드+스모크+Codex 7라운드 통과). → **사용자 WBP 콘텐츠 작성 + PIE 검증 후 `--no-ff` main 머지** 대기.

## ▶▶ 새 세션 우선 작업 = P3-D 사용자 콘텐츠(WBP) + PIE 검증 → main 머지
- **현재 활성 브랜치**: `phase/p3d-cardui`(origin push됨). C++/config/Game.MD 커밋 완료. UI는 헤드리스 검증 불가 → **사용자 PIE 필수**.
- **사용자 작성 WBP**(C++ 베이스 상속):
  - `WBP_PrimaryGameLayout`(부모 `FPSRPrimaryGameLayout`): named `UCommonActivatableWidgetStack` 4개 = **`Layer_Game`/`Layer_GameMenu`/`Layer_Menu`/`Layer_Modal`**(BindWidget 이름 정확히)
  - `WBP_XPBar`(부모 `FPSRXPBarWidget`): `XPBar`(ProgressBar)/`LevelText`/`StackText`(BindWidgetOptional)
  - `WBP_CardSelect`(부모 `FPSRCardSelectWidget`): `CardEntry_0/1/2`(WBP_CardEntry, BindWidget)/`RerollButton`(opt)/`RerollChargesText`(opt)
  - `WBP_CardEntry`(부모 `FPSRCardEntryWidget`): `SelectButton`(BindWidget)/`CardNameText`/`RarityText`/`DescriptionText`/`MagnitudeText`(opt)
- **BP_FPSRPlayerController 할당**: `PrimaryLayoutClass=WBP_PrimaryGameLayout` / `XPBarWidgetClass=WBP_XPBar` / `CardSelectWidgetClass=WBP_CardSelect`. (WBP_CardSelect 내부 `CardEntryClass` 불필요 — 엔트리는 BindWidget 고정 3개)
- **PIE 검증 시나리오**: ① XP바 표시·`FPSR.AddXP`로 Lv/XP/Stack 갱신 ② `FPSR.SetPhase breather`→픽 보유 시 카드 자동 발급 ③ 카드 3장 선택/리롤 ④ `FPSR.OpeningSeed`로 2연속 선택 ⑤ (가능하면 2-client로 플레이어별 픽 독립 확인). 빌드 시 `DefaultEngine.ini` ViewportClient로 `LogUIActionRouter` 에러 소멸 확인.
- **빌드/검증**: §6-6 (`Build.bat FPSRogueliteEditor ... -WaitMutex` / 스모크 `FPSRoguelite.Smoke.ModuleLoads` / `Scripts/codex-review.ps1 -Uncommitted`).
- **P4 이월(Codex 라운드7 — P3-D 스코프 외)**: 오프닝 시드의 런 시작 자동 트리거(현재 디버그 `FPSR.OpeningSeed`만; 미션/런플로우=P4). 카드 UI 표시 실패(설정 오류)는 `ServerAbandonOffer`로 해제만(재시도 안 함 — 정상 설정에선 발생 안 함).

---

## 📋 P3-D 정식 플랜 (다음 세션 착수 — 카드 UI / 공유 XP바 / 오프닝 시드)

> **새 작업이므로 착수 시 플랜모드 재확인 후 진행**(아래는 확정 설계). **분기: `git checkout main` → `git checkout -b phase/p3d-cardui` → origin push**(§6-7). 구현=Haiku 위임 / 검증=Opus 직접(빌드+스모크+Codex). HIGH_RISK는 승인 후.

### 확정 설계 결정 (사용자, 2026-06-02)
1. **트리거 = 디버그**: 미션은 P4 → P3-D는 `FPSR.SetPhase breather` + `FPSR.AddXP`(레벨업 큐)로 카드 UI 검증. (+신규 `FPSR.OpeningSeed`로 오프닝 시드 테스트)
2. **비주얼 = 플레이스홀더**(레이아웃·바인딩만, 스타일링 최소)
3. **오프닝 시드 = 2장**(런 시작 시 2회 선택, `ApplyCard(..., bConsumeLevelUp=false)`)
4. **풀 CommonUI 스택**(Activatable Widget Stack 레이어: Game / GameMenu / Menu / Modal)

### 산출물 (C++ 베이스 = Haiku, 콘텐츠 WBP = 사용자)
**A. CommonUI 인프라 (토대)**
- `FPSRoguelite.Build.cs`에 `CommonUI`, `CommonInput` 모듈 추가
- `UFPSRGameViewportClient : UCommonGameViewportClient` + `DefaultEngine.ini`의 `GameViewportClientClassName` 지정 → §8 `LogUIActionRouter` 에러 해소
- **PrimaryGameLayout 경량 재구현**(CommonGame 플러그인 없음 §3): `UFPSRPrimaryGameLayout : UCommonUserWidget` — 4개 named `UCommonActivatableWidgetStack`(Game/GameMenu/Menu/Modal), 레이어 태그로 등록. PlayerController(로컬) BeginPlay에서 뷰포트에 push + 레이어별 push/pop API. (Lyra `PrimaryGameLayout` 패턴 참조, UIManagerSubsystem/GameUIPolicy까지는 불필요 — 단일 레이아웃 직접 push)
- 신규 GameplayTag: `UI.Layer.Game/GameMenu/Menu/Modal` (DefaultGameplayTags.ini)
- CommonUI 입력 데이터(`UCommonUIInputData`: Back/Confirm 액션) — 사용자 콘텐츠 + 프로젝트 설정

**B. 공유 XP바 HUD (Game 레이어, 상시)**
- `UFPSRHUDWidget`/`UFPSRXPBarWidget : UCommonUserWidget` — GameState `SharedXP`/`PartyLevel`/`PendingLevelUps` 바인딩(OnRep 또는 폴링). `Lv n / XP x/y / Stack s` 표시(플레이스홀더 ProgressBar+Text). 로컬 PlayerController가 Game 레이어에 push

**C. 카드 선택 위젯 (Modal 레이어)**
- `UFPSRCardSelectWidget : UCommonActivatableWidget`(3카드 오버레이) + `UFPSRCardEntryWidget`(카드 1장: 이름/등급/설명/Magnitude 텍스트 + 선택 버튼) + 리롤 버튼
- 표시 데이터 = `FFPSRCardDraw[]`(이름/등급/수치). 선택=인덱스

**D. 배선 / 네트워크 (서버 권위)**
- `AFPSRPlayerController`에 RPC: `ServerRequestCardOffer`(서버: 서브시스템 `DrawCards` → **서버가 현재 offer를 PC/PlayerState에 캐시**) → `ClientPresentCards(const TArray<FFPSRCardDraw>&)`(오너 클라에 전달) / `ServerSelectCard(int32 Index)`(캐시된 offer에서 인덱스 검증 후 `ApplyCard`) / `ServerRerollOffer`(`TryReroll` 성공 시 재추첨→`ClientPresentCards`)
- ⚠️ **보안 리팩터**: 현재 `ApplyCard(PC, FFPSRCardDraw, bConsumeLevelUp)`는 임의 draw를 받음 → P3-D에서 **서버가 발급한 offer 캐시(per-PC)에서 인덱스로 선택**하도록 게이트(클라가 임의 카드/수치 적용 못 하게). offer 캐시 + 검증 경로 추가

**E. 흐름**
- **오프닝 시드**: 런 시작(또는 `FPSR.OpeningSeed`) → 서버가 플레이어별 2회 offer 발급 → 위젯 2연속 선택(`bConsumeLevelUp=false`)
- **정비시간**: `RunPhase==Breather && PendingLevelUps>0` → offer 발급·선택 1회당 `ApplyCard(bConsumeLevelUp=true)`로 레벨업 1 소비 → 스택 0까지 반복. 리롤 가능

### 작업 순서(권장)
1. CommonUI 모듈+ViewportClient+레이어 태그(A) → 빌드 통과 + LogUIActionRouter 사라짐 확인
2. PrimaryGameLayout + XP바 HUD(B) → PIE에서 XP바 표시·`FPSR.AddXP`로 갱신
3. 카드 선택 위젯 + PC RPC 배선 + offer 캐시/검증(C·D)
4. 오프닝 시드 2장 + 정비시간 흐름(E) + `FPSR.OpeningSeed` 디버그
5. 사용자 WBP 콘텐츠 작성 → PIE 검증 → Codex → `--no-ff` main 머지

### 사용자 콘텐츠 (P3-D)
- `WBP_PrimaryGameLayout`/`WBP_XPBar`/`WBP_CardSelect`/`WBP_CardEntry`(C++ 베이스 상속), CommonUI InputData 에셋, DefaultEngine.ini ViewportClient 설정
- 카드 콘텐츠는 P3-C 것 재사용(`Content/Cards/Character/`)

### 검증/메모
- 빌드 + 헤드리스 스모크 + Codex(§6-6). UI는 PIE 수동 확인(헤드리스로 위젯 렌더 검증 불가 → 사용자 PIE)
- **P4 메모**: 무기 카드 아키텍처(무기별 부착[현재] vs 중앙 weapon 풀) 확정 + ThisWeapon/AllWeapons 적용 + 무기 모디파이어 Fragment. PickupRadius/XPGain 속성 추가(픽업 후속).

---

## ✅ 완료 이력 (요약 — 상세는 `git log` / Game.MD)

- **P3-D (C++ 베이스, `phase/p3d-cardui`, WBP+PIE 대기)** 카드 UI/공유XP바/오프닝시드 — CommonUI 인프라(`CommonUI`/`CommonInput`/`UMG` 모듈, `UFPSRGameViewportClient`, `DefaultEngine.ini` ViewportClient, 경량 `UFPSRPrimaryGameLayout`=4 레이어 스택) + `UFPSRXPBarWidget`(OnRep 델리게이트 이벤트기반, 폴링 없음) + `UFPSRCardSelectWidget`/`UFPSRCardEntryWidget` + PC RPC 배선(서버 캐시+인덱스+offer nonce 검증, 클라는 인텐트만). **설계 변경: 레벨업 스택=공유 카운터 → 플레이어별 `AFPSRPlayerState::CardPicksPending`**(4인 협동 정합, Game.MD §2-2). breather 진입/AddXP 시 서버 자동 발급. 디버그 `FPSR.OpeningSeed`/`FPSR.RequestCards`(권한 보유 시). Codex 7라운드로 보안(클라 임의카드/무한리드로/리롤악용)·정합(nonce/지연바인딩/데드락) 하드닝. 빌드+스모크 통과.
- **P3-C** 카드 시스템(main 머지) — `UFPSRCardDataAsset`(`RarityTiers` 등급별 수치) + `UFPSRCardPoolDataAsset` + `UFPSRCardSubsystem`(등급 가중 비복원 추첨/`CardFamily` 디듀프/`ApplyCard` 레벨업 게이트/`TryReroll`). 리롤=PlayerState(플레이어별 3). `Luck` 단일 행운축(RarityBonus 폐지). 수치=`SetByCaller`(태그 `SetByCaller.CardMagnitude`). `IsDataValid` 검증. 최대체력 증가=현재체력 동반증가(서버권위). Character 카드 콘텐츠 5종+풀+GE(`Content/Cards/Character/`). PIE 확인됨. (Game.MD §2-3)
- **P3-B** XP 픽업+자석 — `AFPSRXPPickup`(서버 자석 이동·수령) + `UFPSRPickupSubsystem`(cap 150, 초과 시 XP 직접가산). 적 사망 시 드롭.
- **P3-A** 런 상태(GameState 호스팅) — `AFPSRGameState`에 `SharedXP/PartyLevel/PendingLevelUps/RunPhase`(Push Model). 레벨업=스택 누적(프리즈 없음 §2-2). Breather 시 스폰·공격 게이팅.
- **P2** 적 대량화(main 머지) — `UFPSREnemySpawnSubsystem`(풀링+SpawnDirector, 하드캡 500) + `UFPSRFlowFieldSubsystem`(BFS flow-field+separation) + 거리 LOD(Significance 티어/NetUpdateFreq) + 이속 ±10% 편차 + 적 근접데미지·공격토큰·i-frame + 충돌무시 대시(+IA_Dash 콘텐츠). (Game.MD §5)
- **P1.5** 사격/이동 감각 — 사격코어(FullAuto 연사/반동="복구 빚"모델/확산·블룸) + 탄약·재장전(MagSize/R, **예비탄 무한**) + ADS(FOV/확산/반동 배율) + 반동 ADS의존(힙 산탄/ADS climbing). `FPSR.RecoilPreview`. (Game.MD §2-4-2)
- **P1** Net-aware 1P 슬라이스 — `AFPSRCharacter`(1P 카메라+Separated Arms+EnhancedInput) + `Weapon/`(3슬롯 서버권위 인벤토리, Push Model) + 발사/근접 GA(히트스캔·구체오버랩) + `AFPSREnemyBase`(경량 Pawn)+`UFPSREnemyHealthComponent` 데미지 브릿지. 코드리뷰 하드닝(서버 cadence 검증). 사용자 BP 3종+무기 DA+IA 셋업 완료.
- **P0** 경량 C++ 스캐폴드 — UE5.7, 플러그인 enable, GameplayTags(`Config/DefaultGameplayTags.ini`), 빌드+스모크 테스트(`FPSRoguelite.Smoke.ModuleLoads`).
- **문서/리뷰 인프라** — `Game.MD`(SSOT) + `PROGRESS.md` 체계. 외부 AI 문서리뷰=`GameConfirm.MD`(§10), Codex 코드리뷰=`Scripts/codex-review.ps1`→`Docs/reviews/`(gitignore, §6-6).

---

## 빌드 / 검증 방법
- 빌드(에디터 닫고): `"D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\Build.bat" FPSRogueliteEditor Win64 Development -Project="E:\Git_Project\FPSRoguelite\FPSRoguelite.uproject" -WaitMutex`
- 헤드리스 스모크: `UnrealEditor-Cmd.exe <uproject> -unattended -nopause -nullrhi -nosplash -nosound -ExecCmds="Automation RunTests FPSRoguelite.Smoke.ModuleLoads" -TestExit="Automation Test Queue Empty" -abslog=...`
- Codex 리뷰: `powershell -File Scripts\codex-review.ps1 -Uncommitted`(작업트리) / `-Base main`(브랜치 diff). 결과 `Docs/reviews/`.
- 새 UCLASS 다수면 Live Coding 불가 → 풀빌드. IA 에셋 생성은 `Scripts/gen_input_assets.py`.

## 확정 사항 / 주의점 (운영)
- 모델 정책: **구현=Haiku 위임 / 검증(빌드·diff·스모크·Codex·UI)=Opus 직접**(§6-5). 각 P단계 `phase/` 브랜치→검증→`--no-ff` 머지→브랜치 삭제(§6-7).
- 프로덕션 원칙: 콘텐츠=BP/DataAsset/config, **C++ 경로 하드코딩 금지**. 엔진 API는 소스 grep 후 사용(§6-3). 검증 없이 "완료" 보고 금지.
- **MCP(unreal) 인증 실패로 미사용** → UBT 빌드 + 헤드리스 자동화로 검증. UI는 사용자 PIE 확인.
- UE5.7 IMC 매핑은 Python 미반영 → 에디터 수동. 디버그/플레이스홀더(전환 대상)는 Game.MD §8 인벤토리 참조.
- Phase 종료 시 해당 Phase 사용자 콘텐츠 동반 커밋 여부를 사용자에게 물을 것(메모리 규칙).
