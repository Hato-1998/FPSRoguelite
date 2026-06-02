# PROGRESS — 진행 현황 (라이브 핸드오프 문서)

> 다른 세션/다른 AI가 **즉시 이어받기** 위한 단일 진행 현황 문서.
> **작업 단계를 끝낼 때마다, 그리고 중단 전 반드시 이 파일을 갱신하고 커밋한다.**
> 확정 설계·기획·코드구조·규칙은 `Game.MD`(SSOT), 상세 이력은 `git log --oneline`.

**최종 갱신: 2026-06-01**

## 한 줄 요약
**P2 전체 → main 머지 완료**(+대시 사용자콘텐츠 `4715138`). **P3-A·B·C 코드 완료**(런 상태 + XP픽업/자석 + 카드 데이터/로직). 빌드+스모크+Codex 통과. → 다음 P3-D(카드 UI/오프닝시드/공유XP바 HUD).

## ▶▶ 새 세션 우선 작업 = P3-A/B/C PIE 확인 → P3-D 착수
**브랜치**: `phase/p3-progression` (활성, main에서 분기·origin push). 구현=Haiku 위임 / 검증=Opus.

### 🧭 핸드오프 요약 (이 줄부터 읽으면 즉시 이어받기 가능)
- **현재 위치**: P3(진행 시스템) 진행 중. 분해 = **P3-A✅ → P3-B✅ → P3-C✅ → P3-D(다음)**.
- **다음 작업 = P3-D (카드 UI, 새 작업이므로 플랜 우선)**: CommonUI 뷰포트/입력 설정(`CommonGameViewportClient` — §8 LogUIActionRouter 해결) + **정비시간 3카드 선택 위젯**(레벨업 스택 연속 소비, 본인 화면 오버레이) + **오프닝 시드 1~2장**(런 시작, `ApplyCard(..., bConsumeLevelUp=false)`) + **공유 XP바 HUD**(GameState `SharedXP/PartyLevel/PendingLevelUps` 바인딩) + 리롤 버튼(PlayerState `RunRerollCharges`). **배선**: 클라 위젯 → PlayerController Server RPC → `UFPSRCardSubsystem::DrawCards/ApplyCard/TryReroll`. (Game.MD §2-2/§2-3)
- **P3-C가 P3-D에 노출한 서버 API**: `World->GetSubsystem<UFPSRCardSubsystem>()` → `DrawCards(PC, N, Exclude)`(Character scope만·가중추첨) / `ApplyCard(PC, Card, bConsumeLevelUp)`(레벨업 게이트, 오프닝시드=false) / `TryReroll(PC)`(PlayerState 차감). 풀 주입=`BP_FPSRGameMode.CardPool`.
- **⏳ 미확인 PIE(사용자)**: P2 전체(스웜 분산/풀 재활용/LOD/근접피해/대시) + P3-A/B(`FPSR.AddXP`·`FPSR.SetPhase`·XP 오브 흡수). 각 단계 ⏳ 항목 참조. **코드는 빌드+스모크 통과 상태**.
- **🧹 housekeeping(미처리)**: 머지 완료된 원격 브랜치 `phase/p1-review-hardening`, 오래된 `phase/p1.5-b-ammo-reload` 삭제 보류(사용자 확인 시 정리).
- **규칙(메모리 저장됨)**: Phase 종료 시 해당 Phase 사용자 콘텐츠(에셋/BP/IMC) 동반 커밋 여부를 사용자에게 물을 것.
- **빌드/검증**: §6-6 (`Build.bat FPSRogueliteEditor ... -WaitMutex` / 헤드리스 스모크 `FPSRoguelite.Smoke.ModuleLoads`). 구현=Haiku 위임, 검증=Opus 직접(§6-5).

