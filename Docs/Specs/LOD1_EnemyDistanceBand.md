# LOD1 — 적 프로토 메시 LOD·거리밴드 재정합

## 1. 메타

| 항목 | 값 |
|---|---|
| 유닛 ID / 이름 | `LOD1` / 적 프로토 메시 LOD·거리밴드 재정합 (구 "VAT-4", 2026-08-24 재정의로 VAT 제거) |
| 브랜치 | `perf/enemy-lod-distanceband` |
| 작성 모델 | `claude-opus-5` (§6-5-2 개정 2026-08-26 = C1 설계 주체는 Opus) |
| 작성일 / 최종 갱신 | 2026-08-27 (rev3 — G1 통과 + P3 반영) |
| 상태 | `확정` — G1 통과(2회차) + 사용자 승인 2026-08-27 |
| 관련 SSOT | `Performance.md` §5-1(Significance 티어)·§5(NetUpdateFreq 표) · `Enemy.md` §2-6 · `Game.md` §1 |
| 관련 메모리 | `[[enemy-perf-remediation-roadmap]]`(F8 · RC-A) · `[[reason-in-multiplayer-terms]]` · `[[code-is-immutable-structure-only]]` · `[[leave-fine-tuning-to-user]]` · `[[production-structure-first]]` |
| 보드 행 | https://app.notion.com/3ba3972ddd8881eaad7dd996b9cdf9ae |

> **rev3 (G1 통과 후 P3 반영)** — 거리 메트릭 XY→3D 통일을 §6-1이 **명시적으로 소유**(의도된 동작 변경, 실효 반경 −1.5%) · 개명 딸린 주석 잔재 청소 대상 9곳 열거(`bEnableEnemyShadowLOD` 키는 불변) · `FPSRAnimCPDParams.h`는 alias 없이 상수 삭제 · `:388`→`:392` 오기 · 비사망 휴면 헬스바 비대칭을 후속 행 후보로 기록.
>
> **rev2 변경 요약 (G1 반려 반영)** — P1-1 사망 가드 신설 · P1-2 시그니처 갭 2건 해소(`SetViewerLOD(Band, DistSq)` + 히스테리시스 읽기 접근자·순수 헬퍼) · P2-1 **엔진 주장 정정**(`TickMode` 기본은 `Automatic`이 **아니라** `Enabled` — 명시적 `SetTickMode` 추가) · P2-2 프리즈를 에지→**레벨 트리거** · P2-3 서브시스템 생성 조건 정정 · P2-4 §2/§4 모순 해소(서버 쪽은 **식별자 치환만**) · P2-5 `ensure`→로그 강등 · P3 7건 반영.

---

## 2. 목표 / 비목표

### 목표

`Performance.md` §5-1이 계약한 **"적/VFX/SFX/anim tick/mesh/healthbar를 단계별로 다운"** 중, **렌더(코스메틱) 쪽 소비자가 실제로 밴드를 따르게** 만든다. 끝나면:

1. **거리밴드 반경이 한 파일에서 나온다.** 오늘 3곳(`FPSREnemySpawnSubsystem`의 private constexpr / `FPSRAnimCPD::AnimFreezeRadiusSq`의 손복사 미러 / `FPSREnemyRenderSettings::ShadowCastRadius`의 코드 기본값)에 흩어진 값이 `FPSREnemyTuning.h` 하나를 참조한다.
2. **헬스바가 거리로 꺼진다.** 오늘은 밴드가 **전혀 없다** — 적 200~300마리가 각자 월드공간 `UWidgetComponent`를 들고 있다.
3. **애니 프리즈가 리슨서버 호스트에서도 걸린다.** 오늘은 클라이언트 경로(`PostNetReceiveLocationAndRotation`)에만 있어서, **이 프로젝트의 기준선인 호스트**(4인 협동·전용서버 없음, §2-10)가 유일하게 혜택을 못 받는다.
4. **뷰어 거리 계산이 액터당 1회/패스로 합쳐진다.** 코스메틱 소비자가 각자 거리를 재지 않고 한 배치 패스가 낸 값을 읽는다.
5. **후속 VFX 소비자가 붙을 자리가 생긴다**(seam만 — 비목표 참조).

> **행 요구 "착수 시 대상 메시·머티리얼을 프로토 것으로 다시 잡을 것" = 이미 충족 상태다.** 밴드의 렌더 소비자는 전부 `AFPSREnemyBase::Mesh`(`UStaticMeshComponent`)를 대상으로 하고, 그 메시는 절차적 프로토 교체(머지 `95124189`) 이후 `SM_EnemyProto_AtomCubes`/`SM_EnemyProto_Bipyramid` + `MI_EnemyProto_*`다. 스켈레탈/VAT를 가리키는 코드 경로는 남아 있지 않다. 이 유닛은 **대상을 다시 잡을 것이 없고**, 그 사실 확인이 요구의 이행이다.

### 비목표 (구현자가 "친절하게" 채우면 안 되는 것)

- **적 VFX 다운그레이드 구현 금지.** 실측: 적 경로에 Niagara/파티클이 **하나도 없다**(`grep -rl "Niagara|SpawnEmitter|SpawnSystemAt" Source/` = `FPSRCharacter.cpp`·`FPSRWeaponDataAsset.cpp` 뿐). **다운그레이드할 대상이 아직 없다.** 이 유닛은 밴드 조회 seam만 열고, 실제 배선은 U13(VFX·오디오)이 소유한다. 없는 VFX 시스템을 여기서 만들지 않는다. ⚠️ 원 요구에 명시된 항목이므로 **사용자 승인 단계에서 이 재해석을 명시 확인받는다**.
- **SFX 밴드 손대지 않음.** `UFPSRBlindspotAudioComponent`가 `ThreatRadius` 단일 컷을 이미 "Significance gate (§5-1)"로 주석해 두고 있으나, 그건 **플레이어 컴포넌트**가 적을 스캔하는 구조라 소유자가 다르다. 별건 → 후속 행.
- **스태틱 메시 LOD(트라이앵글 감소) 저작 금지.** 프로토 메시의 LOD 체인 저작은 **콘텐츠 = 사용자 몫**(`[[da-edits-are-user-work]]`). 이 유닛은 코드만.
- **F7(캡슐 40/90 · `GroundSnapTolerance` 60 산재) 통합 금지.** RC-A 묶음의 형제지만 이 행의 범위는 "LOD·거리밴드"다. 같은 헤더에 넣고 싶어도 **밴드 반경만** 옮긴다. F7은 별도 행 제안(§11).
- **`GlobalAliveCap`·`MaxAliveCount`·`MaxActiveEnemies` 숫자 조정 금지.** 보드 행 본문의 "숫자 4개 어긋남"은 별도 행 소유.
- **서버 배치패스의 밴드 분기를 재작성하지 않는다.** 값·경계·분기 **원형**을 그대로 두고 **상수 식별자만 치환**한다(§4·§12-5). `ClassifyBand`는 뷰어 패스·테스트 전용이다 — 서버 쪽에 끌어다 쓰면 §12-5의 "diff로 자명" 증명이 사람 판단으로 격하된다.

