# U14R — U14 perf 측정 항목 레지스트리 (N-1 복제 폴백 + GMS §7-7)

> 이 명세는 **측정 항목 2건의 정본**이다: 무엇을(메트릭·CSV 컬럼명), 어떻게(시나리오·러너), 무엇으로 판정하는지(잠정 기준).
> 실측 자체는 이 유닛의 범위가 아니다 — **M0 EC ① 베이스라인 패스**(보드 후행 행)가 이 명세를 읽고 실행한다.

---

## 1. 메타

| 항목 | 값 |
|---|---|
| 유닛 ID / 이름 | U14R / U14 perf 측정 항목 등록 |
| 브랜치 | `perf/u14-measure-registry` |
| 작성 모델 | `claude-fable-5` |
| 작성일 / 최종 갱신 | 2026-08-19 |
| 상태 | `구현완료` |
| 관련 SSOT | `Docs/SSOT/Performance.md` §5 · `Docs/OpenIssues_Network.md` N-1 · `Docs/Refactor_20260806_Report.md` §7-7 · `Docs/SSOT/Workflow.md` §6-5-2·§6-6 |
| 관련 메모리 | `[[refactor-report-stale-premises]]`(전제 재대조 완료 — 이번 2건은 참) |

## 2. 목표 / 비목표

**목표** — 이 유닛이 끝나면:
1. 2026-08-07 "측정 후 결정" 이연 2건(N-1 Push Model 폴백, GMS §7-7 리스너 복사)이 **CSV 컬럼명 단위로 측정 가능**해진다 — EC ① 패스 세션이 러너 1회 실행 + 분석기 1회 실행으로 두 축의 수치를 얻는다.
2. 판정 기준(잠정 임계 + 초과 시 다음 행동)이 문서로 고정되어, 패스 세션이 기준을 재발명하지 않는다.
3. `Workflow.md:28` 환경표의 "소스 빌드" 오기가 Installed Build로 정정된다(N-1 A안 비용 오판 차단).

**비목표(Non-goals)** — 구현자가 채워 넣지 말 것:
- 실측 실행(EC ① 패스 몫) · GMS 재설계(세대 카운터/톰스톤) · `Target.cs` 변경 · 소스 엔진 전환 · RepGraph 도입 · 요청 밖 정리/리팩토링.
- GMS의 브로드캐스트/구독 **동작 변경 일절 금지** — 계측은 관찰만 한다(early-out 계약 포함 기존 제어 흐름 무변경).
- 신규 헤더·클래스·서브시스템 신설 금지(기존 CSV 인프라 재사용).

## 3. 제1원리 3줄 (핵심원칙 4)

1. **제1원리 근거** — 적 ~200-300 동시 + 4인 리슨서버에서 호스트 프레임 예산이 최대 리스크(§5). 두 이연 결정 모두 "호스트 CPU에서 실제로 몇 ms인가"가 결정 변수이므로, 측정 축을 예산 잣대(§5)와 같은 구성·같은 시나리오로 등록해야 비교 가능하다.
2. **엔진 기본값·기존 인프라와의 관계** — 그대로 쓴다. 복제 비용은 엔진 기본 CSV 스탯이 이미 내보낸다(실물 캡처 대조: `Exclusive/GameThread/ServerReplicateActors`·`NetworkOutgoing`, `Replication/*`; 카테고리 기본 on — `CsvProfiler.cpp:71-72`, `NetCoreModule.cpp:25`, `NetDriver.cpp:5940`). 프로젝트 커스텀 스탯 패턴(`FPSREnemyMetricsSubsystem.cpp:22-24`, `CSV_PROFILER_STATS` 게이트 + default-on 카테고리)도 그대로 미러링한다. 새 프로파일링 기구 신설 없음.
3. **프로젝트 제약과의 정합** — 측정 구성 = Development 패키지(§5 정정 2026-08-13, 런처 엔진 Test 불가)에서 CSV 스탯 활성. Shipping에선 `CSV_PROFILER_STATS=0`으로 컴파일 아웃. 서버권위/복제 경로 코드 무변경이라 회귀 표면 없음.

## 4. 파일 목록