### ✅ P3-C 코드 완료 (2026-06-01, 빌드+스모크+Codex 통과) — 카드 데이터/로직
- **신규 `Card/`**: `FPSRCardTypes.h`(`ECardScope` Character/ThisWeapon/AllWeapons, `ECardRarity` Common/Rare/Epic/Legendary) + `UFPSRCardDataAsset`(DisplayName/Description/Scope/Rarity/`AppliedEffect`=GE/Weight, 스탯 하드코딩 없음 §2-3) + `UFPSRCardPoolDataAsset`(Cards[] + 등급별 기본가중치 4종 + Luck/RarityBonus 스케일).
- **신규 `UFPSRCardSubsystem`(UWorldSubsystem, 서버권위)**: `DrawCards`(풀+보유무기 `WeaponCards` 후보 → **Rarity 가중 비복원 추첨**, 유효가중치=`RarityBase×Weight×(1+RarityBonus·scale+Luck·scale)×등급tier`) / `ApplyCard(PC,Card,bConsumeLevelUp=true)`(Character scope GE→플레이어 ASC) / `TryReroll`. 풀 주입=`AFPSRGameMode::BeginPlay`→`SetActivePool(CardPool)`(BP 할당, 경로 하드코딩 없음).
- **리롤=플레이어별**(사용자 확정): `AFPSRPlayerState`에 `RunRerollCharges`(기본 3, Push Model 복제, 서버 BeginPlay 초기화) + `ConsumeRerollCharge/ResetRerollCharges/SetRerollCharges`.
- **신규 글로벌 속성**: `UFPSRCombatSet`에 `Luck`/`RarityBonus`(기본 0, 기존 GlobalCrit 패턴 미러) — 추첨 가중·향후 카드/메타 타깃(§4-1).
- **Codex 리뷰 반영(2건 P2)**: ① **레벨업 게이트** — `ApplyCard`가 `bConsumeLevelUp` 시 `PendingLevelUps>0` 선검사 후 GE 적용·소비를 원자적으로(무료 지급 방지), 오프닝시드는 `false`로 게이트 우회(§2-2). ② **무기 카드 거부** — ThisWeapon/AllWeapons는 추첨에서 제외 + `ApplyCard` 거부(선택 낭비 방지), 무기스탯 적용은 P4.
- **디버그 콘솔**(`#if !UE_BUILD_SHIPPING`): `FPSR.DrawCards [N]` / `FPSR.ApplyCard [index]` / `FPSR.Reroll [N]` / `FPSR.RerollCharges [N]`.
- **보강(2026-06-02, 빌드+스모크+Codex 통과)**:
  - ① **카드별 `Magnitude`(SetByCaller, 태그 `SetByCaller.CardMagnitude`)** — GE 1개를 다른 값으로 재사용(고정 GE 호환).
  - ② **`CardFamily`(GameplayTag) 디듀프** — 같은 family(미설정 시 AppliedEffect GE 클래스)는 한 추첨에 1장만 제안.
  - ③ **등급별 수치 테이블 리팩터(사용자 확정)**: 카드의 단일 `Rarity`/`Magnitude` → **`TArray<FFPSRCardRarityTier{Rarity,Magnitude}> RarityTiers`**. **"모든 등급에서 나오는 카드 = 1 에셋"**(등급당 1티어, 추첨이 등급 굴림). `DrawCards`는 (카드×티어) 평탄화 후 가중추첨, 반환=**`TArray<FFPSRCardDraw{Card,Rarity,Magnitude}>`**, `ApplyCard(PC, FFPSRCardDraw, bConsumeLevelUp)`. ⚠️ **기존 카드 .uasset은 RarityTiers 재작성 필요**(Rarity/Magnitude 필드 제거됨).
  - ④ **데이터 검증**: `UFPSRCardDataAsset::IsDataValid`(WITH_EDITOR, RarityTiers 빔=에러/AppliedEffect 없음=경고) + 런타임 빈 티어 경고 로그(무음 실패 방지, Codex 지적 반영).
  - Game.MD §2-3 + 가이드(`Docs/P3-C_UserContent_Guide.md`) RarityTiers 모델로 갱신.

**⏳ P3-C PIE 확인(사용자 콘텐츠 필요)**: 샘플 카드 DA들 + `DA_CardPool`(+GE 에셋) 생성 → `BP_FPSRGameMode.CardPool` 할당 → `FPSR.AddXP 120`(레벨업 큐)→`FPSR.DrawCards 3`(로그에 3장)→`FPSR.ApplyCard 0`(applied, GE 적용)→레벨업 큐 없으면 rejected. `FPSR.RerollCharges 3`→`FPSR.Reroll`→차감·재추첨.

### ✅ P3-B 코드 완료 (2026-06-01, 빌드+스모크 통과) — XP 픽업 + 자석
- **신규 `AFPSRXPPickup`(`Pickup/`)**: 적 사망 드롭 경량 XP 오브(placeholder 스피어 메시). 서버 권위 Tick — 최근접 플레이어로 자석 이동, `CollectRadius` 접촉 시 `GameState->AddSharedXP` 후 `Destroy`.
- **신규 `UFPSRPickupSubsystem`**: `SpawnXPPickup(Loc, XP)` — lazy-prune + **활성 cap `MaxActivePickups=150`**(§5) 초과 시 오브 없이 XP 직접 가산. `UPROPERTY(Transient) TArray<TObjectPtr>` GC-safe.
- **적 사망 훅**: `AFPSREnemyBase::HandleDeath`가 풀 반환 전 `SpawnXPPickup(위치, XPReward=5)`.
- **후속**: 자석 배칭/픽업 풀링, 클라 코스메틱+서버수령 분리, 인접 병합, PickupRadius 어트리뷰트 연동(§5).

**⏳ P3-B PIE 확인**: 적 처치 → XP 오브 드롭 → 다가가면 빨려와 흡수 → 화면 readout `XP x/y` 증가, 누적 시 레벨업·Stack 증가.

