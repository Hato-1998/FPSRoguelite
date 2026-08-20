# 리팩토링 리포트 — 2026-08-06

> 브랜치 `refactor/mechanical-cleanup` (← `refactor/character` `1df8dde3` 에서 분기)
> 베이스라인 태그 `refactor-baseline-20260806` · 커밋 `5f496e4f`
> 기준 프롬프트: `REFACTOR_LOOP_PROMPT.md` (PHASE 0~6 + 불변 제약 18개)
> 사용자 지시: **기계적으로 확실한 것만 수정 / 판단·트레이드오프가 걸린 것은 분석·정리만**

---

## 0. 결론 먼저

**이 코드베이스는 프롬프트가 겨냥한 축에서 이미 거의 깨끗했다.**
프롬프트는 "헤더가 무겁고, 값 복사가 널려 있고, 복제 dirty 마킹이 빠져 있을 것"을 전제했는데
셋 다 실측으로 무너졌다. 그래서 이번 작업의 실제 산출물은 **대량 수정이 아니라
소량의 확실한 수정(18파일) + 사용자 결정이 필요한 항목 9건 + 문서 드리프트 12건**이다.

**새로 찾은 것 중 제일 중요한 두 가지** (둘 다 수정하지 않았다 — 판단이 필요해서):
1. 🔴 **패키지(출시) 빌드에서 Push Model 이 통째로 꺼진다.** 에디터/PIE 에서만 켜져 있다. → §7-1
2. 🔴 **아무 클라이언트나 아무 때나 파티 전원을 로비로 강제 이동시킬 수 있다.** → §7-2

---

## 1. 실행 요약

| 단계 | 상태 | 변경 파일 | 비고 |
|---|---|---|---|
| R0 베이스라인 빌드 | ✅ 성공 | 0 | HEAD 그린 확인 |
| R1 중복 include 제거 | ✅ 완료 | 10 (12줄) | |
| R2 루프 힙 할당 정리 | ✅ 완료 | 1 | 샷건 펠릿 루프 |
| R3 `Reserve()` 추가 | ✅ 완료 | 11 | 상한이 확정된 곳만 |
| R4 GC 안전성 | 🔍 조사만 | 0 | 수정 후보 1건 → 판단 필요 |
| R5 매직 넘버 | 🔍 조사만 | 0 | 사용자 지시대로 후보 표만 |
| — 복제 정합 | 🔍 조사만 | 0 | **고칠 것이 없었다** |
| 리포트 2종 | ✅ 완료 | 2 | 이 문서 + `ProjectStructure_Report.md` |

**커밋**: `5f496e4f` (R1+R2+R3, 소스 18파일 / +20 −14)
**제외**: PHASE 6(계약 검사 스크립트) — 사용자 지시로 제작하지 않았고 후속작업으로도 남기지 않았다.

---

## 2. 빌드 측정 — 프롬프트의 전제가 크게 빗나갔다

프롬프트는 non-unity 빌드가 오래 걸릴 것을 전제로 "15분 이하면 모듈마다, 초과면 Phase 끝에 1회"라는
분기를 두었다. **실측은 67초다.**

| 항목 | 값 |
|---|---|
| `-DisableUnity` 풀빌드 | **66.99초** · `Result: Succeeded` |
| 액션 수 | 384 (octobuild/XGE 가 8물리/16논리 코어에 분산) |
| 개별 TU 컴파일 | 프로젝트 `.cpp` **145 / 146** |
| 유니티 소스 블롭 | **0** (로그의 `Module.*.gen.N.cpp` 는 UHT 생성 반사코드지 유니티 블롭이 아니다) |
| 에러 / 경고 | **0** |

> 💡 부수 발견: 일반 빌드도 UBT 가 `git status` 로 작업셋을 잡아 **adaptive non-unity** 를 쓴다.
> 다만 이번 실측에서 에디터 모듈은 유니티 블롭으로 묶여 컴파일됐다 —
> **그래서 일반 빌드만으로는 include 제거를 검증했다고 말할 수 없었고**, `-DisableUnity` 로 다시 돌렸다.
> 67초면 앞으로도 이걸 기본 검증으로 써도 된다.

