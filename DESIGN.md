# FPSRoguelite — 게임 설계 확정본 (v6, Locked)

> **이 문서는 프로젝트의 단일 진실 공급원(Single Source of Truth)입니다.**
> 설계 변경 시 이 문서를 먼저 갱신하고, 코드/에셋을 그 다음에 수정합니다.
> 최종 확정일: 2026-05-28 / 엔진: **UE 5.7** (`D:\UnrealEngine\UE_5.7`)

---

## ⚠️ 장르 정체성 — 가장 먼저 읽을 것

이 프로젝트는 **1인칭 FPS × 뱀파이어 서바이벌 × 4인 협동 로그라이트** 입니다.
레퍼런스: **The Spell Brigade (Steam)**.

**이것은 Hero Shooter(Overwatch/Valorant/Apex)가 아닙니다.**
참조한 PDF 커리큘럼(`HeroShooter_Portfolio_Curriculum.pdf`)은 PvP Hero Shooter 기준이라,
그 기술 스택(Lyra 풀 fork, 모든 적에 GAS, 적별 StateTree/NavMesh, Iris 핵심 의존,
Server-Side Rewind, Motion Matching, Bhop/Wall-run)을 **그대로 가져오면 안 됩니다.**

| | Hero Shooter (가져오면 안 됨) | **이 게임 (Survivor Swarm)** |
|---|---|---|
| 액터 수 | ~12명 | **적 수백 (~300-500)** |
| 액터당 비용 | 높아도 OK | **반드시 최소화** |
| AI | 개별 스마트 (StateTree/BT) | **단순 추격 스티어링 + Flow-Field, 배치** |
| 길찾기 | 에이전트별 NavMesh | **Flow-Field (고정맵 사전계산)** |
| GAS | 모든 액터 | **플레이어 한정** |
| 적 애니메이션 | 풀 피델리티 | **인스턴싱/VAT + LOD** |
| 리플리케이션 | 플레이어별 고정밀 | **적은 최소상태/Push Model** |

---

## 1. 핵심 게임 루프

```
로비 (메타 강화) → 30분 런 → 보스 처치/클리어 → 재화 획득 → 로비
```

- **고정 맵 / 고정 구조** (절차적 생성 PCG 사용 안 함)
- 런 시간 기본 **30분** (마지막 시간대 보스로 종료)
- 시간 경과 → 몬스터 수·종류·HP·공격력 증가 (**이동속도는 불변**)
- **1~4인 협동** (리슨서버 P2P)

## 2. XP / 레벨업 (확정)

- XP는 몬스터 사망 시 **드롭 아이템(범용 Pickup)** → 근접 시 자석 흡수 줍기
- **파티 공유 경험치 풀** — 누가 줍든 하나의 통에 누적, 모두 같은 하단 XP바 UI 공유
- 공유 풀 임계 도달 시:
  1. **전원 동시 레벨업**
  2. **전체 게임 프리즈** (TimeDilation≈0 + RunPhase 게이팅)
  3. **각 플레이어 개별 3카드 선택** (본인 화면 오버레이)
  4. **선택 안 한 플레이어를 전원 대기**
  5. 전원 완료 시 재개
- 다중 레벨업(XP 폭증) 시 순차 처리

## 3. 카드 시스템 (확정)

- 데이터 방식: **DataAsset + GameplayEffect(GE)** — 스탯 하드코딩 금지
- 새 스탯 추가 = Attribute 1 + GE 1 + DataAsset 1 (코드 변경 0)
- `UCardDataAsset`: `Scope`(Character / ThisWeapon / AllWeapons), `Rarity`, `AppliedEffect`(GE), `Weight`
- **등급 4단계** (Common/Rare/Epic/Legendary) — Luck/RarityBonus 스탯이 추첨 가중치에 작용
- **무기별 전용 카드**: 무기 보유 시 해당 무기 카드가 레벨업 카드 풀에 동적 합류 (Gunfire Reborn식)
- **리롤**: 캐릭터 메타로 해금, **게임당 3회 제한** (`RunRerollCharges`, 서버 권위 차감)

## 4. 무기 시스템 (확정)