| 경로 | 신규/수정 | 한 줄 설명 |
|---|---|---|
| `Source/FPSRoguelite/Private/Messages/FPSRGameplayMessageSubsystem.cpp` | 수정 | GMS CSV 계측(카테고리 `FPSRMsg` + 스탯 4종). 헤더 무변경 |
| `Source/FPSRoguelite/Private/Core/FPSRPlayerController.cpp` | 수정 | `FPSR.Invuln` late-joiner 재적용(디버그 픽스처 내부, §5-C) |
| `Scripts/measure_swarm_render.ps1` | 수정 | `-ClientCount N` 멀티클라 오케스트레이션 |
| `Scripts/analyze_swarm_csv.py` | 수정 | `WANTED` 컬럼 9종 추가(카운트성 6종) |
| `Docs/SSOT/Performance.md` | 수정 | §5 측정 항목 레지스트리 소절 + N-1 주 갱신 (Fable 직접) |
| `Docs/SSOT/Workflow.md` | 수정 | :28 환경표 "소스 빌드"→"Installed Build" 정정 (Fable 직접) |
| `Docs/OpenIssues_Network.md` | 수정 | N-1 C안 상태 갱신 (Fable 직접) |
| `Docs/Refactor_20260806_Report.md` | 수정 | §7-7 포인터 1줄 추기 (Fable 직접) |

## 5. 인터페이스 선언 (이 유닛의 "인터페이스" = 측정 항목 계약 + 계측 삽입 계약)

### 5-A. 측정 항목 A — N-1 복제 폴백 비용 (엔진 스탯, 신규 계측 0)

| | |
|---|---|
| **결정 메트릭** | `Exclusive/GameThread/ServerReplicateActors` avg·P95 (호스트 게임스레드 ms) |
| **보조 메트릭** | `Exclusive/GameThread/NetworkOutgoing` · `Replication/NumberOfActiveActors`·`NumberOfFullyDormantActors`(dormancy 동작 확인) · `FPSREnemy/ServerAlive`(적 수 유효성). ⚠️ `Replication/NumConnections` 등 커넥션 통계는 **Game 패키지에서 영원히 0**(`USE_SERVER_PERF_COUNTERS = (UE_SERVER \|\| UE_EDITOR) && WITH_PERFCOUNTERS`, `Build.h:115` — 컬럼은 등록되나 갱신 블록이 컴파일 아웃, 스모크 실측) — 쓰지 말 것 |
| **유효성 게이트(접속)** | **호스트 로그 `AddClientConnection` 카운트 = 클라 수**(러너가 게이트 실행·출력). CSV 커넥션 스탯이 죽어 있으므로 로그가 유일한 접속 증거다. 보조: 판정 창에서 `ServerRepActors` 비영 |
| **시나리오** | VAT-1 고정 시나리오 재사용: `L_Map1_City?listen` · Development 패키지 · `-novsync` · `FPSR.SpawnEnemies N 6000` · PlayerStart 고정 카메라 + **클라 3 인스턴스**(`127.0.0.1` 접속, `-nullrhi -nosound`) = 4인. N=300(판정) + N=500(스트레스) 2회 |
| **실행** | `Scripts\measure_swarm_render.ps1 -BuildDir <pkg> -Label N1_300_4p -EnemyCount 300 -ClientCount 3 -CaptureFrames 18000` → `python Scripts/analyze_swarm_csv.py <capture.csv> --skip-seconds <조인 완료 시점>`. ⚠️ **멀티클라 런은 `-CaptureFrames`를 크게(≥18000)** — 캡처는 부트 ExecCmds에서 시작되는데(`CsvProfile`엔 지연 시작 옵션 없음 — `CsvProfiler.cpp:951-988` 실물) 클라 조인은 캡처 시작 +35~60s에 완료되므로, 기본 6000프레임은 uncapped fps에선 조인 전에 닫힌다(스모크 실측: 278fps × 6000 = 21.6s). 판정 창 = ⑥ |
| **판정 기준(잠정 — 패스 실행 시 사용자 비준)** | `ServerReplicateActors` avg **≤1.5ms @300×4클라** → **B안**(현상 유지 + §5에 "출시=비교 기반 복제" 명기) 권고. 초과 → 소스 엔진 전환(A안) 전에 **RepGraph 앞당김을 먼저 평가**(§5 도구 평가 순서 Push→RepGraph→Iris; 비교 기반 폴백 비용의 대부분은 connection별 relevancy로 깎이는 게 먼저다) |
| **캐비앗(측정 기록에 명기)** | ① 동일 머신 4인스턴스 = CPU 경합 → 수치는 비관(보수) 방향, 합격 판정엔 안전 ② Development는 Test 대비 5~15% 보수적(§5 2026-08-13) ③ Push-on 대조군은 Installed Build에서 제작 불가 → **절대비용 판정**(델타 아님). 이 구성은 "현상 그대로"(Push Model 꺼진 출시 상태)를 잰다 ④ `Exclusive/*`는 게임스레드 자기시간 — 클라 인스턴스 CPU 경합이 늘리는 방향이지 줄이는 방향이 아님 ⑤ **루프백 = `-nosteam` + IpNetDriver 강제**(🔁 정정 2026-08-19, 스모크 2회 실측): 패키지는 GameNetDriver를 SteamSockets로 라우팅(`DefaultEngine.ini:104-105`)해 `127.0.0.1` 다이얼과 프로토콜이 어긋나고(1차: 클라 핸드셰이크 타임아웃), NetDriverOverrides만으로는 **소켓 서브시스템이 Steam으로 대체**된 상태라 raw UDP 바인드가 실패한다(2차: `SteamSockets: setsockopt SO_BROADCAST failed` → `NetDriverListenFailure` → 메인메뉴 폴백). → `ClientCount>0`이면 러너가 host·클라 모두 **`-nosteam`**(OSS Steam 비활성 = SteamSockets 소켓 서브시스템 미등록, 엔진 `OnlineSubsystemModuleSteam.cpp:73`·`SteamSocketsModule.cpp:17-20` 실물) **+ `-NetDriverOverrides=/Script/OnlineSubsystemUtils.IpNetDriver`**(`UnrealEngine.cpp:14543`) 부여. `ServerReplicateActors`(게임스레드 직렬화·비교 비용)는 드라이버 무관 — 소켓 계층이 프로덕션(SteamSockets 릴레이)과 다름은 기록에 명기 ⑥ 클라 접속은 캡처 시작 +5~10s(웜 부팅)~수십 초(콜드) 뒤 완료 → **판정 창은 호스트 로그 `AddClientConnection` 마지막 시각 이후로 `--skip-seconds` 상향**(CSV `NumConnections`는 Game 빌드에서 죽은 스탯이라 창 판정에 못 쓴다 — 위 유효성 게이트 행) ⑦ **오프닝 카드 프리즈(§5-C(2-b))**: late-join 클라의 오프닝 시드가 전역 프리즈를 재유발한다(스모크6 실측) — 러너가 `SkipCards` 반복 모드로 5s 내 해소하지만, 조인 직후 최대 5s의 일시 프리즈 구간은 남으므로 **판정 창은 마지막 `[Run] RESUME` 이후**로 잡는다. 러너 프리즈 게이트(FREEZE>RESUME 경고)가 미해소 잔존을 잡는다 |

