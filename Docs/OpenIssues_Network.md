# 미해결 이슈 — 네트워크 / 빌드 (결정 대기)

> 등록 2026-08-07 · 발견 경위 = `Docs/Refactor_20260806_Report.md`(리팩토링 1회차 조사)
> **둘 다 코드를 고치지 않았다.** 올바른 답이 무엇인지가 사용자 결정 사항이기 때문이다.
> 해결되면 이 문서에서 지우고 `Docs/WorkLog.md` 로 내린다.

---

## N-1. 🔴 패키지(출시) 빌드에서 Push Model 이 통째로 꺼진다

**위치**: `Source/FPSRoguelite.Target.cs`
**영향**: `Game.md §1` · `Performance.md §5` 의 "복제 = Push Model" 전제가 **출시 빌드에선 성립하지 않는다**

### 무슨 일인가
`Config/DefaultEngine.ini:44` 에 `net.IsPushModelEnabled=1` 이 켜져 있고 코드도 Push Model 로 짜여 있는데,
**패키지 빌드에서는 Push Model 이 컴파일 단계에서 아예 빠진다.** 에디터/PIE 에서만 살아 있다.

결과: `MARK_PROPERTY_DIRTY*` 는 no-op 이 되고 `bIsPushBased` 는 무시되며 **비교 기반 복제로 폴백**한다.
**동작은 정상이다** — 복제는 제대로 된다. 다만 의도한 CPU 절감이 출시 빌드에 없다.
적 200마리 × 4인 협동의 호스트(리슨서버) 프레임 예산을 계산할 때 이 차이가 그대로 들어간다.

### 근거 (엔진 소스 직접 확인, 2026-08-06)
| 단계 | 위치 | 내용 |
|---|---|---|
| 1 | `Runtime/Net/Core/Public/Net/Core/PushModel/PushModelMacros.h:5-7` | `WITH_PUSH_MODEL` 기본값 = **0** |
| 2 | `UnrealBuildTool/Configuration/UEBuildTarget.cs:6076` | UBT 가 `TargetRules.bWithPushModel` 로 이 값을 정의 |
| 3 | `UnrealBuildTool/Configuration/TargetRules.cs:1420` | `bWithPushModel` 기본값 = **`(Type == TargetType.Editor)`** |
| 4 | `Source/FPSRoguelite.Target.cs` | `Type = TargetType.Game`, 오버라이드 **없음** |

### 왜 한 줄로 못 고치나
`bWithPushModel` 은 `[RequiresUniqueBuildEnvironment]` 속성이 붙어 있다.
이 엔진은 **Installed Build**(`D:\UnrealEngine\UE_5.7\Engine\Build\InstalledBuild.txt` 존재)라
고유 빌드 환경을 만들 수 없다 → Target.cs 에 한 줄 넣으면 UBT 가 거부한다.

### 선택지
| 안 | 내용 | 비용 / 리스크 |
|---|---|---|
| A | **소스 엔진 빌드로 전환** 후 `bWithPushModel = true` | 엔진 빌드 시간·디스크 큼. 얻는 것은 확실(설계 의도대로 동작) |
| B | **현 상태 유지** + `Performance.md §5` 에 "출시 빌드는 비교 기반 복제"라고 명시 | 비용 0. 대신 §5 예산 수치를 비교 기반 기준으로 다시 봐야 함 |
| C | **U14 perf 패스에서 측정 후 결정** | 판단을 데이터 위에서. 그때까지 §5 에 미결 표시 |

> 📌 함께 알아 둘 것: `FDoRepLifetimeParams::bIsPushBased` 기본값도 **`false`** 다
> (`UnrealNetwork.h:146`). **`DOREPLIFETIME_WITH_PARAMS_FAST` 를 썼다는 사실만으로는 push 기반이 아니다** —
> 호출부가 명시해야 한다. 이 프로젝트는 12개 클래스가 명시했고 `MARK_PROPERTY_DIRTY` 를 가진 파일이
> 정확히 그 12개라 **코드 자체는 일관돼 있다.** 문제는 오직 타깃 설정이다.

---

## N-2. 🔴 아무 클라이언트나 파티 전원을 로비로 강제 이동시킬 수 있다

**위치**: `Source/FPSRoguelite/Private/Core/FPSRPlayerController.cpp:613-625`
선언 = `Source/FPSRoguelite/Public/Core/FPSRPlayerController.h:155-156`

### 무슨 일인가
```cpp
void AFPSRPlayerController::ServerRequestReturnToLobby_Implementation()
{
    if (!HasAuthority()) { return; }          // Server RPC 라 항상 참 — 사실상 무의미한 검사

    if (AFPSRGameMode* GM = ...GetAuthGameMode<AFPSRGameMode>())
    {
        GM->RequestReturnToLobby();           // 파티 전체 ServerTravel
    }
}
```
`HasAuthority()` 검사 하나뿐인데, **Server RPC 는 서버에서 실행되므로 이건 언제나 참**이다.
그다음 바로 파티 전체가 로비로 `ServerTravel` 한다.

**없는 검사**: 런이 끝났는지 · 호출자가 호스트인지 · 다른 플레이어 동의 여부.
→ 런 도중 아무 클라이언트나 이 RPC 를 보내면 **진행 중인 런이 전원 강제 종료**된다.

### 왜 `WithValidation` 으로 못 고치나
파라미터가 없다. 문제는 인자 검증이 아니라 **몸통에 권한/상태 게이트가 없는 것**이다.

### 선택지 (게임 규칙 결정이라 코드로 정할 수 없음)
| 안 | 게이트 | 생각할 점 |
|---|---|---|
| A | **호스트만** 호출 가능 | 가장 단순. 4인 협동에서 호스트가 나가면 어차피 세션이 끝나므로 권한이 자연스럽다 |
| B | **`RunPhase == PostRun` 일 때만** | 원래 의도("결과 화면의 Return 클릭")에 가장 가깝다. 런 중 강제 종료를 막는다 |
| C | **전원 동의** | 가장 안전하지만 UI 작업이 붙는다 |
| D | A + B 둘 다 | 보수적. 결과 화면에서 호스트만 즉시 복귀, 나머지는 자동 이동 대기 |

> 원래 의도는 코드 주석에 적혀 있다 — *"비-권위 결과 위젯 경로: GameMode 의 자동 로비 이동을 미러링하되
> 딜레이 후가 아니라 플레이어가 Return 을 누른 즉시 발동"* (`FPSRPlayerController.h:152-154`).
> **의도대로면 B(런 종료 후)가 맞고, A 는 추가 안전장치다.**

---

## 관련 문서
- 전체 조사 결과 · 그 밖의 결정 대기 항목 = `Docs/Refactor_20260806_Report.md §7`
- 코드 구조 전반 = `Docs/ProjectStructure_Report.md`
