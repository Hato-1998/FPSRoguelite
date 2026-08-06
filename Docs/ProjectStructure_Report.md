# FPSRoguelite — 전체 프로젝트 구조 리포트

> 작성 2026-08-06 · 기준 브랜치 `refactor/character` HEAD `1df8dde3` · UE 5.7
> **읽는 법**: 이 문서는 `Game.md`(SSOT 허브)가 *약속한 것*과 코드가 *실제로 하는 것*을 나란히 놓은 것이다.
> 설계 의도는 `Game.md` + `Docs/SSOT/*.md`가 정본이고, **이 문서는 "지금 코드가 어떻게 생겼나"의 정본**이다.
>
> **검증 범위**: C++ 소스 292파일(약 54,400줄) 전수 읽기. **빌드·실행·PIE는 안 했고, `Content/` 안의
> 블루프린트·에셋(.uasset)은 안 열어봤다.** 따라서 "BP가 이렇게 배선돼 있다"는 말은 전부
> *C++이 그렇게 기대한다*는 뜻이지 실제 저작 상태가 아니다. 확인 못 한 것은 그때그때 표시했다.

---

## 0. 한 장 요약

| | |
|---|---|
| 장르 | 1인칭 FPS × 뱀파이어 서바이벌 × 4인 협동 로그라이트 |
| 모듈 | **2개** — `FPSRoguelite`(런타임) · `FPSRogueliteEditor`(에디터) |
| 규모 | `.h` 146 + `.cpp` 146 = 292파일 / 약 54,400줄 |
| 런타임 도메인 폴더 | 19개 (AbilitySystem · Audio · Boss · Card · CityGen · Combat · Core · Director · Door · Enemy · Hero · Map · Messages · MetaProgression · Pickup · Run · Settings · UI · Weapon) |
| 서브시스템 | **16개** (GameInstance 4 + World 12) — LocalPlayer/Engine/Editor 서브시스템은 0 |
| 자동화 테스트 | 런타임 25개 + 에디터 5개 |
| 가장 큰 클래스 | `AFPSRCharacter` (헤더 899줄 + 구현 2,743줄) |

**가장 중요한 한 문장**: 제1원리("적을 싸게 굴린다")가 **코드에 실제로 관철돼 있다.**
적 계열 파일 전체에서 GAS 참조가 **0건**이고, 적은 개체별 Tick도 BehaviorTree도 NavMesh도 AIController도 없다.
적 200마리는 **서브시스템 한 곳의 배치 루프 한 번**으로 전부 처리된다.

---

## 1. 제1원리가 코드에서 어떻게 지켜지는가 (`Game.md §1` 대조)

| Game.md 가 약속한 것 | 코드 실제 | 판정 |
|---|---|---|
| 적 = 경량 풀액터 (개체별 트리·길찾기 없음) | `AFPSREnemyBase : APawn`, 개체 Tick 없음. 전부 `UFPSREnemySpawnSubsystem` 의 배치 패스에서 처리 | ✅ 일치 |
| 적 체력 = 비-GE 경량 컴포넌트 | `UFPSREnemyHealthComponent` — 헤더에 "Lightweight, **non-GAS** health" 명시 | ✅ 일치 |
| **GAS는 스웜 적에 절대 안 붙인다** | `Enemy/`·`Boss/` 폴더 전체에서 `AbilitySystem`/`GameplayEffect`/`GameplayAbility`/`AttributeSet` 참조 **0건** (직접 grep 확인) | ✅ 일치 |
| GAS는 플레이어 + **보스/엘리트**에 | 플레이어는 맞다. **보스는 GAS가 없다** — `FPSRBossBase.h:23` 이 "no ASC/GAS is attached" 라고 명시. 엘리트 클래스는 C++에 아예 없다 | ⚠️ **문서가 앞서감** (§8-D1) |
| 길찾기 = Flow-Field 사전계산 | `UFPSRFlowFieldComputer` — 월드 쿼리 없는 순수 배열 BFS. 2층(`NumLayers=2`) 유계 멀티레이어 | ✅ 일치 |
| 복제 = Push Model | 에디터에선 맞다. **패키지 빌드에선 꺼진다** (§7-1) | ⚠️ **출시 빌드에서 불일치** |
| 적 동시 ~200-300, 하드캡 500 | **실효 상한은 200**. `GlobalAliveCap=200`이 모든 스폰을 무조건 막는다(단일맵은 예약분 8 빼고 **192**) | ⚠️ **문서가 과대** (§8-D8) |

