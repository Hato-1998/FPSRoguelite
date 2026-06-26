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
| 레벨 | 고정 맵(**복수 authored, 런마다 선택** — §2-1) | ❌ PCG |

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
├── Settings/         **UFPSRGameUserSettings**(UGameUserSettings 서브클래스=로컬설정 영속전담, MasterVolume) + **UFPSRAudioSettings**(UDeveloperSettings, SoundMix/SoundClass soft ref=에셋경로 데이터드리븐)
├── Audio/            **UFPSRAudioSubsystem**(UWorldSubsystem, OnWorldBeginPlay 마스터볼륨 재적용=SetSoundMixClassOverride+PushSoundMixModifier; 콘솔 FPSR.SetMasterVolume)
└── UI/               HUD(공유XP바/하단 무기바), CardSelect, MissionUI, MetaUI, **FPSRSettingsWidget**(CommonActivatableWidget 공용 설정 오버레이=메뉴 push/인게임 논-포즈 GameMenu push)
```
- 사운드 설정(마스터 볼륨, MVP): 영속=`UFPSRGameUserSettings`(GameUserSettings.ini), 적용=`UFPSRAudioSubsystem`(SoundClass+SoundMix 표준), 라우팅 에셋=`UFPSRAudioSettings` soft ref(DefaultGame.ini). 확장(SFX/Music/UI)=자식 SoundClass+필드 추가(중앙 0수정). 설계 상세 `Docs/SoundSettings_Handoff.md`.
- 글로벌 스탯(Luck, GlobalCrit, CritMult, MoveSpeed, MaxHealth, HealthRegen, PickupRadius, XPGain) → Character ASC AttributeSet
  - ※ Luck = 광역 행운(카드 등급 가중 + 향후 드랍품질·희귀스폰 등). RarityBonus는 Luck으로 통합·폐지(2026-06-02). PickupRadius·XPGain·MoveSpeed·HealthRegen은 미구현(필요 단계에서 추가)
- 무기별 스탯 → WeaponInstance 스탯 블록 (ASC 아님)
- 하단 무기바 HUD: 가시성을 HUD State(GMS/Tag)에 바인딩 → ADS/카드UI/미션UI 시 숨김

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