### ✅ P3-A 코드 완료 (2026-06-01, 빌드+스모크 통과) — 런 진행 상태(GameState 호스팅)
- **`AFPSRGameState`에 복제 런 상태**(Push Model, 서버권위, Game.MD §4-1 갱신: WorldSubsystem 복제불가 → GameState 호스팅): `SharedXP`/`PartyLevel`/`PendingLevelUps`(레벨업 스택)/`RunPhase`(Combat/Breather). `AddSharedXP`(레벨업 누적·프리즈 없음 §2-2)/`SetRunPhase`/`ConsumePendingLevelUp`(P3-D 카드선택용) + XP 임계 placeholder(`XPBaseRequired=100`+`(Lv-1)*XPPerLevel=50`, UCurveFloat는 후속 §2-8).
- **SpawnDirector 게이팅**: `TickDirector`가 `Breather`면 스폰 정지(§2-2), `TickEnemyMovement` 공격도 `bCombatPhase` 게이트(안전구간 무피해). 이동은 유지.
- **디버그**: 콘솔 `FPSR.AddXP [N]`/`FPSR.SetPhase [combat|breather]`(#if !UE_BUILD_SHIPPING) + 캐릭터 화면 readout `Lv n XP x/y Stack s [Phase]`(ENABLE_DRAW_DEBUG).

**⏳ P3-A PIE 확인**: `FPSR.AddXP 120`→레벨업·Stack 증가(화면 readout) / `FPSR.SetPhase breather`→적 스폰 정지·피해 없음 / `combat`→재개. (정비시간 카드 소비 UI는 P3-D.)

### ✅ P2 대시 사용자 콘텐츠 — 커밋 완료 (2026-06-01)
`IA_Dash` + `IMC_Default`(매핑) + `BP_FPSRPlayer`(DashAction 할당) → main `4715138` 커밋·LFS 푸시 + p3 브랜치 머지. PIE 대시 정상 작동 사용자 확인됨.

### ✅ P2-C2 코드 완료 (2026-06-01, 빌드+스모크 통과) — 충돌무시 대시 (§2-13)
- `AFPSRCharacter`: `IA_Dash`→`Input_Dash`(이동입력 방향, 없으면 정면) → `ServerDash(Dir)` RPC(서버권위, ServerReload 패턴).
- `ServerDash`: 쿨다운 게이트(`DashCooldown=2.0`) → 캡슐 `ECC_Pawn` 응답=`Ignore`(적·아군 통과) + `LaunchCharacter(Dir*DashSpeed=2000, bXY=true,bZ=false)`(공중대시 가능) → `DashDuration=0.2s` 타이머로 `EndDash`(응답 `Block` 복원). 튜닝값 EditDefaultsOnly.

### ✅ P2-C1 코드 완료 (2026-06-01, 빌드+스모크 통과) — 적 근접 데미지 + 공격토큰 + 플레이어 피해/clamp
- **적 근접(contact) 공격**: `AFPSREnemyBase`에 `AttackRange=150/AttackDamage=8/AttackInterval=1.0`(EditDefaultsOnly) + `LastAttackTime` + `CanAttack/NotifyAttacked`. 배치 패스(`TickEnemyMovement`)에서 사거리 내 + 쿨다운 경과 + **공격토큰** 여유 시 데미지.
- **공격토큰**(§2-6/§5): 플레이어당 패스당 공격 허용 적 수 상한 `AttackTokenLimit=10`(`AttackersThisPass[]`). 수백 마리 동시타격 방지.
- **플레이어 피해=서버권위+clamp**: 적→`AFPSRCharacter::ApplyContactDamage`→`ASC->ApplyModToAttribute(Health, -dmg)`(엔진 확인: base값 수정·서버가드). `UFPSRHealthSet` `PreAttributeChange`/`PreAttributeBaseChange` clamp(Health 0~Max, MaxHealth≥1, 리뷰 #7 선반영) + `PostAttributeChange`에서 Health 0 도달 시 `OnOutOfHealth` 1회 브로드캐스트 → `AFPSRCharacter::HandleOutOfHealth`(현재 로그만; **완전 DBNO/리스폰=P5**).
- **i-frame(피격 무적, 2026-06-01 추가)**: 피격 수용 후 `DamageInvulnerabilityDuration=0.25s`(EditDefaultsOnly, 밸런스 튜닝) 동안 추가 `ApplyContactDamage` 무시(`LastDamagedTime` 게이트, 서버권위·플레이어당). 스웜 동시타격 한 프레임 멜팅 방지(공격토큰과 별개 2차 방어). ※ 물리 충돌 자체는 유지(피해만 무시) — 물리 통과까지 원하면 후속.

**⏳ C1 PIE 확인**: `FPSR.EnemyTarget 50` + 적에게 둘러싸이면 **체력 감소**(서버), 다수에 둘러싸여도 토큰으로 동시 피해 제한, 0 도달 시 로그(`[Player] ... reached 0 health`). 체력은 0~Max clamp.
- **디버그 표시(2026-06-01)**: 로컬 플레이어 화면에 `HP: x / y`(녹색) / 사망 시 `DEAD (HP 0/y)`(적색) — Ammo 표시처럼 `AFPSRCharacter::Tick`에서 `GEngine->AddOnScreenDebugMessage`, **`#if ENABLE_DRAW_DEBUG` 게이트(틱 자체도 디버그 빌드에서만 활성**, 쉬핑 오버헤드 0). HUD는 P3 전환 대상(§8).

### ✅ P2-B2 코드 완료 (2026-06-01, 빌드+스모크 통과) — Flow-Field 그리드 + separation
- **신규 `UFPSRFlowFieldSubsystem`(UWorldSubsystem, 서버권위)**: 고정맵 2D 그리드(`CellSize=200`, `HalfExtent=10000`→100×100, 원점 중심), 타이머(`FlowUpdateInterval=0.2s`)로 **다중소스 BFS**(생존 플레이어=소스, 4-연결) 적분필드 → 8-이웃 최급강하로 셀별 흐름방향. `SampleFlowDirection(Loc)` O(1)(범위밖/미준비=Zero→호출측 직접방향 폴백). 장애물 없으면 ≈ 최근접 플레이어 방향이지만 적별 탐색을 그리드 1회로 분할상환 + 장애물 토대.
- **이동 통합(서브시스템)**: `TickServerMovement`가 타겟→**이동방향** 파라미터로 변경. 패스마다 균일 그리드 공간해시(셀=`SeparationRadius=120`) 구축 → 적별 flow방향 + **separation**(3×3 이웃 반발, 거리 감쇠, `SeparationStrength=1.5`) 합성. 정지거리 내에선 flow 0(플레이어에 스택 방지, separation만으로 분산). ±10% 속도편차(P2-A)와 합쳐 유기적 스웜.
- **스폰 지면 보정(버그픽스)**: `AcquireEnemy`가 `SnapToGround`로 후보 XY 아래 **WorldStatic 라인트레이스**(Pawn 무시) → 바닥+캡슐반고(90)에 생성. 점프 중 공중 스폰 해소(디렉터·버스트 공통). **알려진 한계(보류)**: 적은 CMC 없이 XY 수동 이동이라 **중력/지형추적 없음** → 경사·낙하 미대응. NavMesh/이동 정교화는 후속(§5-2).

### ✅ P2-B1 코드 완료 (2026-06-01, 빌드+스모크 통과) — 중앙 배치 이동 + Significance 거리 LOD
per-actor naive chase Tick 폐지 → `UFPSREnemySpawnSubsystem`이 `FTickableGameObject`로 배치 이동 구동(서버권위). 리뷰 #6(적 tick/이동 비용) 해소.
- **`AFPSREnemyBase`**: `PrimaryActorTick.bCanEverTick=false`, `Tick`/`FindNearestPlayer` 제거 → 신규 `TickServerMovement(Target, ScaledDelta)`(서버 가드 + 기존 chase 수식). Activate/Deactivate의 `SetActorTickEnabled` 제거(ActiveEnemies 멤버십이 갱신 게이트).
- **서브시스템(FTickableGameObject)**: `Tick`→`TickEnemyMovement`. 플레이어 위치 1회 캐시 → 적별 최근접(2D) → **거리 티어 LOD**: S0(<1500) stride1/30Hz, S1(<3500) stride2/10Hz, S2(<6000) stride4/5Hz, S3 stride8/2Hz(§5 표). `SetNetUpdateFrequency`(UE5.7 API) 티어별 적용 + `(frame+UniqueID)%stride`로 throttle 분산 + `DeltaTime*stride` 보정 이동. (엔진 `UTickableWorldSubsystem` 관용구 참조.)

**⏳ B1 PIE 확인**: `FPSR.EnemyTarget 200`→근거리 적 부드럽게 추격, 원거리 적은 갱신 throttle(끊겨 보여도 정상), 처치/재활용 정상. 300+에서 hitch 감소.

### ✅ P2-A 코드 완료 (2026-06-01, 빌드+스모크 통과) — 적 대량화 기반
Object Pooling + SpawnDirector + 개체별 이속 ±10% 편차. (Flow-Field·Significance=P2-B, 근접데미지·공격토큰·대시=P2-C.)
- **신규 `UFPSREnemySpawnSubsystem`(UWorldSubsystem, 서버권위)** — `Public/Enemy/` + `Private/Enemy/`:
  - 풀=**free-list 분리**: `DormantPool`(휴면 재사용) + `ActiveEnemies`(TSet) → Acquire/Release/AliveCount **O(1)**. `AcquireEnemy(Loc)`/`ReleaseEnemy(e)`/`GetAliveCount()`/`SetTargetAliveCount(N)`.
  - 디렉터: `OnWorldBeginPlay`에서 반복 타이머(`SpawnInterval=0.1s`) 시작 → `Alive<Target`이면 플레이어 주변 링(`Inner 1200~Outer 1500`)에 Acquire. **하드캡 `MaxActiveEnemies=500`**(Game.MD §5) + **버스트당 `MaxSpawnPerTick=10`**(히치 방지). 권위 가드 `GetNetMode()!=NM_Client`, `ShouldCreateSubsystem`=게임월드만.
  - 적 클래스 = `AFPSREnemyBase::StaticClass()`(cube placeholder; 데이터드리븐 로스터는 후속).
- **`AFPSREnemyBase`**: `Activate(Loc)`(Hidden=false/Collision=on/Tick=on/`FlushNetDormancy` + `HealthComponent->ResetForReuse()` + `CurrentMoveSpeed=MoveSpeed*FRandRange(0.9,1.1)`) / `Deactivate()`(역 + `SetNetDormancy(DORM_DormantAll)` → 휴면 적 복제비용 절감, §5 선반영). `HandleDeath`→`Destroy()` 대신 서브시스템 `ReleaseEnemy(this)`(폴백 Destroy). Tick `MoveSpeed`→`CurrentMoveSpeed`.
- **`UFPSREnemyHealthComponent::ResetForReuse()`**: 서버 가드 + `Health=MaxHealth; bDead=false` + Push Model 복제 더티.
- **콘솔(서브시스템 cpp로 집약)**: `FPSR.SpawnEnemies [N]`=버스트 Acquire(호환), 신규 `FPSR.EnemyTarget [N]`=디렉터 목표수 지속(0=중단).

**⏳ PIE 확인 대기**: `FPSR.EnemyTarget 100`→~100 유지·처치 시 풀 재활용(`obj list class=FPSREnemyBase` 안정)·이속 편차로 분산 / `EnemyTarget 0` 중단 / `FPSR.SpawnEnemies 20` 버스트. (상세 플랜: `~/.claude/plans/curried-painting-quill.md`)

### ▶ 다음: P2-C2 (충돌무시 대시, §2-13)
C1(적 근접데미지+공격토큰+플레이어 clamp) 완료. C2 = 입력 `IA_Dash`→로컬→`ServerDash` RPC(서버권위) → 짧은 런치 + 대시 창 동안 캡슐 `ECC_Pawn` 응답=Ignore(적·아군 통과) + 쿨다운 타임스탬프. **사용자 작업**: `IA_Dash`(Bool) 생성+IMC 매핑+`BP_FPSRCharacter` `DashAction` 할당. C2 후 **phase/p2-enemy-mass → main `--no-ff` 머지**(§6-7). 후속: NavMesh/동적장애물, 데이터드리븐 적 로스터, 사망/스폰 FX, SignificanceManager 플러그인 평가, 데미지 GE(실행계산)=P3/P4.

## 🔴 이전 완료 이력 (P1/P1.5)

**1) `Game.MD` 작성 + 문서 통합 — ✅ 완료 (2026-05-30)**
   - `Game.MD`를 단일 SSOT 본문으로 작성(기획·설계·기술스택·코드구조·규칙·구현현황·로드맵·디버그인벤토리).
   - `DESIGN.md`·`AI_DESIGN_HANDOFF.md` → 내용 흡수 후 **stub**("Game.MD로 이전됨").
   - `CLAUDE.md`·`AGENTS.md` → **포인터 축소**(Game.MD·PROGRESS.md 읽기 + 절대금지 3원칙).
   - `PROGRESS.md` 분리 유지. → **AI가 읽는 본문 = Game.MD + PROGRESS.md 2개.**
**2) 리뷰 루프 1회차 — ✅ 완료 (2026-05-30)** — `GameConfirm.md`(외부 AI 작성) 9개 변경안 검토 → 전부 장르 방향성 부합·안티편향 위반 없음 확인 → **Game.MD에 반영 + 로드맵 재구성 완료**. 사용자 결정 2건 반영: ① 초반 페이싱=오프닝 카드 시드, ② 미션 모디파이어 해금을 정비시간 카드로 선택.
   - **반영 요약**: 프리즈 폐지→레벨업 스택+정비시간(§2-2) / 무한 예비탄약(§2-4-2) / 무기 버림·교체 없음(§2-4) / 아군 오사 10%+호스트 토글(§2-10) / 적 이속 ±10% 편차·원거리 투사체 규격·공격토큰(§2-6) / 수동부활 DBNO·충돌무시 대시(§2-13) / 게임필·위협 인디케이터(§2-14). 로드맵 P1.5~P7 재구성(§7-3).
   - **다음 리뷰 회차**: 새 `GameConfirm.md`가 오면 동일 절차로 비교·반영.
   - **코드 리뷰 게이트(신규)**: `Scripts/codex-review.ps1` → Codex(gpt-5.5) 비대화 diff 리뷰(read-only). 결과 `Docs/reviews/`(gitignore). 상세 Game.MD §6-6 / §10.

