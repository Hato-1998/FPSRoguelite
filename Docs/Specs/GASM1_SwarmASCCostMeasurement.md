# GASM1 — 적 스웜 300 에 ASC 를 붙이는 비용 실측 (측정 수단)

> 이 명세는 **측정 수단의 정본**이다: 무엇을 추가하고(CVar·클래스·덤프), 어떻게 켜며(러너 인자),
> 무엇을 통과라 부르는지. 측정 *결과*는 `Docs/SSOT/Performance.md` §5 가 소유한다.

## 1. 메타

| 항목 | 값 |
|---|---|
| 유닛 ID / 이름 | GASM1 — Swarm ASC Cost Measurement |
| 브랜치 | `main` (트렁크 기반, §6-7) |
| 작성 모델 | `claude-opus-5` — 템플릿 머리말의 "Fable이 만든 명세"는 **낡았다**. 모델 배분 개정(2026-08-26, `Workflow.md` §6-5)에서 Fable 은 주도자가 아니라 **2게이트 검증자**가 됐고 명세 작성은 Opus 소관이다. Fable 은 이 문서의 근거가 된 플랜을 **G1 에서 3라운드 검증**했다(P1 4 · P2 11 · P3 13, 전량 수용·기각 0) |
| 작성일 | 2026-09-12 |
| 상태 | `확정` |
| 관련 SSOT | `Performance.md` §5 · `Enemy.md` §2-6(시간축 계약) · `Workflow.md` §6-5-2 |
| 관련 ADR | `0013-enemy-tier-axis-and-elite-gas.md` 불변식 1 — **이 측정이 그 전제 문장을 재는 구성이다** |
| 보드 행 | https://app.notion.com/p/3d83972ddd8881559fc4ce8a77beadb5 |
| 관련 메모리 | `[[uht-ignores-shipping-guard]]` · `[[do-not-launch-game]]` · `[[production-structure-first]]` |

## 2. 목표 / 비목표

**목표** — 이 유닛이 끝나면 다음이 가능해진다.
1. 일반 적 한 종(`BP_EnemyRangedBase`)만으로 **ASC 유무·GAS 로드아웃 유무**를 런타임에 토글해 300/500 마리를 세울 수 있다.
2. 같은 캡처에서 프레임(`FrameTime`·`GameThread`·`TickActors`·`AbilitySystemComponent` 틱)·메모리(구조체 본체 + 컨테이너 페이로드)·복제(`ServerRepActors`)가 **델타로** 나온다.
3. ADR 0013 불변식 1 의 전제 문장(*"스웜 200~300 에 ASC 가 붙어 제1원리가 붕괴한다"*)이 **추정이 아니라 수치**로 기록된다.

**비목표** — 일부러 하지 않는 것.
- ❌ **엘리트 300 스폰**(사용자 결정 4, 2026-09-12). `AFPSREnemyEliteBase` 는 **한 줄도 고치지 않는다**.
  엘리트 특유 부하(스케일 3배 = 커버리지 9배 렌더, 3배 캡슐의 분리 계산)는 **미측정으로 남긴다** —
  그것은 GAS 질문이 아니라 아키타입 질문이고, 엘리트로 재면 절대값이 GPU 바운드라
  (`Performance.md:47`, 적300 GameThread 2.67ms < GPUTime 3.84ms) GAS 에 대해 아무 말도 못 한다.
