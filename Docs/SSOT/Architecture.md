# Architecture — 기술 스택 / 프로그래밍 구조 (FPSRoguelite SSOT 분할)

> `Game.md`(SSOT 허브)의 분할 문서. **섹션 번호(§x)는 원본 그대로 보존** — 소스 주석·교차참조 호환.
> 작업 시작 전 허브 `Game.md` + `PROGRESS.md`를 먼저 읽고, 신규 클래스·모듈·폴더 구조·기술 채택 결정 관련 작업 시 본 파일을 연다. 성능 예산은 `Performance.md`(§5), 작업 규칙은 `Workflow.md`(§6).
> 담는 섹션: §3 확정 기술 스택 / §4 프로그래밍 구조(§4-1 목표 구조, §4-2 실제 클래스맵, §4-3 서버 RPC 규약).

---

## 3. 확정 기술 스택

| 레이어 | 채택 |
|---|---|
| 베이스 | 경량 커스텀 C++ 모듈 + 엔진 플러그인 체리픽 |
| 플레이어 | GAS · EnhancedInput · CommonUI · Push Model |
| 적 스웜 | 경량 풀액터 + Flow-Field(길찾기) + 스티어링(AI) + Significance + 인스턴싱 · 체력 = 경량 `UHealthComponent` + 비-GE 데미지 |
| 보스/엘리트 | 일반 Actor + StateTree (+GAS) |
| 메시징 | GameplayMessageSubsystem (경량 재구현) |
| UI | CommonUI + Activatable Widget Stack |
| 저장 | SaveGame |
| 네트워크 | 리슨서버 P2P, Push Model |
| 무브먼트 | 표준 CMC + **충돌무시 대시(회피기)** |
| 레벨 | 고정 authored 맵 · **다중맵 심리스**(문 파괴→인접맵 스트림-in, §2-1 · 피벗 2026-07-03) · **U 연속필드**(고정 3×3 단일 flow 그리드, 2026-07-07 `Docs/Review/20260707-plan-continuous-field-arch.md`) · 스트리밍 = LoadStreamLevel / WP Data Layer |

- **엔진 포함 플러그인(바로 enable)**: GameplayAbilities, EnhancedInput, ModularGameplay, GameFeatures, CommonUI, StateTree, GameplayStateTree, SignificanceManager, Iris(off)
- **엔진에 없는 플러그인(P3+ 필요 시 경량 재구현)**: CommonUser, CommonGame, ModularGameplayActors, GameplayMessageRouter
- **Build.cs 의존성**: Phase별 실제 사용 시점에 추가(CommonUI/CommonInput/StateTreeModule/GameplayStateTreeModule/SignificanceManager, 필요 시 ReplicationGraph). 미사용 모듈 선등록 지양

---

## 4. 프로그래밍 구조