### 5-B. 측정 항목 B — GMS §7-7 브로드캐스트 비용 (신규 계측 = 5-C)

| | |
|---|---|
| **결정 메트릭** | `FPSRMsg/GameThread/GMSBroadcast` (프레임당 인클루시브 ms 합 — 🔁 정정 2026-08-19: 타이밍 스탯 컬럼은 엔진이 스레드명을 끼운다, `Networking/GameThread/*`와 동일 규칙·캡처 실물 확인) |
| **보조 메트릭** | `FPSRMsg/Broadcasts`(early-out 통과 브로드캐스트/프레임) · `FPSRMsg/Dispatches`(콜백 호출/프레임) · `FPSRMsg/ListenersCopied`(**복사된 리스너 요소 총합/프레임 = TFunction 힙 할당 프록시 — 부모 태그 체인 증폭이 여기 잡힌다**) |
| **시나리오** | 항목 A와 **같은 캡처**에서 공짜 수집 |
| **🔴 선행조건 (2026-08-19 grep 실측)** | **현재 GMS엔 프로덕션 발행처·구독처가 0이다**(데모 커맨드·테스트뿐 — 적 사망 발행은 U13 배선 몫, `FPSRGameplayMessageSubsystem.cpp:114` 주석). 구독 0이면 early-out이라 전 스탯 = 0. **U13 발행/구독 배선 전 캡처에서 `FPSRMsg/*`가 0인 것은 "통과"가 아니라 "측정 불가(연기)"로 기록**하고, 항목 B 실측은 U13 배선 후 캡처로 판정한다 |
| **판정 기준(잠정)** | `GMSBroadcast` avg **≤0.2ms @300** → §7-7 재설계 불요(보류 유지). 초과 → 세대 카운터/톰스톤 재설계 **신규 코어 행 생성**(§7-7 "구조 변경 = 별도 작업" 그대로) |
| **캐비앗** | 스코프 타이밍 스탯은 캡처 중 브로드캐스트당 마커 2개를 쌓는다 — 캡처 중에만 발생하는 관찰 비용이며, 측정치에 자기비용이 소량 포함된다(과대 방향 = 판정엔 안전) |

### 5-C. 계측 삽입 계약 (구현자는 이대로만)

**(1) `FPSRGameplayMessageSubsystem.cpp`** — 헤더 무변경, cpp만.