---

## 2. 코어 런타임 뼈대 — GameMode 3계통

이 프로젝트는 맵 역할마다 **프레임워크 3종 세트를 따로** 갖는다. 처음 보면 놀라는 지점이다.

| 역할 | GameMode | PlayerController | Pawn |
|---|---|---|---|
| **런**(게임 플레이) | `AFPSRGameMode` | `AFPSRPlayerController` | `AFPSRCharacter` |
| **로비**(대기실) | `AFPSRLobbyGameMode` | `AFPSRLobbyPlayerController` | `AFPSRLobbyDisplayPawn` |
| **메뉴** | `AFPSRMenuGameMode` | `AFPSRMenuPlayerController` | (엔진 기본) |

**셋이 공유하는 것** (여기가 상태의 중심이다):

- **`AFPSRGameState`** — 복제되는 런 상태를 **전부** 소유한다.
  `SharedXP` · `PartyLevel` · `RunPhase` · `bRunPaused`(전역 프리즈) · `bVisionRestricted` · `RunClockSeconds` ·
  `bFriendlyFireEnabled` · `ActiveBoss` · `ActiveMissionData` · `MissionProgress` · `RunScheduleAsset` ·
  `TopologyGeneration` · `LobbyCountdownEndServerTime` — 13개.
- **`AFPSRPlayerState`** — **ASC(어빌리티 시스템 컴포넌트)의 주인**이다. 캐릭터가 아니라 PlayerState가 들고 있다.
  어트리뷰트셋 2개(`UFPSRHealthSet`·`UFPSRCombatSet`)도 여기. 그 외 생존 상태(Alive/DBNO/Dead) ·
  카드 선택 대기수 · 리롤 횟수 · 선택 무기 · 준비완료 · 로비 좌석 · 현재 맵 ID — 9개 복제.
  `CopyProperties` 가 **로비→런 심리스 이동 때 로드아웃을 넘긴다.**

**GameInstance는 커스텀이 없다.** `DefaultEngine.ini:13` 이 엔진 기본 `GameInstance` 를 쓰고,
GameInstance 급 동작은 전부 서브시스템 4개로 나가 있다. (좋은 구조다 — 상속 대신 조합)

**플레이어 소유 관계**
```
AFPSRPlayerState
 └ UFPSRAbilitySystemComponent
    ├ UFPSRHealthSet   (Health / MaxHealth)
    └ UFPSRCombatSet   (크리확률·크리배율·데미지배율·행운·픽업반경·XP획득·이동속도배율)

AFPSRCharacter  (생성자에서 FObjectInitializer 로 CMC를 UFPSRCharacterMovementComponent 로 교체)
 ├ UFPSRWeaponInventoryComponent
 │   └ UFPSRWeaponInstance  (복제 서브오브젝트) → ActiveFragments[] + 스탯 모디파이어
 ├ UFPSRWeaponFireComponent      (소유 클라의 연사 타이밍)
 ├ UFPSRRecoilComponent          (CrystalRecoil 어댑터)
 ├ UFPSRReviveComponent          (DBNO/부활)
 ├ UFPSRPlayerFeedbackComponent
 ├ UFPSRBlindspotAudioComponent  (사각지대 경고음)
 ├ FirstPersonCamera
 └ FirstPersonArms               (1인칭 팔 — 현재 작업 트랙, ADR 0006)
```

---

## 3. 서브시스템 16개 — 실질적인 "매니저" 계층

이 프로젝트는 싱글톤 매니저 액터를 안 쓰고 **전부 엔진 서브시스템**으로 짰다.

