# Architecture — 기술 스택 / 프로그래밍 구조 (FPSRoguelite SSOT 분할)

> `Game.md`(SSOT 허브)의 분할 문서. **섹션 번호(§x)는 원본 그대로 보존** — 소스 주석·교차참조 호환.
> 작업 시작 전 허브 `Game.md` + `PROGRESS.md`를 먼저 읽고, 신규 클래스·모듈·폴더 구조·기술 채택 결정 관련 작업 시 본 파일을 연다. 성능 예산은 `Performance.md`(§5), 작업 규칙은 `Workflow.md`(§6).
> 담는 섹션: §3 확정 기술 스택 / §4 프로그래밍 구조(§4-1 목표 구조, §4-2 실제 클래스맵).

---

## 3. 확정 기술 스택

| 레이어 | 채택 | 비채택(편향) |
|---|---|---|
| 베이스 | 경량 커스텀 C++ 모듈 + 엔진 플러그인 체리픽 | ❌ Lyra 풀 fork |
| 플레이어 | GAS · EnhancedInput · CommonUI · Push Model | |
| 적 스웜 | 경량 풀액터 + Flow-Field + Significance + 인스턴싱 | ❌ GAS, ❌ MassEntity, ❌ 적별 StateTree/NavMesh |
| 보스/엘리트 | 일반 Actor + StateTree (+GAS) | |
| 메시징 | GameplayMessageSubsystem (경량 재구현/Lyra 복사) | |
| UI | CommonUI + Activatable Widget Stack | |
| 저장 | SaveGame | |
| 네트워크 | 리슨서버 P2P, Push Model | ❌ Iris 핵심 의존, ❌ Server-Side Rewind |
| 무브먼트 | 표준 CMC + **충돌무시 대시(회피기)** | ❌ Bhop/Wall-run/Motion Matching |
| 레벨 | 고정 authored 맵 · **다중맵 심리스**(문 파괴→인접맵 스트림-in, §2-1 · 피벗 2026-07-03) · **U 연속필드**(고정 3×3 단일 flow 그리드, 2026-07-07 `Docs/Review/20260707-plan-continuous-field-arch.md`) | ❌ PCG · ❌ WP 런타임 오픈월드 그리드(authored·bounded라 불요, 스트리밍=LoadStreamLevel/WP Data Layer) |

- **엔진 포함 플러그인(바로 enable)**: GameplayAbilities, EnhancedInput, ModularGameplay, GameFeatures, CommonUI, StateTree, GameplayStateTree, SignificanceManager, Iris(off)
- **엔진에 없는 Lyra 출신 플러그인(P3+ 필요 시 경량 재구현/복사)**: CommonUser, CommonGame, ModularGameplayActors, GameplayMessageRouter
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
- 사운드 설정(마스터 볼륨, MVP): 영속=`UFPSRGameUserSettings`(GameUserSettings.ini), 적용=`UFPSRAudioSubsystem`(SoundClass+SoundMix 표준), 라우팅 에셋=`UFPSRAudioSettings` soft ref(DefaultGame.ini). 확장(SFX/Music/UI)=자식 SoundClass+필드 추가(중앙 0수정). 설계 상세 `Docs/SoundSettings_Handoff.md`.
- 크로스헤어 크기 설정(U17): 영속=`UFPSRGameUserSettings.CrosshairScale`(GameUserSettings.ini, Clamp 0.5~2.5, 기본 1.0), 조절=`FPSRSettingsWidget` 슬라이더(**GUS 직접**·크로스헤어 서브시스템 無 — 오디오와 달리 world-scoped 적용 없음), 실시간 반영=`UFPSRRunHUDWidget`이 `OnCrosshairSettingsChanged` 구독→`CrosshairImage` RenderScale. **비대칭 근거=소비자(HUD)가 라이브**라 델리게이트 필요(볼륨은 즉시 적용이라 불요). 고아 `WBP_BasicCrosshair`(V3 잔재)는 미사용.
- 글로벌 스탯(Luck, GlobalCrit, CritMult, MoveSpeed, MaxHealth, HealthRegen, PickupRadius, XPGain) → Character ASC AttributeSet
  - ※ Luck = 광역 행운(카드 등급 가중 + 향후 드랍품질·희귀스폰 등). RarityBonus는 Luck으로 통합·폐지(2026-06-02). PickupRadius·XPGain·MoveSpeed·HealthRegen은 미구현(필요 단계에서 추가)
- 무기별 스탯 → WeaponInstance 스탯 블록 (ASC 아님)
- 하단 무기바 HUD: 가시성을 HUD State(GMS/Tag)에 바인딩 → ADS/카드UI/미션UI 시 숨김
- **다중맵(#3, 설계 수렴 2026-07-05 `Docs/Review/20260705-multimap-budget-regroup.md`)**: 단일 `UFPSREnemySpawnSubsystem` → **map-aware allocator**(전역 공유 예산·점유맵 배분·빈 맵 드레인·"2인+ 맵 > 솔로 맵" 가중), U7 플로우필드 → **per-map 레지스트리**(`ULevel*` 키). `UFPSRRunDirectorSubsystem`은 단일·런클럭 전역 유지(미션/스폰만 대상맵 파라미터화). 다중맵 점유상태 중 복제 필요분=GameState/PlayerState(WorldSubsystem 복제 불가). 레벨 스트리밍=LoadStreamLevel(서브레벨) 우선. rally pad·split 감지·양성 인센티브=Tier 2(콘텐츠/밸런스). Tier 0 실행=`Docs/MultiMap_Tier0_ResumePrompt.md`.
- **다중맵 U 대전환(#3, 2026-07-07 `/plan-consult` → `Docs/Review/20260707-plan-continuous-field-arch.md`)**: 위 per-map 레지스트리(`ULevel*` 키)가 심리스 연속 추격과 충돌 → **U(고정 3×3 world extent 프리사이즈 단일 `UFPSRFlowFieldComputer`)로 피벗**. 맵 stream-in 시 그 슬롯 셀 구간만 shared grid에 **증분 atomic bake**(staging commit), 미로드 슬롯=blocked. **문 부수면 door cells `blocked→open` stamp + 단일 `RunBFS` + generation bump** → 적이 단일 필드 O(1) 샘플로 열린 문 넘어 seamless 추격. **신규 1차요구**: subregion atomic bake(경계 양쪽 edge 클리어=ghost path 방지) · door-cell stamping(문 leaf=`ECC_FPSRPlayerPawn`·blocker=`WorldDynamic`이라 `WorldStatic` bake 미포집→명시 stamp) · **origin-aware connectivity 전투게이트**(`FPSRCombatStatics::CanAffectTarget`을 instigator 원점셀↔타겟셀 open-grid 연결로 대체, explosion=별도 `Center` 원점, **시그니처 계약변경**) · FrontId occupancy(→allocator, `RunFlow §2-1`) · TopologyGeneration(late-join ack·freeze pre-unfreeze RunBFS·reset baseline). **제거(연속성 증명 후 마지막=P-G)**: per-map 레지스트리(`Computers`/`BakeDiscoveredMap`/`EvictMap`) · movement same-map target gate · 전환 추적자 전부 · enemy MapId sync · combat MapId gate(→origin connectivity) · 맵간 갭. 구현 P-0~P-H = 리포트 §2-4. **D1 확정**: 슬롯 100~132m/변(near-cap). 재계산 예산=`Performance.md §5`.

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
