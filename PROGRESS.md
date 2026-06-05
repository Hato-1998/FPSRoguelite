# PROGRESS — 진행 현황 (라이브 핸드오프 문서)

> 다른 세션/다른 AI가 **즉시 이어받기** 위한 단일 진행 현황 문서.
> **작업 단계를 끝낼 때마다, 그리고 중단 전 반드시 이 파일을 갱신하고 커밋한다.**
> 확정 설계·기획·코드구조·규칙은 `Game.MD`(SSOT), **완료 작업 상세는 `git log --oneline`**. 여기엔 *무엇을 했는지*만 요약한다.

**최종 갱신: 2026-06-05**

## 한 줄 요약
**P0~P4-A + P4-B-1 → main 머지 완료**. **P4-B-1(무기 스탯 모디파이어 기반)** 구현+콘텐츠 **PIE 검증·빌드·스모크·Codex 통과 후 `--no-ff` main 머지(2026-06-05)**. 런타임 컨테이너 `UFPSRWeaponInstance`(UObject 복제 서브오브젝트) 신설, 스탯 해석(베이스×누적 모디파이어)을 발사/탄약 경로 배선, `ApplyCard` weapon-scope 실적용, 무기 스탯 카드(연사/탄창/반동×ThisWeapon/AllWeapons 6종) 레벨업 풀 합류. → 다음 **P4-B-2**(행동 Fragment) → **P4-B-3**(미션 6종).

## ▶▶ 새 세션 우선 작업 = P4-B-2 착수 (행동 Fragment)
- **브랜치**: `main`만 존재(P4-B-1 머지·phase 삭제 완료). **P4-B-2는 `main`에서 새 `phase/p4b2-...` 분기**(§6-7). 구현=Haiku 위임 / 검증=Opus 직접(빌드+스모크+Codex). HIGH_RISK는 승인 후.
- **P4-B-1이 P4-B-2에 남긴 토대**: `UFPSRWeaponInstance`(UObject 복제 서브오브젝트)가 ①스탯·②Fragment 단일 홈으로 존재 → **인스턴스 확장만으로 Fragment 추가**(`ActiveModifiers[]` + 발사 GA 훅). 스탯 해석/캐시/복제 패턴 확립. `AFPSRPlayerState::AllWeaponsMods`(캐릭터 광역). 발사 3곳(FireComp·Hitscan·Melee) 전부 `GetResolvedStats()` 경유.
- **P4-B-2 범위**(Game.MD §2-4-1 ②): 행동 Fragment(합성형 훅 PreFire/ModifyShotCount/ModifyChargeTime/OnProjectileSpawn/OnHitActor/PostFire) — `UFPSRWeaponInstance.ActiveModifiers[]`(DA참조+param, 경량·히트당 무할당) + 발사 GA 훅 + 무기 DA `AvailableModifiers`(약 4종) → **미션 클리어 프리즈에 모디파이어 카드 1종**(MissionReward 경로). Fragment를 UObject 상속 asset vs Instanced UObject vs 순수 DataAsset+runtime struct 중 결정(성능: OnHitActor 500마리 virtual dispatch/heap 금지).
  - **무기별 weapon-scope 카드 경로(P4-B-1에서 미연결)**: 무기 DA의 weapon-scope 카드를 **MissionReward 오퍼로 소싱**하는 배선이 P4-B-2 과제(현재 `GatherCandidatePool`은 레벨업에서 무기별 weapon-scope 제외). `BuildSingleDraw`(단일 카드 오퍼)가 이미 존재.
- **⚠️ 임시 테스트값(프로덕션 전환 시 노티·원복)**: 스케줄 DA(`DA_RunSchedule`)의 미션 60/120/180s·보스 300s → 프로덕션 5/10/15분·보스 20분. 메모리 `p4a-temp-test-values`. (XP는 프로덕션 공식)
- **P4 잔여(이월)**: **P4-B-2** 행동 Fragment. **P4-B-3** 나머지 6종 미션. **P4-C** 무기 7종. **P4-D** 게임필(히트마커/핑/위협 인디케이터/사각 오디오) + PickupRadius/XPGain + **런상태 HUD 위젯**(데이터 복제됨: `GetRunClockSeconds`/`GetRunPhase`/`IsRunPaused`/`GetPartyLevel`/`GetCardPicksPending`/`GetMissionRewardPicksPending` → WBP 바인딩).
- **이월(P2 플로우필드 후속) — 적 수직 이동/계단**: 적 이동은 **NavMesh 아님**(Game.MD §1·§5-2, 에이전트별 NavMesh 금지). 현 플로우필드(BFS grid+separation)가 계단/높이를 잘 못 타면 **플로우필드를 높이 인지(3D/멀티레벨 샘플링)하게 개선**할 것. NavMeshBoundsVolume 도입 금지. 후순위(P4-A 무관).
- **P4 백로그 — 디자이너 배치 스폰 포인트**: 현재 스폰은 첫 플레이어 주변 링 랜덤([`UFPSREnemySpawnSubsystem::ComputeSpawnLocation`](Source/FPSRoguelite/Private/Enemy/FPSREnemySpawnSubsystem.cpp:292)). → **맵에 배치한 고정 스폰 포인트 중 "전 플레이어 비가시 & 최소거리 이상"인 곳을 가중 랜덤 선택**으로 전환(랜덤 위치 X, 디자이너 지정 지점). 구현: 경량 액터 `AFPSREnemySpawnPoint`(Weight/Zone/MinPlayerDistance 데이터) → `OnWorldBeginPlay`에서 `TActorIterator`로 캐시 → 디렉터 틱마다 플레이어 카메라/위치 캐시 후 시야각(dot>cosHalfFOV)+거리 게이트로 후보 필터 → Weight×거리가중치 가중 랜덤. 후보 없으면 링 랜덤 폴백(미배치 맵도 동작). Zone은 TimeGate(§2-8)와 연동해 시간대별 스폰 구역 전환 가능. (서버 권위, 고정맵 §1 부합)
- **빌드/검증**: §6-6 (`Build.bat FPSRogueliteEditor ... -WaitMutex` / 스모크 `FPSRoguelite.Smoke.ModuleLoads` / `Scripts/codex-review.ps1 -Base main`).

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