### GameInstance 서브시스템 (4) — 맵을 넘어 살아남는 것
| 클래스 | 하는 일 |
|---|---|
| `UFPSRGameFlowSubsystem` | 메뉴↔런 이동, 런 결과 캐시 |
| `UFPSRSessionSubsystem` | Steam 세션 — 호스트는 ServerTravel(listen), 초대 수락은 ClientTravel |
| `UFPSRFlowLogSubsystem` | 분기점 자동 로깅 (맵 로드·네트워크 실패·이동 실패) |
| `UFPSRSaveGameSubsystem` | **세이브 I/O의 유일한 창구**. 주 슬롯 + 백업 슬롯, CardId 리다이렉트 |

### World 서브시스템 (12) — 맵 하나의 수명
| 클래스 | 하는 일 |
|---|---|
| `UFPSREnemySpawnSubsystem` ⏱ | 적 풀 + 스폰 디렉터 + **배치 이동/LOD/공격 패스** + 공격 토큰 + 다중맵 배분 (1,607줄) |
| `UFPSRFlowFieldSubsystem` | 통합 그리드 1개, 0.2초 재계산 타이머, 문 파괴 스탬프, 토폴로지 세대 |
| `UFPSREnemyMetricsSubsystem` ⏱ | 가독성 5지표 CSV 계측 |
| `UFPSREnemyShadowLODSubsystem` ⏱ | 거리별 적 그림자 on/off (기기별) |
| `UFPSRRunDirectorSubsystem` | 런 시계 · 스폰 강도 · 미션 창 · 보스 진입 (서버 전용) |
| `UFPSRDirectorSensorSubsystem` | 폐루프 디렉터의 **센서만** — 5개 신호 측정, 복제 0, 액추에이터 없음 |
| `UFPSRCardSubsystem` | 카드 풀, 행운 가중 등급 굴림, 서버 권위 뽑기/적용 |
| `UFPSRPickupSubsystem` | XP 젬 스폰 + 개수 캡 (캡 초과 시 XP 직접 지급) |
| `UFPSRProjectileSubsystem` ⏱ | 복제 발사체 풀 + 팀별 예산 + 프리즈 정지 |
| `UFPSRMapStreamSubsystem` | 서브레벨 스트림인 → 콜리전 확인 → 필드 굽기 → 스폰포인트 재캐시 |
| `UFPSRAudioSubsystem` | 마스터 볼륨 SoundMix 적용 |
| `UFPSRGameplayMessageSubsystem` | 태그 채널 pub/sub (Lyra GMS 경량 재구현) |

⏱ = Tickable

---

## 4. 적 스웜 경로 — 실제 런타임 흐름

**여기가 이 프로젝트의 심장이다.** 아래 전부가 **서버 한 패스**에서 일어난다.
개체별 Tick 없음 · BehaviorTree 없음 · NavMesh 없음 · AIController 없음.

### 4-1. 스폰
1. `UFPSRRunDirectorSubsystem` 이 목표 생존수 계산 — 파티 레벨 기반 구간선형(`AliveCountByLevel`) 우선, 없으면 시간 램프
2. 스케줄 에셋의 `MaxAliveCount`(기본 300)로 클램프
3. `UFPSREnemySpawnSubsystem::TickDirector`(0.1초 타이머) —
   맵별 점유 계산 → 배분(`Apportion`) → 후방 적 서서히 회수(다중맵) → 라운드로빈 채우기
4. **모든 스폰이 `ActiveEnemies.Num() < GlobalAliveCap(200)` 에 무조건 걸린다**
5. 스폰지점 선택 = 활성 존 + `MinPlayerDistance` 만족하는 곳 중 균등 랜덤
6. `AcquireEnemy` — **휴면 풀에서 꺼내 쓰고**, 없으면 새로 스폰
7. `Activate` — 표시 켜기 · 콜리전 켜기 · `DORM_Awake` · 체력 리셋 · 이동속도 ±10% 랜덤

### 4-2. 길찾기 (Flow-Field)
- 맵 시작 시 `BuildUnifiedField` 로 **한 번** 굽는다. **서버에만 존재**한다(클라는 null)
- `UFPSRFlowFieldComputer` = **월드가 없는 평면 배열 코어**.
  XY 셀마다 최대 2개 층(`NumLayers=2`, `static_assert` 로 잠금) → 겹친 2층(메자닌) 추격 지원
