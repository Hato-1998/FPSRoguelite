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
  2. **게임플레이 프리즈** — `RunPhase.LevelUpPause`(Replicated) 상태로 적 이동·스폰·공격·무기 발사·픽업 흡수를 게이트. **전역 `TimeDilation=0`에 의존하지 않음** (타이머·AbilityTask·RPC·이펙트 부작용 회피). UI·네트워크 RPC·카드 timeout은 정상 tick 유지
  3. **각 플레이어 개별 3카드 선택** (본인 화면 오버레이)
  4. **선택 안 한 플레이어를 전원 대기** (사용자 확정 기본값)
  5. 전원 완료 시 재개
- 다중 레벨업(XP 폭증) 시 순차 처리
- **카드 선택 = 무조건 전원 대기** (사용자 확정 2026-05-28). 타임아웃·자동선택 **미도입** — 모든 플레이어가 선택 완료할 때까지 프리즈 유지

## 3. 카드 시스템 (확정)

- 데이터 방식: **DataAsset + GameplayEffect(GE)** — 스탯 하드코딩 금지
- 카드 확장 비용 (정정): **기존 Attribute 범위 내 새 카드 = GE + DataAsset 추가 (코드 변경 0)**. **완전히 새로운 Attribute = C++ AttributeSet 확장 + GE + DataAsset (코드 변경 필요)**. → AttributeSet 설계 시 스탯 축을 넉넉히 미리 확보할 것
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
- **무기 교체 입력**: 숫자키 **1/2/3** 직접 슬롯 선택 (`IA_EquipSlot1/2/3`, 마우스휠 미사용)

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

---

## 15. 성능 / 네트워크 예산 (⚠️ P2 착수 전 수치 확정·검증 — 최우선 보완)

> 이 프로젝트의 최대 리스크는 Hero Shooter 과설계가 아니라, **적 500마리 협동의 성능/복제 예산이 미수치화된 점**이다. 아래는 잠정값이며 P2에서 Unreal Insights + NetProfiler로 검증·조정한다.

| 항목 | 잠정 목표 | 비고 |
|---|---|---|
| 최대 활성 적 수(서버) | 하드캡 500, 통상 200~350 | 풀 고갈 시 스폰 보류 |
| 클라이언트별 관련(relevant) 적 | 상한 ~150 | relevancy cull |
| 적 NetCullDistance | 잠정 ~40m (조정 대상) | 화면 밖 컬링 |
| 적 NetUpdateFrequency | 위협도별 S0 30Hz / S1 10Hz / S2 5Hz / S3 2Hz | Significance 연동 |
| 적 Dormancy | 원거리·비활성 DORMANT, 접근 시 wake | |
| 적 복제 상태 | Transform(위치/Yaw)만 최소 복제, 체력=서버 권위 | 히트/사망 코스메틱은 GameplayMessage/Cue (복제 액터 상태 아님) |
| XP/픽업 | 개수 cap + 인접 병합, 자석=클라 코스메틱·서버 권위 수령 | |
| 복제 발사체 액터 | 실제 발사체(바주카/유탄)만 ≤64 동시 | 히트스캔/연사=비복제 코스메틱 |
| 적 공격 판정 | 서버 배치 처리(거리 체크 배치) | |

**리플리케이션 도구 평가 순서**: Push Model(기본) → 부하 시 **Replication Graph**(spatial grid relevancy, 검증된 도구) → 그래도 부족 시 Iris(Beta) 평가. **Iris를 1순위로 두지 않음** (RepGraph가 다수 액터·연결별 relevancy 병목에 더 직접적).

## 16. 보강 설계 노트 (AI 리뷰 반영, 2026-05-28)

- **16-1 Significance 티어**: 플러그인 enable≠최적화. 적/VFX/SFX/anim tick/mesh/healthbar를 단계별 다운.
  - S0 근접 위협: full update / S1 근거리: 저빈도 / S2 중거리 군집: anim·VFX 축소 / S3 원거리: coarse movement·no cosmetic. **AI update budget에도 연동.**
- **16-2 Flow-Field 적 타겟**: P2는 **고정맵 grid + 단일 목표점 field(가장 가까운 플레이어) + local separation steering**으로 시작. 타겟 규칙(가까운/위협도/파티중심/미션목표)은 데이터로 전환 가능하게. 동적 장애물은 비용 대비 나중에.
- **16-3 Weapon Modifier 상세(P4 확정 대상)**: 적용 순서 / 중첩 가부 / 충돌 해결 / 서버 권위 적용 위치 / 클라 예측 범위 / 런 종료 시 소멸 / UI 표시 / 훅 호출 순서(`PreFire→ModifyShotCount→ModifyChargeTime→OnProjectileSpawn→OnHitActor→PostFire`). ⚠️ **성능**: `OnHitActor`가 500마리 타격 시 과도한 virtual dispatch/heap alloc 금지 → 훅은 경량(데이터 기반·히트당 무할당).
- **16-4 메타 저장 정책**: SaveData 버전 필드+마이그레이션, 슬롯명 규칙, Steam Cloud 대상, 저장 실패 처리, 해금 데이터 삭제/리네임 fallback, 런중 vs 로비 저장 구분. **`UGameInstanceSubsystem` SaveManager 경유**(UI/Actor가 SaveGame 직접 접근 금지).
- **16-5 난이도 압박 수단(이속 불변 유지)**: 스폰 밀도↑ / 원거리 적 비율↑ / 특수 적 패턴 / 미션 목표 압박 / 보스 phase pressure.
- **16-6 CommonUI 셋업(P1 후반~P3)**: 프로젝트 ViewportClient(CommonUI), InputData·Back/Click ActionData·ControllerData, Activatable Widget Stack 레이어(Game/GameMenu/Menu/Modal). 카드선택 UI는 RunPhase/서버 상태와 결합(단순 위젯 표시 아님).
- **16-7 Build.cs 의존성**: Phase별 실제 사용 시점에 추가(CommonUI/CommonInput/StateTreeModule/GameplayStateTreeModule/SignificanceManager, 필요 시 ReplicationGraph). 미사용 모듈 선등록 지양.
- **16-8 테스트 런 길이**: 정식 30분 유지하되, TimeGateSchedule DataAsset로 **데모/테스트용 10~12분 스케줄**을 데이터 교체만으로 운용.