- **P4-B-1 (main 머지)** 무기 스탯 모디파이어 기반 — 런타임 컨테이너 `UFPSRWeaponInstance`(UObject 등록형 복제 서브오브젝트: Source DA + ThisWeapon `Modifiers` + 탄약/리로드 + 해석 스탯 lazy 캐시, Push Model) 신설. 인벤토리 `Slots[]` 인스턴스화(탄약·리로드 병렬배열 → 인스턴스 응집), `AFPSRCharacter`·인벤토리 컴포넌트 `bReplicateUsingRegisteredSubObjectList=true`. 스탯 해석 = `BaseStats × 누적(ThisWeapon[인스턴스] + AllWeapons[`AFPSRPlayerState::AllWeaponsMods`, 리스폰 생존])`, 발사 3곳(FireComp·Hitscan·Melee)+탄약 `GetResolvedStats()` 배선. `ApplyCard` weapon-scope 실적용(ThisWeapon→인스턴스, AllWeapons→PlayerState, 무기 없으면 거부), `UFPSRCardDataAsset.WeaponStat/WeaponStatOp`+IsDataValid, DrawCards 범용풀 weapon-scope 합류(무기 보유 시), 프리즈 중 `ServerEquipSlot` 차단(ThisWeapon 타깃 결정성). 디버그 `FPSR.DumpWeaponStats`. 카드 magnitude 표시 `+%.0f`→퍼센트/소수 수정. 재장전 중 반동 큐 지속 버그 수정(`!CanFire()` 플러시). 콘텐츠: 무기 스탯 카드 6종(연사/탄창/반동×ThisWeapon/AllWeapons)+풀. 빌드+스모크+Codex 3R+PIE 통과. (Game.MD §2-4-1 ①)
- **P4-A (main 머지, 재설계)** 런 흐름 — **라운드제 폐지 → 레벨업 전역 프리즈**. `AFPSRGameState.bRunPaused`(복제, 페이즈독립)+`RefreshPauseState`(전원 보류픽 기준 프리즈/재개)+`AddSharedXP` 즉시 프리즈. `ERunPhase`=Combat/Boss. 프리즈 게이팅(스폰·적이동/공격 동결·플레이어 입력/속도·발사GA). 오퍼 일반화 `EFPSROfferType{OpeningSeed,LevelUp,MissionReward}`+`MissionRewardPicksPending`+`ApplyCard` 타입별(weapon-scope 수락·소비, GE적용 P4-B). `UFPSRRunDirectorSubsystem`(런클럭+시간 미션스케줄 `FFPSRMissionEvent`+`BossTime`+시간 스폰스케일링, 오프닝홀드, 보스>미션 우선). 미션 프레임워크(`AFPSRMissionActor`+`UFPSRMissionDataAsset`+`AFPSRMission_HoldZone`+`AFPSRMissionSpawnPoint` 태그매칭가중랜덤)+클리어 즉시 프리즈 보상. 적 클래스 설정화(`BP_Enemy` via GameMode). 콘텐츠(미션태그/L_Sandbox 스폰포인트/GameMode/BP_Enemy/DA_RunSchedule/미션DA·BP/존데칼) 동반. Codex 다회(라운드종료적정리·폰전스폰·거리폴백·중복바인딩크래시·스폰홀드·보스미션유실) 하드닝. 빌드+스모크+PIE 통과. (Game.MD §2-1/2-2/2-7/2-8)
- **P3-D (main 머지)** 카드 UI/공유XP바/오프닝시드 — CommonUI 인프라(`CommonUI`/`CommonInput`/`UMG` 모듈, `UFPSRGameViewportClient`, `DefaultEngine.ini` ViewportClient, 경량 `UFPSRPrimaryGameLayout`=4 레이어 스택) + `UFPSRXPBarWidget`(OnRep 델리게이트 이벤트기반, 폴링 없음) + `UFPSRCardSelectWidget`/`UFPSRCardEntryWidget` + PC RPC 배선(서버 캐시+인덱스+offer nonce 검증, 클라는 인텐트만). **설계 변경: 레벨업 스택=공유 카운터 → 플레이어별 `AFPSRPlayerState::CardPicksPending`**(4인 협동 정합, Game.MD §2-2). breather 진입/AddXP 시 서버 자동 발급. 디버그 `FPSR.OpeningSeed`/`FPSR.RequestCards`(권한 보유 시). Codex 7라운드로 보안(클라 임의카드/무한리드로/리롤악용)·정합(nonce/지연바인딩/데드락) 하드닝. 빌드+스모크 통과.
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