---

## 3. 제1원리 3줄 (핵심원칙 4)

1. **제1원리 근거** — 액터당 비용 최소화가 다른 모든 편의보다 우선한다(`Game.md` §1). 헬스바는 **적 1마리당 `UWidgetComponent` 1개**라 300마리면 300개가 매 프레임 틱한다 — 밴드 없이는 "가장 비싼 코스메틱이 가장 안 보이는 곳에서 가장 많이 돈다". 그리고 밴드 평가 자체가 액터당 매프레임이면 해결이 아니라 이전이므로, **뷰어 거리 계산은 배치 1패스**로 묶는다.
2. **엔진 기본값·기존 인프라와의 관계** — 엔진 소스 실측(`D:\UnrealEngine\UE_5.7`):
   - `WidgetComponent.cpp:58` — `static bool bUseAutomaticTickModeByDefault = false;` (CVar `WidgetComponent.UseAutomaticTickModeByDefault`). 이 프로젝트 `Config/`·`Source/`는 이 CVar를 **설정하지 않는다**(grep 0건).
   - `WidgetComponent.cpp:642` — 생성자 `TickMode(bUseAutomaticTickModeByDefault ? Automatic : Enabled)` → **기본값 = `Enabled`**.
   - `WidgetComponent.cpp:1262` — 자기-틱-차단 분기는 `TickMode != ETickMode::Enabled` 조건이라 **기본 상태에서 절대 발화하지 않는다**.
   - `WidgetComponent.cpp:1367-1383`·`SceneComponent.cpp:3528-3537` — `SetHiddenInGame(true)`는 `IsVisible()`을 false로 만들어 `ShouldDrawWidget()`을 막는다 → **RT 드로우는 확실히 멈춘다**.
   - `WidgetComponent.cpp:807-819` — `OnHiddenInGameChanged`가 언하이드 시 틱을 되켠다.
   - `SceneComponent.cpp:3619`·`PrimitiveComponent.cpp:2101` — `SetHiddenInGame`·`SetCastShadow` 둘 다 값 변화 시에만 동작(자기-가드) → 매 패스 무조건 호출해도 정상 상태 비용 0.

   → **결론: 엔진 기본값을 덮는다. 덮는 이유 =** 기본 `Enabled`는 "위젯이 몇 개 안 된다"는 전제 위의 값인데 이 게임은 **300개**를 띄운다(제1원리 위반). 덮는 방법은 자체 틱 관리 코드가 아니라 **엔진이 제공하는 모드 선택** — `InitHealthBarWidget`의 1회 캐시 시점에 `SetTickMode(ETickMode::Automatic)`를 호출한다. 그러면 위 :1262 분기가 살아나 숨김 시 컴포넌트가 스스로 틱을 끄고, 언하이드 시 :807이 되켠다. 콘텐츠 BP가 `TickMode`를 무엇으로 저작했든 코드가 결정론적으로 고정한다(BP 저작값은 uasset이라 **미검증**이며, 명세가 미검증 콘텐츠 상태에 기대지 않기 위한 조치이기도 하다).

   **기존 인프라** — `UFPSREnemyShadowLODSubsystem`이 이미 *뷰어 기준 0.2초 배치 패스 + 자체 등록 레지스트리 + 히스테리시스*를 갖고 있고, 그 헤더 주석이 **"코스메틱은 보는 사람의 것이라 서버 전용 패스에 두면 안 된다"**는 이 유닛의 논거를 그대로 적어 두었다. 새 서브시스템을 만들지 않고 **이것을 일반 코스메틱 LOD 패스로 승격**한다.
3. **프로젝트 제약과의 정합** — **서버 밴드와 뷰어 밴드는 MP에서 서로 다른 값이며, 합치면 틀린다.** 서버 밴드는 *전체 플레이어 중 최근접*(`BestDistSq`)으로 AI/복제를 정하고, 뷰어 밴드는 *이 머신의 로컬 뷰어*로 렌더를 정한다. 4인 협동에서 다른 플레이어 옆의 적은 서버 밴드 S0이면서 내 뷰어 밴드는 S3일 수 있다 — 하나로 합치면 원격 클라가 전부 풀 코스메틱을 그리게 되는(=`[[reason-in-multiplayer-terms]]`가 금지하는 "솔로는 문제없음") 구조가 된다. **평가자는 둘로 유지하고, 반경 상수만 한 파일로 모은다.**

---

## 4. 파일 목록

| 경로 | 신규/수정 | 한 줄 설명 |
|---|---|---|
| `Source/FPSRoguelite/Public/Enemy/FPSREnemyTuning.h` | **신규** | 거리밴드 반경 SSOT(F8/RC-A 코드분) + 밴드 열거형 + 순수 판정 함수 2개 |
| `Source/FPSRoguelite/Public/Enemy/FPSREnemyCosmeticLODSubsystem.h` | **신규(`git mv`)** | `FPSREnemyShadowLODSubsystem.h` 개명 — 뷰어 밴드 배치 패스 |
| `Source/FPSRoguelite/Private/Enemy/FPSREnemyCosmeticLODSubsystem.cpp` | **신규(`git mv`)** | 위의 구현 |
| `Source/FPSRoguelite/Public/Settings/FPSREnemyRenderSettings.h` | 수정 | 헬스바 밴드 노브 3개 추가 |
| `Source/FPSRoguelite/Public/Enemy/FPSREnemyBase.h` | 수정 | 뷰어 LOD 상태 + 헬스바 2축 계약 + 위젯 포인터 캐시 |
| `Source/FPSRoguelite/Private/Enemy/FPSREnemyBase.cpp` | 수정 | 위의 구현 + 프리즈 소유권 이전(호스트 대칭) + `SetTickMode` |
| `Source/FPSRoguelite/Public/Enemy/FPSRAnimCPDParams.h` | 수정 | `AnimFreezeRadiusSq` 상수 **삭제**(alias 남기지 않는다 — 소비자가 `FPSREnemyTuning`을 직접 참조) + 미러 설명 주석 제거 |
| `Source/FPSRoguelite/Public/Enemy/FPSREnemySpawnSubsystem.h` | 수정 | `TierS0/S1/S2RadiusSq` → `FPSREnemyTuning` **식별자 치환만** |
| `Source/FPSRoguelite/Private/Enemy/FPSREnemySpawnSubsystem.cpp` | 수정 | 밴드 분기의 **상수 이름만** 치환. 분기 원형·값·순서 불변 |
| `Source/FPSRoguelite/Private/Tests/FPSREnemyDistanceBandTest.cpp` | **신규** | 밴드 분류 · 히스테리시스 · 반경 값 잠금 worldless 유닛테스트 |