- include 추가: `#include "ProfilingDebugging/CsvProfiler.h"` (기존 include 블록 말미).
- 카테고리 정의(파일 스코프, `DEFINE_LOG_CATEGORY_STATIC` 아래) — `FPSREnemyMetricsSubsystem.cpp:22-24` 패턴 미러:
```cpp
#if CSV_PROFILER_STATS
CSV_DEFINE_CATEGORY(FPSRMsg, true);
#endif
```
- `BroadcastMessageInternal` 계측 3지점 — **`ListenerMap.IsEmpty()` early-out(cpp:24-27)보다 앞에는 어떤 계측도 넣지 않는다**(적 사망 ×수백/프레임 zero-cost 계약):
  - (a) early-out 직후(현재 cpp:29 주석 위):
```cpp
#if CSV_PROFILER_STATS
	CSV_SCOPED_TIMING_STAT(FPSRMsg, GMSBroadcast);
	CSV_CUSTOM_STAT(FPSRMsg, Broadcasts, 1, ECsvCustomStatOp::Accumulate);
#endif
```
  - (b) 리스너 배열 복사 직후(현재 cpp:37 `TArray<...> ListenerArray(...)` 다음 줄):
```cpp
#if CSV_PROFILER_STATS
			CSV_CUSTOM_STAT(FPSRMsg, ListenersCopied, ListenerArray.Num(), ECsvCustomStatOp::Accumulate);
#endif
```
  - (c) 콜백 호출 지점 — 기존 `#if !UE_BUILD_SHIPPING ++TotalDispatchCount; #endif`(cpp:50-52) **바로 아래에 별도 블록**으로:
```cpp
#if CSV_PROFILER_STATS
					CSV_CUSTOM_STAT(FPSRMsg, Dispatches, 1, ECsvCustomStatOp::Accumulate);
#endif
```
- 기존 `TotalDispatchCount`(`!UE_BUILD_SHIPPING`)는 **그대로 유지**(테스트가 사용). 두 카운터는 게이트가 다르므로 통합 금지(`CSV_PROFILER_STATS`는 `!UE_BUILD_SHIPPING`의 부분집합이 아니라 별개 구성 축 — CsvProfilerConfig.h).
- **(d) `FPSR.GMS.Demo` 반복 모드**(계측 실증용 — 1회 발행은 ExecCmds 시점이라 CSV 캡처 시작(프레임 경계) 전에 끝나 비영 확인 불가): 사용법 `FPSR.GMS.Demo [RepeatSeconds=0]`.
  - `RepeatSeconds <= 0`(기본) = **기존 동작 문자 그대로**(즉시 1회 발행 + 즉시 Unregister).
  - `> 0` = 임시 리스너를 유지한 채 **1초 주기 월드 타이머**로 같은 메시지를 재발행, 만료 시 타이머 해제 + `Handle.Unregister()` + 완료 로그. 타이머 핸들 = 파일 스코프 static `FTimerHandle`(Invuln §5-C(2)와 같은 디버그 픽스처 패턴 — 월드 소유라 월드 소멸 시 자동 정리), 만료 시각은 람다 값 캡처.
  - **잔존 리스너 정리(레드팀 P2-3 수용, 2026-08-19)**: 반복 진행 중 커맨드 재실행 시 `SetTimer`가 기존 타이머를 지우면 이전 람다가 실행 없이 파괴되어 값 캡처된 리스너 핸들이 `Unregister` 못 하고 누수된다(엔진 `TimerManager.cpp` `FindTimer→InternalClearTimer` 실물) → 리스너가 잔존하면 이후 `Dispatches`/`ListenersCopied`가 배수로 오염. 해법: **활성 리스너 핸들도 파일 스코프 static**으로 보관하고, 커맨드 진입 시(양 모드 공통) 기존 타이머 `ClearTimer` + 기존 핸들 `Unregister`를 먼저 수행한 뒤 새로 등록한다.
  - 전부 기존 `#if !UE_BUILD_SHIPPING` 데모 블록 안. 도움말 텍스트에 반복 모드 1줄 추가.
- 그 외 어떤 줄도 변경 금지(제어 흐름·주석 포함).