---

## 3. 수정한 것 (전부 동작 변경 없음)

### 3-1. 중복 include 12건 / 10파일
`.cpp` 와 **자기 짝 `.h`** 양쪽에 있던 것만 제거했다. `.cpp` 가 자기 `.h` 를 먼저 include 하므로
**빌드 모드와 무관하게 증명된다** — 유니티가 가려주는 함정에 걸리지 않는다.
같은 파일 안의 중복 include 는 실측 **0건**이었다.

| 파일 | 제거한 include |
|---|---|
| `FPSRGA_WeaponFire_ChargeLaser.cpp` | `Weapon/FPSRWeaponFragment.h` |
| `FPSRSessionSubsystem.cpp` | `OnlineSessionSettings.h` |
| `FPSREnemyRosterDataAsset.cpp` | `Enemy/FPSREnemyBase.h` |
| `FPSRFlowFieldSubsystem.cpp` | `Enemy/FPSRFlowFieldComputer.h` |
| `FPSRCharacter.cpp` | `Weapon/FPSRWeaponDataAsset.h` |
| `FPSRRunDirectorSubsystem.cpp` | `Run/FPSRRunScheduleDataAsset.h` |
| `FPSRMainMenuWidget.cpp` | `CommonActivatableWidget.h` |
| `FPSRRunHUDWidget.cpp` | `Core/FPSRGameState.h` |
| `SFPSRWeaponAssemblerTab.cpp` | `AssetRegistryModule.h` · `Engine/StaticMesh.h` · `SCheckBox.h` |
| `SFPSRDataEditorWidget.cpp` | `DataEditor/FPSRDataEditorHelpers.h` |

**헤더는 손대지 않았다** — 사용자 결정(§4-1의 실측 근거).

### 3-2. 샷건 펠릿 루프의 힙 할당 — `FPSRGA_WeaponFire_Hitscan.cpp`
`TArray<FHitResult> PawnHits` 와 `TArray<FPSRCombat::FResolvedHit> ResolvedHits` 가 **펠릿 이중 루프 안**에
선언돼 있었다 → 샷건 1발에 `2 × PelletCount` 회 힙 할당. 두 배열을 라운드 루프 밖으로 올렸다.

> 🪤 **작업 중 직접 확인한 함정**: `FPSRCombat::DedupePawnHitsByActor`(`FPSRCombatStatics.cpp:373-398`)는
> `OutHits.Add()` 로 **덧붙이기만 하고 비우지 않는다**. 배열만 올리고 `Reset()` 을 빼먹으면
> **펠릿마다 앞 펠릿의 대상이 다시 맞아서 샷건 데미지가 조용히 중복된다.**
> 그래서 두 배열 모두 사용 지점에서 명시적으로 `Reset()` 하고, 이유를 코드 주석에 남겼다.
> (`PawnHits` 는 엔진이 비워주더라도 명시하는 쪽이 엔진 동작에 안 기대므로 무조건 옳다)

### 3-3. `Reserve()` 11건
`.Add()` 루프인데 **최종 개수가 루프 헤더의 `.Num()` 으로 이미 확정된 곳**만 골랐다.

