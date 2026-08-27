# HB1 — 적 체력바 네이티브 승격 (위젯 컴포넌트 + 바인딩을 C++ 로)

## 1. 메타

| 항목 | 값 |
|---|---|
| 유닛 ID / 이름 | `HB1` / 적 체력바 네이티브 승격 |
| 브랜치 | `feat/enemy-native-healthbar` |
| 작성 모델 | `claude-opus-5` (§6-5-2 C1 = Opus) |
| 작성일 | 2026-08-27 |
| 상태 | `확정` — **rev3** · G1 통과(3회차, P1/P2 0 · P3 2건 반영) · 사용자 승인 대기 |
| 관련 SSOT | `Performance.md` §5-1 · `Enemy.md` §2-6 · `Game.md` §1 |
| 관련 메모리 | `[[cpp-uproperty-name-collides-with-bp]]` · `[[da-edits-are-user-work]]` · `[[bp-node-edits-are-user-work]]` · `[[code-is-immutable-structure-only]]` · `[[reason-in-multiplayer-terms]]` |
| 보드 행 | https://app.notion.com/p/3c93972ddd88817b8563fd4dc7760004 |
| 선행 | LOD1(`957928c2` + rev4 `8b315695`) — 완료 |

---

## 2. 목표 / 비목표

### 목표

**적이 체력바를 갖는 것이 아키타입 저작의 선택이 아니라 베이스의 계약이 된다.**

1. 모든 `AFPSREnemyBase` 파생이 **체력바 위젯 컴포넌트를 네이티브로** 갖는다 — BP 마다 손으로 붙이지 않는다.
2. **체력 컴포넌트와의 바인딩도 C++ 가 한다** — BP 이벤트 그래프 배선이 아키타입마다 필요하지 않다.
3. 위젯 클래스는 **config 소프트경로**에서 온다(C++ 하드코딩 0). BP 가 아키타입별로 덮을 수 있다.
4. LOD1 이 남긴 **문서 오류(월드공간 ↔ 스크린 공간)를 정정**한다.

**왜 지금 구조가 안 되는가(실측)** — 적 BP 3종 중 위젯 컴포넌트가 있는 것은 `BP_EnemyMeleeBase` 하나뿐이다. `BP_EnemyRangedBase`(사용자가 말한 "큐브형", 로스터 [1] weight 0.5)와 `BP_EnemyEliteBase` 에는 **없다** → `FindComponentByClass<UWidgetComponent>()` 가 null → 체력바가 뜬 적이 없다. **셋 중 둘이 잊혔다는 것이 이 구조에 대한 평가다.**

### 비목표

- **`WBP_EnemyHealthBar` 의 내부 로직을 바꾸지 않는다.** 이 유닛은 위젯을 **어떻게 만들고 언제 묶는지**만 소유한다. 바 자체의 표현·애니메이션은 콘텐츠다.
- **「적 재스폰 시 체력바 잔존 표시 버그」(M1 백로그, `3b53972ddd8881cb9e94e84cbbef8385`)를 고치지 않는다.** 같은 영역이지만 원인이 `OnRep_bDead` 엣지 처리 타이밍으로 추정돼 있고 미확인이다. **다만 이 유닛이 그 버그를 악화시키지 않음을 §12 에서 확인**하고, 네이티브 경로가 그 조사를 쉽게 만든다는 점만 기록한다.
- **LOD 밴드 로직을 바꾸지 않는다.** 2축 가시성(수명주기 AND 거리)·히스테리시스·패스 순서는 LOD1 그대로다. 소비하는 **포인터의 출처만** 바뀐다.
- **엘리트 전용 보스형 체력바를 만들지 않는다.** BP 가 컴포넌트의 위젯 클래스를 덮는 길만 열어 둔다.
- **`DefaultGame.ini` 의 5m 관찰용 임시치를 원복하지 않는다** — M2 이월 행(2인 MP 검증)이 소유한다.

---

## 3. 제1원리 3줄