**3) 반동 오버홀 — ✅ 코드 완료 (2026-06-01, 빌드+스모크 통과)** — 최신 FPS(Apex/COD/CS2) 조사 후 재설계. `UFPSRWeaponFireComponent`:
   - **오버슈트 버그 수정**: 누적반동을 "복구 빚(`RecoilDebtPitch`)" 모델로 전환 + 플레이어 하향 입력이 빚을 상쇄(`NotifyPlayerPitchCompensation`, `Input_Look` 훅) → 연사 중 수동 보정 시 종료 후 조준점 아래로 강제 하강 없음.
   - **부드러운 상승**: 발사당 즉발 스냅 → `RecoilRiseRate` 보간(snappy rise).
   - **수평 패턴화**: 순수 랜덤 → 발사인덱스 sin 패턴 + 소량 variance(`RecoilHorizontalPatternFreq`/`RecoilHorizontalVariance`).
   - **회복 토글 `ERecoilRecovery`(Auto/Always/Never, 기본 Auto)**: Auto=단발(Single)만 자동복구·연사/점사는 회복없음(직접 내림). Always/Never=무기 해금용 오버라이드. → 기존 DA_Weapon_Rifle(FullAuto)은 자동으로 회복없음(설정 불필요).
   - **디버그 툴 `FPSR.RecoilPreview [발수]`**: 카메라 앞에 스프레이 패턴 점/선 표시. 런타임과 동일한 `ComputeShotRecoilDelta` 사용(일치 보장).
   - **사용자 작업(선택)**: DA_Weapon_Rifle 강도 완화 — `RecoilVertical 1.0→0.45`, `RecoilHorizontal 0.3→0.12` 권장(에디터에서, `FPSR.RecoilPreview`로 확인하며).

