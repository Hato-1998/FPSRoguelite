# PROGRESS — 진행 현황 (라이브 핸드오프 문서)

> 다른 세션/다른 AI가 **즉시 이어받기** 위한 단일 진행 현황 문서.
> **작업 단계를 끝낼 때마다, 그리고 중단 전 반드시 이 파일을 갱신하고 커밋한다.**
> 확정 설계·기획·코드구조·규칙은 `Game.MD`(SSOT), 상세 이력은 `git log --oneline`.

**최종 갱신: 2026-06-01**

## 한 줄 요약
**P1+P1.5 완료 + P1 리뷰 하드닝(main 머지) + P2-A·P2-B1 코드 완료**(풀링/디렉터/이속편차 + 중앙배치이동/Significance LOD). 빌드+스모크 통과. **PIE 확인 대기**. → **다음: P2-B2**(Flow-Field 그리드 + separation).

## ▶▶ 새 세션 우선 작업 = P2-A/B1 PIE 확인 → P2-B2 착수
**브랜치**: `phase/p2-enemy-mass` (활성, main의 P1 하드닝 머지 반영됨). 구현=Haiku 위임 / 검증=Opus.

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

### ▶ 다음: P2-B2 (Flow-Field 그리드 + separation)
B1에서 배치이동+거리LOD 완료. B2 = `UFPSRFlowFieldSubsystem`(고정맵 2D 그리드 + 다중소스 BFS, 적이 셀 방향 O(1) 샘플) + 균일 그리드 공간해시 기반 separation → 유기적 스웜·장애물 토대. 현재 타겟팅은 직접 최근접(B1) → B2에서 그리드 샘플로 교체. 이후 **P2-C**(근접 데미지 + 공격토큰 + 충돌무시 대시).

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