> **개명 근거(실측)**: `grep -rl "ShadowLOD" Content/` = **0건**, `UFPSREnemyShadowLODSubsystem`은 `UCLASS()`(BlueprintType 아님), config 섹션은 별도 클래스(`UFPSREnemyRenderSettings`)라 ini 무영향. `git mv` 대상은 **2파일**(h/cpp). "ShadowLOD"라는 이름으로 헬스바·프리즈를 끄면 다음 사람이 못 찾는다는 것이 개명 사유다.
>
> **개명에 딸린 주석 잔재 청소 = 이 유닛의 범위에 포함한다**(범위 밖 리팩토링이 아니라 개명의 완결). 축자 구현이 스테일 주석을 남기지 않도록 대상을 열거한다 — 아래 **주석 문자열만** 고치고 로직은 건드리지 않는다:
> `FPSRStageFadeSubsystem.h:24,25,89` · `FPSRStageFadeSubsystem.cpp:43,52,217` · `FPSREnemySpawnSubsystem.cpp:1518` · `FPSREnemyBase.h:232` · `FPSREnemyBase.cpp:8`(include 주석).
> ⚠️ `UFPSREnemyRenderSettings`의 **`bEnableEnemyShadowLOD` 프로퍼티 이름은 바꾸지 않는다** — 그것은 여전히 그림자 전용 스위치이고, 이름을 바꾸면 저작된 ini 키가 깨진다.

---

## 5. 인터페이스 선언 (헤더 스케치)

### 5-1. `FPSREnemyTuning.h` (신규)

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** 적 스웜의 손동기화 튜닝 상수 SSOT (F8 / RC-A). 여러 헤더에 손으로 복사돼 있던 거리밴드 반경을 한 곳으로 모은다.
 *  선례 = UFPSRFlowFieldComputer 의 셀상한 constexpr 잠금.
 *
 *  범위 주의: 이 헤더는 **거리밴드 반경만** 담는다. 캡슐 40/90·GroundSnapTolerance 60 의 산재(F7)는 별도 유닛이다. */
namespace FPSREnemyTuning
{
	// ---- 게임플레이 Significance 밴드 (Performance.md §5-1) ----
	// 평가 주체 = UFPSREnemySpawnSubsystem 서버 배치패스. 기준 = **전체 플레이어 중 최근접**(BestDistSq).
	// 소비 = 이동 스트라이드 / 공격결정 스트라이드 / NetUpdateFrequency.
	inline constexpr float SignificanceS0RadiusSq = 1500.0f * 1500.0f; // S0 근접 위협: full update
	inline constexpr float SignificanceS1RadiusSq = 3500.0f * 3500.0f; // S1 근거리: 저빈도 update
	inline constexpr float SignificanceS2RadiusSq = 6000.0f * 6000.0f; // S2 중거리 군집 (초과 = S3 원거리)

	// ---- 코스메틱 애니 프리즈 반경 ----
	// ⚠️ 오늘 SignificanceS1RadiusSq 와 **값이 같지만 뜻이 다르다**(F8 적대재검증 결론 2026-07-08:
	//    "net-freq 티어 vs anim-freeze 가 우연히 같은 3500² 라 하나로 합치지 말 것").
	//    합치면 넷프리퀀시 튜닝이 애니 프리즈 거리를 조용히 옮기고, 그 반대도 성립한다.
	// 아래 static_assert 는 불변식이 아니라 **트립와이어**다 — 둘 중 하나를 의도적으로 바꾸면 빌드가 깨져서
	// "정말 둘 다 옮길 셈인가"를 묻는다. 갈라놓기로 했다면 이 assert 를 지우는 것이 올바른 대응이다.
	inline constexpr float AnimFreezeRadiusSq = 3500.0f * 3500.0f;
	static_assert(AnimFreezeRadiusSq == SignificanceS1RadiusSq,
		"AnimFreezeRadiusSq 와 SignificanceS1RadiusSq 가 갈라졌다. 의도한 분리라면 이 static_assert 를 지우고 "
		"두 값이 독립임을 주석에 남겨라. 의도치 않았다면 한쪽을 되돌려라. (F8)");

	/** 뷰어/서버 공용 밴드 라벨. 값 순서 = 가까움→멀음(비교로 "이 밴드 이상 멀다"를 쓸 수 있게).
	 *  ⚠️ 네임스페이스 안 순수 C++ enum 이라 BP 에서 못 읽는다. 현행 소비자는 전부 C++(AnimProfile 패턴과 동일)라
	 *  문제없으나, 후속 U13 이 BP 에서 밴드를 읽어야 하면 그때 UENUM 래퍼를 추가한다(지금 만들지 않는다). */
	enum class EFPSRDistanceBand : uint8
	{
		S0 = 0, // 근접 위협: full update
		S1,     // 근거리: 저빈도 update
		S2,     // 중거리 군집: anim·VFX 축소
		S3,     // 원거리: coarse movement · no cosmetic
	};

	/** 제곱거리 → 밴드. 순수(월드 접근 0)라 worldless 유닛테스트 대상.
	 *  경계 규칙(`<=` 로 안쪽 포함)은 서버 패스의 기존 분기와 **같은 규칙**이다.
	 *  ⚠️ 이 함수는 **뷰어 패스와 테스트 전용**이다 — 서버 배치패스의 기존 분기를 이것으로 재작성하지 않는다(§2 비목표). */
	constexpr EFPSRDistanceBand ClassifyBand(float DistSq)
	{
		return DistSq <= SignificanceS0RadiusSq ? EFPSRDistanceBand::S0
		     : DistSq <= SignificanceS1RadiusSq ? EFPSRDistanceBand::S1
		     : DistSq <= SignificanceS2RadiusSq ? EFPSRDistanceBand::S2
		     :                                    EFPSRDistanceBand::S3;
	}