- 0.2초마다 다중 소스 BFS + 최급강하. **월드 쿼리 0** — 순수 배열 연산
- 문이 부서지면 → 셀 막기 해제 → BFS 재실행 → 토폴로지 세대 +1 → 클라가 재확인

### 4-3. 이동/스티어링 (한 패스에서 전부)
적 하나당:
1. 슬롯 AABB 로 현재 맵 갱신
2. 같은 맵의 가장 가까운 살아있는 플레이어 선택. 없으면 **다른 슬롯 추격**(이동만, 공격 안 함)
3. **중요도 티어 판정** — ⚠️ 엔진 `USignificanceManager` 를 **안 쓴다**. 손수 짠 거리밴드다:

   | 티어 | 거리 | 갱신 간격 | 공격 간격 | 복제 빈도 |
   |---|---|---|---|---|
   | S0 | ≤15m | 매 패스 | 매 패스 | 30Hz |
   | S1 | ≤35m | 2패스마다 | 매 패스 | 10Hz |
   | S2 | ≤60m | 4패스마다 | 2패스마다 | 5Hz |
   | S3 | 그 밖 | 8패스마다 | 4패스마다 | 2Hz |

4. **복제 빈도는 티어가 실제로 바뀔 때만 설정한다** — UE 5.7 의 setter 가 무조건 브로드캐스트해서
   매 프레임 부르면 적 200마리 핫패스가 죽는다 (코드에 가드 주석 있음)
5. 공격 판정 = 가상 함수 `ServerTickAttack` 하나. 근접은 접촉 + **공격 토큰**(플레이어당 동시 10),
   원거리는 정지→차지→발사→쿨다운 + **원거리 토큰**(플레이어당 3)
6. 스티어링 = (저작된 탈출 경로가 있으면 그것) 아니면 `플로우방향 + 분리력×1.5`.
   분리는 공간 해시 3×3 스캔, 반경 120cm. **정확히 겹친 개체는 황금각으로 흩는다**
7. 이동 = 스윕 오프셋 + 지면 투영 + 계단 오르내리기(180cm) + 중력(지면 재확인 0.15초 간격)

### 4-4. 데미지 — 단일 병목 지점
`GA → FPSRCombat::ResolveDamage → FPSRCombat::ApplyDamage`
분기가 **클래스가 아니라 컴포넌트 기준**이다:
`FindComponentByClass<UFPSREnemyHealthComponent>()` 가 있으면 경량 경로, 없으면 `AFPSRCharacter` GAS 경로.
→ **문(`AFPSRDoor`)도 같은 컴포넌트를 달아서 무기 코드 한 줄 안 고치고 부술 수 있다.** 깔끔한 설계다.

### 4-5. 죽음 → 재활용 (완전 풀링)
```
체력 0 → OnDeath → HandleDeath
   → 코스메틱 재생 → XP 젬 스폰 → ReleaseEnemy(this) → return
```
`ReleaseEnemy` = 활성 목록에서 빼고 → `Deactivate`(숨기기 + 콜리전 끄기 + `DORM_DormantAll`) → **휴면 풀에 넣기**.
**액터는 절대 Destroy 되지 않는다.** `Destroy()` 는 서브시스템이 없을 때의 폴백뿐.
그 외 회수 경로: 맵 밖 추락(KillZ −10000) · 후방 서서히 회수 · 런 리셋.

> 🪤 **코드에 적힌 알려진 구멍**: 사망 애니메이션이 안 보인다 — `ReleaseEnemy` 가 같은 프레임에 숨겨버리기 때문.
> 서버 "사망 잔류 시간"은 다음 단계로 미뤄져 있다.

---

## 5. GAS 경계 — 실측

**GAS는 딱 한 군데에만 있다: 플레이어의 PlayerState.**

| 요소 | 위치 |
|---|---|
| ASC | `UFPSRAbilitySystemComponent` — `AFPSRPlayerState` 가 소유. 캐릭터는 `GetAbilitySystemComponent()` 로 넘겨주기만 함 |
| 어트리뷰트셋 | `UFPSRHealthSet`(Health/MaxHealth) · `UFPSRCombatSet`(7종) |
| 어빌리티 | `UFPSRGameplayAbility` 기반 + 발사 GA 4종(히트스캔·발사체·차지레이저·근접) + 패시브 2종 |
| GameplayEffect | **C++ 서브클래스 없음.** GE는 전부 콘텐츠 에셋이고 `SetByCaller` 로 카드 수치를 넣는다 |

