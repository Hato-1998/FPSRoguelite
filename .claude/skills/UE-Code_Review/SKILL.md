---
name: UE-Code_Review
description: 모든 Unreal Engine 프로젝트에서 사용 가능한 커스텀 C++ 코드 리뷰·리팩토링 스킬. .uproject에서 UE 버전을 자동 감지하고, 해당 버전에 맞는 Lyra Starter Game 패턴 + 최신 업계 트렌드(Iris, StateTree, Push Model, GameFeatures, GameplayMessageSubsystem 등)와 대조해 우선순위별 리뷰를 산출. 사용자가 항목을 선택하면 Sonnet 위임 + Opus 검증으로 안전 리팩토링. 인자로 범위 지정 가능 (예: `AbilitySystem`, `Source/MyGame/Character`, 빈값=전체). 트리거: 사용자가 UE/Unreal 프로젝트의 코드 리뷰·아키텍처 검토·Lyra 패턴 비교·GAS 코드 정리를 요청할 때.
---

# UE Code Review (Lyra & Latest Trends)

## 0. 입력 인자 처리

`$ARGUMENTS`에 따라 리뷰 범위 결정:

| 인자 | 의미 |
|---|---|
| 빈값 | 프로젝트의 모든 게임 모듈(Source/<Module>/) 전체 |
| 디렉토리명 (예: `AbilitySystem`) | 모든 모듈의 해당 서브디렉토리 |
| 상대경로 (예: `Source/MyGame/Character`) | 해당 경로만 |
| 절대경로 또는 .h/.cpp 파일 | 단일 파일 정밀 리뷰 |

## 1. 사전 자동 감지 (Phase 0)

리뷰 시작 전 반드시 다음을 자동 추출:

### 1-1. 프로젝트 메타
- `*.uproject` 파일을 Glob으로 찾아 Read
- `EngineAssociation` → UE 버전 (예: "5.5", "5.6")
- `Modules[].Name` → 게임 모듈명 (Lyra 예시: "LyraGame")
- `Plugins[]` → 활성 플러그인 (GameplayAbilities, EnhancedInput, StateTree, ModularGameplay 등)

### 1-2. 엔진 소스 경로
- 가능하면 `EngineAssociation`을 사용해 엔진 설치 경로 추론
  - Windows: `C:\Program Files\Epic Games\UE_<버전>\Engine\Source\` 또는 `Plugins/`
  - Lyra Sample: `Engine\Plugins\GameFeatures\LyraStarterGame\` (UE_<버전> 설치 시 동봉)
- 추론 실패 시 사용자에게 한 번 묻기. 답을 메모리에 저장 권장.

### 1-3. 프로젝트 컨벤션
- `Source/<Module>/` 트리에서 다음 패턴 grep:
  - GameplayTag 싱글톤: `struct .*GameplayTags` → 캐싱 패턴 사용 여부
  - AssetManager 서브클래스: `: public UAssetManager` → `StartInitialLoading()` 내부의 `InitializeNativeGameplayTags` + `InitGlobalData()` 호출 여부
  - ASC 서브클래스: `: public UAbilitySystemComponent`
  - AttributeSet 서브클래스: `: public UAttributeSet` → `ATTRIBUTE_ACCESSORS` 매크로 사용 여부

## 2. 절대 규칙 (모든 UE 프로젝트 공통)

스킬 실행 중 다음을 강제:

### 2-1. 금지 사항
- ❌ **자동 git commit/add/push 금지** — 변경은 working copy에만, 사용자 명시 요청 시에만 커밋
- ❌ **Build.bat / UnrealBuildTool / `dotnet UnrealBuildTool.dll` 자동 실행 금지** — 사용자가 빌드 검증 요청한 경우에만
- ❌ **UCLASS / USTRUCT / UENUM 리네임 금지** — Blueprint 에셋 및 BP 노드 손상 위험
- ❌ **UPROPERTY / UFUNCTION 제거 전 BP 참조 검색 필수** — 미검증 제거 금지
- ❌ **.uasset / .umap 직접 편집 금지** — MCP 또는 에디터 경유

### 2-2. 모델 분배
- **구현 (Edit/Write 작업)** → `Agent` 도구 + `model="sonnet"`(현 최신 = Sonnet 5)로 위임. 2026-07-02 Haiku에서 전환 — 회귀·하드코딩·의도 누락이 줄어든다.
- **검증 (git diff 재검토, grep 회귀 확인, self-critique)** → 메인 세션이 직접 수행, **하위 모델(Sonnet/Haiku) 위임 금지**
- **단순 1줄 수정·읽기 전용 조회** → 분리 없이 메인 세션 즉시 처리
- **프로젝트가 자체 게이트를 정의하면 그쪽이 우선** — 예: FPSRoguelite 는 코어 갈래(구조·리팩토링)에 Fable 2게이트(플랜 G1 / 머지 G2)를 얹는다(`Docs/SSOT/Workflow.md` §6-5).

### 2-3. 메모리 우선
- 사용자 메모리(`MEMORY.md`)에 프로젝트별 컨벤션이 있으면 항상 우선 적용
- 예: "에디터 DataAsset 매핑 거절", "메인 레포 기준 작업" 등 — 자동 메모리 로딩에 의해 컨텍스트에 포함됨

## 3. 워크플로 (4 Phase)

### Phase A — 매핑 (Discovery)
1. Phase 0 자동 감지 결과 출력 (UE 버전, 모듈, 활성 플러그인 표)
2. 인자 범위에 따라 Glob/Grep으로 .h/.cpp 수집
3. 파일 수가 30개 초과면 `Agent` (subagent_type=Explore, 매우 자세하게)로 전수 매핑 위임
4. 디렉토리 트리 + 클래스/구조체 한 줄 요약 표 출력
5. **사용자에게 매핑 결과 보여주고 다음 단계 진행 확인**

### Phase B — 리뷰 (Analysis)
체크리스트(섹션 4)를 카테고리 순으로 적용. 각 발견 항목은 **표준 출력 형식**(섹션 5)으로 기록. 발견 없으면 "✓ 통과"만 표기.

핵심 원칙:
- **Engine Source First**: 엔진 API/매크로 사용 시 추론 금지, 엔진 소스(또는 Lyra)에서 grep으로 실제 사용례 확인 후 비교
- **버전 인식**: 감지된 UE 버전에 따라 가용 기능 범위 판단 (섹션 6 매트릭스 참조)
- **WebFetch 보강**: 감지된 UE 버전이 학습 데이터 cutoff 이후이거나 새로운 기능이 의심되면, `https://docs.unrealengine.com/<버전>/en-US/unreal-engine-<버전>-release-notes/` 또는 `https://dev.epicgames.com/community/unreal-engine/release-notes` 를 WebFetch로 확인