**(2) `FPSRPlayerController.cpp` — `FPSR.Invuln` late-joiner 재적용.**
현재 커맨드(cpp:916-)는 호출 시점의 PC 이터레이션 1회라 **부트 `-ExecCmds` 이후 접속한 원격 클라를 못 덮는다** → 4인 측정에서 클라 폰이 수 초 내 다운 = 캡처 무효. 다음 계약으로 확장:
- 기존 즉시 적용 루프는 유지. 추가로 같은 람다 안에서 **월드 타이머 반복 재적용**을 건다: 주기 10s, 만료 = 커맨드의 `Seconds` 시점. 재적용 콜백은 기존과 동일한 "전 PC 이터레이션 + `BeginGraceWindow(남은 시간)`" — `BeginGraceWindow`는 래칫(더 짧으면 무시, `FPSRCharacter.cpp:1576-1582`)이라 재적용은 멱등 안전.
- 타이머 핸들은 파일 스코프 static `FTimerHandle`. 월드가 죽으면 타이머도 같이 죽고, 다음 호출의 `SetTimer`가 핸들을 재사용한다(디버그 픽스처 허용 범위 — 프로덕션 수명주기 아님).
- 전부 기존 커맨드가 사는 것과 같은 컴파일 게이트 안(커맨드 등록 블록 밖으로 코드가 새지 않게).
- 도움말 텍스트에 "re-applies every 10s to cover late-joining clients" 한 줄 추가.

**(2-b) `FPSR.SkipCards` 반복 모드 (레드팀 P2-1 수용, 2026-08-19)** — 스모크6 실측: 클라 조인 1초 뒤 `[Run] FREEZE (card selection)` 재발 + RESUME 없음. late-join 클라의 오프닝 시드(`ServerNotifyClientReady`→`BeginOpeningSeed`)가 무인 클라에서 영원히 미해소 → 전역 프리즈로 4인 캡처가 "멈춘 월드"를 잰다(복제 비용 과소 = 거짓 B안 위험). 해법 = Invuln (2)와 동일 패턴:
- 사용법 `FPSR.SkipCards [RepeatSeconds=0]` — 델리게이트를 `FAutoConsoleCommandWithWorldAndArgs`로 전환 허용(인자 수용의 유일 경로).
- `<= 0`(기본) = 기존 동작 문자 그대로(전 PC 슬롯0 해소 + `RefreshPauseState` 1회).
- `> 0` = **5초 주기** 월드 타이머로 같은 해소 루프+`RefreshPauseState` 재실행(멱등 — pending 있는 PC만 해소된다), 만료 시 `ClearTimer`. 파일 스코프 static `FTimerHandle`, 기존 해소 루프는 헬퍼로 추출해 즉시/타이머 공유. 도움말 1줄 추가.
- 측정 함의(§5-A ⑦): 조인→다음 타이머 틱 사이 최대 5s의 일시 프리즈 구간은 남는다 — 판정 창을 마지막 `[Run] RESUME` 이후로 잡아 배제한다.

**(3) `Scripts/measure_swarm_render.ps1`** — `-ClientCount N`(기본 0 = 기존 동작 완전 무변경).
- param 블록에 `[int]$ClientCount = 0` 추가.
- **루프백 강제(§5-A ⑤)**: `$ClientCount -gt 0`이면 호스트 `$gameArgs`에 `-nosteam` + `-NetDriverOverrides=/Script/OnlineSubsystemUtils.IpNetDriver`를 추가(0이면 기존 인자 그대로 = VAT 솔로 시나리오 무변경). 클라 인자에도 동일 2플래그.
- **삽입 지점 = 1단계(캡처 시작 감지) 성공 직후, 2단계(스크린샷) 전**: `if ($csv -and $ClientCount -gt 0)` → 클라 N개를 2초 간격 순차 기동:
  `Start-Process $exe -ArgumentList @("127.0.0.1", "-nullrhi", "-nosound", "-windowed", "-resx=640", "-resy=360", "-log", "-nosteam", "-NetDriverOverrides=/Script/OnlineSubsystemUtils.IpNetDriver")`
  (CSV 생성 = 엔진 초기화 후 ExecCmds 실행 완료 = 맵 로드·리슨 확립의 실측 가능한 신호 — 그 전 조인은 접속 실패 리스크).