### 4-1. 목표 폴더 / 모듈 구조 (설계 전체)
```
Source/FPSRoguelite/Public/
├── Core/             GameMode, GameState, PlayerController, PlayerState, 로그/Assert 매크로
├── Hero/             UHeroDataAsset, AFPSRCharacter (Separated Arms)
├── AbilitySystem/    ASC, AttributeSets(글로벌), GA, GE, Cue, DamageCalc(브릿지)
├── Weapon/           Archetype 7종, WeaponInstance, StatBlock, ModifierFragment
├── Card/             CardDataAsset(폴리모픽 Instanced `Effects[]`·`ECardGroup`, v2 U18), CardPool, DrawSystem(서버권위 인덱스선택), Reroll, `UFPSRCardEffect` 서브클래스(CharacterGE/CharacterPassive/WeaponStat/WeaponBehavior/GrantWeapon)
├── Enemy/            경량 풀액터, HealthComponent, AttackType, FlowField, ScalingProfile
├── Boss/             ABossBase, BossDefinition, StateTree
├── Run/              SpawnDirector, **RunDirector(`UFPSRRunDirectorSubsystem`=런클럭/스폰강도/시간 미션 스케줄/보스타임, 서버 전용)**, RunSchedule(`UFPSRRunScheduleDataAsset`), **Mission/**(`AFPSRMissionActor` 베이스+`UFPSRMissionDataAsset`+`AFPSRMissionSpawnPoint`+서브클래스, P4-A)  ※ 공유XP/레벨/RunPhase/**bRunPaused** = **복제 필요 → `AFPSRGameState` 호스팅**(Push Model, 서버 권위). WorldSubsystem은 복제 불가라 런 상태는 GameState에 둔다(P3-A 확정 2026-06-01). **레벨업 보류 픽은 플레이어별이라 `AFPSRPlayerState::CardPicksPending`(+미션보상 `MissionRewardPicksPending`)에 둔다**(§2-2). 레벨업/미션클리어 시 `bRunPaused`로 전역 프리즈(§2-2, 재설계 2026-06-04).
├── Pickup/           범용 PickupActor + PickupDefinition(자석)
├── MetaProgression/  SaveGame, UpgradeTree, Subsystem
├── Performance/      ActorPool, SignificanceConfig, 인스턴싱
├── Messages/         GMS Payload structs
├── Settings/         **UFPSRGameUserSettings**(UGameUserSettings 서브클래스=로컬설정 영속전담, MasterVolume·CrosshairScale+`OnCrosshairSettingsChanged` 델리게이트) + **UFPSRAudioSettings**(UDeveloperSettings, SoundMix/SoundClass soft ref=에셋경로 데이터드리븐)
├── Audio/            **UFPSRAudioSubsystem**(UWorldSubsystem, OnWorldBeginPlay 마스터볼륨 재적용=SetSoundMixClassOverride+PushSoundMixModifier; 콘솔 FPSR.SetMasterVolume)
└── UI/               HUD(공유XP바/하단 무기바), CardSelect, MissionUI, MetaUI, **FPSRSettingsWidget**(CommonActivatableWidget 공용 설정 오버레이=메뉴 push/인게임 논-포즈 GameMenu push)
```
- 사운드 설정(마스터 볼륨, MVP): 영속=`UFPSRGameUserSettings`(GameUserSettings.ini), 적용=`UFPSRAudioSubsystem`(SoundClass+SoundMix 표준), 라우팅 에셋=`UFPSRAudioSettings` soft ref(DefaultGame.ini). 확장(SFX/Music/UI)=자식 SoundClass+필드 추가(중앙 0수정).
- 크로스헤어 크기 설정(U17): 영속=`UFPSRGameUserSettings.CrosshairScale`(GameUserSettings.ini, Clamp 0.5~2.5, 기본 1.0), 조절=`FPSRSettingsWidget` 슬라이더(**GUS 직접**·크로스헤어 서브시스템 無 — 오디오와 달리 world-scoped 적용 없음), 실시간 반영=`UFPSRRunHUDWidget`이 `OnCrosshairSettingsChanged` 구독→`CrosshairImage` RenderScale. **비대칭 근거=소비자(HUD)가 라이브**라 델리게이트 필요(볼륨은 즉시 적용이라 불요). 고아 `WBP_BasicCrosshair`(V3 잔재)는 미사용.
- 글로벌 스탯(Luck, GlobalCrit, CritMult, MoveSpeed, MaxHealth, HealthRegen, PickupRadius, XPGain) → Character ASC AttributeSet
  - ※ Luck = 광역 행운(카드 등급 가중 + 향후 드랍품질·희귀스폰 등). RarityBonus는 Luck으로 통합·폐지(2026-06-02). PickupRadius·XPGain·MoveSpeed·HealthRegen은 미구현(필요 단계에서 추가)