1. **제1원리 근거** — 적 200~300 각각에 위젯이 붙으므로 **비용은 여전히 액터당**이다. 그래서 네이티브화가 "무조건 켜기"가 되면 안 된다: LOD1 의 거리 밴드가 그대로 위에 얹혀야 하고(그것이 이 승격의 전제), 전용 서버에서는 아예 만들지 않는다. 네이티브화가 사는 이유는 성능이 아니라 **누락 불가능성** — 잊혀서 안 뜨는 것이 잊혀서 켜지는 것보다 나쁘다(체력바는 장식이 아니라 전투 판독의 일부다).
2. **엔진 기본값·기존 인프라와의 관계** — `UWidgetComponent` 의 동작을 **그대로 쓴다**. 스크린 공간의 실제 체인(전부 소스 실측): `SetHiddenInGame(true)` → 컴포넌트 `IsVisible()` false → `UpdateWidgetOnScreen()`(`WidgetComponent.cpp:1306-1328`)이 `RemoveWidgetFromScreen()`(`:1548`)을 골라 **스크린 레이어에서 엔트리 제거 → 슬레이트 참조 해제** → **≤1틱 내**(같은 틱에도 발화 가능 — `UpdateWidget` 이 가드보다 앞이다) `UWidget::IsVisible()` 이 **캐시된 슬레이트가 없어 false**(`Widget.cpp:395-404`) → `IsWidgetVisible()` false → `TickComponent` 의 `Widget && TickMode != Enabled && !IsWidgetVisible()` 가드(`:1262`)가 **`SetComponentTickEnabled(false)` 를 발화**한다. 즉 **절감은 둘 다 실재한다** — 스크린 레이어 제거(투영·페인트 소멸) *그리고* 자기-틱-차단. 기존 인프라 = `AFPSREnemyBase` 의 2축 가시성 계약·`InitHealthBarWidget` 훅·`UFPSREnemyRenderSettings`(헬스바 LOD 노브가 이미 여기 산다)를 **재사용**하고 새 서브시스템을 만들지 않는다.
3. **프로젝트 제약과의 정합** — 체력바는 **클라이언트 비주얼**이고 복제 대상이 아니다(신규 복제 0). 리슨서버 호스트도 뷰어이므로 호스트에서도 만든다. 데이터드리븐 경계: 위젯 **클래스**는 config(에셋 경로 하드코딩 금지), 위젯 **배치값**(크기·높이)은 ctor 기본값 + BP 오버라이드(아키타입마다 키가 다르므로 인스턴스별 편차가 정상).

---

## 4. 파일 목록

| 경로 | 신규/수정 | 한 줄 설명 |
|---|---|---|
| `Source/FPSRoguelite/Public/UI/FPSREnemyHealthBarWidget.h` | **신규** | 적 체력바 위젯의 C++ 베이스 — 바인딩 계약 1개만 |
| `Source/FPSRoguelite/Public/Settings/FPSREnemyRenderSettings.h` | 수정 | 위젯 클래스 소프트경로 추가 |
| `Source/FPSRoguelite/Public/Enemy/FPSREnemyBase.h` | 수정 | 네이티브 위젯 컴포넌트 + 캐시 멤버 제거 |
| `Source/FPSRoguelite/Private/Enemy/FPSREnemyBase.cpp` | 수정 | ctor 생성 · BeginPlay 초기화/바인딩 · 문서 정정 |
| `Source/FPSRoguelite/Private/Enemy/FPSREnemyCosmeticLODSubsystem.cpp` | 수정 | 주석의 "월드공간" 정정만(로직 무변경) |
| `Docs/Specs/LOD1_EnemyDistanceBand.md` | 수정 | §3-2·§10 의 스크린/월드 공간 오류 정정 **+ §11 백로그 후보의 반대로 적힌 기전 정정**(§6-3) |
| `Source/FPSRoguelite/Private/Tests/FPSREnemyBlueprintParentTest.cpp` | 수정 | 적 BP 3종이 체력바를 갖는지 검사 추가 |

> `.cpp` 본문은 명세하지 않는다(§5·§6 의 선언·계약까지가 범위).

---

## 5. 인터페이스 선언

### 5-1. `FPSREnemyHealthBarWidget.h` (신규)

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "FPSREnemyHealthBarWidget.generated.h"

class UFPSREnemyHealthComponent;

/** 적 머리 위 체력바 위젯의 C++ 베이스. 담는 것은 **바인딩 계약 하나**뿐이다 — 바의 표현·레이아웃·애니메이션은
 *  전부 콘텐츠(WBP)의 몫이다(§6-2 데이터드리븐 경계).
 *
 *  왜 이 클래스가 필요한가: 종전 바인딩은 AFPSREnemyBase 가 OnHealthBarReady(BlueprintImplementableEvent)를 쏘면
 *  **각 적 BP 가** GetUserWidgetObject -> Cast -> InitHealthComp 를 손으로 배선하는 구조였다. 그래서 컴포넌트만
 *  네이티브로 옮겨도 배선은 여전히 아키타입마다 잊힐 수 있다. 위젯 쪽에 C++ 진입점을 두면 베이스가 직접 부를 수
 *  있고, 그때 비로소 "적이면 체력바가 묶인다"가 코드의 계약이 된다. */