| 파일 | 배열 | 상한 |
|---|---|---|
| `FPSRCombatStatics.cpp` | `ActorToIndex`(TMap) | `InHits.Num()` — 히트스캔 펠릿마다 |
| `FPSRCombatStatics.cpp` | `Processed`(TSet) | `Overlaps.Num()` — 폭발 |
| `FPSRProjectile.cpp` | `Damaged` | `Overlaps.Num()` — 발사체 착탄 |
| `FPSRGA_WeaponMelee.cpp` | `Processed`(TSet) | `Overlaps.Num()` — 근접 |
| `FPSRWeaponInventoryComponent.cpp` | `Result` | `Slots.Num()` |
| `FPSRCharacter.cpp` | `AddedScopeDescriptors` | `Selected.Num()` |
| `FPSRCardSubsystem.cpp` | `Result` / `GrantedSeen` | `Count` / `WeaponUnlockCards.Num()` |
| `FPSRRunDirectorSubsystem.cpp` ×2 | `FarEnough` | `TagMatched.Num()` |
| `FPSRMission_CollectOrbs.cpp` | `SpawnLocations` | `Offsets.Num()` |
| `FPSRCardEntryWidget.cpp` | `EffectLines` | `Effects.Num()` |

**의도적으로 제외한 것**
- **적 계열 전부**(`Enemy/`·`FlowField*`) — 손댈 게 없다. **작성자가 이미 `TInlineAllocator<4/8/32>`
  또는 명시적 `Reserve` 를 다 걸어 뒀다.** 그리고 §5 대로 적500 정량 측정이 미실시라 그 외 최적화는 금지.
- 상한이 *하한*일 뿐인 곳 (`FPSRCardSubsystem.cpp:116-117` 은 카드 하나가 등급마다 여러 오퍼로 펼쳐진다)
- 여러 리스트를 도는 람다라 단일 `.Num()` 이 상한이 아닌 곳 (`FPSRCardPoolDataAsset.cpp:51`)
- 에디터/검증 전용 20여 곳 — 가치 대비 변경 노이즈

---

## 4. 조사 결과 — "고칠 것이 없다"로 끝난 축들

### 4-1. 헤더 정리(IWYU) — 전제가 안 맞는다

| 실측 | 값 |
|---|---|
| 헤더 파일 | 146개 / `#include` **총 417줄** (헤더당 평균 2.9) |
| `#include` 3줄 이하 헤더 | **115 / 146** (= `CoreMinimal` + 부모클래스 + `.generated.h` 뿐) |
| 최다 | 9줄 (`SFPSRDataEditorWidget.h`) |
| 최빈 | `CoreMinimal`(53) · `GameplayTagContainer`(24) · `Engine/DataAsset`(13) · `GameFramework/Actor`(12) |

상위 include 는 **부모 UCLASS**(전방선언 불가)거나 **값 타입 `FGameplayTagContainer`**(전방선언 불가)다.
→ 헤더를 열어봤자 바꿀 게 사실상 없다. 사용자 결정으로 "싼 부분만"(§3-1) 처리.

### 4-2. 값 의미론 — 이미 되어 있다
- 컨테이너/문자열 **값 전달 파라미터: 0건**
  (`TArray`/`TMap`/`TSet`/`FString`/`FText`/`FTransform`/`FGameplayTagContainer` + 프로젝트 USTRUCT 20종 전수 — 전부 이미 `const&`)
- 범위 for **값 복사: 0건** (240개 중 223개가 이미 `const auto&`, 나머지 17개는 스칼라/enum = 올바름)
- `FGameplayTag` 값 전달 12곳은 **바꾸면 안 된다** — `FName` 하나짜리라 값 전달이 엔진 관례

### 4-3. 복제 정합 — 대조표

| 항목 | 발견 | 수정 | 보고만 |
|---|---|---|---|
| 3-1 등록 ↔ 선언 대조 | 선언 **54** ↔ 등록 **54**, 불일치 **0** | 0 | — |
| 3-2 Push Model dirty 마킹 누락 | 68개 write 사이트 중 **1** (결함 아님) | 0 | 1 |
| 3-3 Dormancy × 복제 상태 | 구멍 **0** | 0 | — |
| 3-4 Server RPC 검증 | 12개 전부 `WithValidation` 없음 | 0 | **12** |
| 3-5 권한 검사 누락 | 무가드 3건 (전부 의도적·주석 있음) | 0 | 3 |

#### 3-0 — 등록 매크로 인벤토리 + push 기반 판정 근거 (프롬프트가 요구한 선행 조건)