- 최대 **3개 동시 보유** (기본 1 + 추가 2). 5렙/20렙 등에 무기 카드 등장
- **아키타입 7종**: 연사총 / 점사총 / AOE(바주카·유탄) / 근접(칼) / 차징 관통 레이저 / 단발 스나이퍼 / 샷건
- **무기별 스탯 = WeaponInstance 스탯 블록(리플리케이트 struct)**, 캐릭터 ASC와 분리
  - 무기 스탯 예: Damage, FireRate, ReloadTime, MagSize, BulletScale, ProjectileSpeed, Pierce, Spread, Range, AOERadius, ChargeTime, PelletCount
- **ADS**: 무기 DA에 `bHasADS`+FOV (스나이퍼/차징=정밀, 연사/샷건=약함, 근접=없음)

### 4-1. 무기 모디파이어 = 게임의 핵심 재미 (확정)

- 미션 보상으로 무기 동작을 **근본 변경** (2연발, 차징 무효, 아군 힐 빔 등)
- 적용 방식: **Weapon Behavior Fragment (합성형 훅) + 누적 가능**
  - `UWeaponInstance.ActiveModifiers[]`에 누적, 서로 상호작용 (예: 2연발+관통)
  - `GA_WeaponFire`(아키타입별 베이스)에 훅: `ModifyShotCount/ModifyChargeTime/OnProjectileSpawn/OnHitActor/PostFire`
  - 각 무기 DA가 `AvailableModifiers`(약 4종) 정의 → 미션 클리어 시 1종 해금
- **GA 교체 방식 금지** (조합 폭발), **거대 태그 분기 금지** (유지보수 지옥)

## 5. 시작 캐릭터 (확정)

- 3종: **연사총 / 점사총 / 근접칼**
- `UHeroDataAsset` (PawnData 개념) Base 구조 — DefaultWeapon, BaseAttribute, PassiveAbilitySet, FP팔/3P바디 메시
- 추가 용이하게 데이터 드리븐. 고유 패시브가 무거워지면 그때만 GameFeature 플러그인으로 승격

## 6. 몬스터 (확정)

- 공격 타입 **근거리 / 원거리 / 특수 중 정확히 1개 고정** (상황 따라 전환 안 함)
- **GAS 미사용** — 경량 `UHealthComponent` + 비-GE 데미지 적용
- 이동: **Flow-Field 샘플링(고정맵 사전계산) + 분리(separation)**, 배치 업데이트
- 렌더: 인스턴싱/VAT + 거리 LOD (Significance Manager)
- 풀링 필수 (`UActorPool`)
- 시간 스케일링: `UEnemyScalingProfile` DataAsset — HP/공격력 커브 (이속 불변, 스탯별 슬롯 확장 가능)

## 7. 보스 (확정)

- **Base 구조**: `ABossBase` + `UBossDefinitionDataAsset` + StateTree (소수라 GAS/StateTree OK)
- 현재 30분 단일 보스·2페이즈. TimeGate 스케줄에 행 추가로 **다중 보스 확장**

## 8. 시간 게이트 / 미션 (확정)

- `UTimeGateScheduleDataAsset` = `FTimeGateEvent[]` 행 기반 (기본 30분, **행 추가로 이벤트 확장**)
- 5/10/20/30분 등에 **전원 협동 미션** → 클리어 시 무기 모디파이어 해금
- 난이도 곡선은 `UCurveFloat`로 분리

## 9. 카메라 / 메시 (확정)

- **Separated Arms**: 본인 = FP팔(`OnlyOwnerSee`) + 무기 / 타인 = 3P 캐릭터(`OwnerNoSee`)
- True First Person 풀바디 렌더링은 사용 안 함 (가독성/속도감 우선)

## 10. 발사체 / 네트워크 (확정)

- 발사체: **클라 예측 + 서버 검증**
  - 스나이퍼/레이저 = 히트스캔, 바주카/유탄 = 실제 발사체(소수), 샷건 = 다중 히트스캔, 연사 = 히트스캔/경량
