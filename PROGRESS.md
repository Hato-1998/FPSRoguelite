# PROGRESS — 진행 현황 (라이브 핸드오프 문서)

> 다른 세션/다른 AI가 **즉시 이어받기** 위한 단일 진행 현황 문서.
> **작업 단계를 끝낼 때마다, 그리고 중단 전 반드시 이 파일을 갱신하고 커밋한다.**
> 확정 설계·기획·코드구조·규칙은 `Game.MD`(SSOT), **완료 작업 상세는 `git log --oneline`**. 여기엔 *무엇을 했는지*만 요약한다.

**최종 갱신: 2026-06-09**

## 한 줄 요약
**P0~P4-A + P4-B-1/2 + P4-D(게임필) → main 머지 완료**. **스폰포인트 코드 → main 머지(2026-06-09, `phase/p4-enemyspawnpoints` 코드분, Codex 5R 하드닝)**: 디자이너 배치 `AFPSREnemySpawnPoint`(비가시+거리 가중랜덤, 링 폴백) + 플로우필드 장애물 회피 + 적 중력/지면추종(+KillZ 회수·접촉 수직게이트·동일위치 분리 근본수정). **콘텐츠 배치(L_Sandbox)는 PIE 미검증이라 제외 → 브랜치 잔류(사용자 PIE 후 별도 머지)**. → 미머지 잔여 = **`phase/p4b3-missions`**(미션 콘텐츠+PIE) + **`phase/p4-enemyspawnpoints`**(콘텐츠 배치만). **다음 = 아래 '코드 선행 백로그' 순서대로(세션 단위)**. **A1 투사체 코어 완료·main 머지(2026-06-09, `phase/p4c-projectile-core`, Codex 클린)** → 권장 다음 = A2 Hitscan 또는 A3 AOE(A1 의존 충족).

## ▶▶ 미머지 브랜치 (콘텐츠 PIE 대기) — `phase/p4d-gamefeel`·`phase/p4-enemyspawnpoints`는 2026-06-09 완전 머지됨
> 코드는 main에 있고, 남은 건 사용자 콘텐츠+PIE. 머지 순서·시점은 사용자 판단.

- **`phase/p4b3-missions`** — 미션 5종(콘텐츠+PIE 대기, 상세 아래 P4-B-3 절). `AFPSRMission_LimitedVision`은 비주얼 디자인 결정 보류.

- **`phase/p4-enemyspawnpoints` = 완전 머지 완료(코드+콘텐츠, 2026-06-09)** — 브랜치 정리 대상. 상세는 완료 이력. **유일 후속(C1 백로그)**: 플로우필드 셀 클리어런스(좁은 통로 과차단) + 멀티레벨/높이 인지 — PIE 의존, `phase/p2-flowfield-height`. (계단/높이 주행 PIE 관찰 시 C1 우선순위 판단)

## ▶▶ 새 세션 우선 작업 = P4-B-3 (미션 6종) / P4-C / P4-D
- **P4-B-2 완료·main 머지됨(2026-06-08)** — 무기 행동 Fragment 전체(상세는 아래 완료 이력 + `git log`). 다음 후보: **P4-B-3**(나머지 6종 미션), **P4-C**(무기 7종), **P4-D**(게임필 히트마커/핑/위협인디케이터/사각오디오 + PickupRadius/XPGain + 런상태 HUD 위젯). 새 작업이므로 **플랜 우선**, `phase/` 브랜치 분기(§6-7).
- **Fragment 후속(미완)**: `ModifyChargeTime`/`OnProjectileSpawn` 훅(차징/투사체 아키타입 도입 시), Melee fragment, fragment 제거/교체 UI. 무기별 `AvailableModifiers` 콘텐츠 확장(현재 Rifle만 MultiShot/BonusDamage 등록).
- **⚠️ 임시 테스트값(프로덕션 전환 시 노티·원복)**: 스케줄 DA(`DA_RunSchedule`)의 미션 60/120/180s·보스 300s → 프로덕션 5/10/15분·보스 20분. 메모리 `p4a-temp-test-values`. (XP는 프로덕션 공식)
- **P4-D 완료(main 머지 2026-06-09, `phase/p4d-gamefeel`)** — 게임필. 상세는 완료 이력. **후속(이월)**: ① **히트마커 최종 연출 재확인**(크로스헤어/발사체 작업 후). ② **원거리 타겟 사전경고 생산자**(원거리 적 AI 구현 시 `ClientNotifyRangedTarget` 호출 배선; 현재 디버그 `FPSR.TestRangedWarn`만). ③ 핑/Gibs/사각 오디오(§2-14). ④ 근접 사각지대 인지=사운드(오디오 단계).
- **P4 잔여(이월)**: **P4-B-3**(브랜치 `phase/p4b3-missions`, 미션 5종 콘텐츠+PIE+머지 대기) + 나머지 미션. **스폰포인트**(브랜치 `phase/p4-enemyspawnpoints`, 코드 완료·배치+PIE+머지 대기). **P4-C** 무기 7종.
- **디자이너 배치 스폰 포인트 = 완료(코드 main 머지 2026-06-09)**. 콘텐츠 배치+PIE만 잔류(위 미머지 절). 후속 플로우필드 품질은 아래 백로그 C1.
- **빌드/검증**: §6-6 (`Build.bat FPSRogueliteEditor ... -WaitMutex` / 스모크 `FPSRoguelite.Smoke.ModuleLoads` / `Scripts/codex-review.ps1 -Base main`).