- 🔁 ~~휴면 풀 ASC 상시 틱 결함 수정~~ — **그런 결함은 없다**(G2 P2-1 에서 정정). 유휴 ASC 는 엔진이
  스스로 끄고(§10 자기해제 체인), 휴면 액터는 정의상 진행 중 태스크가 없으며 `Deactivate` 의
  `CancelAbilities` 가 남은 것도 닫는다. 이 전제로 열었던 보드 행은 **폐기**한다
  (https://app.notion.com/p/3d83972ddd888123b4cbf4b1e3cb09f2).
  `ASCDump` 의 휴면 총수는 결함 기록이 아니라 **부착된 ASC 의 실측 하한**을 세는 참고치로 남는다.
- ❌ **실제 엘리트 어빌리티 콘텐츠 저작** — 여기서 만드는 것은 **합성 부하 모델**이다.
- ❌ **프로덕션 동작 변경** — 모든 CVar 기본값 off, 기본 경로 diff 0.
- ❌ **`AcquireEnemy` 시그니처 변경** — 호출부 4곳(디렉터 fill loop 2 · `FPSR.SpawnEnemies` ·
  `Tests/FPSRStatusWorldTest.cpp:149`)을 건드리지 않는다.

## 3. 제1원리 3줄 (핵심원칙 4)

1. **제1원리 근거** — 제1원리는 "적 수백을 싸게 = 액터당 비용 최소화"다. 그 위에 선 ADR 0013 불변식 1
   의 근거 문장은 한 번도 측정된 적이 없고, 그 미측정 위에 티어 축·엘리트 캡·GAS 경계가 전부 쌓여 있다.
   **①↔② 가 정확히 그 문장을 재는 구성이다** — 같은 일반 적, ASC 유무만 다르다.
2. **엔진 기본값·기존 인프라와의 관계** —
   - 엔진 기본값을 **그대로 쓴다**: ASC 의 틱 정책에 개입하지 않는다. 런타임 부착도 엔진 표준 경로만 쓴다.
   - 기존 인프라 **전부 재사용**: 러너 `measure_swarm_render.ps1` · 분석기 `analyze_swarm_csv.py` ·
     `FPSR.SpawnEnemies` · §5 판정선 · CsvProfiler 가 **이미 기록 중인** 컬럼
     (`TickActors`·`AbilityTasks`·`PhysicalUsedMB`/`MemoryFreeMB` = `CsvProfiler.cpp:4188-4189`).
     신규 CSV 계측은 0이다(콘솔 진단 `ASCDump`·`ExecAfter` 와 카운터는 캡처 밖에서 도는 별개 수단).
   - **덮는 것**: `AcquireEnemy` 의 로스터 픽(CVar 켜졌을 때만) 하나뿐.
3. **프로젝트 제약과의 정합** — 서버권위(부착·구동 전부 `Activate`/`ServerTickAttack` = 서버 경로) ·
   4인 기준선(⑤·⑤′) · 에셋 경로 하드코딩 금지(강제 클래스는 로스터에서 **이름으로** 찾는다) ·
   §2-2 프리즈 계약(**시간형 GE 금지** — Instant GE + `Ctx.DeltaSeconds` 누산기, `Enemy.md` §2-6).

## 4. 파일 목록

| 경로 | 신규/수정 | 한 줄 설명 |
|---|---|---|
| `Public/AbilitySystem/Attributes/FPSRMeasureAttributeSet.h` / `Private/…cpp` | 신규 | 측정 부하용 어트리뷰트 4종 |
| `Public/AbilitySystem/Abilities/FPSRMeasureDummyAbility.h` / `…cpp` | 신규 | Instant GE 를 적용하고 즉시 종료하는 더미 |
| `Public/AbilitySystem/Effects/FPSRMeasureInstantGE.h` / `…cpp` | 신규 | Instant + Health Modifier 1개 |
| `Public/Enemy/FPSREnemyBase.h` / `Private/…cpp` | 수정 | 측정 시임(§5-C 멤버 전부 · 부착 · 구동 · 티어다운) |
| `Private/Enemy/FPSREnemySpawnSubsystem.cpp` | 수정 | `ForceSpawnClass` 분기 · `FPSR.Debug.ASCDump` |
| `Private/Core/FPSRDebugExec.cpp` | 신규 | `FPSR.Debug.ExecAfter` |
| `Private/Tests/FPSRStatusWorldTest.cpp` | 수정 | 테스트 시작 시 측정 CVar 5종 리셋 — 🔴 `ECVF_SetByConsole` 로 Set 해야 한다(G2 P2-2: `ECVF_SetByCode`=0x0E < `ECVF_SetByConsole`=0x10 이라 콘솔로 켜 둔 값을 못 되돌린다 — `IConsoleManager.h:183,187` · `ConsoleManager.cpp:275-281` `CanChange`) |
| `Scripts/analyze_swarm_csv.py` | 수정 | 컬럼 4종 + `--baseline` 비교 모드 + 노이즈 플로어 병기 |
| `Scripts/measure_swarm_render.ps1` | 수정 | 인자 5종 + MemReports 대기·수집 + 전달수 게이트 + ASC 부착 실증 게이트 + 클라 생존 게이트 |

## 5. 인터페이스 선언

### 5-A. CVar 5 + 콘솔 명령 2

```cpp
// FPSREnemySpawnSubsystem.cpp — 파일 로컬 static. 전부 기본값 off = 프로덕션 경로 diff 0.
static TAutoConsoleVariable<FString> CVarForceSpawnClass(
    TEXT("FPSR.Debug.ForceSpawnClass"), TEXT(""),
    TEXT("AcquireEnemy 의 로스터 가중추첨을 무시하고, 로스터 규칙 중 클래스 이름이 일치하는 것을 쓴다. "
         "BP 클래스는 _C 접미가 붙는다 (예: BP_EnemyRangedBase_C). 불일치 시 경고 1회 후 로스터 기본 동작."),
    ECVF_Cheat);

// FPSREnemyBase.cpp — 부착·구동 게이트
static TAutoConsoleVariable<int32> CVarAttachASC(
    TEXT("FPSR.Debug.AttachASC"), 0,
    TEXT("일반 적에 UFPSRAbilitySystemComponent 를 런타임 부착한다(액터 실수명당 1회)."), ECVF_Cheat);

static TAutoConsoleVariable<int32> CVarMeasureLoadout(
    TEXT("FPSR.Debug.MeasureLoadout"), 0,
    TEXT("측정용 AttributeSet 부착 + 더미 어빌리티 부여·구동. AttachASC 를 함의한다(코드에서 강제)."), ECVF_Cheat);

static TAutoConsoleVariable<float> CVarMeasureCadence(
    TEXT("FPSR.Debug.MeasureCadence"), 1.0f,
    TEXT("더미 어빌리티 발동 주기(초). 허용값 = 1.0 / 0.2 뿐이며, 그 밖의 값은 경고 후 가까운 쪽으로 스냅한다."),
    ECVF_Cheat);

// 🔁 신설 (G2 P2-1, 구성 ③ᵀ) — 측정 AttributeSet 이 ITickableAttributeSetInterface::ShouldTick() 에
// true 를 돌려주게 해서, 엔진이 정한 틱 조건(§10 자기해제 체인의 ③번)을 **합법적으로 만족**시킨다.
// 이것 없이는 ASC 틱이 첫 프레임 뒤 스스로 꺼져 틱 축을 아예 못 잰다. MeasureLoadout 을 함의한다
// (세트가 있어야 Tickable 을 걸 곳이 있다) — 코드에서 강제.
static TAutoConsoleVariable<int32> CVarMeasureTickable(
    TEXT("FPSR.Debug.MeasureTickable"), 0,
    TEXT("구성 ③ᵀ: 측정 AttributeSet 을 Tickable 로 만들어 ASC 틱이 켜진 채 유지되게 한다. "
         "MeasureLoadout 을 함의한다."), ECVF_Cheat);
```

**콘솔 명령 2종**(CVar 아님):
- `FPSR.Debug.ExecAfter <초> <명령>` — `FPSRDebugExec.cpp`
- `FPSR.Debug.ASCDump` — `FPSREnemySpawnSubsystem.cpp`, `FPSR.EliteDump` 옆
  (`ActiveEnemies`·`DormantPool` 이 private 이라 밖에서는 휴면 수를 못 센다)

### 5-B. 측정 클래스 3종 — **UCLASS 는 무조건 컴파일한다**

> 🔴 `#if !UE_BUILD_SHIPPING` 안에 `UCLASS`/`USTRUCT`/`UPROPERTY` 를 두면 **모든 구성에서 UHT 오류**가
> 난다 — `'UCLASS' must not be inside preprocessor blocks, except for WITH_EDITORONLY_DATA`
> (`UhtTokenBufferReader.cs:719-733`). UHT 는 `UE_BUILD_SHIPPING` 을 인식 목록에 갖고 있지 않다
> (`UhtHeaderFileParser.cs:1046-1113` → `Unrecognized`). 선언은 무조건 컴파일하고 **가드는 사용처에만**
> 건다. 인스턴스가 생기지 않으면 무해하다. `[[uht-ignores-shipping-guard]]`

```cpp
/** 측정 부하 발생기. 게임 로직 소비자 0 — 의도적이다.
 *  ADR 0013 이 거부한 것은 "죽은 데이터를 프로덕션 구조에 넣는 것"이고, 이것은 프로덕션 구조가 아니다. */
// 🔁 ITickableAttributeSetInterface 상속 추가 (G2 P2-1, 구성 ③ᵀ). 이 인터페이스는
// UAbilitySystemComponent::GetShouldTick() 이 직접 검사하는 세 조건 중 하나다
// (AbilitySystemComponent_Abilities.cpp:235-243) — ShouldTick() 이 true 인 동안 ASC 틱이 유지된다.
UCLASS()
class FPSROGUELITE_API UFPSRMeasureAttributeSet : public UAttributeSet, public ITickableAttributeSetInterface
{
    GENERATED_BODY()
public:
    UFPSRMeasureAttributeSet();

    //~ITickableAttributeSetInterface — 구성 ③ᵀ 에서만 true. CVar 를 직접 읽지 않고 아래 플래그를 본다
    //  (매 프레임 × 300 세트가 CVar 를 조회하면 그 비용이 ③↔③ᵀ 델타로 새어든다 — MeasureCadenceCached 와 같은 이유).
    virtual bool ShouldTick() const override { return bMeasureTickable; }
    virtual void Tick(float DeltaTime) override {} // 의도적 no-op — 재는 것은 "틱이 도는 비용"이지 틱 내용이 아니다

    /** AFPSREnemyBase::Activate 가 CVar 로부터 1회 설정한다. 비-UPROPERTY(리플렉션 불요·복제 불요). */
    bool bMeasureTickable = false;

    // 4종 전부 ReplicatedUsing — 복제 트래픽이 측정 대상이므로 COND_None(전원에게) 그대로 둔다.
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Health,        Category = "FPSR|Measure")
    FGameplayAttributeData Health;
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxHealth,     Category = "FPSR|Measure")
    FGameplayAttributeData MaxHealth;
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_AttackPower,   Category = "FPSR|Measure")
    FGameplayAttributeData AttackPower;
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MoveSpeedMult, Category = "FPSR|Measure")
    FGameplayAttributeData MoveSpeedMult;

    // 🔁 정정(구현 중 엔진 대조) — `ATTRIBUTE_ACCESSORS` 는 **엔진에 없다**. AttributeSet.h:419 의 그것은
    //    "이렇게 직접 정의해 쓰라"는 **주석 안의 예시**이고, 실제 매크로는 :465 의 ATTRIBUTE_ACCESSORS_BASIC
    //    이다(같은 4개 접근자를 만든다). 이 리포는 처음부터 후자를 써 왔다 — FPSRHealthSet.h / FPSRCombatSet.h.
    ATTRIBUTE_ACCESSORS_BASIC(UFPSRMeasureAttributeSet, Health)
    ATTRIBUTE_ACCESSORS_BASIC(UFPSRMeasureAttributeSet, MaxHealth)
    ATTRIBUTE_ACCESSORS_BASIC(UFPSRMeasureAttributeSet, AttackPower)
    ATTRIBUTE_ACCESSORS_BASIC(UFPSRMeasureAttributeSet, MoveSpeedMult)

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** 풀 재사용 시 값을 처음으로 되돌린다. 🔴 필수 — CancelAbilities / RemoveActiveEffects /
     *  ClearAbility 중 무엇도 어트리뷰트에 손대지 않으므로, 이걸 부르지 않으면 값이 삶을 넘어 이어진다. */
    void ResetForMeasure();

protected:
    UFUNCTION() void OnRep_Health(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_MaxHealth(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_AttackPower(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_MoveSpeedMult(const FGameplayAttributeData& Old);
};
```

```cpp
/** 측정 부하용 더미. ActivateAbility 에서 GE 를 자신에게 적용하고 **즉시 EndAbility** 한다.
 *
 *  🔴 즉시 종료하지 않으면 활성 상태로 남아 이후 TryActivateAbility 가 전부 거부되고, 구성 ③ 이
 *     조용히 ② 와 같아진다(= 측정이 무음으로 무효가 된다). 리포의 유일한 자기활성 선례
 *     UFPSRPassiveAbility(:51-64)는 **상시 활성 패시브**라 그대로 따라가면 이 함정에 빠진다.
 *
 *  부모는 UFPSREliteGameplayAbility 가 **아니다** — 그 클래스의 쿨다운은 엘리트 전용 시계를 읽는데,
 *  여기서 붙는 대상은 일반 적이다. 주기는 AFPSREnemyBase 의 누산기가 소유한다(§5-C). */
UCLASS()
class FPSROGUELITE_API UFPSRMeasureDummyAbility : public UGameplayAbility
{
    GENERATED_BODY()
public:
    UFPSRMeasureDummyAbility();   // ⚠️ 생성자에서 아래 2개를 **반드시 명시**한다
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                 const FGameplayAbilityActorInfo* ActorInfo,
                                 const FGameplayAbilityActivationInfo ActivationInfo,
                                 const FGameplayEventData* TriggerEventData) override;

    /** 적용할 Instant GE. C++ 기본값 = UFPSRMeasureInstantGE (콘텐츠 저작 불요 — 측정 전용이다). */
    UPROPERTY(EditDefaultsOnly, Category = "FPSR|Measure")
    TSubclassOf<UGameplayEffect> EffectToApply;
};
```

> 🔴 **생성자 명시 2건** (빠뜨리면 측정이 오염된다)
> - `InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor`
>   — 엔진 기본값은 **`InstancedPerExecution`**(`GameplayAbility.cpp:102`)이라, 그대로 두면 N=0.2 에서
>     **초당 1500개 UObject 생성 + GC 압력**이 ③ 에 섞인다.
> - `NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly`
>   — 액터 소유 ASC 의 리포 선례(`FPSRFreezeCooldownAbility.cpp:5-11`)와 같은 경로. 실제 엘리트
>     어빌리티가 지불할 경로와 맞춘다.

```cpp
/** Instant + Health Modifier 1개. Instant 이므로 EnableTimeAxisGuard() 에 걸리지 않는다
 *  (GetPeriod() 가 Instant 에 NO_PERIOD 를 반환 — FPSRAbilitySystemComponent 의 판정 조건). */
UCLASS()
class FPSROGUELITE_API UFPSRMeasureInstantGE : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UFPSRMeasureInstantGE();  // DurationPolicy = Instant, Modifiers[0] = Health Additive
};
```

### 5-C. `AFPSREnemyBase` 측정 시임 — **비-리플렉션 + `#if` 가드**

> 🔴 `UPROPERTY` 도 `UCLASS` 와 **같은 규칙**이라 `#if !UE_BUILD_SHIPPING` 안에 둘 수 없다
> (`UhtTokenBufferReader.cs:725`). 리플렉션 멤버로 만들면 Shipping 빌드의 **스웜 액터 500개 전부에**
> 측정 시임이 상주한다 — 사용자 결정 4 가 허용한 것은 *디버그* 시임이지 프로덕션 구조가 아니다.
> **리포에 정확한 선례가 있다**: `bDebugAnimPinned`(`FPSREnemyBase.h:858-863`) = 비-리플렉션 멤버를
> `#if !UE_BUILD_SHIPPING` 안에 둔 것. 그 패턴을 그대로 쓴다.

```cpp
#if !UE_BUILD_SHIPPING
    // --- GASM1 측정 시임. 전부 비-UPROPERTY: UHT 가 #if 안의 UPROPERTY 를 거부하기 때문이고
    //     (UhtTokenBufferReader.cs:725), 선례는 bDebugAnimPinned 다.
    //     GC 안전한 이유: 부착된 컴포넌트는 AActor::OwnedComponents 가 강참조로 살려 둔다.
    //     TObjectPtr 이 아니라 raw 인 이유도 같다 — 소유권이 여기 있지 않다.
    UFPSRAbilitySystemComponent* MeasureASC = nullptr;
    UFPSRMeasureAttributeSet*    MeasureSet = nullptr;
    FGameplayAbilitySpecHandle   MeasureAbilityHandle;
    float                        MeasureClockSeconds = 0.0f;  // 프리즈-멈춤 누산기
    int32                        MeasureActivationCount = 0;  // N 사후 검증용(ASCDump 가 읽는다)
    bool                         bMeasureLoadoutCached = false; // Activate 에서 1회 캐시 — 틱에서 CVar 를 읽지 않는다
    float                        MeasureCadenceCached = 1.0f;   // 🔁 추가(구현 검증) — 아래 참조
#endif
```

> 🔁 **`MeasureCadenceCached` 추가 사유**(구현 후 Opus 검증에서 드러남): 캐던스를 틱에서 CVar 로 조회하면
> **그 조회 비용이 구성 ③ 에만 붙어 ②↔③ 델타로 새어든다** — 재려는 것이 "GAS 를 쓰는 비용"인데 거기에
> "CVar 를 읽는 비용"이 섞인다. 구성별로 별도 기동하므로(§12-A) 캡처 도중 값이 바뀌지 않아 캐시로 충분하다.
> 허용값 검증·스냅·엣지트리거 경고는 `Activate` 의 그 1회 호출 안에서 처리한다.

## 6. 함수별 계약

| 함수 | 권위 | 호출자 | 전제조건 | 실패 시 동작 |
|---|---|---|---|---|
| `AcquireEnemy` 의 `ForceSpawnClass` 분기 | 서버 | 기존 4곳 | CVar 비어 있지 않음 | 이름 불일치 = 경고 1회(edge-triggered) 후 로스터 기본 |
| `AFPSREnemyBase::Activate` 의 측정 부착 | 서버 | 풀 | `CVarAttachASC \|\| CVarMeasureLoadout` | CVar off = 완전 no-op |
| `AFPSREnemyBase::ServerTickAttack` 의 구동 | 서버 | `TickEnemyMovement` | `bMeasureLoadoutCached` | false = bool 검사 1회로 끝 |
| `FPSR.Debug.ExecAfter` | — | 러너 `-ExecCmds` | 인자 2개 | 파싱 실패 시 경고 |
| `FPSR.Debug.ASCDump` | 서버 | 러너 `ExecAfter` | — | 서브시스템 없으면 조기 반환 |

### 6-A. 부착 — **액터 실수명당 1회** (🔴 G1 P2-1)

`Activate` 는 **풀 재사용마다** 돈다. 디렉터의 rear-drain·재충전이 캡처 도중에도 같은 풀 액터를
재활성화하므로(`Performance.md:47` "요청 300 → 정착 254" 가 그 실재 증거), 멱등하지 않으면 ASC 가
하나씩 더 붙어 전부 틱·복제하고 **①↔② 델타가 churn 횟수에 비례해 부푼다**.

```
Activate(Location):
  Super 의 SetNetDormancy(DORM_Awake) 뒤에서 실행할 것 (부착의 복제가 깨어난 뒤 실리도록)

  if (!bAttach && !bLoadout) return;                    // CVar off = no-op

  // 🔴 이중 부착 가드 — **자기 것이 아닌** ASC 만 걸러낸다(🔁 정정, 구현 후 Opus 검증).
  //    `if (FindComponentByClass<...>()) return;` 로 쓰면 두 번째+ 삶에서 지난 삶의 MeasureASC 를 찾아내
  //    아래 tail 까지 스킵한다 → ResetForMeasure() 누락 · MeasureActivationCount 가 실수명 누적치로 변질 ·
  //    **bMeasureLoadoutCached 갱신 누락**(러너가 CVar 를 세우기 전에 한 번이라도 Activate 된 액터는 캐시가
  //    false 로 굳어 그 캡처 내내 측정에서 조용히 빠진다). 풀은 액터를 파괴하지 않으므로(불변식 5)
  //    재활성화는 예외가 아니라 상시다 — §5 의 "요청 300 → 정착 254"(Performance.md:47)가 그 증거.
  ExistingASC = FindComponentByClass<UAbilitySystemComponent>();
  if (ExistingASC && ExistingASC != MeasureASC) return;  //    엘리트의 진짜 AbilitySystem = 얹지 않는다

  if (!MeasureASC)                                       // 🔴 실수명당 1회
  {
      MeasureASC = NewObject<UFPSRAbilitySystemComponent>(this, TEXT("MeasureASC"));
      MeasureASC->SetIsReplicated(true);                 // RegisterComponent 앞
      MeasureASC->RegisterComponent();
      MeasureASC->SetReplicationMode(Minimal);           // 엘리트와 동일
      MeasureASC->InitAbilityActorInfo(this, this);
      MeasureASC->EnableTimeAxisGuard();
  }
  if (bLoadout)
  {
      if (!MeasureSet)                                   // 🔁 정정(구현 중 엔진 대조): GetOrCreateAttributeSubobject
          MeasureSet = const_cast<UFPSRMeasureAttributeSet*>(          // 는 **protected** 라(AbilitySystemComponent.h:1893,
              MeasureASC->AddSet<UFPSRMeasureAttributeSet>());         // 직전 접근지정자 = :1681 protected:) 무관한
                                                         // 클래스에서 직접 못 부른다. 공개 래퍼 AddSet<T>() 의
                                                         // 본체가 정확히 그 호출이다(:153-157) — 같은 함수이고
                                                         // IsA 클래스-중복 필터링(:132-140)도 그대로다.
                                                         // 반환이 const 인 것도 원문 설계라 const_cast 한다.
                                                         // ⚠️ AddSpawnedAttribute(:3207)는 포인터 중복만 거른다 — 쓰지 말 것
      MeasureSet->ResetForMeasure();                     // 🔴 값은 삶을 넘어 이어진다
      if (!MeasureAbilityHandle.IsValid())               // 🔴 실수명당 1회 — 삶마다면 스펙이 누적된다
          MeasureAbilityHandle = MeasureASC->GiveAbility(FGameplayAbilitySpec(DummyClass, 1, INDEX_NONE, this));
  }
  MeasureClockSeconds = 0.0f;  MeasureActivationCount = 0;
  bMeasureLoadoutCached = bLoadout;
```

**엔진 경로 대조 (전부 실물 확인)** — 런타임 등록이 CDO 서브오브젝트와 정상 상태에서 같은 이유:

| 필요한 것 | 근거 |
|---|---|
| 틱 함수 등록 + `InitializeComponent` | 🔁 정정(G2): 오너가 있는 이 경우의 실제 경로는 `Actor.cpp:6427-6452` `HandleRegisterComponentWithWorld` 다. `ActorComponent.cpp:2040-2047` 의 분기는 `MyOwner == nullptr` 경로라 여기 해당하지 않는다 — **결론(틱 등록 + `InitializeComponent` 가 돈다)은 같다** |
| `bAutoActivate` 발동 | `UActorComponent::OnRegister` → `if (Owner->IsActorInitialized()) Activate(true)` |
| 컴포넌트 `BeginPlay` → `CacheIsNetSimulated` | 같은 함수 후반 `if (bHasBegunPlay)`; `AbilitySystemComponent.cpp:203,259` |
| `SetIsReplicated` 를 `NewObject` 직후 호출해도 ensure 안 뜸 | `NeedsInitialization()` = `RF_NeedInitialization`(`ActorComponent.cpp:3415-3418`) — `NewObject` 반환 시점에 이미 꺼져 있다 |
| 복제 목록 등록 | `Actor.cpp:3856-3864` `UpdateReplicatedComponent → ReplicatedComponents.AddUnique`; 순회 = `ActorReplication.cpp:600` |
| 클라 쪽 `ActorInfo` 초기화 | `_Abilities.cpp:270-277` `OnRep_OwningActor → InitAbilityActorInfo` |

### 6-B. 구동 — `ServerTickAttack`

```
ServerTickAttack(Ctx):
  Super::ServerTickAttack(Ctx);
  if (!bMeasureLoadoutCached) return;            // 멤버 bool 1회 — CVar 조회 아님
  MeasureClockSeconds += Ctx.DeltaSeconds;       // 🔴 Ctx.DeltaSeconds = §2-2 프리즈 중 멈춘다
  if (MeasureClockSeconds >= Cadence)
  {
      MeasureClockSeconds = 0.0f;
      if (MeasureASC->TryActivateAbility(MeasureAbilityHandle)) ++MeasureActivationCount;
  }
```

- 형태는 **보스 선례 `FPSRBossBase.cpp:960-962` 를 그대로 복제**한다.
- ⚠️ `ServerTickAttack` 은 tier 별 `AttackStride` 로 스킵된다(F1) → **활성화 시도 빈도가 tier 에 따라
  다르다.** 보고서에 명기하고, `MeasureActivationCount` 로 실제 발동 수를 확인한다.
- ⚠️ 순수 `UGameplayAbility` 가 서버 소유 AI ASC 에서 활성화되는 근거: `IsLocallyControlled()` 가
  비-Pawn 오너에서 `IsNetAuthority()` 로 떨어져 서버에서 true(`GameplayAbilityTypes.cpp:107-126`)
  → `_Abilities.cpp:1764-1766` 의 "not local" 거부를 통과한다.

### 6-C. 티어다운

`Deactivate`/`EnterDyingState` 에서 `bMeasureLoadoutCached` 아래에 엘리트와 **같은 쌍**
(`CancelAbilities()` + `RemoveActiveEffects(FGameplayEffectQuery())`)을 둔다. 빈 컨테이너라 비용은
미미하지만, **실제 엘리트가 매 티어다운에 지불하는 경로**라 구성 ③ 의 대표성이 오른다.
🔁 정정(G2 P3): 이 쌍은 `bMeasureLoadoutCached` 게이트 안이라 **③·③ᵀ 에서만 돈다** — ② 는 밟지 않는다.
⚠️ ASC 는 파괴하지 않는다(실수명당 1회 원칙).

## 7. 복제표

| 프로퍼티 / 대상 | 종류 | Push Model | 조건 | 비고 |
|---|---|---|---|---|
| `UFPSRMeasureAttributeSet` 4종 | `ReplicatedUsing` | 미사용 | `COND_None` | **복제 트래픽이 측정 대상**이므로 조건을 좁히지 않는다 |
| `MeasureASC` 컴포넌트 | 동적 복제 서브오브젝트 | — | — | `SetIsReplicated(true)` → `ReplicatedComponents` |
| ASC 자체의 복제 | `Minimal` | — | — | 엘리트와 동일(`FPSREnemyEliteBase.cpp:18`) |
| RPC | 없음 | — | — | 측정 수단은 RPC 를 추가하지 않는다 |

> ⚠️ 패키지 빌드에서 Push Model 은 컴파일 아웃된다 → ⑤·⑤′ 수치는 **그 상태의 절대비용**이다.
> ⚠️ 클라 쪽은 동적 서브오브젝트를 생성·등록한다(엘리트는 기본 서브오브젝트). 호스트 수치와는 무관.

## 8. 수명주기 · 소유권

- **생성/등록**: `Activate` 에서 **액터 실수명당 1회**(§6-A). 서버 전용.
- **해제**: 하지 않는다 — 풀 액터는 파괴되지 않고, ASC 를 붙였다 뗐다 하면 측정 대상이 바뀐다.
  CVar 를 꺼도 이미 붙은 ASC 는 남는다(**구성별 별도 기동**이라 실해 없음 — 보고서 한 줄).
- **GC 소유**: `AActor::OwnedComponents` 가 `MeasureASC` 를, ASC 의 `SpawnedAttributes` 가
  `MeasureSet` 을 살린다. 비-UPROPERTY raw 포인터가 안전한 이유가 이것이고, **주석으로 남긴다**.
- **델리게이트**: 추가하지 않는다.

## 9. 데이터드리븐 경계

| 값 | 나가는 곳 | 기본값 | 비고 |
|---|---|---|---|
| 강제 스폰 클래스 | CVar(러너 인자) | `""` | **에셋 경로 하드코딩 금지** — 로스터 규칙에서 이름으로 찾는다 |
| 캐던스 N | CVar(러너 인자) | 1.0 | 허용 1.0 / 0.2 — 코드에서 검증·스냅 |
| 어트리뷰트 초기값 | C++ 상수 | Health 100 / Max 100 / Atk 10 / Move 1.0 | 측정 전용이라 콘텐츠로 빼지 않는다(**소비자 0**) |

## 10. 성능 예산

> 🔁 **전면 개정 (G2 P2-1, 2026-09-12).** 이 절의 원문은 "ASC 는 액터당 틱 함수 1개(상시). 300 마리 =
> 틱 함수 300개. **이것이 측정 대상 그 자체다**" 였다. **엔진 실물과 모순이라 폐기한다.**
>
> **유휴 ASC 는 첫 틱 뒤 스스로 꺼진다** — 자기해제 체인(UE 5.8 실물, 전부 대조 확인):
> ```
> UGameplayTasksComponent::TickComponent        GameplayTasksComponent.cpp:303-307
>   → NumActuallyTicked == 0 이면 UpdateShouldTick()
> UGameplayTasksComponent::UpdateShouldTick     :323-330
>   → GetShouldTick() == false 면 SetActive(false)
> UAbilitySystemComponent::GetShouldTick        AbilitySystemComponent_Abilities.cpp:223-247
>   → ① 권위 + 몽타주 복제중(RepAnimMontageInfo.IsStopped == false) ② Super = TickingTasks.Num() > 0
>     ③ ITickableAttributeSetInterface 세트가 ShouldTick() — 셋 다 아니면 false
> UActorComponent::SetActive(false) → Deactivate() → SetComponentTickEnabled(false)
>                                               ActorComponent.cpp:2841-2854, 2824-2832
> ```
> 🪤 **왜 놓쳤나**: 자기해제는 `GameplayAbilities` 플러그인이 아니라 **부모 클래스**
> `UGameplayTasksComponent`(`Engine/Source/Runtime/GameplayTasks/`)에 있다. 플러그인 안에서만
> `UpdateShouldTick` 을 grep 하면 몽타주 호출 1건만 잡히고 **상시 틱으로 오독하게 된다.**
>
> **함의**: 구성 ②·③ 의 합성 부하는 위 세 조건 중 **하나도** 만족하지 않는다(더미는 즉시 `EndAbility`
> 라 태스크 0 · GE 는 Instant · 측정 세트는 기본적으로 Tickable 아님). 그래서 ③ᵀ 를 둔다.

- **틱** — 세 갈래로 갈린다.
  - **유휴 ASC(구성 ②, ③)**: 첫 프레임 1회 틱 후 `SetComponentTickEnabled(false)`. 정상 상태 틱 비용 **0**.
  - **틱이 켜진 ASC(구성 ③ᵀ)**: `GetShouldTick()` 이 true 를 유지하는 동안 매 프레임.
    실제 엘리트 어빌리티(AbilityTask·몽타주·지속 GE)가 여기 해당한다. **이것이 틱 축의 측정 대상이다.**
  - 전이 자체(활성↔비활성)도 공짜가 아니다 — `UpdateShouldTick` → `SetActive` → 틱 등록/해제.
- **액터당 비용**
  - 구성 ②: ASC 본체 + `ReplicatedComponents` 항목 1 + 복제 서브오브젝트 1 + GC 대상 1 (**틱 0**)
  - 구성 ③: + AttributeSet 1(복제) + 어빌리티 인스턴스 1 + N 초마다 Instant GE 1회 (**틱 0**)
  - 구성 ③ᵀ: + **상시 틱 1** (`ITickableAttributeSetInterface` 가 `GetShouldTick()` 을 true 로 고정)
- **기본 경로 비용(CVar off)**: `Activate` 에서 CVar 조회, `ServerTickAttack` 에서 **멤버 bool 1회**.
- **완화**: 하지 않는다 — 완화하면 재려던 것이 사라진다. (단 **엔진이 스스로 하는 완화**는 건드리지
  않는다. 그것을 끄면 출시 코드와 다른 것을 재게 된다 — ③ᵀ 는 틱을 *강제로 켜는* 것이 아니라
  엔진이 정한 조건(`Tickable` 세트)을 **합법적으로 만족시켜** 켜지게 하는 것이다.)

## 11. 미결정 항목

- **⑤ 의 `ServerRepActors ≤1.5ms`** 는 일반 스웜 N-1 결정용으로 등록된 기준
  (`U14R_PerfMeasureRegistry.md:62`)이다. GASM1 에 재사용하려면 **사용자 비준 필요** —
  비준 전에는 수치만 보고하고 합/불을 적지 않는다.
- **「성능 재측정」 행**(https://app.notion.com/p/3b93972ddd88812b9c21cdeb0e9f41a3)은 로스터 기본
  혼합을 요구해 이 7회에 없다. 같은 패키지로 2회 더 돌리면 닫힌다 — **사용자 선택**.

**갭 처리 규칙**: 구현 중 명세에 없는 판단이 필요해지면 **추측해서 채우지 말고 멈추고 "명세 갭"으로
보고**한다.

## 12. 검증 기준

| # | 검사 | 통과 조건 |
|---|---|---|
| 1 | 명세 대조 | §5·§6·§7 의 선언·시그니처·복제 설정이 코드와 1:1 |
| 2 | **UHT 가드** | 측정 UCLASS 3종이 `#if` **밖**, `AFPSREnemyBase` 의 §5-C 측정 멤버가 **하나도 빠짐없이** `#if !UE_BUILD_SHIPPING` **안**이며 **전부 비-UPROPERTY**(개수를 세지 말고 `git diff` 의 헤더 블록 전체를 본다 — 숫자를 적으면 멤버가 늘 때마다 드리프트한다) |
| 3 | **멱등성** | `Activate` 2회 호출로 ASC·핸들이 **늘지 않는다**. 엘리트에 CVar 를 켜도 두 번째 ASC 가 안 붙는다.<br>🔑 **실증 = 측정 캡처 자체**: `ASCDump` 의 `instances: ASC N x …` 에서 N 이 그 시점 생존 수와 **일치**해야 한다. 재활성화마다 붙었다면 N 이 생존 수를 넘는다(풀은 액터를 파괴하지 않으므로 재활성화는 상시다 — §5 의 "요청 300 → 정착 254"). 별도 테스트를 만들지 않는다 |
| 4 | 빌드 | `Build.bat FPSRogueliteEditor Win64 Development -DisableUnity` **Succeeded** (로그의 `Result:` 줄로 판정 — 종료코드 아님) |
| 5 | 헤드리스 스모크 | `FPSRoguelite.Smoke.ModuleLoads` 통과 |
| 6 | 기본 경로 무변경 | CVar 전부 off 로 기존 자동화 전부 통과(`FPSRStatusWorldTest` 포함) |
| 7 | **러너 게이트** | `delivered N/N` + `Composition: BP_EnemyRangedBase_C xN` 확인. 미달 시 실패 보고 |
| 8 | **MemReports 수집** | `-MemReport` 시 러너가 파일 완료를 기다렸다가 `Packaged/Measurements/<Label>/` 로 복사 |
| 9 | 레드팀 게이트 | Fable G2. **P1 잔존 시 머지 금지** |

### 12-A. 러너 프로토콜 (8회, 전부 `ForceSpawnClass=BP_EnemyRangedBase_C`)

| # | 인자 | 답하는 질문 | 정상 상태 ASC 틱 |
|---|---|---|---|
| ① | 300 | 기준선 (ASC 없음) | — |
| ② | 300 `-AttachASC` | **ASC 를 붙이는 비용** (①↔②) | **0**(엔진 자기해제) |
| ③ | ② `-MeasureLoadout -Cadence 1.0` | GAS 사용 비용 — **하한**(Instant GE 만) (②↔③) | **0** |
| ③′ | ② `-MeasureLoadout -Cadence 0.2` | 캐던스 민감도 | 0 |
| ③ᵀ | ③ `-MeasureTickable` | GAS 사용 비용 — **상한**(틱이 켜진 상태) (③↔③ᵀ) | **300** |
| ④ | 500 · ③ᵀ 구성 | 스트레스 | 500 |
| ⑤ | ③ᵀ `-ClientCount 3` | 복제 비용(4인) | 300 |
| ⑤′ | ① `-ClientCount 3` | **⑤ 의 대조군** — 없으면 델타를 못 낸다 | — |

> 🔁 **③ᵀ 신설 사유 (G2 P2-1, 사용자 결정 5 · 2026-09-12)**: ②③ 의 합성 부하는 엔진의 틱 조건 세 가지를
> 하나도 만족하지 않아 **정상 상태 틱이 0** 이다(§10). 그래서 ②③ 만으로는 *"실제로 GAS 를 쓰면 얼마인가"*
> 의 틱 축이 통째로 빠진다 — 실제 엘리트 어빌리티는 AbilityTask·몽타주를 쓰고 그러면 틱이 켜진다.
> ③ᵀ 는 측정 AttributeSet 이 `ITickableAttributeSetInterface` 를 구현하게 해(엔진이 정한 조건을
> **합법적으로 만족**시켜) 틱을 켠다. **틱을 강제로 켜는 우회가 아니다.**
> **③ 과 ③ᵀ 사이가 실제 엘리트 어빌리티가 떨어질 범위다** — 보고서는 둘을 함께 적는다.

**판정선** — §5 기존(적300 평균 ≥60fps · P95 ≤20ms · 스웜 렌더 ≤4ms @300)을 **②·③·③ᵀ 세 절대값에**
적용한다. *"붙이기만 해도 되는가"* · *"Instant GE 만 써도 되는가"* · *"틱까지 켜도 되는가"* 는 서로 다른
답이다. 스케일 1배라 기준선과 렌더 조건이 같으므로 이 절대 판정이 유효하다.

**판정축** (🔁 G2 P2-1 로 개정)
- **①↔②** = ASC 부착 비용. 축 = **메모리**(§12-A 메모리 계측) · **`Excl/ServerRepActors`** ·
  `GameThread`. ⚠️ **`TickActors`·`AbilityTasks` 는 여기서 ≈0 이 정상이다** — 0 이 나왔다고
  "비용 없음"이 아니라 **틱 축이 이 구성에 존재하지 않는다**는 뜻이다. 보고서에 그렇게 적는다.
- **②↔③** = Instant GE 구동 비용(틱 무관).
- **③↔③ᵀ** = **틱 축 그 자체.** 여기서 `TickActors`·`AbilityTasks` 가 처음으로 유의미해진다.
- **⑤−⑤′** = GAS 와이어 비용.

**러너 종료 순서** (🔴 G1 P2-3) — `-MemReport` 일 때:
1. `ExecAfter <t> CsvProfile Stop` 을 **먼저** 예약해 캡처를 시간 기준으로 닫는다
   (캡처는 `Frames=N` 으로도 끝나므로 `Frames` 를 충분히 크게 두어 Stop 이 먼저 오게 한다)
2. 그 뒤 `ASCDump` → `obj list class=FPSRAbilitySystemComponent` → `memreport -full`
3. 러너의 kill 조건을 "CSV 크기 10초 무변화"가 **아니라** `MemReports/` 새 파일 존재 + 크기 안정으로
   바꾼다 — 그러지 않으면 300 액터 월드의 `memreport -full` 이 kill 에 잘린다
4. 그 파일을 `Packaged/Measurements/<Label>/` 로 복사

**메모리 계측 규칙** — `obj list` 는 객체 **본체를 세지 않는다**(컬럼 = `Class|IncMax|IncNum|ResExc|…`,
값 = `FArchiveCountMem` 컨테이너 합 + `ResourceSize`; `UObject::Serialize` 는 카운팅 아카이브에 본체를
더하지 않는다 — `UnrealEngine.cpp:9463-9490`). 유휴 ASC 는 **본체가 지배항**이므로:
- **1차** = `ASCDump` 의 `UClass::GetStructureSize()` × 인스턴스 수 (ASC · 측정셋 · 더미 3종)
- **2차** = `obj list` 의 `IncNum`/`ResExc` (컨테이너 페이로드)
- **참고치** = `PhysicalUsedMB` — 프로세스 워킹셋(`WindowsPlatformMemory.cpp:342`)이고 §5 자신이 실행 간
  변동 15%를 기록했다(`Performance.md:93`)
- **잔여 과소 항**: `AbilityActorInfo`(`TSharedPtr`, 비-리플렉션 힙)는 세 방법 어디에도 안 잡힌다 — 명기

### 12-B. `ExecAfter` 구현 제약

- **호출마다 새 `FTimerHandle`** — 하나를 재사용하면 앞 예약이 덮인다
- 실행은 `GEngine->Exec(World, Cmd)` — `UnrealEngine.cpp:5722` 에서 콘솔 매니저까지 닿아
  `FPSR.*`·`obj`·`memreport` 를 전부 처리한다
- **예약 명령에 쉼표 금지** — `ParseExecCommands.cpp:9` 가 쉼표로 자른다
- 선례 = `FPSRPlayerController.cpp:912-977`(Invuln/SkipCards 타이머)

## 13. 레드팀 지적 원장

*(C3 검증 전용 — 구현·G2 후 채운다)*

**G1 플랜 게이트 이력**(참고, 이 문서의 근거): 3라운드 · P1 4 · P2 11 · P3 13 · **전량 수용, 기각 0**.
rev.3 판정 = P1 0건, 조건부 착수 가능(P2 3건을 이 문서에 명문화 — §6-A 멱등성 · §5-C 멤버 거처 ·
§12-A 러너 MemReports). 그 3건이 이 문서의 §6-A·§5-C·§12-A 다.