**4) P1.5-B (1/2) 탄약/재장전 — ✅ 코드 완료 (2026-06-01, 빌드+스모크 통과)** — 탄창(MagSize)+재장전(R, 서버 타이머), **예비탄 무한**(재장전=항상 풀충전). Game.MD §2-4-2.
   - **탄약 상태=서버권위**: `UFPSRWeaponInventoryComponent`에 `SlotAmmo[]`/`bReloading`(Push Model 복제), `AddWeapon` 시 MagSize 초기화, 무기 전환 시 재장전 취소. 신규 스탯 `ReloadTime`(기본 1.5).
   - **소비=발사 GA 서버 경로**: `GA_WeaponFire_Hitscan`에서 빈탄창/재장전 중 발사 차단 + 서버 `ConsumeAmmo(1)`.
   - **게이팅=오너 클라**: `FireComponent.CanFire()`(복제값 기반)로 빈탄창/재장전 중 발사 차단. 디버그 화면 탄약 표시(`#if ENABLE_DRAW_DEBUG`, HUD는 P3).
   - **자동 재장전(2026-06-01 추가)**: 연사 중 탄 소진 시 오너 클라가 `RequestReload`→`ServerReload` 1회 요청(`bReloadRequestPending` 가드), 완료 후 계속 누르면 자동 재발사. R 수동도 유지.
   - **재장착 재리로드(2026-06-01 추가, 규칙 수정)**: 리로드 중 무기 전환 시 취소 + 떠난 슬롯 기억(`PendingReloadSlot`) → 재장착 시 **탄약 0일 때만** 리로드 재개. 탄약 남아있으면 취소 유지(부분 탄창 유지).
   - **근접무기 처리(2026-06-01 추가)**: Archetype==Melee 기준 — **화면 반동 없음**(recoil/bloom 스킵) + **탄약 개념/디버그 표시 없음**(히트스캔만 소비) + 신규 스탯 `MeleeAttackDelay`(공격 간 딜레이, 누르고 있으면 그 간격으로 반복·빠른 클릭도 제한). **사용자 확인 필요: DA_Weapon_Knife Archetype = Melee.**
   - **입력**: `IA_Reload`(R)→`ServerReload` RPC→`StartReload`. **C++ 슬롯/바인딩만 구현, 에셋은 사용자 직접.**
   - **사용자 작업**: `IA_Reload`(Bool) 생성 → IMC_Default에 R 매핑(수동) → `BP_FPSRCharacter`의 `ReloadAction` 할당. (`DA_Weapon_Rifle` ReloadTime 1.5/MagSize 30 확인)