- 종료·정리: 기존 프로세스-**이름** 기준 정리(스크립트 상단 함정 주석)가 클라도 같이 덮는다 — 추가 정리 코드 금지.
- 유효성 게이트 확장: 기존 END/DBNO 그렙 유지 + `$ClientCount > 0`이면 복사된 `game.log`에서 `AddClientConnection` 발생 수를 세어 `== $ClientCount`면 통과 로그, 불일치면 경고 출력 — **초과는 재접속(타임아웃 후 재시도) 의심을 병기**(레드팀 P3-3)(🔁 정정 2026-08-19: 종전 "CSV NumConnections 확인" 안내는 무효 — 그 스탯은 Game 빌드에서 갱신되지 않는다, §5-A).
- **호스트 로그 고정(레드팀 P2-2 수용)**: 4인스턴스가 같은 `Saved/Logs`를 공유하고 게이트 로그 전부(`AddClientConnection`·END/DBNO·FREEZE)가 서버측이라, "최신 로그 1개" 복사는 클라 로그를 집을 수 있다(비결정) → **클라 기동 직전**(그 시점 유일한 신규 로그 = 호스트) 최신 `FPSRoguelite*.log` 경로를 `$hostLog`로 스냅샷하고, 종료 후 복사·게이트는 그 파일을 우선 사용(스냅샷 없으면 기존 최신-선택 폴백).
- **프리즈 게이트(레드팀 P2-1 수용)**: `game.log`에서 `[Run] FREEZE` 수 > `[Run] RESUME` 수면 "캡처에 프리즈 구간 잔존 — 측정 무효 의심" 경고. `$ClientCount > 0`이면 ExecCmds의 `FPSR.SkipCards`를 **`FPSR.SkipCards $invulnSeconds`**(반복 모드, (2-b))로 바꿔 late-join 프리즈를 5s 내 해소(0이면 기존 1회 호출 그대로 = VAT 솔로 무변경).
- **완주 대기 자동 산출(레드팀 P2-4 수용)**: `-MaxWaitSeconds` 미지정 시 `max(300, CaptureFrames/30 + 120)`로 자동 상향(60fps 미만에서 18000프레임이 300s를 초과해 캡처가 중간 킬 → 꼬리 헤더 없는 불완전 CSV가 되는 모순 차단). 명시 지정 시 그 값 그대로.
- **호스트 단독 크래시 감시(레드팀 P3-1 수용)**: 클라 기동 직전 시점의 `FPSRoguelite*` PID들을 호스트 후보로 스냅샷 → 3단계 대기 루프에서 그 PID가 전멸하면 "host exited (crash?)" 경고 후 중단(클라 생존이 호스트 크래시를 가리는 것 방지). `$ClientCount = 0`이면 기존 이름 기반 검사 그대로.
- 헤더 주석의 사용 예에 `-ClientCount 3` 예시 1줄 추가.

**(4) `Scripts/analyze_swarm_csv.py`** — `WANTED` 리스트에 아래 9줄 추가(기존 순서 뒤에, 라벨=컬럼명 그대로):
```
("Excl/ServerRepActors", "Exclusive/GameThread/ServerReplicateActors"),
("Excl/NetworkOutgoing", "Exclusive/GameThread/NetworkOutgoing"),
("Repl/ActiveActors",    "Replication/NumberOfActiveActors"),
("Repl/FullyDormant",    "Replication/NumberOfFullyDormantActors"),
("FPSRMsg/GMSBroadcast", "FPSRMsg/GameThread/GMSBroadcast"),
("FPSRMsg/Broadcasts",   "FPSRMsg/Broadcasts"),
("FPSRMsg/Dispatches",   "FPSRMsg/Dispatches"),
("FPSRMsg/ListenersCopied", "FPSRMsg/ListenersCopied"),
("FPSREnemy/ServerAlive", "FPSREnemy/ServerAlive"),
```
- 출력 포맷: 카운트성 컬럼(`Repl/ActiveActors`·`Repl/FullyDormant`·`FPSRMsg/Broadcasts`·`Dispatches`·`ListenersCopied`·`FPSREnemy/ServerAlive`)은 기존 `RHI/` 분기와 같은 정수 포맷(avg/P50/max)으로 — 라벨 접두 판정을 `RHI/` 하드코딩에서 **카운트 라벨 집합**으로 바꾼다(기존 `RHI/` 2종 포함). 시간성(`Excl/*`·`FPSRMsg/GMSBroadcast`)은 ms 포맷 유지. 결측 컬럼은 기존 "(no data)" 경로(구 CSV 재분석 호환 — 동작 확인만, 로직 변경 금지).

## 6. 함수별 계약

| 함수/지점 | 권위 | 호출자 | 전제조건 | 실패 시 동작 |
|---|---|---|---|---|
| `BroadcastMessageInternal` 계측 | 권위 무관(순수 로컬 버스) | 기존 호출자 전부 | 캡처 중일 때만 기록(CSV 매크로 자체 게이트) | 계측 실패 개념 없음(no-op 매크로) |
| `FPSR.Invuln` 재적용 타이머 | 호스트(authority PC만 적용 — 기존 루프 조건 유지) | 콘솔/ExecCmds | 월드 유효 | 월드 소멸 시 타이머 자동 소멸 |
| 러너 클라 기동 | — | 측정 실행자 | 호스트 CSV 생성 감지 후 | 클라 미접속 시 러너 유효성 게이트(`AddClientConnection` 카운트) 경고로 드러남 |