- 무기별 스탯 → WeaponInstance 스탯 블록 (ASC 아님)
- 하단 무기바 HUD: 가시성을 HUD State(GMS/Tag)에 바인딩 → ADS/카드UI/미션UI 시 숨김
- **다중맵(#3) — U 연속필드 단일 그리드 (구현 완료·`--no-ff` main 머지 `34b5eea`, 설계 `Docs/Review/20260707-plan-continuous-field-arch.md`)**: 단일 `UFPSREnemySpawnSubsystem` + **FrontId occupancy allocator**(전역 공유 예산·전선별 배분·"2인+ 전선 > 솔로" 가중, `RunFlow §2-1`), 길찾기 = **고정 3×3 world extent 프리사이즈 단일 `UFPSRFlowFieldComputer`**(맵 stream-in 시 그 슬롯 셀 구간만 shared grid에 증분 atomic bake=경계 양쪽 edge 클리어; 미로드 슬롯=blocked). **문 부수면 door cells `blocked→open` stamp**(문 leaf=`ECC_FPSRPlayerPawn`·blocker=`WorldDynamic`이라 `WorldStatic` bake 미포집→명시 stamp) **+ 단일 `RunBFS` + generation bump** → 적이 O(1) 샘플로 열린 문 넘어 seamless 추격. **origin-aware connectivity 전투게이트**(`FPSRCombatStatics::CanAffectTarget`=instigator 원점셀↔타겟셀 open-grid 연결, explosion=`Center` 원점). late-join = `GameState.TopologyGeneration` ack + freeze pre-unfreeze RunBFS + reset baseline. Tier-0 복제 = 대칭 거리컬 교전버블 NetCull(Option A); 진짜 공간 relevancy=RepGraph 후속. `UFPSRRunDirectorSubsystem`은 단일·런클럭 전역(미션/스폰만 대상슬롯 파라미터화). 슬롯 100~132m/변(near-cap, D1). 재계산 예산=`Performance.md §5`.
  - **(폐기된 중간설계, 2026-07-05)**: per-map `ULevel*`-키 플로우필드 레지스트리 + map-aware allocator는 심리스 연속 추격과 충돌해 U로 피벗·제거(`Computers`/`EvictMap`·전환추적자·enemy MapId sync·combat MapId gate 삭제 `abd9624`; ⚠️`BakeDiscoveredMap`은 삭제 아님 — U slot bake 진입점으로 존치·재용도). 원 설계 근거=`Docs/Review/20260705-multimap-budget-regroup.md`. rally pad·split 감지·양성 인센티브=Tier 2 후속.

### 4-2. 구현 클래스맵 (⚠️ P0~P1.5-A 시점 역사 스냅샷 — 현재 전체 구조는 §4-1 목표구조 + `git log`·`PROGRESS.md` 참조; 이후 Boss/·Card v2·Run/Mission/·Combat/·Pickup/·UI Menu·Lobby·Session 등 대폭 추가됨)
```
Source/FPSRoguelite/
├── FPSRoguelite.Build.cs / *.Target.cs (UE5.7, V6, Unreal5_7)
├── Core/
│   ├── AFPSRGameMode / AFPSRGameState / AFPSRPlayerController / AFPSRPlayerState
│   └── FPSRLogChannels (로그 카테고리)
├── AbilitySystem/
│   ├── UFPSRAbilitySystemComponent
│   ├── Attributes/ UFPSRHealthSet, UFPSRCombatSet  (글로벌 속성)
│   └── Abilities/
│       ├── UFPSRGameplayAbility           (GA 베이스)
│       ├── UFPSRGA_WeaponFire_Hitscan     (카메라 히트스캔 + 크리티컬 + 적 데미지 브릿지)
│       └── UFPSRGA_WeaponMelee            (전방 구체 오버랩 다중 타격)
├── Weapon/
│   ├── FPSRWeaponTypes.h  (EFPSRWeaponArchetype, EFPSRFireMode, FFPSRWeaponStatBlock)
│   ├── UFPSRWeaponDataAsset               (무기 콘텐츠 바인딩)
│   ├── UFPSRWeaponInventoryComponent      (3슬롯 서버권위, Push Model, 장착 시 발사 GA 부여)
│   └── UFPSRWeaponFireComponent           (오너클라 연사 cadence/반동/블룸, 샷마다 발사 GA 활성)
├── Enemy/
│   ├── AFPSREnemyBase                     (경량 Pawn, 최근접 추격 스티어링, placeholder 큐브)
│   └── UFPSREnemyHealthComponent          (비-GAS 체력/데미지)
├── Hero/
│   └── AFPSRCharacter                     (1P 카메라 + Separated Arms + EnhancedInput + 인벤토리/발사)
└── Tests/
    └── FPSRSmokeTest                      (모듈 로드 자동화 테스트)
```
- ASC는 `AFPSRPlayerState`가 소유, `AFPSRCharacter`가 `PossessedBy`/`OnRep_PlayerState`에서 ActorInfo 초기화
- 데미지 브릿지: 플레이어 GAS 계산 → `UFPSREnemyHealthComponent::ApplyDamage` (적 ASC 없음)

### 4-3. 서버 RPC 규약 — 실패 처분 = **조용히 거부** (`WithValidation` 미채택, 결정 2026-08-07)

**규약**: Server RPC 는 **`_Implementation` 몸통(또는 그 위임처)에서 직접 검사하고, 유효하지 않으면 아무 일도 하지 않고 반환**한다. `WithValidation` 은 쓰지 않는다. 신규 RPC 도 이 규약을 따른다.

- **필수**: 클라가 보낸 인덱스/ID 는 **서버가 들고 있는 상태와 대조**한다(`CachedOffer.IsValidIndex` · `Pool->IsValidIndex` · `Slots.IsValidIndex` · `OfferId != CurrentOfferId` 등). 클라는 **인덱스만** 보내고 실체(카드·무기)는 서버 캐시에서 꺼낸다 — 실체 주입이 구조적으로 불가능해야 한다.
- **금지**: 검사를 `_Validate` 로 올리는 것. `_Validate` 가 false 를 반환하면 **그 클라이언트의 연결이 끊긴다** — 엔진 경로 = `RPC_ValidateFailed`(`CoreNet.cpp:667`) → `ReceivedRPC` false(`DataReplication.cpp:1461`) → **`Connection->Close(ENetCloseResult::ObjectReplicatorReceivedBunchFail)`**(`DataChannel.cpp:3440`, UE 5.7 실측). 즉 걸러내기가 아니라 **강제 퇴장**이다.
- **거부 사유의 절반은 악의가 아니라 정상 레이스다** — 스테일 오퍼·더블클릭(`HandleCardSelection`·`ServerRerollOffer` 주석이 명시). `_Validate` 로 올리면 **더블클릭한 정상 플레이어가 킥**된다.
- **채택 근거(제1원리)**: 4인 협동이라 치터가 망치는 건 자기 파티뿐이고 랭킹·경제·PvP 가 없다 → 오탐 킥의 손실 > 억지력. 이미 12개 전부 안전하게 거부되므로 `WithValidation` 이 더하는 건 보호가 아니라 **처벌뿐**이다. **경쟁 요소(랭킹/매치메이킹)가 들어오면 이 규약을 재검토**한다 — 그때는 결론이 뒤집힌다.
- **스팸은 별개 계층** — 유효한 값을 대량 전송하는 공격은 `_Validate` 를 전부 통과한다. 엔진 전용 계층 `RPCDoSDetection` 소관이며 **현재 꺼져 있다**(`BaseEngine.ini:1866` `bRPCDoSDetection=false`, 프로젝트 오버라이드 없음).
- 조사 전문 = `Docs/Refactor_20260806_Report.md §7-4`. 실측 대상 = Server RPC 12개(파라미터 없음 3 / `bool` 2 / `int32` 7).