---

## 🧱 코드 선행 백로그 (세션 단위 큐 — 콘텐츠 없이 미리 작업, 2026-06-09 수립)
> 남은 로드맵 전체에서 **콘텐츠(메시/VFX/DA/BP/사운드) 없이 미리 만들 수 있는 C++ 베이스**만 뽑은 큐. 각 유닛 = 전용 `phase/` 브랜치 · 플랜 우선 · Haiku 구현/Opus 검증 · 빌드+스모크+Codex 게이트 · **콘텐츠 보류분은 만들지 말 것**. 하나 완료 시 머지 후 사용자 호출 → 다음 유닛은 새 세션.
> **권장 순서**: ~~A1(투사체 코어)~~ ✅완료 → 다음 권장 = A2 Hitscan 또는 A3 AOE(A1 의존 충족). 5~11은 순서 자유.

| 순서 | 유닛 | 브랜치 | 코드 산출물 | 콘텐츠 보류 | 의존 |
|---|---|---|---|---|---|
| ~~A1 투사체 코어~~ | ✅ **완료·main 머지(2026-06-09, Codex 클린)** | `phase/p4c-projectile-core` | `AFPSRProjectile`+`UFPSRProjectileSubsystem`(서버권위 풀·≤64캡·프리즈정지·KillZ회수) | 메시/VFX·발사GA(A3) | — |
| 1 | A2 Hitscan 3종 | `phase/p4c-hitscan` | Burst/Sniper/Shotgun 발사 GA + `FireMode.Burst` 처리 | 무기 DA/BP | — |
| 2 | A3 AOE+ChargeLaser | `phase/p4c-aoe-charge` | 투사체 발사 GA(=`AcquireProjectile` 호출+클라 예측 cosmetic 스폰) + 차징 AbilityTask + Fragment 훅 `OnProjectileSpawn`/`ModifyChargeTime` | 무기 DA/BP | A1✅ |
| 3 | B1 원거리 적 AI | `phase/p4-ranged-enemy` | 원거리 아키타입(투사체[Team=Enemy]+차징경고+공격토큰) + P4-D `ClientNotifyRangedTarget` 생산자 배선 | 적/투사체 VFX | A1✅ |
| 4 | A4 Fragment 마무리 | `phase/p4b-fragment-finish` | Melee fragment + 제거/교체 서버 로직 | 교체 UI | A3 |
| 5 | C1 플로우필드 높이/클리어런스 | `phase/p2-flowfield-height` | 멀티레벨 BFS 샘플링(계단/높이) + 셀 클리어런스 인지 프로브(좁은 통로 과차단 해소) | — | — |
| 6 | C2 GMS 재구현 | `phase/infra-gms` | GameplayMessageSubsystem + Payload struct | — | — |
| 7 | D1 아군 오사(FF) | `phase/p5-friendly-fire` | 데미지 브릿지 팀체크+10%+호스트토글 | — | — |
| 8 | D2 수동 부활 DBNO | `phase/p5-dbno` | Alive/DBNO/Dead 상태기계 + 근접 부활게이지 | 다운 anim/UI | — |
| 9 | D3 메타 SaveGame | `phase/p6-savegame` | `URogueliteSaveGame` + SaveManager 서브시스템 | 업그레이드 트리 | — |
| 10 | D4 보스 스캐폴드 | `phase/p6-boss-scaffold` | `ABossBase`+BossDefinition+StateTree 골격(P4-A 게이트 연결) | 실제 보스 | — |
| 11 | D5 세션 스캐폴드(선택) | `phase/p5-session` | 세션 서브시스템 골격 | Steam/EOS 설정 | — |