## 7. 복제표

해당 없음 — 복제 프로퍼티·RPC·수명주기 변경 0. GMS는 순수 로컬 버스(헤더 계약), `FPSR.Invuln` 확장은 기존 서버측 `BeginGraceWindow` 재호출뿐(복제 표면 기존과 동일).

## 8. 수명주기 · 소유권

해당 없음(신규 오브젝트 0) — 유일한 상태는 ① CSV 카테고리 전역(엔진 관리) ② `FPSR.Invuln` static `FTimerHandle`(월드 타이머 매니저 소유, 월드 소멸 시 자동 정리 — 5-C(2)에 명시).

## 9. 데이터드리븐 경계 (핵심원칙 2)

해당 없음 — 스탯명·카테고리명은 구조(분석기와의 계약)라 C++ 상수가 맞다. 판정 임계(1.5ms/0.2ms)는 코드가 아니라 **이 문서**에 산다(§11 — 사용자 비준 대상).

## 10. 성능 예산 (핵심원칙 1)

- **비캡처 시**: GMS 추가 비용 = CSV 매크로 내부 카테고리 체크뿐(엔진 전역 스탯들과 동일한 수용 비용). early-out 앞 계측 금지로 구독자 0 핫패스는 문자 그대로 무변경. Shipping은 컴파일 아웃.
- **캡처 시**: 브로드캐스트당 타이밍 마커 2 + 커스텀 스탯 push — 관찰 비용, 측정치에 과대 방향으로만 반영(판정 안전).
- **틱 신설 없음** · 액터당 비용 없음 · 복제 대역 없음.
- `FPSR.Invuln` 타이머 = 10초당 PC 이터레이션 1회(≤4) — 무시 가능.

## 11. 미결정 항목 · 명세 갭 처리

**미결정(의도적)**:
1. 판정 임계 1.5ms(항목 A)·0.2ms(항목 B)는 **잠정** — EC ① 패스에서 실측치를 앞에 두고 사용자가 비준/조정한다(임계의 논거: §5 호스트 예산 16.6ms 중 복제 몫 ~10%, GMS는 코스메틱 버스가 프레임 1% 초과 시 구조 재검토가 §7-7의 원 문제의식).
2. 4인 캡처의 `CaptureFrames`/대기 상한 튜닝 — 패스 세션이 첫 실행에서 조정(기본값으로 시작).

**갭 처리 규칙(고정)**: 구현 중 명세에 없는 판단이 필요하면 **멈추고 "명세 갭"으로 보고**한다. 갭은 C1으로 돌아와 이 문서를 고친 뒤 재개한다.

## 12. 검증 기준 (무엇을 통과라 부르는가)

| # | 검사 | 통과 조건 |
|---|---|---|
| 1 | 명세 대조 | §5-C의 삽입 지점·스탯명·게이트가 diff와 1:1 일치, 금지선(early-out 앞 계측, 제어 흐름 변경) 위반 0 |
| 2 | 빌드 | `Build.bat FPSRogueliteEditor Win64 Development -Project="E:\Git_Project\FPSRoguelite2\FPSRoguelite.uproject"` **Succeeded** — include 추가가 있으므로 **`-DisableUnity` 1회 병행**(유니티 블롭이 include 누락을 가린다) |
| 3 | 헤드리스 스모크 | `FPSRoguelite.Smoke.ModuleLoads` + 기존 GMS 테스트 통과 |
| 4 | 계측 실증 | `UnrealEditor-Cmd -game -nullrhi ... -ExecCmds="FPSR.GMS.Demo 30, CsvProfile Frames=300"` 캡처 CSV에 `FPSRMsg/*` 4컬럼 존재 + 반복 발행 프레임에서 비영(반복 모드 근거 = §5-C(1)(d)) |
| 5 | 러너 실증 | 기존 2026-08-13 패키지(`E:\Git_Project\FPSRoguelite\Packaged\26_8_13_BuildTest_1_B`)로 `-ClientCount 3` 스모크: 클라 접속(호스트 로그 `AddClientConnection` = 3, 러너 게이트 통과 출력) + 프로세스 정리 완주 + 캡처 CSV의 넷 컬럼(`ServerRepActors`·`ActiveActors`) 유효 + **프리즈 게이트가 구 바이너리의 미해소 프리즈를 경고로 검출**(SkipCards 반복 모드는 구 바이너리에 없어 프리즈가 실제로 남는다 — 게이트 음성이 아니라 **경고 출력이 통과 조건**). 구 바이너리라 `FPSRMsg/*` 부재·DBNO 오염은 이 스모크의 실패가 아님 — 오케스트레이션·게이트만 판정 |
| 6 | 레드팀 게이트 | `Workflow.md` §6-6-1대로. **P1 잔존 시 머지 금지** |
| 7 | 회귀 | `-ClientCount` 미지정 시 러너 동작 diff 0 · 분석기가 구 CSV(B_300_fixed)를 종전과 동일 수치로 분석 + 신규 컬럼 출력 |
| 8 | PIE / 사용자 스모크 | 없음(사용자 확인 불요 — 계측·스크립트·문서만). EC ① 패스가 실전 검증을 겸한다 |