### Phase C — 출력 (Report)
표준 형식으로 발견 항목 나열 + 마지막에 **우선순위 종합 표**:

```
| # | 항목 | 파일 | 우선순위 | 작업량 | 의존성 |
|---|---|---|---|---|---|
```

**사용자에게 어느 항목을 리팩토링할지 선택 요청**. 선택 없이 종료해도 됨 (리뷰만).

### Phase D — 리팩토링 (Optional, 사용자 선택 후)
1. 선택된 항목들을 **하나의 통합 플랜**으로 작성
   - 영향 파일/라인, before/after 스니펫, 리스크, 작업 단위 분할
   - 확인 필요 사항(예: "부모 태그 prefix가 X인지 Y인지")이 있으면 명시
2. **사용자 승인 대기** (CLAUDE.md `[PLAN MODE PRIORITY]` 준수)
3. 승인 후 `Agent` (model="sonnet")로 구현 위임. 프롬프트에 다음 포함:
   - 정확한 절대경로 + 라인 번호
   - before/after 스니펫
   - 다른 코드 미수정 명령
   - git/build 도구 실행 금지 명령
4. 메인 세션이 검증 (Opus 직접):
   - `git status` + `git diff` 재검토
   - 회귀 grep (예: 매직 문자열 0건 확인)
   - 자기 비판 (하드코딩, 디버그 로그, 의도 외 변경, 스타일 일관성)
5. 결과 보고. 커밋은 사용자 명시 요청 시에만.

## 4. 체크리스트 (UE 5.3 베이스라인 + 5.4/5.5/5.6+ 점진 적용)

각 항목은 grep/Read로 자동 점검. 미사용/위반 발견 시 표준 형식으로 보고.

### 4-1. GAS 핵심 패턴
- [ ] **ASC 초기화 분기** — 플레이어=PlayerState 소유 + `Mixed` 복제, AI=Self 소유 + `Minimal` 복제
- [ ] **PossessedBy + OnRep_PlayerState** 양쪽에서 `InitAbilityActorInfo` 호출 (서버/클라 모두)
- [ ] **ATTRIBUTE_ACCESSORS** 매크로 일관 적용 (모든 FGameplayAttributeData)
- [ ] **DOREPLIFETIME_CONDITION_NOTIFY** + `REPNOTIFY_Always` (속성 0 이하 → 다시 0 케이스)
- [ ] **ExecCalc** — `DECLARE_ATTRIBUTE_CAPTUREDEF` + `DEFINE_ATTRIBUTE_CAPTUREDEF` + 정적 함수 싱글톤 캡처 구조체
- [ ] **ModMagnitudeCalc** — `bSnapshot` 명시적 설정, Source/Target 캡처 정확
- [ ] **Effect 적용 흐름**: `MakeEffectContext()` → `AddSourceObject` → `MakeOutgoingSpec` → `AssignTagSetByCallerMagnitude` → `ApplyGameplayEffectSpecToTarget`
- [ ] **Custom EffectContext** — `NetSerialize` 비트팩, `Duplicate()` deep copy, `TStructOpsTypeTraits<>::WithNetSerializer/WithCopy`
- [ ] **AssetManager::StartInitialLoading** — `InitializeNativeGameplayTags()` + `UAbilitySystemGlobals::Get().InitGlobalData()` (TargetData RPC 직렬화 필수)
- [ ] **FScopedAbilityListLock** — `GetActivatableAbilities()` 순회 시 사용