UCLASS(Abstract, BlueprintType)
class FPSROGUELITE_API UFPSREnemyHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 이 바가 표시할 체력 컴포넌트를 넘긴다. AFPSREnemyBase::InitHealthBarWidget 이 위젯 생성 직후 **한 번** 부른다
	 *  (액터 수명당 1회 — 위젯은 풀 재사용을 넘어 살아남는다).
	 *
	 *  ⚠️ 이름이 `InitHealthComp` 가 아닌 것은 의도다. 현행 `WBP_EnemyHealthBar` 가 이미 그 이름의 **BP 함수**를
	 *  갖고 있어서, 부모에 동명 이벤트를 두면 재부모화 시 이름 충돌이 난다(같은 계열의 실사고 = LOD1 의
	 *  HealthBarWidget 충돌, [[cpp-uproperty-name-collides-with-bp]]). 이 이벤트는 기존 함수를 **호출만** 하면 된다.
	 *
	 *  BlueprintImplementableEvent = C++ 기본 구현 없음. 구현하지 않은 WBP 는 조용히 아무 일도 하지 않는다(크래시 아님). */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Enemy")
	void BindHealthComponent(UFPSREnemyHealthComponent* HealthComp);
};
```

### 5-2. `FPSREnemyRenderSettings.h` (추가분)

```cpp
	/** 적 머리 위 체력바로 쓸 위젯. **에셋 경로를 C++ 에 하드코딩하지 않기 위한 소프트경로**(핵심원칙 2,
	 *  선례 = UFPSRPlaceholderVisualSettings / UFPSRStageFadeSettings). 기본값은 콘텐츠가 ini 에 저작한다 —
	 *  코드에는 `ConstructorHelpers` 도, 하드코딩 경로도 두지 않는다.
	 *
	 *  비어 있으면 **체력바 기능 전체가 꺼진다** — 컴포넌트는 존재하되 위젯이 없고, InitHealthBarWidget 이 그때
	 *  TickMode 를 Disabled 로 내려 **틱 비용까지 0** 으로 만든다(그 한 줄이 없으면 위젯 없는 스크린 공간 컴포넌트가
	 *  영구히 매 프레임 틱한다 — 엔진 실측, 명세 §6-2 step 4). 그것이 "체력바를 끄는" 지원되는 방법이다.
	 *
	 *  ⚠️ BP 가 네이티브 컴포넌트의 WidgetClass 를 덮었으면 **BP 가 이긴다** — 이 값은 덮이지 않은 경우의
	 *  기본값이다(엘리트 전용 바 같은 아키타입별 분기를 코드 변경 없이 열어 두기 위함). */
	UPROPERTY(Config, EditAnywhere, Category = "Health Bar",
		meta = (DisplayName = "적 체력바 위젯"))  // MetaClass 불요 — TSoftClassPtr 템플릿 인자가 이미 피커를 필터한다(G1 P3-4)
	TSoftClassPtr<UFPSREnemyHealthBarWidget> HealthBarWidgetClass;
```

### 5-3. `FPSREnemyBase.h` (변경분)

```cpp
protected:
	/** 머리 위 체력바. **네이티브인 것이 이 유닛의 핵심**이다 — BP 저작에 맡기면 잊힌다(실측: 적 BP 3종 중 2종에
	 *  없었다). 아키타입은 BP 디테일 패널에서 이 컴포넌트의 값(WidgetClass·DrawSize·RelativeLocation 등)을 덮을 수
	 *  있고, 그때는 BP 가 이긴다.
	 *
	 *  ⚠️ 이름이 `HealthBarWidget` 이 아닌 것은 의도다 — `BP_EnemyMeleeBase` 가 **같은 이름의 수동 컴포넌트를
	 *  이미 저작**해 두었고, 부모에 동명 UPROPERTY 가 생기면 그 BP 의 컴파일이 깨진다(LOD1 실사고,
	 *  [[cpp-uproperty-name-collides-with-bp]]). 사용자가 그 수동 컴포넌트를 제거해도 이름은 되돌리지 않는다
	 *  (안정적인 이름이 더 가치 있다). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPSR|Enemy")
	TObjectPtr<UWidgetComponent> HealthBarWidgetComponent;

	/** BP 훅: 위젯이 만들어지고 C++ 바인딩까지 끝난 **뒤** 발화한다. 베이스가 이미 BindHealthComponent 를 불렀으므로
	 *  **아키타입은 아무것도 안 해도 된다** — 이 훅은 추가 저작(아이콘·이름표 등)을 위한 확장점으로만 남는다.
	 *  종전에는 이 훅이 바인딩 그 자체를 책임졌고, 그래서 잊히면 바가 안 떴다. */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Enemy")
	void OnHealthBarReady();