**엔진 소스에서 직접 확인했다** (추론 아님):
- `FDoRepLifetimeParams::bIsPushBased` **기본값 = `false`**
  — `D:\UnrealEngine\UE_5.7\Engine\Source\Runtime\Engine\Public\Net\UnrealNetwork.h:146`
- ⇒ **`DOREPLIFETIME_WITH_PARAMS_FAST` 를 썼다는 사실만으로는 push 기반이 아니다.**
  호출부가 `Params.bIsPushBased = true` 를 명시해야 한다.

| 등록 매크로 | 건수 | push 기반? | dirty 마킹 필요? |
|---|---|---|---|
| `DOREPLIFETIME_WITH_PARAMS_FAST` + `bIsPushBased=true` | 45 (12 클래스) | ✅ | ✅ 필요 |
| `DOREPLIFETIME_CONDITION_NOTIFY` | 9 (GAS 어트리뷰트셋 2) | ❌ | ❌ 불필요 |

push 를 켠 클래스 12개: `AFPSRGameState`(13) · `AFPSRPlayerState`(9) · `UFPSRWeaponInstance`(5) ·
`UFPSRCharacterMovementComponent`(4, `COND_SkipOwner`) · `UFPSREnemyHealthComponent`(3) ·
`AFPSRMissionActor`(3) · `AFPSRDoor`(2) · `UFPSRWeaponInventoryComponent`(2) ·
`UFPSRWeaponFireComponent`(1, `COND_SkipOwner`) · `UFPSRReviveComponent`(1) ·
`AFPSRBoundaryBlocker`(1) · `AFPSRMissionOrb`(1)

**`MARK_PROPERTY_DIRTY` 를 가진 파일이 정확히 이 12개와 일치한다.**

#### 3-2 유일한 누락 — 그리고 왜 결함이 아닌가
`FPSRWeaponInventoryComponent.cpp:57` `InitializeComponent()` 의 `Slots.SetNum(...)` 에 dirty 마킹이 없다.
그러나 이 함수는 `PostInitializeComponents` 안에서 **액터가 복제 대상으로 고려되기 전에** 돈다.
→ 초기 풀-스테이트 복제가 이 값을 그대로 싣는다. **수정하지 않았다.**

#### 그 외 정상 확인
- **Dormancy**: `AFPSREnemyBase::Activate` 가 `DORM_Awake` 를 체력 리셋 **앞에** 설정 → 순서 정상.
  `FlushNetDormancy` 는 코드 전체에 **없는데 이건 의도적**이다 — 설계가 "활성 수명 내내 Awake" 라서
  한 번만 깨우는 `FlushNetDormancy` 는 오히려 움직이는 적을 클라에서 얼린다(코드에 근거 주석 있음).
  게다가 **깨질 수 있는 유일한 지점에 경고가 이미 적혀 있다** — `FPSRWeaponFireComponent.cpp:98-100`:
  *"플레이어 폰은 절대 dormant 가 아니다… 그게 바뀌면 dirty 마킹은 dormant 액터를 깨우지 못하므로
  옆에 `FlushNetDormancy()` 가 필요하다."*
- **`NetMulticast`**: 1개(`MulticastFireCosmetics`). 서버 발신·unreliable·소유자 스킵 — 정상.
- **서브오브젝트 복제**: 컴포넌트와 소유 액터 **양쪽에** `bReplicateUsingRegisteredSubObjectList=true` 가
  걸려 있다. 액터 쪽이 빠지면 `UFPSRWeaponInstance` 5개 프로퍼티가 조용히 전부 복제 안 되는데, 있다.

### 4-4. GC 안전성
- `UPROPERTY` 안의 raw `U*`/`A*`: **0건** — 전부 이미 `TObjectPtr`
- `FGCObject` / `AddToRoot` 오용: **0건**
- 유일한 반사 구멍 1건 → **수정하지 않음**, §7-5 로
- 에디터 프리뷰 컴포넌트들은 `FPreviewScene` + `AddReferencedObjects` 로 커버됨
  (`ArmsComp` 하나만 `AddReferencedObjects` 에서 빠져 있으나 등록 해제와 null 대입이 항상 짝이라 안전)