	/** 켜짐/꺼짐 코스메틱의 히스테리시스 판정. 켜져 있으면 더 넓은 반경까지 유지한다(경계 진동 방지).
	 *  기존 그림자 패스가 인라인으로 하던 것과 **같은 수식**을 순수 함수로 뽑은 것 — 그림자·헬스바가 같은 규칙을
	 *  쓰게 하고(§12-6 회귀 증명), worldless 테스트가 가능해진다. */
	constexpr bool ApplyRadiusHysteresis(bool bCurrentlyOn, float DistSq, float OnRadiusSq, float OffRadiusSq)
	{
		return DistSq <= (bCurrentlyOn ? OffRadiusSq : OnRadiusSq);
	}
}
```

### 5-2. `FPSREnemyRenderSettings.h` (수정 — 추가분만)

```cpp
	/** 헬스바 거리 LOD 마스터 스위치. OFF = 밴드가 헬스바를 건드리지 않는다(수명주기 가시성만 남는다).
	 *  그림자 스위치와 분리한 이유: 비용 성격이 다르고(그림자=셰도우패스, 헬스바=RT 드로우+슬레이트 틱),
	 *  한쪽만 끄고 재는 것이 perf 베이스라인(M0 후행 행)의 대조군에 필요하다. */
	UPROPERTY(Config, EditAnywhere, Category = "Health Bar LOD",
		meta = (DisplayName = "적 헬스바 거리 LOD 사용"))
	bool bEnableHealthBarLOD = true;

	/** 이 거리 안의 적만 헬스바를 그린다. 기본값 = S1 반경(3500cm) — §5-1 이 "S2 중거리 = 코스메틱 축소,
	 *  S3 원거리 = no cosmetic" 이라 헬스바는 S1 밖에서 떨어지는 것이 계약에 맞다.
	 *  ⚠️ 이 숫자는 **게임필/가독성 값**이라 최종 조정은 사용자 몫이다(코드는 기본값만 제시). 0 = 헬스바 전면 OFF. */
	UPROPERTY(Config, EditAnywhere, Category = "Health Bar LOD",
		meta = (DisplayName = "헬스바 표시 반경", ClampMin = "0.0", UIMax = "6000.0", ForceUnits = "cm",
			EditCondition = "bEnableHealthBarLOD", EditConditionHides))
	float HealthBarVisibleRadius = 3500.0f;

	/** 이미 보이는 헬스바가 유지되는 추가 거리. 없으면 경계를 걷는 적의 바가 깜빡인다(그림자와 같은 이유). */
	UPROPERTY(Config, EditAnywhere, Category = "Health Bar LOD",
		meta = (DisplayName = "헬스바 히스테리시스", ClampMin = "0.0", UIMax = "2000.0", ForceUnits = "cm",
			EditCondition = "bEnableHealthBarLOD", EditConditionHides))
	float HealthBarHysteresis = 300.0f;
```

> `ShadowCastRadius`의 선언 기본값(`1500.0f`)은 **바꾸지 않는다** — `UPROPERTY` 초기화식이라 `constexpr` 제곱근을 쓸 수 없고, 무엇보다 저작된 ini 값이 있으면 그쪽이 이긴다. 주석만 "S0 반경(`FPSREnemyTuning::SignificanceS0RadiusSq`)과 같은 값"으로 갱신한다. ini가 S0에서 이탈하는 것은 **정당한 저작 행위**이므로 어설트하지 않는다(§12-7).

### 5-3. `FPSREnemyCosmeticLODSubsystem.h` (개명 + 확장)

```cpp
UCLASS()
class FPSROGUELITE_API UFPSREnemyCosmeticLODSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** AFPSREnemyBase::BeginPlay 에서 호출(렌더하는 모든 넷모드). 서브시스템 부재 시 안전. */
	void RegisterEnemy(AFPSREnemyBase* Enemy);
	void UnregisterEnemy(AFPSREnemyBase* Enemy);

	// UTickableWorldSubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickable() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;
	virtual TStatId GetStatId() const override;

private:
	/** 등록된 적. 약참조 — 풀 해제/월드 종료 순서에 의존하지 않고 Tick 이 stale 을 걷어낸다(기존 동작 유지). */
	TArray<TWeakObjectPtr<AFPSREnemyBase>> RegisteredEnemies;

	float TimeSinceLastPass = 0.0f;
};
```

> **생성 조건(정정)**: `IsRunningDedicatedServer()`면 거부, **그 외에는 항상 생성**한다. 종전 `bEnableEnemyShadowLOD` 게이트를 그대로 두거나 `||`로 넓히기만 하면, 두 코스메틱 스위치를 다 끈 순간 **스위치가 없는 제3 소비자인 애니 프리즈까지 조용히 죽는다** — 오늘 클라 프리즈는 어떤 설정과도 무관하게 무조건이므로(`FPSREnemyBase.cpp:719` 하드코드) 그건 무회귀가 아니라 **회귀**다. 종전 주석의 "no registration, no pass, no cost"는 소비자가 그림자 하나뿐이던 시절의 성질이었음을 주석으로 정리한다. 개별 소비자의 OFF는 Tick 안에서 각자 분기한다.

### 5-4. `FPSREnemyBase.h` (수정 — 추가/변경분만)

> 🔴 **C3 정정 (2026-08-27, 헤드리스 자동화가 잡음)** — 캐시 멤버 이름이 rev3까지 `HealthBarWidget` 이었으나
> **콘텐츠 BP 와 이름이 충돌해 BP 컴파일이 깨진다**. `BP_EnemyMeleeBase` 가 월드공간 위젯 컴포넌트를 이미
> `HealthBarWidget` 으로 저작해 두었고, 부모 C++ 에 같은 이름의 `UPROPERTY` 가 생기면
> `FPSRoguelite.Enemy.BlueprintParent` 가 실패한다(실측 로그: *"Internal Compiler Error: Tried to create a property
> HealthBarWidget in scope BP_EnemyMeleeBase_C, but another object (ObjectProperty
> /Script/FPSRoguelite.FPSREnemyBase:HealthBarWidget) already exists there"* + BP 그래프의 `Get HealthBarWidget` 이
> private 프로퍼티로 해석되어 접근성 에러). → **`CachedHealthBarWidget` 으로 개명**. 동작·계약은 불변, 이름만 바뀐다.
> **교훈(이 유닛 밖으로 일반화)**: 적 베이스에 새 `UPROPERTY` 를 추가할 때 그 이름은 **콘텐츠 BP 의 변수·컴포넌트
> 이름공간과 충돌할 수 있다.** 헤더만 보고는 안 보이며, `Enemy.BlueprintParent` 자동화가 유일한 조기 경보다.

```cpp
public:
	/** 코스메틱 LOD 패스가 매 패스 밀어넣는 **이 머신 뷰어 기준** 상태. 한 번의 호출로 밴드 라벨과 프리즈를 함께
	 *  갱신한다 — 프리즈는 밴드 라벨이 아니라 **DistSq 와 AnimFreezeRadiusSq 로 직접** 판정해야 F8 의 두-상수
	 *  분리가 살아 있기 때문에, 라벨만으로는 구현할 수 없다(그래서 DistSq 를 함께 받는다).
	 *
	 *  프리즈는 **레벨 트리거**다: 프리즈 중이면 매 패스 SetAnimState(Idle, 0) 을 다시 요구한다. SetAnimState 의
	 *  dedupe 가 흡수하므로 정상 상태 비용은 비교 몇 번이고, 그 대가로 우회 기록자(OnRep_Charging / ServerTickAttack)
	 *  가 남긴 Attack 포즈가 다음 패스에 반드시 회수된다(에지 1회 호출이면 언프리즈까지 고착된다).
	 *
	 *  ⚠️ 죽은 적에게는 프리즈를 적용하지 않는다 — 사망 dwell 은 사망 모션을 보여주려고 만든 창인데
	 *  Death→Idle 은 SetAnimState 의 one-shot 재진입 가드에 안 걸려 그대로 CPD 에 써진다. */
	void SetViewerLOD(FPSREnemyTuning::EFPSRDistanceBand NewBand, float ViewerDistSq);

	/** 후속 코스메틱 소비자(U13 VFX 등)의 조회 seam. 서버 배치패스의 게임플레이 밴드와 **다른 값이며 섞어 쓰면
	 *  MP 에서 틀린다** — 이 값은 코스메틱 전용이다. */
	FPSREnemyTuning::EFPSRDistanceBand GetViewerBand() const { return ViewerBand; }

	void SetShadowCasting(bool bEnabled); // 기존 유지

	/** 헬스바 가시성의 **거리 축**. 수명주기 축과 독립이며, 둘 다 참일 때만 실제로 보인다. */
	void SetHealthBarInRange(bool bInRange);

	/** 히스테리시스 판정에 필요한 현재 상태 읽기(그림자가 Mesh->CastShadow 를 직접 읽는 것과 같은 자리). */
	bool IsHealthBarInRange() const { return bHealthBarInRange; }