### 4-2. GameplayTag 위생
- [ ] **싱글톤 캐싱** — `FProjectGameplayTags::Get()` 멤버로 모든 네이티브 태그 캐시
- [ ] **Hot-path에서 `RequestGameplayTag` 호출 0건** — Tick/Loop/Lambda 내부 호출 금지
- [ ] **부모 태그 명시 등록** — `MatchesTag` 비교 대상 부모는 `AddNativeGameplayTag`로 자식보다 *먼저* 등록
- [ ] **TMap 매핑 활용** — DamageType↔Resistance, Tag↔Attribute 등은 싱글톤 TMap 권장 (이 프로젝트 컨벤션이면 우선)

### 4-3. 코드 위생 (UE5 표준)
- [ ] **TObjectPtr\<>** — 모든 UPROPERTY 포인터 (raw 포인터는 임시 USTRUCT에만 허용)
- [ ] **PrimaryActorTick.bCanEverTick = false** 기본값. PlayerController PlayerTick는 IsLocalController 가드
- [ ] **매직 문자열 0건** — Blackboard 키, BoneName, SocketName, Section Name 등은 `static const FName` 또는 namespace 상수
- [ ] **check / ensure / null-check** 일관성 — 치명적 invariant=`check`, 디버그 가정=`ensure`, 선택적=`if/skip`
- [ ] **Cast vs CastChecked** 적절성 — null 가능=`Cast`+가드, 보장됨=`CastChecked`
- [ ] **FindComponentByClass\<\>() 결과 BeginPlay에서 캐싱**
- [ ] **로그 카테고리 분리** — `DECLARE_LOG_CATEGORY_EXTERN` 모듈별 카테고리

### 4-4. Composition over Inheritance
- [ ] **HealthComponent 분리** — 캐릭터 클래스에 체력 델리게이트/속성 콜백이 있으면 컴포넌트로 추출 (Lyra `ULyraHealthComponent` 패턴)
- [ ] **WidgetController 팩토리** — HUD에서 템플릿 기반 `GetOrCreateWidgetController<T>()`
- [ ] **EnhancedInput + InputConfig DataAsset** — `UInputConfig`로 InputAction↔GameplayTag 매핑, `UEnhancedInputComponent`에 `BindAbilityActions` 템플릿 메서드
- [ ] **인터페이스 일관성** — `ICombatInterface`, `IHighlightInterface` 등 BlueprintNativeEvent로 노출

### 4-5. 2024-2026 트렌드 (UE 5.3+)

#### UE 5.3+ 가용
- [ ] **StateTree** — 복잡 AI 로직은 BehaviorTree보다 StateTree 권장 (5.3 GA, Schema 기반 강타입 변수)
- [ ] **Enhanced Input** — Legacy InputComponent 잔존 시 마이그레이션 (5.1 GA)
- [ ] **GameplayMessageSubsystem** (Lyra Plugin) — 시스템 간 직접 호출 대신 메시지 버스
- [ ] **Modular Game Features** — 콘텐츠 단위는 `UGameFeatureData` + 플러그인 분리 (Lyra 핵심 패턴)
- [ ] **Common UI** — UMG 직접 사용보다 CommonUI 위젯 (네비게이션·입력 라우팅·플랫폼 추상화)

#### UE 5.4+ 가용
- [ ] **Iris Replication** (5.4 production-ready, 5.5 GA) — 새 프로젝트는 Iris 활성화 검토
- [ ] **Animation Overhaul** — Motion Matching, Choosers (Pose Search 플러그인)
- [ ] **Substrate Materials** (5.4 experimental, 5.5+ production) — 차세대 머티리얼

#### UE 5.5+ 가용
- [ ] **Push Model Replication** — `MARK_PROPERTY_DIRTY_FROM_NAME` (AttributeSet 같은 빈번 복제에 효과 큼)
- [ ] **Nanite Skeletal Meshes** (5.5 production) — 캐릭터에도 Nanite 적용 가능
- [ ] **GameplayCue V2** — 효율적인 GC 시스템