**5) P1.5-B (2/2) ADS + 반동 ADS의존 재설계 — ✅ 코드 완료 (2026-06-01, 빌드+스모크 통과)**
   - **ADS(우클릭 hold)**: FOV 줌(`FireComponent`가 카메라 캐시·`FInterpTo`) + 확산 배율(GA, 서버 포함) + 입력 로컬+`ServerSetAiming` RPC. 무기별 `bHasADS`(근접/무ADS=false). 신규 스탯 `ADSFieldOfView/ADSSpreadMultiplier/ADSInterpSpeed`.
   - **반동 ADS의존 재설계(레퍼런스 Apex AR/R99)**: 힙=수직 약(`HipVerticalScale`)+수평 랜덤 강(`HipHorizontalRandom`)→산탄 / ADS=수직 강(`ADSVerticalScale`)+랜덤 약(`ADSHorizontalRandom`)→학습가능 climbing 패턴. 산포 주동력=확산(힙 넓게/ADS `ADSSpreadMultiplier` 좁게). 기존 `RecoilHorizontalVariance`·`ADSRecoilMultiplier` 제거. `FPSR.RecoilPreview`는 ADS climb 표시. **수평 반동도 수직처럼 보간(`PendingRiseYaw`)해 즉발 jitter(화면 흔들림) 제거** (2026-06-01).
   - **사용자 작업**: `IA_ADS`(Bool) 생성→IMC 우클릭 매핑→`BP_FPSRCharacter` `ADSAction` 할당. `DA_Weapon_Rifle` `bHasADS=true`+반동/확산 튜닝(`FPSR.RecoilPreview`로 확인). `DA_Weapon_Knife` `bHasADS=false`.