---

## 5. 하지 않은 것과 그 이유

| 안 한 것 | 이유 |
|---|---|
| PHASE 6 계약 검사 스크립트 | **사용자 지시** — 만들지 말고 후속작업으로도 남기지 말 것 |
| 헤더 수정 | §4-1 실측상 얻을 것이 없음 (사용자 결정 "싼 부분만") |
| 매직 넘버 승격 | 트레이드오프 → §6 후보 표만 |
| 문서 드리프트 수정 | 프롬프트 지시 "문서를 고치지 마라". 목록은 `ProjectStructure_Report.md §8` |
| **PIE / 네트워크 실동작 검증** | 불변제약 11. 세션을 안 띄웠으므로 "확인함"이라 보고하지 않는다 |
| `Game.md` 미커밋 1줄 | 사용자가 직접 쓴 레퍼런스 문장(Apex Legends 추가). 내 작업이 아니라 손대지 않았다 |

---

## 6. 매직 넘버 — 후보 표만 (수정 없음)

### 6-0. 먼저: 이미 노출된 것
**이 프로젝트는 이미 튜닝값을 아주 많이 노출하고 있다** — `EditDefaultsOnly` 프로퍼티 **406개 / 40파일**.
기존 오버라이드 관례도 확립돼 있다:
`AFPSRFlowFieldBoundsVolume` 의 `CellSizeOverride`·`ClimbableStepHeightOverride`·`ProbeApexAboveOriginOverride`(per-map),
`AFPSREnemyBase::NetCullRadius`(BP per-archetype).
→ **새 관례를 발명할 필요가 없다.** 승격한다면 이 두 형태 중 하나를 따라야 한다.

### 6-1. 남은 하드코딩은 거의 전부 "손대면 안 되는 곳"에 몰려 있다

| 분류 | 상수 | 판단 |
|---|---|---|
| 🔒 **의도된 잠금** (불변제약 12-15) | `NumLayers=2`(+`static_assert`) · `MaxTotalCells=40000` · `MaxGridDimPerAxis=256` · `MaxActiveEnemies=500` | **승격 금지.** 데이터화하면 초과 시 조용한 품질 저하가 되살아난다 |
| ⚡ **프레임마다 읽히는 스웜 핫패스** | 티어 반경 1500/3500/6000 · `SeparationRadius=120` · `SeparationStrength=1.5` · `AttackVerticalRange=150` · `StopClimbBelowPlayer=30` | **목록만.** 프로퍼티 접근이 캐시 지역성을 해칠 수 있다 — 판단은 사용자 |
| 🟡 **핫패스 아님 — 승격 가능하나 이름 확정 = 일방통행** | `GlobalAliveCap=200` · `SeedReserve=8` · `MapGroupBonus=1` · `AttackTokenLimit=10` · `RangedAttackTokenLimit=3` · `ChaseEnterCells=40`/`ExitCells=50`/`HoldSeconds=2.0` · `PerFrontSlotBudget=12` · `FrontBudgetCeiling=36` · `BaseDrainRatePerSec=2`/`Burst=20` · `WorldKillZ=-10000` · `NetCullWeaponRangeCm=10000` · `NetCullSeamMarginCm=4000` | **사용자 결정** — 승격 후 이름을 바꾸면 BP 오버라이드가 날아간다 |
| ⚠️ **따로 봐야 할 것** | `SpawnGroundHalfHeight = 90.0f` | 캐릭터 캡슐 half-height 를 **손으로 복사한 값**이다. 캡슐을 바꾸면 조용히 어긋난다 → 상수보다 **런타임 조회**가 맞을 수 있다 |