## 13. 레드팀 지적 원장 (C3, 2026-08-19 — P1 0 · P2 4 · P3 3)

| 심각도 | 지적 (요약 + 파일:줄) | 처리 | 근거 |
|---|---|---|---|
| P2 | late-join 클라 오프닝 시드가 전역 프리즈 재유발 → 4인 캡처가 "멈춘 월드"를 잼 (`FPSRPlayerController.cpp` ServerNotifyClientReady 체인, 러너 SkipCards 1회 한계) | **수용** | 스모크6 호스트 로그 실증(조인 +1s `[Run] FREEZE` 재발·RESUME 없음). 수정 = §5-C(2-b) SkipCards 반복 모드 + 러너 프리즈 게이트(FREEZE>RESUME 경고) + §5-A ⑦ 판정 창 규칙. 스모크7에서 게이트가 구 바이너리의 잔존 프리즈(FREEZE=2 RESUME=1)를 경고로 검출 |
| P2 | `game.log` 최신-선택이 비결정 — 4인스턴스 공유 로그에서 클라 로그를 집으면 두 게이트가 헛돎 (러너:107,122) | **수용** | 수정 = 클라 기동 직전 호스트 로그 경로 스냅샷·우선 사용(§5-C(3)). 스모크7에서 조인 게이트 3/3 정상 판독 |
| P2 | `FPSR.GMS.Demo` 반복 중 재실행 = 리스너 누수 + `Dispatches`/`ListenersCopied` 배수 오염 (SetTimer가 이전 람다를 실행 없이 파괴 — `TimerManager.cpp` FindTimer→InternalClearTimer) | **수용** | 수정 = 리스너 핸들 파일 스코프 static + 커맨드 진입 시 타이머·핸들 선정리(§5-C(1)(d)). 2회 연속 호출 캡처에서 max=1.0 실증(이중 등록 0) |
| P2 | 정본 커맨드 `-CaptureFrames 18000`이 러너 기본 `MaxWaitSeconds=300`과 모순 — 60fps 이하에서 캡처 중간 킬 → 꼬리 헤더 없는 불완전 CSV | **수용** | 수정 = 미지정 시 `max(300, CaptureFrames/30+120)` 자동 산출(§5-C(3)). 스모크7 로그에서 invuln 1220(=600+320+300)으로 적용 확인 |
| P3 | 클라 생존이 호스트 단독 크래시를 가림(이름 기준 검사) | **수용** | 수정 = 클라 기동 직전 호스트 PID 스냅샷 → 3단계 전멸 감시(§5-C(3)) |
| P3 | 컬럼 수 3중 불일치(10종/10줄/신규 8종 vs 실제 9줄) | **수용** | §4·§5-C(4)를 9종(카운트성 6종)으로, 분석기 주석 정정 |
| P3 | 조인 게이트 경고 문구가 초과(재접속) 방향을 미달처럼 서술 | **수용** | 경고 문구에 "초과 = 재접속 의심" 병기 |

- **레드팀에 무엇을 줬나**: `git diff main...HEAD`(4커밋 전체 = 머지 단위와 일치) · 이 명세 경로 · `Docs/InternalRedTeamReview.md` 경로 · 리포+엔진 소스 읽기 권한. 설계 변호·토론 이력은 싣지 않음(§6-6-1 성립 조건 ①).
- **레드팀이 통과로 본 것**: GMS 계측 삽입·게이트 명세 1:1, 스탯 컬럼명 엔진 로직 정합(`CsvProfiler.cpp:2271-2284`), 엔진 넷 스탯 실재·갱신 조건, Invuln 래칫·서버권위·컴파일 게이트, `-ClientCount 0` 무회귀, 분석기 구 CSV 호환.
- **수정 후 재검증**: 증분 빌드 Succeeded · 헤드리스 스모크 4/4 · 계측 실증(2회 호출) · 러너 실증 smoke7(조인 3/3 + 프리즈 경고 검출 + 자동 MaxWait). P1 0건 → 머지 가.