```

**제거**: `CachedHealthBarWidget`(LOD1 이 도입한 `FindComponentByClass` 캐시). 네이티브 멤버가 곧 그 포인터이므로 캐시도 스캔도 불필요해진다. `ApplyHealthBarVisibility`·`SetHealthBarAllowed`·`SetHealthBarInRange`·`IsHealthBarInRange` **시그니처는 불변**이고 내부에서 참조하는 멤버만 바뀐다.

---

## 6. 함수별 계약

| 함수 | 권위 | 호출자 | 전제조건 | 실패 시 |
|---|---|---|---|---|
| `AFPSREnemyBase` ctor | — | 엔진 | — | 컴포넌트 무조건 생성(넷모드를 알 수 없는 시점) |
| `InitHealthBarWidget()` | 서버+클라 | `BeginPlay` | — | 아래 계약 |
| `UFPSREnemyHealthBarWidget::BindHealthComponent` | 로컬 | `InitHealthBarWidget` | 위젯 인스턴스 유효 | WBP 미구현이면 no-op |
| `ApplyHealthBarVisibility()` | 로컬 | 두 세터 + **재사용 엣지 2곳**(§6-3) | `HealthBarWidgetComponent` 유효 | null 이면 no-op |

### 6-1. ctor 계약

`HealthBarWidgetComponent` 를 `CreateDefaultSubobject` 로 만들고 루트(`Capsule`)에 붙인다.

> ⚠️ **C2 착수 시 실측 확인 1건** — 멜리 수동 컴포넌트의 부착 부모가 `Capsule` 인지 `Mesh` 인지는 **미검증**이다(G1 조사 시점에 에디터가 꺼져 있었다). `Mesh` 였다면 월드 Z 가 약 6cm 달라진다. 에디터에서 확인해 다르면 §11 미결정 2(바 높이)로 흡수한다 — 구현을 막을 사안은 아니다. 기본값은 **현행 `BP_EnemyMeleeBase` 의 실측값을 그대로** 옮겨 무회귀를 보장한다:

| 값 | 기본 | 근거 |
|---|---|---|
| `Space` | `EWidgetSpace::Screen` | 실측(멜리 BP). 월드 공간은 개체마다 렌더타깃을 잡아 스웜 규모에 부적합 |
| `DrawSize` | `(160, 120)` | 실측 |
| `Pivot` | `(0.5, 0.5)` | 실측 |
| `RelativeLocation` | `(0, 0, 120)` | 실측. 캡슐 반높이 기본 90 위 |
| `bDrawAtDesiredSize` | `false` | 실측 |
| `TickWhenOffscreen` | `false` | 실측 |
| `TickMode` | `Automatic` | LOD1 이 도입. **스크린 공간에서도 실제로 동작한다** — 죽은 코드가 아니다(§10 의 기전 참조). 제거 금지 |
| `SetHiddenInGame` | `false` | 2축 가시성의 초기값과 정합 |

### 6-2. `InitHealthBarWidget()` 계약 (재작성)

순서 고정:

1. **전용 서버면 즉시 반환** — `IsRunningDedicatedServer()`. 렌더가 없으므로 위젯을 만들 이유가 없다(이 프로젝트에 전용 서버는 없으나, 없는 것에 기대어 비용을 만들지 않는다). 컴포넌트 자체는 엔진이 이중 차단한다(`WidgetComponent.cpp:1746` `InitWidget` 의 dedi 가드 → `TickMode=Disabled`, `:1243` 첫 틱 가드).
2. `HealthBarWidgetComponent` 유효성 확인 → null 이면 반환.
3. **중복 위젯 경고** — `UWidgetComponent` 가 **2개 이상**이면 `UE_LOG(Warning)` 으로 액터 이름과 함께 알린다. 멜리 BP 의 수동 컴포넌트를 아직 안 지운 상태를 조용히 "바 두 개"로 만들지 않기 위한 진단이다. **아래 4의 조기 반환보다 앞**이어야 한다 — 진단이 가장 필요한 상태(config 공백)에서 안 돌면 의미가 없다.
4. **위젯 클래스 결정** — 컴포넌트의 `GetWidgetClass()` 가 **이미 설정돼 있으면 그대로 둔다(BP 오버라이드 우선)**. 비어 있을 때만 config 소프트경로를 `LoadSynchronous()` 해서 `SetWidgetClass()`.
   - 🔴 **둘 다 비면(= config 미저작) `SetTickMode(ETickMode::Disabled)` 를 호출한 뒤** 반환한다. **이 한 줄이 없으면 위젯 없는 컴포넌트가 매 프레임 영구히 틱한다** — 엔진 실측: `TickComponent` 의 위젯-없음 조기 종료는 `bRenderCleared` 를 요구하는데 그 플래그는 **월드 공간 드로우 경로에서만** set 되고(`WidgetComponent.cpp:1279-1295`), 자기-틱-차단 분기는 `Widget &&` 를 요구하므로(`:1262`) 위젯이 없으면 둘 다 미발화한다. 즉 캡 500 전원이 아무 일도 안 하는 틱을 도는 상태가 되어, **끄기가 오늘(원거리·엘리트 = 컴포넌트 없음 = 비용 0)보다 나빠진다.** 제1원리 직결이므로 선택이 아니라 필수다.
5. `InitWidget()` — 지연 생성을 앞당긴다(종전과 동일 사유).
6. **바인딩** — `Cast<UFPSREnemyHealthBarWidget>(GetUserWidgetObject())` 성공 시 `BindHealthComponent(HealthComponent)`. 실패(다른 위젯 클래스)면 조용히 건너뛴다 — 그건 저작 선택이다.
7. `OnHealthBarReady()` 발화(확장점). ⚠️ **step 4 의 config-공백 반환에 걸리면 이 훅도 발화하지 않는다.** 종전에는 위젯 유무와 무관하게 무조건 발화했으므로(`FPSREnemyBase.cpp:285`) 이것은 **계약 변경**이다 — 전환기 증상은 §11 에 적는다.

### 6-3. 재표시 홀 닫기 (LOD1 잠재 결함 — G1 이 두 번에 걸쳐 진단·교정)

**증상**: **비사망 휴면**(rear-drain 등 `Deactivate` — 액터만 하이드하고 `bHealthBarAllowed` 는 true 유지)으로 돌아온 액터가 **근거리에서 재사용되면 그 생애 내내 체력바가 안 뜬다.**

**기전(소스 실측)**: 숨겨진 위젯은 스크린 레이어에서 빠져 슬레이트가 풀리고 컴포넌트가 스스로 틱을 끈다(§3-2). 틱을 되켜는 유일한 엔진 경로는 `OnHiddenInGameChanged`(`WidgetComponent.cpp:808-820`)인데 그것은 `SetHiddenInGame` 이 **값을 바꿀 때만** 발화한다(`SceneComponent.cpp:3619`). 그런데 비사망 재사용 경로에서는 **두 축이 모두 값 무변화**다:

| 단계 | 호출 | 결과 |
|---|---|---|
| 재사용 `Activate` | `SetHealthBarAllowed(true)` | `true == true` → **값-가드에서 조기 반환**(`FPSREnemyBase.cpp:315-318`) |
| ≤0.2s 후 밴드 패스 | `SetHealthBarInRange(true)` | `true == true`(래치 유지) → **조기 가드 반환**(`:255-258`) |

두 세터의 값-가드가 `ApplyHealthBarVisibility()` **앞**에 있으므로 Apply 자체가 안 불린다. **사망 경유 재사용은 `allowed` false→true 엣지가 있어 안전하다** — LOD1 PIE ⑥ 이 통과한 이유가 이것이며, 그 통과가 이 구멍을 반증하지 못한다.

> 🔴 **rev2 의 처방은 여기서 실패했다.** 재킥을 `ApplyHealthBarVisibility` **안**에 넣었는데, 위 표대로 목표 케이스에서는 Apply 가 도달 불가다. Apply 가 실제로 불리는 경우(=값 변화 = `SetHiddenInGame` 엣지)는 엔진이 이미 틱을 되켜 주므로 **중복일 뿐 새로 닫는 것이 없었다.** rev3 는 트리거를 **재사용 엣지 자체**로 옮긴다(G1 A안).

**처방(A안)** — 값-가드를 우회해 **재사용 엣지에서 직접** 부른다. `Apply` 는 멱등이므로 무조건 호출이 안전하다.

1. `Activate()` 의 코스메틱 리셋 블록(권위) — `bHealthBarAllowed = true` 를 세터가 아니라 **직접 대입**한 뒤 `ApplyHealthBarVisibility()` 를 1회 호출.
2. `SetActorHiddenInGame(false)` 의 클라 언하이드 리셋 — 동일.
3. `ApplyHealthBarVisibility()` 본문:

```
if (!HealthBarWidgetComponent) { return; }                     // 기존 null 가드 유지
const bool bVisible = bHealthBarAllowed && bHealthBarInRange;
HealthBarWidgetComponent->SetHiddenInGame(!bVisible);
if (bVisible && HealthBarWidgetComponent->GetUserWidgetObject())   // ★ 위젯이 있을 때만
{
    HealthBarWidgetComponent->SetComponentTickEnabled(true);        // 멱등 · 값-가드 있음
}
```

**`GetUserWidgetObject()` 조건의 근거** — 재킥의 목적은 *이미 존재하는 위젯의 스크린 재등재*다. 위젯이 없는 도달 가능한 상태(config 공백 · 전용 서버)는 전부 `TickMode=Disabled` 이고, 재킥으로 틱이 켜져도 `TickComponent` 의 `TickMode == Disabled` 가드가 다음 틱 1회로 자기-회수한다(`WidgetComponent.cpp:1264` 부근) — 즉 조건이 없어도 **안전하지만 이벤트당 빈 틱 1회**가 남는다. 조건을 붙이면 그 잔비용이 0이 되고, 조건이 목적과 정확히 일치한다.

**세터의 값-가드는 둘 다 유지한다.** `SetHealthBarInRange` 는 0.2초 패스마다 불리는 핫 경로이므로 가드가 필요하고, `SetHealthBarAllowed` 도 대칭을 위해 남긴다 — 재사용 엣지는 위 1·2 가 이미 우회한다.

> 이 유닛이 위젯 수명주기의 소유자가 되므로 여기서 닫는다. **LOD1 §11 의 백로그 후보("비사망 휴면은 틱이 안 꺼지는 비대칭")는 기전이 반대로 적혀 있었다** — 실제로는 꺼지고, 문제는 다시 안 켜지는 것이다(§11 보드 위생).

## 7. 복제표

| 프로퍼티 / RPC | 종류 | Push Model | 비고 |
|---|---|---|---|
| — | — | — | **신규 복제 0건.** |

체력바는 클라이언트 비주얼이고, 표시할 값(`Health`·`MaxHealth`·`bDead`)은 **이미 복제되는 3개**로 충분하다(`Performance.md` §5 계약 불변). 위젯 컴포넌트·위젯 인스턴스·클래스 포인터는 전부 로컬 상태다.

---

## 8. 수명주기 · 소유권

- **생성** — ctor 에서 `CreateDefaultSubobject`. 즉 **CDO 서브오브젝트**라 BP 자식이 디테일 패널에서 덮을 수 있고(`InheritableComponentHandler`), 풀 재사용과 무관하게 액터와 함께 산다.
- **초기화** — `BeginPlay` 의 `InitHealthBarWidget()` 1회. 위젯 인스턴스와 바인딩은 **액터 수명당 1회**이고 풀 재사용을 넘어 유효하다(종전 주석의 "single bind survives every reuse" 근거 그대로).
- **GC 소유** — `TObjectPtr` + `UPROPERTY` 라 참조가 산다. 위젯 인스턴스는 컴포넌트가 소유한다.
- **가시성 2축(LOD1 계약 불변)** — `bHealthBarAllowed`(수명주기) AND `bHealthBarInRange`(거리). 유일한 `SetHiddenInGame` 지점은 `ApplyHealthBarVisibility`.
- **풀 재사용** — **상태 리셋 대상은 없다**(컴포넌트·위젯·바인딩이 전부 액터 수명 단위라 기존 리셋 목록이 그대로 유효). 다만 §6-3 이 재사용 엣지 2곳에 `ApplyHealthBarVisibility()` 직접 호출을 **신규 동작**으로 추가한다 — 그것은 리셋이 아니라 **재적용**이다(값-가드를 우회해 틱을 되켜기 위함).
- **전용 서버** — 컴포넌트는 존재하되 위젯을 만들지 않는다(§6-2 step 1).

---

## 9. 데이터드리븐 경계

| 값 | 나가는 곳 | 기본값 | 비고 |
|---|---|---|---|
| 체력바 위젯 클래스 | `DefaultGame.ini` **`[/Script/FPSRoguelite.FPSREnemyRenderSettings]`** 소프트경로 | (콘텐츠가 저작) | 비면 체력바 OFF — §6-2 step 4 의 `TickMode=Disabled` 가 그 상태의 비용까지 0으로 만든다 |
| 아키타입별 위젯 클래스 | BP 컴포넌트 오버라이드 | (없음 = config 사용) | 엘리트 전용 바 등 |
| 바 크기·높이·피벗 | BP 컴포넌트 오버라이드 | ctor 기본값(멜리 실측) | 적 키가 다르면 Z 가 달라진다 |
| 바 표현·애니메이션 | `WBP_EnemyHealthBar` | — | 이 유닛 비목표 |

**C++ 에 남는 것 = 구조**: "적이면 체력바 컴포넌트를 갖는다", "위젯이 있으면 체력 컴포넌트에 묶인다", 가시성 2축, 전용 서버 제외. **에셋 경로는 코드에 없다.**

---

## 10. 성능 예산

- **틱** — 신규 서브시스템·신규 패스 0. LOD1 의 0.2초 배치 패스가 그대로 소비자다.
- **액터당 비용** — 위젯 컴포넌트 1개가 **원래 있어야 했던 곳에 생긴다**. 멜리는 순증 0(BP 수동 → 네이티브로 이동). **원거리·엘리트는 순증**이지만, 그건 없어서 안 보이던 기능이 생기는 값이다. LOD1 밴드가 그 위에 그대로 얹힌다.
- **config 공백 상태의 비용 = 0** — §6-2 step 4 의 `TickMode=Disabled` 로 보장한다. 이 한 줄이 없으면 **캡 500 전원이 빈 위젯 컴포넌트 틱을 영구히 돈다**(위젯-없음 조기 종료가 요구하는 `bRenderCleared` 는 월드 공간 드로우에서만 set 되고, 자기-틱-차단은 `Widget &&` 를 요구한다 — 스크린 공간·위젯 없음에서는 **둘 다 미발화**). 그러면 "끄기"가 오늘(원거리·엘리트 = 컴포넌트 없음 = 비용 0)보다 나빠진다. 제1원리 직결이라 선택 사항이 아니다.
- **`FindComponentByClass` 제거** — LOD1 이 캐시로 우회했던 선형 스캔이 사라진다(네이티브 멤버 직접 참조). §6-2 step 3 의 중복 진단만 `BeginPlay` 1회 스캔한다.
- **`LoadSynchronous` 히치** — 위젯 클래스 동기 로드가 **첫 적 `BeginPlay` 에서 1회**(이후 캐시). 런 시작 스폰 웨이브와 겹칠 수 있으나 클래스 1개라 미미하다고 본다 — 실측은 M0 베이스라인 행의 몫.

### 🔴 LOD1 §10 정정 — 무엇이 틀렸고 무엇이 맞았나

LOD1 은 절감을 "**렌더타깃 드로우** + **컴포넌트 틱 정지**"로 적었다. 실제 콘텐츠는 **스크린 공간**이므로:

| LOD1 주장 | 판정 | 근거 |
|---|---|---|
| 렌더타깃 드로우가 사라진다 | ❌ **틀렸다** | `DrawWidgetToRenderTarget` 은 `Space == World` 분기 안에만 있다(`WidgetComponent.cpp:1279-1295`). 스크린 공간은 애초에 그 경로를 타지 않으므로 절감할 것이 없다 |
| 컴포넌트 틱이 멈춘다 | ✅ **맞았다**(기전 서술만 부정확) | `bHiddenInGame` 이 직접 끄는 것이 아니라 **스크린 레이어 제거 → 슬레이트 해제 → `UWidget::IsVisible()` false → `:1262` 가드 발화** 경유다(§3-2 체인) |
| — | ➕ **누락돼 있었다** | 스크린 레이어에서 빠지면 `SWorldWidgetScreenLayer::Tick` 의 per-widget 투영·배치·Slate 페인트가 사라진다. 300 규모에서는 이쪽이 지배적이다 |

> ⚠️ **`SetTickMode(Automatic)` 은 죽은 코드가 아니다 — 제거 금지.** 기본값 `Enabled`(`:58`/`:642`)에서는 `:1262` 가드가 `TickMode != Enabled` 를 요구해 **영구 미발화**하므로, 이 한 줄이 없으면 숨은 바 전부가 매 프레임 틱한다. rev1 이 이것을 "스크린 공간에서 no-op" 이라 적었던 것은 **오독**이었고, 그대로 뒀다면 다음 사람이 죽은 코드로 보고 지웠을 것이다.

- **정량 측정은 이 유닛의 몫이 아니다** — M0 성능 베이스라인 행이 소유한다. 다만 원거리·엘리트에 바가 새로 생기므로 **그 측정은 이 유닛 이후에 해야 유효**하다(그 행에 이 사실을 남긴다).

## 11. 미결정 · 사용자 작업

### 🔴 사용자 작업 (이것 없이는 유닛이 완결되지 않는다 — BP/에셋 편집은 사용자 몫)

각 항목에 **빠뜨렸을 때의 증상**을 적는다. 증상을 모르면 빠뜨린 것을 디버깅으로 착각한다.

1. **`WBP_EnemyHealthBar` 의 부모를 `UFPSREnemyHealthBarWidget` 로 변경**(Widget Blueprint → File → Reparent Blueprint). 그 뒤 이벤트 그래프에 `Bind Health Component` 이벤트를 추가하고 **기존 `InitHealthComp` 함수를 그 안에서 호출**한다(노드 2개). 기존 함수는 지우지 않는다 — 이름 충돌을 피하려고 C++ 이벤트 이름을 달리 지었다(§5-1).
   - **빠뜨리면**: `TSoftClassPtr<UFPSREnemyHealthBarWidget>::LoadSynchronous` 의 `IsChildOf` 필터에 걸려 **null 이 반환된다** → 위젯이 아예 안 만들어지고 **모든 적의 바가 무음으로 사라진다**(멜리 포함). "위젯은 뜨는데 값이 안 묶인다"가 아니다 — 전부 사라진다.
2. **`BP_EnemyMeleeBase` 정리 — 컴포넌트 삭제 + 그래프 배선 제거(둘 다).**
   - 수동 `HealthBarWidget` 컴포넌트를 삭제한다.
   - **그리고 그 BP 의 `OnHealthBarReady` 이벤트 그래프 배선(`GetUserWidgetObject → Cast → InitHealthComp`)도 함께 제거**한다. 이 그래프가 삭제된 컴포넌트 변수를 참조하므로 **그래프를 남기면 BP 컴파일이 깨진다.** (실측: `OnHealthBarReady` 배선은 멜리에만 있고 원거리·엘리트에는 없다.)
   - **컴포넌트만 지우고 그래프를 남기면**: BP 컴파일 실패. **둘 다 안 지우면**: 바가 **둘** 뜬다(네이티브 + 수동) — §6-2 step 3 의 경고 로그가 이 상태를 알린다.
3. **`DefaultGame.ini` 에 위젯 클래스 저작** — 섹션명은 **`[/Script/FPSRoguelite.FPSREnemyRenderSettings]`**(이미 존재하는 섹션, LOD1 이 만든 거리 반경 항목 아래). 코드가 경로를 갖지 않으므로 이 값이 없으면 체력바가 안 뜬다. 구현 시 제안값을 함께 제시한다.
   - **빠뜨리면**: 체력바 없음 + `TickMode=Disabled` 로 비용 0(§6-2 step 4). 조용하지만 **안전한** 상태다.
   - **섹션명을 틀리게 적으면**: 무음 미로드라 위 상태와 **구분되지 않는다.** 섹션명을 그대로 복사할 것.

> ⚠️ **전환기(머지 ~ 사용자 작업 완료 사이)**: config 가 비어 있으면 §6-2 step 4 에서 반환하므로 `OnHealthBarReady` 도 발화하지 않는다. 멜리의 수동 바가 아직 있다면 **바인딩 트리거를 잃어 정적인 풀바로 보인다**(값이 안 줄어든다). 사용자 작업 1·3 을 마치면 해소된다 — 새 결함이 아니라 전환기 증상이다.
> 또한 전환기의 멜리 **수동** 컴포넌트는 새 코드가 더 이상 `SetTickMode(Automatic)` 을 걸어 주지 않으므로 BP 저작값 `Enabled`(실측) 그대로 **매 프레임 틱**하고, 2축 가시성이 네이티브 멤버만 조준하므로 **거리 LOD 도 안 걸린다.** 사용자 작업 2 로 해소된다.

### 보드 위생 (이 유닛에서 함께 정정)

LOD1 §11 이 후속 행 후보로 남긴 **"비사망 휴면의 헬스바 틱 비대칭"의 기전 서술이 반대다.** 실제로는 틱이 **꺼지고**, 문제는 **다시 안 켜지는 것**이다(§6-3). 이 유닛이 그 처방을 포함하므로 해당 백로그 행(`3c93972ddd8881f0b25ee2ce911feded`)은 **기전 정정 + 해소 여부 재판정** 대상이다.

### 미결정

- **엘리트 전용 바를 만들지 여부** — 구조는 열어 뒀다(BP 오버라이드). 만들지는 콘텐츠 결정.
- **바 높이(Z=120)를 아키타입별로 조정할지** — 원거리/엘리트의 실제 메시 높이에 맞는지는 PIE 체감. 코드는 멜리 값만 기본으로 준다.

**갭 처리(고정)** — 구현 중 명세에 없는 판단이 필요하면 **추측하지 말고 멈추고 "명세 갭"으로 보고**한다.

---

## 12. 검증 기준

| # | 검사 | 통과 조건 |
|---|---|---|
| 1 | 명세 대조 | §5·§6·§7 선언·시그니처·복제(신규 0)가 코드와 1:1 |
| 2 | 빌드 | `-DisableUnity -NoXGE`, 로그 **`Result: Succeeded`** (종료코드 신뢰 금지) |
| 3 | **BP 컴파일 회귀** | `FPSRoguelite.Enemy.BlueprintParent` 통과 — **이번 유닛의 핵심 안전망.** 새 `UPROPERTY`/컴포넌트 이름이 콘텐츠 BP 와 충돌하면 C++ 빌드는 통과하고 이 테스트만 실패한다([[cpp-uproperty-name-collides-with-bp]]) |
| 4 | **신규 자동화 — 중복 검사** | `Enemy.BlueprintParent` 에 **"적 BP 당 `UWidgetComponent` 총수(CDO 네이티브 + SCS) == 1"** 검사 추가. 🔴 **이 검사는 사용자 작업 2 전까지 멜리에서 실패하는 것이 정상이다** — 유닛 완결 조건을 기계화한 것이지 구현 결함이 아니다. C3 는 이 실패를 "사용자 작업 미완"으로 판정하고, 사용자 작업 2 이후 통과를 최종 확인한다. (rev1 의 "3종이 컴포넌트를 갖는다"는 네이티브 ctor 이후 자명하게 참이라 아무것도 잠그지 못했다 — G1 P2-4) |
| 5 | 기존 적 자동화 | `DormantPool` · `NetCull` · `Pursuit` · **`DistanceBand`** · `Smoke.ModuleLoads` · **`Allocator.FrontBudget`** 전부 통과 |
| 6 | LOD 회귀 | 2축 가시성 경로가 네이티브 멤버로만 바뀌고 동작 변화 0(diff 대조). **예외 = §6-3 의 신규 동작 일체, 총 3곳**: ① `ApplyHealthBarVisibility` 의 재킥 줄 ② `Activate()` 의 직접 대입 + Apply 호출 ③ 클라 언하이드 리셋의 직접 대입 + Apply 호출. C3 는 이 셋을 이탈이 아니라 **의도된 신규 동작**으로 판정한다 |
| 7 | 레드팀 게이트 | §6-6-1 = G2. **P1 잔존 시 머지 금지** |
| 8 | **PIE (사용자)** | ① 원거리(큐브형)·엘리트에 체력바가 뜬다 ② 멜리에 바가 **둘** 뜨지 않는다 ③ 피격 시 바가 줄고 사망 시 사라진다 ④ LOD1 §12-10 ①③⑥ 무회귀 ⑤ **비사망 드레인(rear-drain) 후 근거리 재사용된 적의 바가 즉시 뜬다** — §6-3 이 닫은 구멍의 확인 검사 ⑥ **풀 재사용 직후 체력바 잔존**(M1 백로그) 이 악화되지 않았는지 |

## 13. 레드팀 지적 원장 (C3에서 채운다)

*(설계 시점 — 비워 둔다)*