### 6-2. 따로 봐야 할 것 — `SpawnGroundHalfHeight`
`SpawnGroundHalfHeight = 90.0f` 는 승격 후보라기보다 **중복된 값**이다.
캐릭터 캡슐의 half-height 를 손으로 복사해 둔 것이라, 캡슐을 바꾸면 스폰 지면 스냅이 조용히 어긋난다.
상수로 노출하는 것보다 **런타임 조회**가 맞을 수 있다.

---

## 7. 🔴 사용자 승인이 필요한 결정 9건 — 이 리포트의 핵심

> 전부 **수정하지 않았다.** 각 항목은 "왜 기계적으로 못 고치는가"를 명시했다.
> 🔴 2건(7-1 · 7-2)은 추적을 위해 전용 문서로도 올렸다 → **[`Docs/OpenIssues_Network.md`](OpenIssues_Network.md)**
> (선택지 표 포함). 해결되면 그 문서에서 지우고 `WorkLog.md` 로 내린다.

### 7-1. 🔴 패키지(출시) 빌드에서 Push Model 이 꺼진다
**위치**: `Source/FPSRoguelite.Target.cs`
**근거 (엔진 소스 추적)**:
- `WITH_PUSH_MODEL` 기본값 = **0** — `Runtime/Net/Core/Public/Net/Core/PushModel/PushModelMacros.h:5-7`
- UBT 가 `TargetRules.bWithPushModel` 로 정의 — `UEBuildTarget.cs:6076`
- `bWithPushModel` 기본값 = **`(Type == TargetType.Editor)`** — `TargetRules.cs:1420`
- `FPSRogueliteTarget` = `TargetType.Game`, 오버라이드 **없음**

**결과**: 에디터/PIE 에서는 Push Model 이 살아 있지만 **패키지 빌드에서는 컴파일 아웃**된다.
`MARK_PROPERTY_DIRTY*` 는 no-op 이 되고 `bIsPushBased` 는 무시되며 비교 기반 복제로 폴백한다.
**동작은 정상이다 — 다만 의도한 CPU 절감이 출시 빌드에 없다.**
`Game.md §1` "복제 = Push Model" 과 `Performance.md §5` 의 예산 전제가 출시 빌드에선 성립하지 않는다.

**왜 한 줄로 못 고치나**: `bWithPushModel` 은 `[RequiresUniqueBuildEnvironment]` 인데
이 엔진은 **Installed Build**(`D:\UnrealEngine\UE_5.7\Engine\Build\InstalledBuild.txt` 존재)라
고유 빌드 환경을 만들 수 없다. → **소스 엔진 빌드로 전환**해야 한다 = 빌드 인프라 결정.

**선택지**: ①소스 엔진 빌드로 전환(비용 큼, 얻는 것 확실) ②현 상태 유지 + `Performance.md §5` 에
"출시 빌드는 비교 기반"이라고 명시 ③U14 perf 패스에서 측정 후 결정

---

### 7-2. 🔴 아무 클라이언트나 파티 전원을 로비로 강제 이동시킬 수 있다
**위치**: `FPSRPlayerController.cpp:613-625` `ServerRequestReturnToLobby_Implementation`
**현재**: 몸통에 `HasAuthority()` 검사 하나뿐인데, 이건 Server RPC 라 **항상 참**이다.
그다음 바로 `GM->RequestReturnToLobby()` → 파티 전체 `ServerTravel`.
런이 끝났는지 검사도 없고, 호스트 전용 검사도 없다.

**왜 기계적으로 못 고치나**: 올바른 게이트가 무엇인지가 **게임 규칙 결정**이다 —
호스트만? `RunPhase == PostRun` 일 때만? 전원 동의? `WithValidation` 으로는 못 고친다(몸통 문제).

---