#### UE 5.6+ (감지 시 WebFetch로 최신 release notes 확인 후 적용)
- 감지된 버전이 학습 데이터 이후이면 release notes를 WebFetch한 뒤, 새로 GA된 기능을 체크리스트에 동적 추가
- 후보 키워드: Iris GA expansion, MoverComponent (CharacterMovement 후속), Verse for non-UEFN, MetaHuman Creator native, GAS 2.0 (있다면)

### 4-6. 데이터 주도 설계
- [ ] **PrimaryDataAsset + AssetManager 비동기 로드** — 캐릭터 클래스/능력 데이터는 `UPrimaryDataAsset` (Lyra `ULyraPawnData`)
- [ ] **AsyncAction UI** — Blueprint UMG에서 `UCancellableAsyncAction` 기반 비동기 작업 (Lyra 패턴)
- [ ] **GameplayCue 등록** — VFX/SFX/Decal은 `GameplayCueNotify_Static/Actor`로 처리 (RPC 직접 호출 회피)

## 5. 표준 출력 형식

각 발견 항목:

```markdown
## [HIGH | MEDIUM | LOW] {간결한 제목}

**위치**: [상대경로](file:line) (마크다운 링크)
**문제**: 1-2 문단. 왜 이게 문제인지, 호출 빈도/영향 범위.
**Lyra/트렌드 비교**: 모범 패턴이 어떻게 다른지. 가능하면 Lyra 파일 경로 인용.
**수정 방향**:
\`\`\`cpp
// Before
{현재 코드}
// After
{권장 코드}
\`\`\`
**리스크**: LOW/MED/HIGH + 근거 (Blueprint 영향, 복제 영향, 성능 영향)
**예상 작업량**: 분 단위 (예: 30분, 2시간, 8시간)
```

리뷰 종료 시 우선순위 종합 표:

```markdown
| # | 항목 | 파일 | 우선순위 | 작업량 | 의존성 |
|---|---|---|---|---|---|
| 1 | Tag 캐싱 | AuraGameplayTags + ASC | HIGH | 30분 | - |
| 2 | ... | ... | ... | ... | 1 선행 |
```

**우선순위 기준**:
- **HIGH**: hot-path 성능 영향, 메모리 누수, 복제 버그, 보안. 작업량 ≤2h.
- **MEDIUM**: 아키텍처 개선, SRP 위반, 유지보수성. 작업량 2~8h.
- **LOW**: 트렌드 마이그레이션, R&D, 점진 개선. 작업량 8h+.

## 6. UE 버전별 기능 매트릭스 (참조)

| 기능 | 5.3 | 5.4 | 5.5 | 5.6+ |
|---|---|---|---|---|
| StateTree | GA | 개선 | 개선 | 확장 |
| Enhanced Input | GA | - | - | - |
| Iris Replication | exp | production | GA | 확장 |
| Push Model | 사용가능 | 권장 | 강력 권장 | 기본 |
| Substrate Materials | exp | exp | production | - |
| Nanite Skeletal | exp | exp | production | - |
| TObjectPtr | 표준 | 표준 | 표준 | 표준 |
| GameFeatures | GA | - | - | - |
| MoverComponent | - | exp | exp | (확인 필요) |

> ⚠️ **5.6 이후 기능은 WebFetch로 release notes 확인 후 결정**. 추측 금지.

## 7. Lyra 참조 경로 (검증용)

엔진 설치 경로 하위:
```
Engine/Plugins/GameFeatures/LyraStarterGame/Source/LyraGame/
├── AbilitySystem/
│   ├── LyraAbilitySystemComponent.h         (ASC 패턴)
│   ├── Attributes/LyraHealthSet.h
│   ├── Attributes/LyraHealthComponent.h     (Composition 모범)
│   └── Abilities/LyraGameplayAbility.h
├── Character/
│   ├── LyraCharacter.h
│   └── LyraPawnData.h                       (PrimaryDataAsset 패턴)
├── Player/
│   └── LyraPlayerState.h
├── Messages/
│   └── LyraGameplayMessageSubsystem.h       (메시지 버스)
├── GameModes/
│   └── LyraExperienceDefinition.h           (GameFeatures 통합)
└── UI/
    ├── LyraHUD.h
    └── Common UI 사용처
```

리뷰 중 Lyra 비교가 필요하면 위 경로를 grep(`Engine\Plugins\GameFeatures\LyraStarterGame\` 하위)으로 직접 확인 후 인용.

## 8. 출력 언어

사용자 메시지가 한국어면 한국어로, 영어면 영어로 답변. 단 **체크리스트 항목명·UE API 명·매크로명**은 원어 유지.

## 9. 종료 조건

- Phase B/C로 끝낸 경우: "리뷰 완료. 항목 선택 시 Phase D 진행" 표기 후 대기
- Phase D까지 진행한 경우: 검증 결과 + 다음 작업 제안 후 대기
- 사용자가 명시적으로 종료/만족 표현 시: 간결한 한 줄 마무리 (불필요한 요약 금지)