**적·보스에는 없다** — `Enemy/`, `Boss/` 폴더 전체 grep 결과 **0건**.
보스는 적과 **똑같은** `UFPSREnemyHealthComponent` 를 쓴다 → 모든 무기가 코드 추가 없이 보스를 때린다.

---

## 6. 기획자가 만지는 데이터 표면

C++에 에셋 경로 하드코딩 금지 원칙(`Workflow.md §6-2`)이 잘 지켜져 있다.

### DataAsset 13종
`UFPSRWeaponDataAsset`(가장 큼 — 헤더 557줄: 아키타입·스탯블록·발사 GA·메시/소켓/몽타주·ADS·이동속도·무기카드·해금기능·파츠규칙·스코프) ·
`UFPSRWeaponFragment`(+서브클래스 8: 다중발사·명중보너스·자해방지·폭발탄·빗맞힘탄약·처치시재장전·점사·마커) ·
`UFPSRLoadoutPoolDataAsset` · `UFPSRCrosshairStyleDataAsset` · `UFPSRCardDataAsset` · `UFPSRCardPoolDataAsset` ·
`UFPSRRunScheduleDataAsset` · `UFPSRMissionDataAsset` · `UFPSREnemyRosterDataAsset` · `UFPSRBossDefinitionDataAsset` ·
`UFPSRCrosshairColorPresetDataAsset` · `UFPSRCityGenKit` · `UFPSRCityGenConfig`

**확장 방식이 일관돼 있다**: 새 종류 = 서브클래스 하나 추가, 중앙 코드 0수정.
(`UFPSRCardEffect` 5종 · `UFPSRMissionTuning` 7종 · `UFPSREnemySpawnRule` · `UFPSRCrosshairFadeCondition` 2종 · 무기 파츠 규칙)

### Project Settings 항목 (`UDeveloperSettings` 7)
런타임 5: FPSR Game Flow · FPSR Audio · FPSR Save · FPSR Placeholder Visuals · FPSR Enemy Rendering
에디터 2: FPSR Blockout · 무기 어셈블러 화면비 프리셋

### 콘솔 변수 5 · 콘솔 명령 42
`FPSR.FlowField.Debug` · `FPSR.Movement.Debug` · `FPSR.Debug.WeaponDraw` · `FPSR.Enemy.ProjectileBudget`(기본 32, 1~100) · `FPSR.Mission.ZoneRadius`
명령 42개는 카드/런흐름/XP/텔레메트리/적스폰/미션/무기 디버그용. 대부분 `ECVF_Cheat`.

### ⚠️ 데이터로 안 빠져 있는 튜닝 상수 (전부 `static constexpr`)
`GlobalAliveCap=200` · `SeedReserve=8` · `MaxActiveEnemies=500` · `AttackTokenLimit=10` · `RangedAttackTokenLimit=3` ·
`SeparationRadius=120`/`Strength=1.5` · 티어 반경 1500/3500/6000 · `NetCullWeaponRangeCm=10000` ·
`NetCullSeamMarginCm=4000` · `ChaseEnter/ExitCells=40/50` · `WorldKillZ=-10000` · `GFlowUpdateInterval=0.2`
→ 이 중 일부는 의도적 잠금(§7-4), 일부는 승격 후보다. 판단은 사용자 몫 — `Refactor_20260806_Report.md §6`.

---

## 7. 에디터 모듈 — 기획자 툴 4개

메뉴는 전부 `Tools > FPSR` 아래.