### 7-3. `ServerAckTopology` 상한 없는 단조 증가
**위치**: `FPSRPlayerController.h:68` / 서버측 `FPSRPlayerState.cpp:101`
서버가 `FMath::Max` 로 단조 증가만 시키고 상한이 없다.
위조된 큰 `Gen` 을 보내면 늦은참여(late join) 토폴로지 게이트를 **영구히 자가 충족**한다.
**결정 필요**: 클램프(`0 ≤ Gen ≤ GameState 의 현재 세대`) / 무시 / 연결 끊기 — 정책 선택.

---

### 7-4. Server RPC 12개 전부 `WithValidation` 없음
| 성격 | 개수 | 내용 |
|---|---|---|
| 파라미터 없음 / bool 만 | 5 | `_Validate` 가 `return true` 보일러플레이트 |
| `int32` 인덱스/ID | 7 | **7개 전부 이미 `_Implementation` 안에서 범위·동일성 검사 후 조용히 무시한다** |

**왜 기계적으로 못 고치나**: 이미 검사가 있으므로 `WithValidation` 추가는 보안을 늘리는 게 아니라
**실패 방식을 "조용히 무시" → "클라이언트 연결 끊기"로 바꾸는 것**이다. 이건 정책 결정이다.
(게다가 인덱스 상한이 런타임 상태라 `_Validate` 에서 그대로 못 쓰는 경우가 많다)

---

### 7-5. `CachedRecoil` 반사(reflection) 구멍
**위치**: `FPSRWeaponFireComponent.h:112` — `TObjectPtr<UFPSRRecoilComponent> CachedRecoil;` **`UPROPERTY()` 없음**
`TObjectPtr` 인데 `UPROPERTY` 밖이면 **GC가 추적하지 않는다.**
오늘 안전한 이유는 같은 액터의 컴포넌트 배열이 우연히 살려주기 때문이지 계약이 아니다.

**왜 기계적으로 못 고치나** — 정답이 둘 중 무엇인지 갈린다:
- 이건 이 클래스가 **만든 게 아니라** `FindComponentByClass` 로 **캐시한 것**이다
  (프롬프트 규칙 4-(a) "그 클래스가 생성한 것만" 에 안 맞는다)
- 게다가 **한 번도 무효화되지 않는다**. 원 의도가 약참조였다면 정답은 `UPROPERTY` 가 아니라 `TWeakObjectPtr` 다
- 참고: `FPSRWeaponFireComponent.cpp:181` 이 `CachedRecoil ? ... : FindComponentByClass<...>()` 로
  폴백을 두고 있다 — 작성자가 null 이 될 수 있다고 생각했다는 뜻인데, 아무 데서도 null 로 만들지 않는다

---

### 7-6. 적 `bDead` 복제 — 계약을 문자 그대로 볼 것인가
**위치**: `FPSREnemyHealthComponent.h:96`
`Performance.md §5` 계약은 "적 복제 상태 = Transform + 체력". 실제로는 `Health` `MaxHealth` `bDead` **3개**다.
- `MaxHealth`: 클라가 체력바 **비율**을 계산하려면 필요. 스폰 때 한 번만 바뀌어서 비용 거의 0 → 두는 게 맞다
- `bDead`: **`Health <= 0` 에서 파생 가능**하다. 굳이 두는 이유는 풀 재사용 시
  `true→false` 전이를 클라가 놓치지 않게 하는 **엣지 감지**용(`OnRep_bDead` 에 처리 있음)
**결정**: 비트 하나를 아끼고 클라에서 `Health` 엣지를 감지할 것인가, 지금대로 둘 것인가.

---

### 7-7. `GameplayMessageSubsystem` 브로드캐스트마다 리스너 배열 통째 복사
**위치**: `FPSRGameplayMessageSubsystem.cpp:37` — `TArray<FFPSRMessageListenerData> ListenerArray(List->Listeners);`
부모 태그 단계마다 배열을 통째로 복사한다. 게다가 `FFPSRMessageListenerData` 가 `TFunction` 을 들고 있어서
복사 1회가 아니라 **리스너 수만큼 추가 할당**이다.
**코드 자신이 이 함수를 핫패스라고 적어 놨다** (`cpp:23` — "hot path: enemy death x hundreds/frame").
다만 리스너가 하나도 없으면 `ListenerMap.IsEmpty()` 로 즉시 빠져나가므로, 비용은 **실제 구독자 수에 비례**한다.