**6) P1 코드리뷰 하드닝 — ✅ 코드 완료 (2026-06-01, 빌드+스모크 통과)** — 외부 리뷰(`Docs/reviews/phase1-code-validation-2026-06-01.md`)를 독립 검증 후 **이 프로젝트(협동 PvE·리슨서버)에 필요한 항목만** 반영. 브랜치 `phase/p1-review-hardening`.
   - **#5 디버그 콘솔 shipping 가드**(§6-2/§8 명시 요구): `FPSR.RecoilPreview`(FireComponent)·`FPSR.SpawnEnemies`(EnemyBase)를 `#if !UE_BUILD_SHIPPING`로 래핑.
   - **#1/#2 서버 발사/근접 cadence 검증**(§2-10 "P1부터 서버 권위"): `UFPSRWeaponInventoryComponent::ServerTryConsumeFireInterval(MinInterval)`(서버 전용 `ServerNextAllowedFireTime`, 지터 허용 0.25×, 장착 시 리셋). Hitscan GA=`1/FireRate` 게이트, Melee GA=`MeleeAttackDelay` 게이트(둘 다 ammo/reload 체크 뒤·소비 전). 클라(예측)는 항상 통과.
   - **보류(독립 검증상 현 단계 불필요)**: #3 서버 bloom 상태 괴리·#4 산포 RNG 클라/서버 독립 → **P5**(협동 2-client 구축·검증 시; 데미지는 이미 서버 단일권위). #7 AttributeSet clamp → **P3**(현재 속성 수정 GE=0개, 카드 도입 시 본격화). #6 적 tick/이동/복제 비용 → **P2**(진행 중).

**in-flight(병행/이후):** 사용자 BP 3종 생성 완료(BP_FPSRGameMode/BP_FPSRPlayer/BP_FPSRPC) → IA_Reload/IA_ADS 셋업 + DataAsset 튜닝 → PIE 확인 → P1 마무리·머지
**git:** 사용자 콘텐츠(L_Sandbox 맵, DA_Weapon_Rifle/Knife @ `Content/Weapons/DataTable/`)는 디스크 존재·**미커밋**(untracked)
- **브랜치 워크플로 도입(2026-05-30, Game.MD §6-7)**: 각 P 단계는 `main`→`phase/<단계>-<키워드>` 분기 → 검증 후 `--no-ff` 머지. phase 브랜치도 origin push.
- **현재 활성 브랜치 = `phase/p1.5-b-ammo-reload`** (P1.5-B 작업용, main에서 분기). P1.5-B 코드 착수는 PIE 테스트 통과 후.

## 완료 (커밋·빌드 검증됨)
- **P0** 스캐폴드 / **P1-0** 코어 / **P1-1** GAS 글로벌 속성 / **P1-2** EnhancedInput(이동·시점·점프 PIE 확인) / **P1-3** 1인칭 카메라+Separated Arms
- **P1-4** 무기 기반 — `Weapon/`(Types/DataAsset/InventoryComponent): 3슬롯 서버권위, Push Model, 장착 시 발사 GA 부여
- **P1-5** 발사 GA — `AbilitySystem/Abilities/`(FPSRGameplayAbility 베이스 + GA_WeaponFire_Hitscan): 카메라 히트스캔 + 디버그 라인 + 크리티컬 + 적 데미지
- **P1-6** 근접 GA — GA_WeaponMelee: 전방 구체 오버랩 다중 타격
- **P1-7** 적 — `Enemy/`(FPSREnemyBase 경량 Pawn + FPSREnemyHealthComponent): 최근접 추격, 엔진 큐브 placeholder, 데미지 브릿지(GAS 계산→HealthComponent.ApplyDamage), 콘솔커맨드 `FPSR.SpawnEnemies [N]`
- **통합**: Character에 인벤토리 부착 + 기본무기 지급(서버) + IA_Fire(클릭당 1발)/IA_EquipSlot1~3(서버 RPC) 배선
- **빌드 성공 + 헤드리스 부팅·스모크 통과** (Fatal 0). 무기 DataAsset 미존재 에러는 예상된 것(아래 사용자 작업)