| 툴 | 메뉴 | 하는 일 |
|---|---|---|
| **무기 파츠 조립기** | `무기 파츠 조립기…` | 임베디드 3D 프리뷰에서 모듈 파츠를 기즈모로 옮겨 소켓에 굽는다. 1인칭 팔 프리뷰 + 애니 재생/스크럽 + `손 위치 저장`(스켈레톤에 그립 소켓 기록). 약 5,530줄로 에디터 모듈에서 가장 큼 |
| **무기 1인칭 뷰** | `무기 1인칭 뷰…` | 같은 프리뷰 씬을 **플레이어 눈 시점 + 게임과 같은 화면비(레터박스)** 로. 도킹 뷰포트 화면비(≈1.14, 세로화각 82.3°)가 게임(16:9, 58.7°)과 달라서 조립기에서 맞춘 그립이 PIE에서 틀어지던 문제 때문에 생김. ⚠️ **에디터 실측 미검증** |
| **블록아웃 툴** | `블록아웃 툴…` | 설정에 적힌 콘텐츠 폴더를 스캔해 배치 팔레트를 만들고, 뷰포트에서 **면 자석 스냅**으로 모듈 맵을 깐다(Synty 조각은 피벗이 구석에 있어 피벗 스냅만으론 틈이 생김). 배치 결과를 6항목으로 검사 |
| **데이터 에디터** | `데이터 에디터…` | 모든 기획 DataAsset을 한 탭에서. 왼쪽 = 분류 트리(앵커에서 도달 못 하는 고아 표시), 오른쪽 = 속성 편집 + **카드 수치 그리드** + **드래그로 고치는 미션 타임라인** |
| **앵커 데이터 검증** | `앵커 데이터 검증` + 커맨드렛 | 앵커(카드풀/런스케줄/로드아웃풀)에서 패키지 의존성을 따라가 도달 못 하는 에셋을 **경고**로 보고(빌드를 막지 않음). 헤드리스 실행 = `-run=FPSRValidateAnchoredData`, 종료코드 0/1. **앵커가 0개면 실패 처리**(거짓 통과 방지) |

검증기 4종은 `UEditorValidatorSubsystem` 이 자동 발견한다(수동 등록 없음):
카드풀 CardId 중복 · 런스케줄 · 로드아웃풀 · 무기 카드 라우팅.

---

## 8. 문서 ↔ 코드 어긋난 곳 13건 (목록만 — 문서는 안 고쳤다)

> SSOT가 통치 문서이므로 어긋남은 실제 유지보수 결함이다. **어느 쪽이 옳은지 판정하지 않고 둘 다 인용**했다.
> 고치는 것은 사용자 결정 사항 — 문서를 코드에 맞출지, 코드를 문서에 맞출지가 각각 다르다.