**왜 기계적으로 못 고치나**: 이 복사는 **재진입 안전**을 위한 것이다(콜백 안에서 구독 해제해도 안전).
없애려면 세대 카운터나 톰스톤 인덱스로 재설계해야 한다 = 구조 변경. → 별도 작업으로 판단 필요.

> ✅ **측정 등록 2026-08-19 (U14R)**: 브로드캐스트 비용 CSV 계측(`FPSRMsg/*` 4스탯, 태그 깊이 증폭 = `ListenersCopied`) + 잠정 판정 기준(≤0.2ms @300 → 보류 유지) 등록 — `Docs/Specs/U14R_PerfMeasureRegistry.md` §5-B. 실측 = M0 EC ① 패스, 유의미할 때만 재설계 코어 행 생성.

---

### 7-8. 스칼라 질문에 배열을 통째로 만드는 곳 2건
- `FPSRCardEffect.cpp:360` — `!Context.Inventory->GetOwnedWeapons().Contains(WeaponToGrant)` (멤버십 질문)
- `FPSRCardSubsystem.cpp:111` — `Inv->GetOwnedWeapons().Num() > 0` (불리언 질문)

`GetOwnedWeapons()` 는 매번 전체 배열을 만든다.
**왜 기계적으로 못 고치나**: 생산자가 `UFUNCTION(BlueprintPure)` 라 **시그니처 변경 = BP 호환성 파괴**
(불변제약 2). 고치려면 `HasOwnedWeapon(...)` / `HasAnyOwnedWeapon()` **새 헬퍼를 추가**해야 한다 = 설계 변경.

---

### 7-9. 문서 ↔ 코드 드리프트 12건
전체 표는 **`Docs/ProjectStructure_Report.md §8`**. 각 건마다 문서 위치·코드 위치·어느 쪽이 최신인지 근거를 달았다.
**문서를 고치지 않았다** — 어떤 건 문서를 코드에 맞춰야 하고(D1·D2·D3·D5·D9·D10),
어떤 건 코드를 문서에 맞춰야 하기(D7 프렌들리 파이어 기본값) 때문에 일괄 처리할 수 없다.

가장 시급한 2건: **D5**(있다고 적힌 크로스헤어 크기 설정이 코드엔 없고, 코드는 "의도적으로 안 만든다"고 명시) ·
**D6**(1인칭 팔 "폐기"라 적혔는데 현재 트랙이 정반대).

---

## 8. 되돌리는 법

```bash
git reset --hard refactor-baseline-20260806
```

단계별로 되돌리려면:
```bash
git revert 5f496e4f
```

브랜치 자체를 버리려면 (`refactor/character` 로 복귀):
```bash
git checkout refactor/character && git branch -D refactor/mechanical-cleanup
```

---

## 9. 이 리포트가 보장하지 않는 것

- **네트워크·PIE 실동작을 검증하지 않았다.** 복제·dormancy·대역폭에 대한 모든 서술은 정적 코드 판독이다.
- **`Content/` 안(블루프린트·DataAsset·맵)을 열어보지 않았다.** BP 그래프가 자체 복제 변수를 선언했다면
  §4-3 인벤토리에 안 잡힌다. (다만 복제 프로퍼티 mutator 중 `BlueprintCallable` 인 것은 하나도 없으므로
  BP가 **이 프로퍼티들**을 쓰지는 못한다)
- **성능 개선을 측정하지 않았다.** §3-2/§3-3 은 할당 *지점*을 없앴다는 정적 사실이지 측정된 이득이 아니다.
  `Performance.md §5` 대로 적500 정량 측정은 미실시(U14 이월) 상태다.