## ⏳ PIE 테스트 대기 (사용자 확인 필요 항목)
- 좌클릭 사격 → 노란 디버그 라인 + 적 처치 / 근접(칼 장착) → 청색 구체 + 처치 / 1·2 무기 전환 / `FPSR.SpawnEnemies 5` 적 스폰·추격
- **탄약/재장전**(IA_Reload 셋업 후): R → 1.5초 후 30 복구·재발사 / 재장전 중 발사 차단 / **연사로 탄 소진 시 자동 재장전→완료 후 자동 재발사**(누르고 있을 때) / **재장전 중 무기전환 시 취소 + 그 무기 재장착 시 자동 재리로드**
- **반동**: 연사 중 마우스 내려 보정 → 종료 후 화면 강제 하강 없음 / 풀오토는 손 떼도 자동 복구 안 함 / `FPSR.RecoilPreview 30` 패턴 표시
- **ADS**(IA_ADS 셋업 후): 라이플 우클릭 → FOV 줌(부드럽게)·떼면 복귀 / 나이프는 변화 없음
- **반동 ADS의존**: 힙파이어=탄착 산탄형·화면 안 올라감 / ADS=위로 climbing 라인(흩어짐 적음)

## 사용자 대기 작업 (PIE 테스트 전)
- ✅ L_Sandbox 맵 / 무기 DataAsset 2개(현재 `Weapons/DataTable/`) — 생성됨
- **BP 3종 생성 + 참조 할당** (프로덕션 BP 참조 패턴 — C++ 경로 하드코딩 제거됨):
  - `BP_FPSRGameMode` (**반드시 `/Game/Core/`**, 부모 `FPSRGameMode`): DefaultPawnClass=`BP_FPSRCharacter`, PlayerControllerClass=`BP_FPSRPlayerController`
  - `BP_FPSRCharacter` (부모 `FPSRCharacter`): IA 8개 + DefaultPrimary/SecondaryWeapon(DA_Weapon_Rifle/Knife) 할당
  - `BP_FPSRPlayerController` (부모 `FPSRPlayerController`): DefaultMappingContext=`IMC_Default`
  - 무기 DataAsset은 위치 무관(BP 하드참조). FireMode: Rifle=FullAuto / Knife=Single·무반동

## 다음 단계
- ✅ **P1 + P1.5(A 사격코어 / B 탄약·재장전·ADS·반동) 완료** — 사용자 PIE 확인 + DataAsset 튜닝 완료 (2026-06-01). `phase/p1.5-b-ammo-reload` → `main` `--no-ff` 머지.
- **▶ 다음: P2** — SpawnDirector + Flow-Field + Pooling + Significance (적 300+ 안정) + **적 이속 ±10% 편차** + **적 근접 데미지·공격토큰 baseline** + **충돌무시 대시**. Game.MD §5·§7-3. → main에서 `phase/p2-<키워드>` 분기로 시작.
- **P3**: 공유XP + **레벨업 스택(프리즈 폐지)** + **정비시간 RunPhase** + **오프닝 카드 시드** + Card UI + 동적풀 + Rarity + 리롤 (정비시간 트리거는 P3 디버그→P4 미션 연동)

## 빌드 / 검증 방법
- 빌드(에디터 닫고): `"D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\Build.bat" FPSRogueliteEditor Win64 Development -Project="E:\Git_Project\FPSRoguelite\FPSRoguelite.uproject" -WaitMutex`
- 헤드리스 검증: `UnrealEditor-Cmd.exe <uproject> -unattended -nopause -nullrhi -nosplash -nosound -ExecCmds="Automation RunTests FPSRoguelite.Smoke.ModuleLoads" -TestExit="Automation Test Queue Empty" -abslog=...`
- 새 UCLASS 다수면 Live Coding 불가 → 풀빌드(에디터 닫아야 함). 입력 IA 생성은 `Scripts/gen_input_assets.py`

## 확정 사항 / 주의점
- 무기 교체 = 숫자키 **1/2/3** (`IA_EquipSlot1~3`) / 사격=좌클릭(클릭당 1발; full-auto 연사 cadence는 후속)
- **UE5.7 IMC 매핑은 Python `set_editor_property` 미반영 → 에디터 수동** (IA 에셋 생성은 Python OK)
- 카드선택 = **정비시간(Breather)에 무제한 대기** — 교전 중 프리즈 폐지, 미션 클리어 안전구간 소비 + 오프닝 시드 1~2장(Game.MD §2-2)
- 잔여 로그: PlayerController `[Input] Added DefaultMappingContext`(Warning, 1회성) — 다음 빌드 때 Verbose로 다운그레이드
- CommonUI `LogUIActionRouter` 에러 → P3에서 `CommonGameViewportClient` 설정 시 해결(현재 무해)
- **MCP(unreal) 인증 실패로 미사용** → UBT 빌드 + 헤드리스 자동화로 검증
- 모델 정책: 구현=Haiku 위임 / 검증(빌드·diff·스모크)=메인(Opus) 직접
- **프로덕션 방식 원칙**(CLAUDE.md/AGENTS.md): 콘텐츠는 BP/DataAsset/config 바인딩, C++ 경로 하드코딩 금지. 디버그 스캐폴딩은 검증용·전환대상
- 디버그/플레이스홀더(프로덕션 전환 대상): 발사/근접 DrawDebug, `FPSR.SpawnEnemies` 콘솔, `FPSR.RecoilPreview` 콘솔(반동 패턴 시각화), 적 큐브 메시, FP팔/3P 메시 미할당, 적 추격=단순 스티어링(P2 Flow-Field 교체)