protected:
	/** 수명주기 축. Activate/클라 언하이드에서 true, HandleDeathCosmetic 에서 false.
	 *  종전 SetHealthBarVisible(bool) 을 대체한다(호출처 3곳: FPSREnemyBase.cpp:393·774·797.
	 *  BP 노출 없음을 실측 확인 — 선언 FPSREnemyBase.h:392 위에 UFUNCTION 없음 → 래퍼 불요). */
	void SetHealthBarAllowed(bool bAllowed);

private:
	/** 두 축을 합쳐 실제 위젯에 적용. 유일한 SetHiddenInGame 호출 지점. */
	void ApplyHealthBarVisibility();

	/** BeginPlay 의 InitHealthBarWidget 에서 1회 캐시. 종전엔 SetHealthBarVisible 이 매 호출
	 *  FindComponentByClass<UWidgetComponent>()(컴포넌트 배열 선형 스캔)를 돌았는데, 밴드 패스가 이를
	 *  적당 매 패스 호출하게 되므로 캐시가 **필수**가 된다. TObjectPtr = GC 가시성, Transient = 직렬화 제외. */
	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> CachedHealthBarWidget; // ⚠️ 이름 주의 — 아래 C3 정정 참조

	FPSREnemyTuning::EFPSRDistanceBand ViewerBand = FPSREnemyTuning::EFPSRDistanceBand::S0;

	/** 코스메틱 애니 프리즈 상태. 코스메틱 패스가 소유하고, 클라 드라이버(PostNetReceiveLocationAndRotation)와
	 *  권위 드라이버(TickServerMovement) **양쪽이 읽는다** — 종전엔 클라 경로에만 거리검사가 박혀 있어서
	 *  리슨서버 호스트(= 이 프로젝트 기준선)만 프리즈를 못 받았다. */
	bool bAnimFrozen = false;

	bool bHealthBarAllowed = true;
	bool bHealthBarInRange = true;
```

---

## 6. 함수별 계약

| 함수 | 권위 | 호출자 | 전제조건 | 실패 시 동작 |
|---|---|---|---|---|
| `FPSREnemyTuning::ClassifyBand` | 없음(순수) | 뷰어 패스 · 유닛테스트 | `DistSq >= 0` | 음수도 S0(무해) — 핫패스라 `ensure` 없음 |
| `FPSREnemyTuning::ApplyRadiusHysteresis` | 없음(순수) | 뷰어 패스 · 유닛테스트 | `OffRadiusSq >= OnRadiusSq` | 역전 시 히스테리시스가 사라질 뿐(진동) — config `ClampMin`으로 방지 |
| `UFPSREnemyCosmeticLODSubsystem::Tick` | 렌더하는 모든 머신 | 엔진 틱 | 로컬 뷰어 폰 존재 | 폰 없으면 **패스 전체 스킵**(기존 동작 유지 — 관전/무폰 프레임에 원점 기준 측정 금지) |
| `AFPSREnemyBase::SetViewerLOD` | 코스메틱 패스 전용 | 위 Tick | — | 아래 본문 계약 참조 |
| `AFPSREnemyBase::SetHealthBarInRange` | 코스메틱 패스 전용 | 위 Tick | — | 값 동일 시 조기 반환 |
| `AFPSREnemyBase::SetHealthBarAllowed` | 서버+클라(수명주기) | `Activate` · `SetActorHiddenInGame` · `HandleDeathCosmetic` | — | 값 동일 시 조기 반환 |
| `AFPSREnemyBase::ApplyHealthBarVisibility` | 로컬 | 위 두 setter | `HealthBarWidget` 유효 | null이면 no-op(BP가 위젯을 안 붙인 아키타입) |
| `AFPSREnemyBase::SetShadowCasting` | 코스메틱 패스 전용 | 위 Tick | `Mesh` 유효 | null이면 no-op(기존 유지) |

### 6-1. 코스메틱 패스 Tick 의 패스별 순서 (고정)

1. 설정 읽기 → 인터벌(`ShadowUpdateInterval`) 미달이면 반환(기존).
2. 로컬 뷰어 폰 취득 → 없으면 반환(기존).
3. 각 등록 적: stale 압축(기존) → `IsHidden()` 스킵(기존) → **`DistSq` 1회 계산**.
4. `Enemy->SetViewerLOD(ClassifyBand(DistSq), DistSq)`.
5. **그림자** — `bEnableEnemyShadowLOD`일 때만: `ApplyRadiusHysteresis(Mesh->CastShadow, DistSq, OnSq, OffSq)` → `SetShadowCasting(...)`. 반경·히스테리시스·수식 **기존과 동일**.
6. **헬스바** — `bEnableHealthBarLOD`일 때만: `ApplyRadiusHysteresis(Enemy->IsHealthBarInRange(), DistSq, OnSq, OffSq)` → `SetHealthBarInRange(...)`.

> ⚠️ **히스테리시스는 밴드 라벨이 아니라 각자의 config 반경으로 판정한다.** 밴드는 라벨이라 히스테리시스가 없고, 라벨로 on/off 하면 경계에서 깜빡인다. 밴드는 **조회 seam**용, 히스테리시스 반경은 **on/off 소비자**용 — 역할이 다르다.
> ⚠️ **거리 메트릭 변경을 이 명세가 소유한다(의도된 동작 변경, C3에서 드리프트로 오독 금지).** 오늘은 두 소비자가 **다른 거리**를 쓴다 — 클라 프리즈는 `FVector::DistSquaredXY`(`FPSREnemyBase.cpp:703`), 그림자는 3D `FVector::DistSquared`(`FPSREnemyShadowLODSubsystem.cpp:131`). 패스가 거리를 1회만 계산하므로 하나로 통일해야 하고, **3D를 택한다**(그림자 쪽 유지 = 그 소비자는 무회귀). 결과적으로 **프리즈만 XY→3D로 바뀐다**: 수직 분리 Z만큼 거리가 커지므로 프리즈가 **더 일찍** 걸린다. 이 맵의 최대 수직 분리(`L_Map1_City` 주 지면 Z=200 ↔ 고가 지면 Z≈800, 차 600cm)에서 S1 반경 3500 대비 실효 반경 감소는 √(3500²−600²)≈3448, 즉 **약 1.5%**다. 절감이 커지는 방향이고 코스메틱 한정이라 수용한다. 세 소비자가 서로 다른 거리를 쓰면 2층 맵에서 설명 불가능한 불일치가 나므로 통일 자체가 목적이기도 하다.

### 6-2. `SetViewerLOD` 본문 계약

```
ViewerBand = NewBand;
bAnimFrozen = (ViewerDistSq > FPSREnemyTuning::AnimFreezeRadiusSq);