**새 세션 유도 가이드 (복붙)** — 각 유닛 시작 시:
```
Game.MD + PROGRESS.md 먼저 읽어. PROGRESS '코드 선행 백로그'의 유닛 [번호]를 진행한다.
- 브랜치: phase/<표의 브랜치명> 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증(빌드+스모크+Codex)
- 콘텐츠 보류분(메시/VFX/DA/BP/사운드)은 만들지 말고 C++ 베이스만 완성
- 완료 시: 이 백로그에서 해당 유닛 줄 제거 + 완료이력 추가, 머지 후 호출 대기
```

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

- **A1 투사체 코어 (main 머지, 2026-06-09, `phase/p4c-projectile-core`, 코드 선행 백로그 #1)** — A3(AOE/유탄)·B1(원거리 적) 공유 의존 범용 베이스. ① `AFPSRProjectile`(Sphere+`UProjectileMovementComponent` 결정적 이동+코스메틱 메시[미할당], `bReplicates`+ReplicateMovement): 서버권위 충돌→데미지 브릿지 **재사용**(Player→`UFPSREnemyHealthComponent::ApplyDamage` / Enemy→`AFPSRCharacter::ApplyContactDamage`), 단일타격+관통 또는 **AOE**(`OverlapMultiByObjectType(ECC_Pawn)`=대시 i-frame 무관), `IsHostileTarget` 팀판정(친화 폭발·instigator 차단). ② `UFPSRProjectileSubsystem`(서버권위 풀+**≤64 동시 복제캡** FIFO 강제회수, 클래스 매칭 재사용, 디버그 `FPSR.SpawnProjectile`). ③ **글로벌 프리즈 준수**(§2-2): `FTickableGameObject` 전환검출→활성 투사체 PMC정지+수명타이머 Pause, 충돌핸들러 `IsRunFrozen` 게이트, freeze프레임 월드충돌 stuck은 resume 지연임팩트로 해소. ④ 생명주기 하드닝: `bActive` 재진입 가드+멱등 해제(이중풀등록 차단), `FellOutOfWorld`→풀 회수(pending-kill 오염 차단)+`IsValid` 전수, `SetUpdatedComponent` 재연결(StopSimulating 후 정지 방지), Lifetime≤0 클램프, 점블랭크 초기오버랩 순서. **설계결정**: 미검증 cosmetic mode 제거 → A1=결정적 PMC(**예측준비**)+서버권위 데미지+풀/캡, **클라예측 로컬 cosmetic 스폰은 A3 발사 GA 책임**(Game.MD §2-10 의도는 결정적 이동으로 충족). **알려진 한계**(미미): 프리즈 *전환 프레임*의 폰 오버랩 1건 드롭(1프레임·무크래시). **콘텐츠 보류**(미작성): 투사체 메시/VFX·무기 DA/BP·발사 GA(A3). 빌드+헤드리스 스모크(Success)+**Codex 11R 하드닝→클린** 통과. (Game.MD §2-10/§5)
- **적 스폰포인트 코드 (main 머지, 2026-06-09, `phase/p4-enemyspawnpoints` 코드분)** — ① **디자이너 배치 스폰포인트** `AFPSREnemySpawnPoint`(Weight/ZoneTag/MinPlayerDistance/bEnabled) + `UFPSREnemySpawnSubsystem` 전 플레이어 비가시(FOV)+거리 가중랜덤 선택(후보 0 시 링 폴백, 미배치 맵 동작), 디자이너 지점 ground-snap 생략(권위 Z 보존), `SetActiveSpawnZone` 훅(TimeGate 후속). ② **플로우필드 장애물 마스크/BFS 라우팅 + 적 중력/지면추종**(`AFPSREnemyBase`, 경사/계단 보정). **Codex 5R 하드닝**: 디자이너 Z 보존(실내 천장 스냅 방지)→월드 밖 추락 KillZ 회수(슬롯 누수)→접촉 데미지 수직 게이트(바닥 관통)→동일 스폰지점 중첩을 **분리(separation) 동일위치 결정적 골든앵글 푸시**로 근본 해소(지터 전량 제거, 스폰 위치 비이동, 맵 비의존)→최종 클린. 빌드+스모크 통과. **콘텐츠 배치(L_Sandbox 스폰포인트+BP_EnemyBase 정렬, `d3a68c6`)도 머지 완료**(코드+콘텐츠 = `phase/p4-enemyspawnpoints` 완전 머지). **후속(C1)**: 플로우필드 셀 클리어런스(좁은 통로 과차단)+멀티레벨 높이 인지. (Game.MD §5-2)
- **P4-D (main 머지, 2026-06-09)** 게임필/피드백 — ① **PickupRadius/XPGain** 어트리뷰트(`UFPSRCombatSet` 승수, 기본 1.0)+XP 픽업 배선(자석 대상=플레이어별 유효반경 거리비 최소, 협동 정합)+카드 2종(Instant·Add·SetByCaller). ② **런상태 HUD** `UFPSRRunHUDWidget : UCommonUserWidget`(BlueprintPure 게터+`OnRunStateUpdated` BIE, 이벤트기반; 픽 카운팅은 레벨업=즉시카드선택이라 제거)+**Game레이어 컨테이너** `UFPSRGameHUDWidget : UCommonActivatableWidget`(입력설정 소유; XPBar 위젯/클래스 폐지·RunHUD로 일원화). ③ **로컬 피드백** `UFPSRPlayerFeedbackComponent`(비복제·이벤트형)+PC Client RPC: **히트마커**(서버권위 Hit/Crit/Kill, 활성화당 1회, Unreliable), **피격 방향**(CoD식 `ApplyContactDamage`→오너클라 카메라기준 각도), **원거리 타겟 사전경고**(다수소스 id별 TMap·각도배열·추적Tick·Reliable; 생산자=원거리 적 AI 후속, 디버그 `FPSR.TestDamageDir`/`FPSR.TestRangedWarn`). **설계 정제**: 근접/사각지대 *시각* 위협 제외→사운드 이전. Codex 다회 하드닝(협동 자석/호스트 클럭/늦은복제 바인딩/확산발산→서버권위/제어상실 클리어/Unreliable·Reliable/다수소스/전방선언). 콘텐츠: 카드 GE·DA 2종+CardPool, WBP GameHUD/RunHUD/HitMarker/ThreatIndicator, BP_FPSRPC 배선. 빌드+스모크+Codex+2-client PIE 통과. **후속**: 히트마커 최종 연출(크로스헤어/발사체 후), 원거리경고 생산자 배선, 핑/Gibs/사각오디오. (Game.MD §2-11/§2-14)
- **P4-B-2 (main 머지, 2026-06-08)** 무기 행동 Fragment — 합성형 발사 훅. `UFPSRWeaponFragment : UPrimaryDataAsset`(무상태, 동작=C++ 서브클래스 virtual 훅 `PreFire/ModifyShotCount/OnHitActor/PostFire`, 수치=DataAsset) + 레퍼런스 2종(`UFPSRFragment_MultiShot{ExtraShots}`, `UFPSRFragment_OnHitBonusDamage{BonusDamage}`). `FFPSRFireContext`(plain struct) / `UFPSRWeaponInstance.ActiveFragments[]`(복제 참조)+`AddFragment`(MaxStacks 스택제한)/`GetFragmentStackCount`/`HasFragment` / `GA_WeaponFire_Hitscan` 훅 배선(PreFire→ModifyShotCount=NumShots 루프→히트당 OnHitActor→PostFire) / `UFPSRCardDataAsset.GrantedFragment`(ThisWeapon→AddFragment) / `UFPSRWeaponDataAsset.AvailableModifiers[]` / `DrawWeaponModifierOffer`(스택 여유분 셔플) + ApplyCard fragment 분기 / 디버그 `FPSR.GrantMissionRewardPick`. **마무리 세션(2026-06-08)**: ① 카드 작성 EditCondition 가드(Scope별 무관 필드 숨김) + `GetCardFamilyKey` Character-scope 게이트(stale GE 패밀리 누수 차단). ② 미션보상 카드 UI(`UFPSRCardEntryWidget`): fragment는 등급 대신 카테고리 라벨(`FragmentCategoryText`, WBP override)+수치 빈칸. ③ **MultiShot 펠릿당 탄약 소모**(잔량 클램프, 최소 1발). ④ Fragment **`MaxStacks` 중첩**(중복 누적, 훅 스택마다 적용 — MultiShot 2스택=3발). 콘텐츠: Fragment DA 2종+Fragment 카드 2종+카드 `Card/`이동+Rifle AvailableModifiers+CardPool. 빌드+스모크+Codex(다회 하드닝, 최종 클린)+PIE 통과. (Game.MD §2-4-1 ②)
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