| # | 문서가 말하는 것 | 코드 실제 | 최신으로 보이는 쪽 |
|---|---|---|---|
| D1 | `Architecture.md:16` "보스/엘리트 = 일반 Actor + StateTree (+GAS)" | `FPSRBossBase.h:23` "no ASC/GAS is attached". 베이스는 `ACharacter`. repo 전체 `StateTree` 참조 **0**. 엘리트 클래스 없음 | **코드** (`RunFlow.md:40` 은 이미 맞게 적혀 있음) |
| D2 | `Architecture.md:15`·`Enemy.md:13` "Significance Manager" + "인스턴싱" | `USignificanceManager` 참조 **0**(플러그인만 켜져 있음). 실제는 손수 짠 거리밴드. **인스턴싱도 없다** — 적마다 개별 StaticMeshComponent | **코드** (손수 짠 쪽이 이동 패스와 융합돼 오히려 더 맞다) |
| D3 | `Enemy.md:14` "풀링 필수 (`UActorPool`)" / `Architecture.md:45` `Performance/` 폴더 | `UActorPool` **0 hits**. `Performance/` 폴더 없음. 풀링은 스폰 서브시스템 안에 인라인 | **코드** (기능은 있고 이름만 없던 것) |
| D4 | `Enemy.md:15` "`UEnemyScalingProfile` DataAsset" | **0 hits**. `RunFlow.md:73` 이 이미 "P5 이연"으로 정정 | **RunFlow.md** |
| D5 | `Architecture.md:52`·`PlayerFeel.md:61` "크로스헤어 크기 설정 `CrosshairScale`" | **0 hits**. `FPSRGameUserSettings.h:21-22` 가 *"크로스헤어 크기는 의도적으로 설정이 아니다 — 크로스헤어는 정직하다(퍼짐 = 실제 탄퍼짐이 화면에 투영된 것)"* 라고 명시. 영속 값은 `MasterVolume`(`:65`)·`CrosshairColor`(`:69`) **둘뿐** | **코드** (2026-08-06 커밋이 최신). 게다가 새로 생긴 2층 크로스헤어는 **어느 SSOT에도 없다** |
| D6 | `PlayerFeel.md:13` "1P 전용 팔 메시 폐기" | 코드가 `FirstPersonArms` 를 만들고 `RefreshFirstPersonRendering()` 로 1인칭 분리를 구현. `PROGRESS.md` 의 **현재 트랙이 정반대**(ADR 0006) | **코드 + PROGRESS** (ADR 0006이 0002를 대체했는데 §2-9 갱신 누락) |
| D7 | `Enemy.md:35`·`Concept.md:62` "FF 기본 ON" | `FPSRGameState.h:242` `bFriendlyFireEnabled = false` | **문서**(설계 결정) — 코드 플립 대기. 문서가 스스로 "코드 후속"이라 표시함 |
| D8 | 🔴 `Game.md:46` "동시 ~200-300, 코드 폴백캡 300, 하드캡 500" | **실효 상한 200**. `GlobalAliveCap=200` 이 모든 스폰을 무조건 게이트(단일맵은 −예약8 = **192**). `MaxAliveCount=300` 은 도달 불가능한 스케줄 클램프, `MaxActiveEnemies=500` 은 *풀 액터 수* 상한이지 동시 생존수가 아님 | **코드** (헤더 주석에도 그렇게 적혀 있음) |
| D9 | `Architecture.md:36`·`CombatWeaponCard.md:157` "`UHeroDataAsset`" | 없는 클래스. `FPSRCharacter.h:600` 에 *"나중에 HeroDataAsset 으로 접을 것"* 이라는 주석만 | **코드** (문서가 계획을 현재형으로 씀) |
| D10 | `Architecture.md:54` "PickupRadius·XPGain·MoveSpeed·HealthRegen 미구현" | 앞의 **3개는 구현·사용 중**. `HealthRegen` 만 진짜 없음 | **코드** |
| D11 | `Enemy.md:28` "방패 아키타입 `UFPSRShieldComponent`" · `:27` "공중 아키타입" | 둘 다 **0 hits**. 적 아키타입은 근접·원거리 2종뿐 | 문서가 계획을 🟢로 표시해 오해 소지 |
| D12 | `Enemy.md:25-26` 고립도 기반 스폰 편향 | 센서만 있고 액추에이터 없음 — `RunFlow.md:74` 와 일치 | 일치(오해 방지용 기록) |
| D13 | `Architecture.md:33-49` 폴더 트리 | `Combat/`·`Director/`·`Door/`·`Map/`·`CityGen/`·`Tests/` **누락**, 없는 `Performance/` **포함**. §4-2는 스스로 "역사 스냅샷"이라 표시했지만 SSOT의 유일한 클래스 지도임 | **코드** |

---

## 9. 확인하지 못한 것 (정직 기록)

1. **`Content/` 안 전부** — `BP_FPSRGameMode`·`BP_FPSRPlayer`·무기 DA·카드풀·런스케줄·미션 튜닝·맵이
   실제로 저작·배선돼 있는지는 확인 안 했다. `PROGRESS.md` 자체가 "무기 DA 8개 —
   `BodyAnimLayerClass`·`WeaponAttachScale`·`LeftHandSocket` 미배정" 이라고 적고 있다.
2. **실행 시 동작** — PIE·패키지·네트워크 세션을 띄우지 않았다. 복제·dormancy·대역폭에 대한 이 문서의
   모든 서술은 **정적 코드 판독**이다.
3. **서드파티 플러그인 내부** — `CrystalRecoil`(반동)·`VibeUE`(에디터 MCP)는 의존성과
   `UFPSRRecoilComponent` 의 부모 클래스까지만 봤다.

---

**관련 문서**: 이번 리팩토링 결과와 **사용자 결정이 필요한 항목 9건**은 `Docs/Refactor_20260806_Report.md`.