- 데미지: **플레이어 GAS 계산 → 적 HealthComponent.ApplyDamage 브릿지** (적 ASC 없음)
- 토폴로지: **리슨서버 P2P** (Steam Sockets/EOS 세션, P5에서 구축)
- **개발 방법론: P1부터 Net-aware (서버 권위 + Push Model), PIE 2-client 상시 검증.** 솔로로 만들고 나중에 리플리케이션 retrofit 금지
- GAS ASC Replication Mode: 솔로 = Minimal, 협동 = Mixed
- Iris: **OFF (디폴트는 Push Model)**. P5에서 평가용으로만 검토 (Beta 리스크)

## 11. 메타 프로그레션 (확정)

- `URogueliteSaveGame`(USaveGame) — 누적 재화, 업그레이드 트리 상태, 캐릭터/무기 해금
- `UGameplayStatics::AsyncSaveGameToSlot`
- Run 시작 시 영구 스탯을 GE로 캐릭터 ASC에 적용
- 통화 1종, Steam 클라우드 세이브 고려

---

## 12. 확정 기술 스택

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
| 무브먼트 | 표준 CMC (+필요 시 대시) | ❌ Bhop/Wall-run/Motion Matching |
| 레벨 | 고정 맵 | ❌ PCG |

### 엔진에 포함된 플러그인 (바로 enable)
GameplayAbilities, EnhancedInput, ModularGameplay, GameFeatures, CommonUI, StateTree, GameplayStateTree, SignificanceManager, Iris(off)

### 엔진에 없는 Lyra 출신 플러그인 (P3+ 필요 시 경량 재구현 or 복사)
CommonUser, CommonGame, ModularGameplayActors, GameplayMessageRouter

---

## 13. Phase 로드맵 (총 ~23주)

| Phase | 기간 | 산출물 |
|---|---|---|
| **P0** | 2주 | 경량 C++ 스캐폴드 + 참조파일 + Git/LFS + 플러그인 enable + GameplayTags + 빌드 OK + 스모크 테스트 |
| P1 | 3주 | Net-aware 1P 캐릭터(Separated Arms) + 무기 2종(연사·근접) + 적 1종 + 데미지 브릿지 |
| P2 | 3주 | SpawnDirector + Flow-Field + Pooling + Significance (적 300+ 안정) |
| P3 | 4주 | 공유XP/파티레벨업/프리즈 + Card UI + 동적 카드풀 + Rarity + 리롤 |
| P4 | 4주 | Weapon Modifier Fragment + 무기 7종 + TimeGate 미션 |
| P5 | 3주 | 4인 협동 + 세션(Steam) + Iris 평가 + NetProfiler |
| P6 | 2주 | 메타 프로그레션 + 보스 + 클리어 플로우 |
| P7 | 2주 | CommonUI 폴리시 + Insights + README + 빌드 |

---

## 14. 폴더 / 모듈 구조

```
Source/FPSRoguelite/Public/
├── Core/             GameMode, GameState, PlayerController, 로그/Assert 매크로
├── Hero/             UHeroDataAsset, AFPSRCharacter (Separated Arms)
├── AbilitySystem/    ASC, AttributeSets(글로벌), GA, GE, Cue, DamageCalc(브릿지)
├── Weapon/           Archetype 7종, WeaponInstance, StatBlock, ModifierFragment
├── Card/             CardDataAsset(Scope), CardPool, DrawSystem, Reroll
├── Enemy/            경량 풀액터, HealthComponent, AttackType, FlowField, ScalingProfile
├── Boss/             ABossBase, BossDefinition, StateTree
├── Run/              RunManagerSubsystem(공유XP/레벨/프리즈), SpawnDirector, TimeGateDirector
├── Pickup/           범용 PickupActor + PickupDefinition(자석)
├── MetaProgression/  SaveGame, UpgradeTree, Subsystem
├── Performance/      ActorPool, SignificanceConfig, 인스턴싱
├── Messages/         GMS Payload structs
└── UI/               HUD(공유XP바/하단 무기바), CardSelect, MissionUI, MetaUI
```

- 글로벌 스탯(Luck, GlobalCrit, CritMult, RarityBonus, MoveSpeed, MaxHealth, HealthRegen, PickupRadius, XPGain) → Character ASC AttributeSet
- 무기별 스탯 → WeaponInstance 스탯 블록 (ASC 아님)
- 하단 무기바 HUD: 가시성을 HUD State(GMS/Tag)에 바인딩 → ADS/카드UI/미션UI 시 숨김