// 레벨 트리거 + 사망 가드. AnimProfile 미할당(휴면) 아키타입은 무비용으로 빠진다.
if (bAnimFrozen && AnimProfile && !(HealthComponent && HealthComponent->IsDead()))
{
    SetAnimState(EFPSRAnimState::Idle, 0.0f);
}
```

**사망 가드가 필요한 근거(실측)** — 오늘의 두 애니 드라이버는 죽은 적에게 절대 쓰지 않는다: 클라 드라이버는 `IsDead()` 조기 반환(`FPSREnemyBase.cpp:688`)이 프리즈 검사(`:719`)보다 **앞**에 있고, 서버 드라이버는 `BeginDying`이 죽는 적을 `ActiveEnemies`에서 **즉시 제거**(`FPSREnemySpawnSubsystem.cpp:1644`)해 아예 순회하지 않는다. 반면 새 코스메틱 패스는 `RegisteredEnemies`(BeginPlay~EndPlay 스코프)를 돌고, dwell 중인 시체는 **보이는 상태라 `IsHidden()` 스킵에 안 걸린다**. `SetAnimState`의 one-shot 가드(`:609`)는 **같은 상태 재진입만** 막으므로 Death→Idle은 그대로 CPD에 써진다 → 시체가 사망 모션에서 Idle 정지 포즈로 튄다. 사망 dwell은 사용자 PIE 피드백(2026-08-25)으로 만든 가시성 창이므로 이 유닛이 깨서는 안 된다.

**레벨 트리거가 필요한 근거(실측)** — `OnRep_Charging`(클라, `:1043`)과 `ServerTickAttack`(호스트, `:864`)은 프리즈와 무관하게 `SetAnimState(Attack, ...)`를 쓴다. 원거리 교전 사거리(1400)는 **타겟 기준** 거리라 내 뷰어 거리와 무관하므로, 4인 협동에서 "남을 공격 중인, 내겐 먼 적"은 상시 존재한다. 오늘은 다음 넷업데이트마다 프리즈 분기가 재동결해 균형이 잡히고, `OnRep_Charging`의 해제 경로는 **명시적으로 `PostNetReceiveLocationAndRotation`의 재유도에 의존**한다고 주석(`:1046-1048`)에 적혀 있다. 에지 1회 호출로 바꾸면 그 의존이 끊겨 한 번 차징한 먼 적이 언프리즈/사망/재사용까지 Attack 포즈에 고착되고 그동안 WPO도 전진한다.

**수용된 한계** — 프리즈 중 `AttackAnimHoldUntil`이 미래로 찍힌 채 언프리즈되면, 권위 드라이버가 그 hold가 만료될 때까지 walk/idle을 안 쓴다(최대 `AttackAnimHoldSeconds`·`RangedChargeTime`). 유계·코스메틱 한정이라 수용한다.

### 6-3. 두 애니 드라이버의 변경

- **클라(`PostNetReceiveLocationAndRotation`)** — 거리 재계산과 `AnimFreezeRadiusSq` 비교를 삭제하고 `if (bAnimFrozen) { return; }`로 대체한다. 위치는 **기존 프리즈 분기 자리 그대로**(= `LastRecvLocation`/`LastRecvTime` 갱신 **뒤**) — 맨 위로 올리면 언프리즈 첫 샘플의 dt가 프리즈 기간 전체가 되어 속도 계산이 폭발한다.
- **권위(`TickServerMovement` walk/idle 분기)** — `if (AnimProfile && !bAnimFrozen && Ctx.Now >= AttackAnimHoldUntil)`. 이것이 **호스트 대칭**을 만든다.
- **프리즈가 억제하지 않는 것**: 사망(`HandleDeathCosmetic`)·히트플래시(`HandleHealthChangedForHitFlash`). 둘 다 드물고 에지 구동이며, 먼 적이 죽었는데 걷는 자세로 굳으면 그건 절감이 아니라 버그다.

---

## 7. 복제표

| 프로퍼티 / RPC | 종류 | Push Model | 신뢰성 | 조건 | 비고 |
|---|---|---|---|---|---|
| — | — | — | — | — | **신규 복제 0건.** |

- `ViewerBand`·`bAnimFrozen`·`bHealthBarInRange`·`bHealthBarAllowed`·`HealthBarWidget` 전부 **비복제 로컬 상태**다. 밴드는 각 머신이 자기 뷰어로 스스로 계산하므로 복제할 이유가 없고, 복제하면 오히려 틀린다(내 화면의 LOD를 남이 정하게 된다).
- Push Model이 패키지 빌드에서 꺼지는 문제(N-1)와 **무관**하다 — 복제 프로퍼티를 추가하지 않으므로 켜짐/꺼짐 어느 쪽에서도 동일 동작.
- 적 복제 계약(`Performance.md` §5 = Transform + Health/MaxHealth/bDead)은 **변경 없음**.

---

## 8. 수명주기 · 소유권

- **생성/등록** — 서브시스템: `ShouldCreateSubsystem`이 `IsRunningDedicatedServer()`만 거부(§5-3). 적: `BeginPlay`의 기존 `ShadowLOD->RegisterEnemy(this)` 호출을 개명된 서브시스템으로 치환(**호출 위치·조건 불변**).
- **해제/등록해제** — `EndPlay`의 대칭 `UnregisterEnemy`(기존 유지). Tick의 stale 압축도 유지(월드 종료 순서 비의존).
- **GC 소유** — `HealthBarWidget`은 `UPROPERTY(Transient)` `TObjectPtr`라 raw 누수 없음. 위젯의 실소유자는 BP 액터이므로 이 참조는 **캐시일 뿐 수명을 늘리지 않는다**. `RegisteredEnemies`는 약참조라 적 수명을 잡아두지 않는다(기존).
- **델리게이트** — 신규 구독 없음. 구독/해제 1:1 대칭 변화 없음.
- **`SetTickMode` 호출 시점** — `InitHealthBarWidget`에서 위젯을 캐시한 직후 1회(`InitWidget()` 뒤). 액터 수명당 1회라 풀 재사용과 무관하고, BP 저작값에 관계없이 결정론적이다.
- **풀 재사용(핵심)** — 재사용 시 리셋해야 하는 신규 상태:
  - `Activate()`(권위, 기존 애니 리셋 블록 옆 `FPSREnemyBase.cpp:388-405`): `bHealthBarAllowed = true` · `ViewerBand = S0` · `bAnimFrozen = false`.
  - `SetActorHiddenInGame(false)` 클라 리셋(`:765-779`): 동일 3개.
  - **`bHealthBarInRange`는 리셋하지 않는다** — 거리 축은 다음 패스(≤0.2초)가 권위 있게 다시 쓴다. true로 리셋하면 먼 곳에서 재활성된 적의 바가 최대 0.2초 깜빡인다.
  - `HealthBarWidget` 캐시·`SetTickMode`는 `BeginPlay` 1회라 풀 재사용에 무관(기존 `InitHealthBarWidget` 주석의 "single bind survives every reuse"와 같은 근거).
- **초기 동기화** — 코스메틱 패스는 최대 0.2초 뒤 첫 값을 쓴다. 그 사이 기본값은 **전부 "가까움"**(S0·프리즈 없음·바 보임)이라, 최악이어도 0.2초간 절감을 못 받을 뿐 **잘못 숨기는 일은 없다**(안전한 방향의 기본값).

---

## 9. 데이터드리븐 경계 (핵심원칙 2)

| 값 | 나가는 곳 | 기본값 | 비고 |
|---|---|---|---|
| 헬스바 LOD 사용 | `DefaultGame.ini` `[/Script/FPSRoguelite.FPSREnemyRenderSettings]` | `true` | perf 측정 단독 토글 |
| 헬스바 표시 반경 | 위 동일 | `3500.0` cm | **게임필 값 — 최종 조정은 사용자**(`[[leave-fine-tuning-to-user]]`) |
| 헬스바 히스테리시스 | 위 동일 | `300.0` cm | 그림자와 같은 폭 |
| 그림자 반경/히스테리시스/주기 | 위 동일(기존) | 변경 없음 | |

**C++에 남는 것 = 구조**: 밴드가 4단이라는 사실, 경계 비교 규칙, 두 평가자(서버/뷰어)의 분리, 소비자 배선. **밴드 반경 자체는 `constexpr`로 남긴다** — 이 값들은 `AttackStride` 무스로틀 밴드 불변식(`RangedEngageRange 1400 < S1 3500`, `FPSREnemySpawnSubsystem.cpp:551-556` 주석)에 결속돼 있어 런타임에 바뀌면 공격 판정 타이밍이 조용히 틀어진다. 즉 조정값이 아니라 **구조 상수**다(`[[code-is-immutable-structure-only]]`). 반대로 코스메틱 on/off 반경은 게임필이라 config로 나간다 — 이 경계가 이 유닛의 데이터드리븐 판정선이다.

---

## 10. 성능 예산 (핵심원칙 1)

- **틱** — 서브시스템 **1개**가 0.2초마다 1패스(기존과 동일 주기·동일 레지스트리). **액터당 틱 신규 0.** 패스가 하는 일은 늘지만 **순회 횟수는 그대로**다.
- **액터당 비용(신규)** — 패스마다: `DistSquared` 1회(기존 재사용) + 비교 ≤6회 + 자기-가드 세터 2~3회. 정상 상태(상태 변화 없음)에서는 세터가 전부 조기 반환 → 추가 렌더/틱 비용 0. 프리즈 중인 적은 매 패스 `SetAnimState` 1회를 더 부르지만 dedupe가 CPD 쓰기 전에 반환한다(비교 2회).
- **제거되는 비용(정밀화)**:
  - **헬스바 컴포넌트 틱** — 오늘 `TickMode`가 `Enabled`라 300개가 **매 프레임** `TickComponent`→`UpdateWidget()`을 돈다. `SetTickMode(Automatic)` + 거리 숨김으로 밴드 밖 위젯의 **컴포넌트 틱이 완전히 정지**한다(엔진 :1262가 스스로 끔). 이것이 이 유닛 최대 절감분이다.
  - **헬스바 RT 드로우** — `ShouldDrawWidget`에 이미 `WasRecentlyRendered(0.5)` 게이트가 있어(`:1373`) **화면 밖** 바는 오늘도 그리지 않는다. 따라서 신규 절감분은 **"화면 안 원거리 바"**로 한정된다(1인칭 FPS라 전방 시야에 원거리 적이 늘 있으므로 0이 아니다).
  - **선형 스캔** — `FindComponentByClass<UWidgetComponent>()`가 호출당 → **액터 수명당 1회**로.
  - **애니(호스트)** — S1 밖 적의 CPD 스칼라 쓰기와 원거리 GPU WPO 전진이 멈춘다(오늘 호스트가 전혀 못 받던 절감).
- **복제 대역** — 개체당 0바이트 증가(§7).
- **완화 수단** — 배치 처리(서브시스템 1패스) + 갱신 주기 분할(0.2초) + 히스테리시스. **Significance 플러그인은 쓰지 않는다**(§5-1 표제 "플러그인 enable ≠ 최적화").
- **정량 측정은 이 유닛의 몫이 아니다** — M0 종료 게이트인 후행 행(`성능 정량 베이스라인 실측`)이 소유한다. 이 유닛은 그 측정이 **잴 대상을 만들고**, §9의 단독 토글로 **대조군을 가능하게** 한다.

---

## 11. 미결정 항목 · 명세 갭 처리

**미결정 (사용자 결정 대기 — 구현을 막지 않음)**

1. **원 요구의 "VFX 다운그레이드"를 seam만 남기고 U13에 넘기는 재해석**(§2 비목표 1). 대상 부재는 실측으로 재확인됐으나, 행에 명시된 항목이므로 행 소유자의 명시 확인이 필요하다.
2. **헬스바 기본 반경 3500cm(S1)가 맞는가.** §5-1 계약에서 유도한 값이지 실플레이 검증치가 아니다. 1인칭 가독성과 절감이 맞부딪히는 지점이라 **PIE 체감으로 사용자가 정할 값**이다. 코드는 config 노브와 기본값만 제공한다.
3. **서브시스템 개명 여부.** 콘텐츠 참조 0건·ini 무영향·`git mv` 2파일로 안전 확인됨. 유지도 가능하나, "ShadowLOD" 이름으로 헬스바·프리즈를 끄면 다음 사람이 못 찾는다는 것이 개명 사유다(설계자·G1 모두 개명 찬성).

**후속 행 제안 (이 유닛에서 만들지 않음)**

- **F7** — 캡슐 `90.0f`(`FPSREnemyBase.h:149`·`FPSREnemySpawnSubsystem.h:389`)·`AgentFootprintRadius 40.0f`(`FPSRFlowFieldComputer.h:421`)·`GroundSnapTolerance 60.0f`(`FPSREnemyBase.h:618`)의 손동기화 산재를 `FPSREnemyTuning.h`로 흡수. 이 유닛이 그 헤더를 만들어 두므로 비용이 낮아진다.
- **적 SFX 밴드** — `UFPSRBlindspotAudioComponent`의 `ThreatRadius` 단일 컷을 §5-1 밴드로 재정합.
- **VFX 밴드 배선** — U13이 적 VFX를 만들 때 `GetViewerBand()`를 소비(필요 시 `UENUM` 래퍼 추가).
- **비사망 휴면의 헬스바 비대칭**(G1 P3-5 발견) — rear-drain 등 **사망을 거치지 않고** 풀로 돌아가는 적은 `bHealthBarAllowed`가 `true`인 채로 휴면한다. 이 유닛 이후 사망 경유 휴면은 위젯 틱이 공짜로 꺼지는데 비사망 휴면은 안 꺼지는 비대칭이 생긴다. **오늘과 동일한 동작이라 회귀는 아니므로** 이 유닛에서 고치지 않는다(범위 밖). 후보 처방 = `Deactivate` + 클라 하이드 에지에 `SetHealthBarAllowed(false)` 미러.

**갭 처리 규칙(고정)** — 구현 중 명세에 없는 판단이 필요해지면 Sonnet은 **추측해서 채우지 말고 멈추고 "명세 갭"으로 보고**한다. 갭은 C1으로 돌아가 Opus가 명세를 고친 뒤 재개하며, 갭이 구조를 바꾸면 G1을 다시 태운다.

---

## 12. 검증 기준 (무엇을 통과라 부르는가)

| # | 검사 | 통과 조건 |
|---|---|---|
| 1 | 명세 대조 | §5·§6·§7의 선언·시그니처·복제 설정(= 신규 복제 0)이 코드와 1:1 일치 |
| 2 | 빌드 | `Build.bat FPSRogueliteEditor Win64 Development -DisableUnity -NoXGE` → 로그의 **`Result: Succeeded`** 줄로 판정(종료코드 신뢰 금지 — `[[build-exit-code-lies-grep-result]]`). 헤더 신설·include 변경이 있으므로 `-DisableUnity` **필수**. **F8 트립와이어(`static_assert`)의 생존도 이 항목이 증명한다**(컴파일 게이트라 런타임 테스트 대상이 아니다) |
| 3 | 헤드리스 스모크 | `FPSRoguelite.Smoke.ModuleLoads` 통과 |
| 4 | 신규 유닛테스트 | `FPSRoguelite.Enemy.DistanceBand` — ① `ClassifyBand` 경계값(각 반경의 정확히 위/아래/같음 = 9케이스) ② `ApplyRadiusHysteresis`가 경계 왕복에서 토글 1회로 수렴 ③ **반경 값 잠금**: 세 상수가 역사적 리터럴(1500²/3500²/6000²)과 일치 — 이것이 §12-5의 자동화 보완이다 |
| 5 | **회귀(서버 밴드)** | `FPSREnemySpawnSubsystem.cpp`의 스트라이드/넷프리퀀시 분기가 **상수 식별자만 바뀐 diff**여야 한다(값 30/10/5/2·스트라이드 1/2/4/8·`<=` 경계·분기 순서 전부 원형 유지). §12-4-③이 반경 값을, diff 자명성이 분기 원형을 증명한다 |
| 6 | **회귀(그림자)** | 그림자 on/off·히스테리시스 동작 변화 0. `ApplyRadiusHysteresis`가 기존 인라인 수식(`bCasting ? OffSq : OnSq` 비교)과 **같은 식**임을 diff로 대조 + §12-4-②가 그 식을 테스트 |
| 7 | 반경 정합 알림 | 서브시스템 최초 Tick 1회, `ShadowCastRadius`²와 `SignificanceS0RadiusSq`가 어긋나면 **`UE_LOG(LogTemp, Log, ...)` 1회**. ini 저작으로 밴드에서 이탈하는 것은 정당한 행위이므로 `ensure`(디버거 브레이크·에러)로 올리지 않는다 |
| 8 | 기존 적 자동화 | `FPSREnemyDormantPoolTest`·`FPSREnemyFrontBudgetTest`·`FPSREnemyNetCullTest`·`FPSREnemyPursuitTest`·`FPSREnemyBlueprintParentTest` 전부 통과 |
| 9 | 레드팀 게이트 | §6-6-1 = §6-5-2 게이트 ②. **P1 잔존 시 머지 금지** |
| 10 | **PIE / 사용자 스모크** (Claude 불가) | `L_Map1_City` 리슨서버 + `FPSR.SpawnEnemies 300` 기준 ① 먼 적의 헬스바가 사라지고 다가가면 깜빡임 없이 돌아오는가 ② **호스트에서** 먼 적의 애니가 멈추고 다가가면 다시 움직이는가 ③ **사망 dwell 중 먼 적의 바가 되살아나지 않는가**(2축 계약) ④ **먼 곳에서 죽은 적이 사망 모션을 끝까지 유지하는가 — 특히 죽은 뒤 카이팅으로 경계를 넘길 때**(P1-1 회귀 검사) ⑤ **남을 공격 중인 먼 적이 Attack 포즈로 굳지 않는가**(P2-2 회귀 검사) ⑥ 풀 재사용된 적이 바 없이/굳은 채로 나타나지 않는가 ⑦ 2인 PIE: 원격 클라 화면에서도 ①②가 **그 클라 자신의 거리** 기준으로 동작하는가(호스트 거리 기준이면 실패) |

---

## 13. 레드팀 지적 원장 (C3에서 채운다)

*(설계 시점 — 비워 둔다)*
