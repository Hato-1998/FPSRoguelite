# 보스 HUD 바 clear 경로 누락 — 재개 프롬프트 (자족)

> 작성 2026-07-15 (S4 계측 아크 머지 세션). **이 문서 하나로 착수 가능.**
> 규모 = 작음(코드 1~2파일). 파일럿(U21)과 **완전 독립** — HOLD 아님.
> ⚠️ **이건 "고쳐라" 지시가 아니라 "판정하고 필요하면 고쳐라" 지시다.** §3의 재현 확인이 먼저다.

---

## ▶ 새 세션 붙여넣기용 (이 블록만 붙여도 됨)

```
보스 HUD 바 clear 경로 누락 작업. 작업 클론 = E:\Git_Project\FPSRoguelite.

먼저 읽어라:
- Docs/BossHUDClear_ResumePrompt.md (이 문서 — 전체 사실관계·검증된 것/미검증인 것)
- CLAUDE.md + Game.md(§0-1 라우팅) → 해당 도메인 SSOT만

[요지] AFPSRGameState::SetActiveBoss(nullptr) 호출처가 전 프로젝트 0건. set만 있고
clear가 없다(FPSRRunDirectorSubsystem.cpp:513). 대조군 SetActiveMission은 set(:401) +
clear(:449) 둘 다 있음. 헤더 주석이 3곳에서 "set/clear" 계약을 약속(FPSRGameState.h:138,
141-142, 247)하는데 clear 쪽이 구현 안 됨.

[⚠️ 먼저 할 일] 이게 실제로 재현되는 버그인지부터 판정하라. 보스 처치는 항상 런을 끝내고
(FPSRBossBase.cpp:148 → FPSRGameMode.cpp:236 NotifyBossDefeated → EndRun(Victory)),
트래블로 GameState가 새로 생기면 stale ActiveBoss를 아무도 관측 못 할 수 있다.
재현 경로를 코드로 확정하기 전에 코드를 고치지 말 것. 상세 = 이 문서 §3.

[분담] 코드/해석 = Claude, PIE 확인 = 사용자. 검증 = 빌드 Result: + 스모크 + Codex(머지 시점).
[프로토콜] main → phase/<브랜치> 분기 → 플랜 승인 → 구현(Sonnet 위임)/검증(Opus 직접)
→ 빌드+스모크 → --no-ff 머지. 항상 4인 협동+서버권위 기준으로 설계할 것.
```

---

## 1. 확정된 사실 (전부 코드로 직접 확인함, 2026-07-15)

| # | 사실 | 근거 |
|---|---|---|
| 1 | **`SetActiveBoss(nullptr)` 호출처 = 0건** | `grep -rn "SetActiveBoss" Source/` → 정의(`FPSRGameState.cpp:38`) · 선언(`FPSRGameState.h:139`) · **set 1곳**(`FPSRRunDirectorSubsystem.cpp:513`) 뿐 |
| 2 | **대조군은 clear가 있음** | `SetActiveMission`: set `FPSRRunDirectorSubsystem.cpp:401` + **clear `:449`**(`DestroyActiveMission` 안, `SetMissionProgress(0.0f)`와 함께) |
| 3 | **계약이 3곳에서 약속됨** | `FPSRGameState.h:138` "Server: set/**clear** the active boss (called by the run director on boss spawn / **clear**)" · `:141-142` "Fires on all clients (+ host) when the active boss is **set/cleared** — the HUD boss bar binds here to **show/hide**" · `:247` "**Set/cleared** by the run director (server)" |
| 4 | **`StartRun`이 same-world 재런을 명시적으로 대비하는데 `ActiveBoss`만 빠짐** | `FPSRRunDirectorSubsystem.cpp:67-100`이 `bRunActive`/`RunClock`/`PostBossElapsed`/`bBossStarted`/`NextRunLogTime` 리셋 + `SetRunSchedule` + `SetMissionProgress(0)` + `ResetSpawnZones()`. **`ResetSpawnZones` 옆 주석이 직접 말함**: *"Re-run safety: ... a same-world re-run would otherwise keep zones opened in the previous run."* → **저자가 same-world 재런을 고려했음이 코드에 남아 있음.** 그런데 `ActiveBoss`(디렉터 멤버·GameState 양쪽)는 리셋 안 함 |
| 5 | 보스 처치 = 런 종료(Victory) | `FPSRBossBase.cpp:148` → `AFPSRGameMode::NotifyBossDefeated`(`FPSRGameMode.cpp:236`) → `EndRun(EFPSRRunOutcome::Victory)` |
| 6 | `SetActiveBoss`는 **같은 값이면 early-return** | `FPSRGameState.cpp:40` `if (!HasAuthority() \|\| ActiveBoss == InBoss) return;` |
| 7 | 호스트는 OnRep을 못 받으므로 세터가 **직접 broadcast** | `FPSRGameState.cpp:47` (주석: "The listen-server host gets no OnRep — broadcast directly so the host's HUD boss bar reacts too") |
| 8 | `ActiveBoss`는 Push Model 복제 | `FPSRGameState.cpp:31` `DOREPLIFETIME_WITH_PARAMS_FAST` + `MARK_PROPERTY_DIRTY_FROM_NAME`(`:45`) · `UPROPERTY(ReplicatedUsing = OnRep_ActiveBoss)`(`FPSRGameState.h:248`) |

## 2. 왜 이게 진짜 문제일 수 있나 (핵심 논리)

`ActiveBoss`는 `TObjectPtr<AFPSRBossBase>` UPROPERTY라, 보스 액터가 파괴되면 **포인터 자체는 GC가 조용히 null로 만듭니다.** 하지만:

- **`OnActiveBossChanged`는 발화하지 않습니다.** GC의 null화는 세터를 안 거치니까요.
- HUD 보스 바는 `OnActiveBossChanged`에 바인딩해서 **show/hide** 합니다(사실 #3).
- **→ 바를 숨기라고 아무도 말해주지 않습니다.**

즉 "포인터가 알아서 null 되니 괜찮다"가 **아닙니다.** 문제는 값이 아니라 **알림**입니다.

## 3. ⚠️ 착수 전 반드시 먼저 판정할 것 — "실제로 재현되는가"

**이 버그는 잠재적(latent)일 수 있습니다. 과대평가하지 마세요.**

보스를 잡으면 항상 런이 끝나고(사실 #5), 런이 끝나면 로비로 트래블합니다(U11a Seamless 트래블: 로비→게임→보스→로비). **트래블로 `AFPSRGameState`가 새로 생성되면 stale `ActiveBoss`는 아무도 관측하지 못하고 사라집니다.** 그러면 이건 "고쳐야 할 계약 위반"이긴 해도 **유저가 보는 버그는 아닙니다.**

**따라서 코드를 고치기 전에 이것부터 코드로 확정하세요:**

1. **`AFPSRGameState`가 런 사이에 살아남는 경로가 있는가?**
   - Seamless travel의 `GetSeamlessTravelActorList` / `bAlwaysRelevant` GameState 처리 확인
   - UE 기본은 트래블 시 GameState 재생성 → 그러면 이 경로로는 재현 안 됨
2. **`StartRun`을 같은 월드에서 두 번 부르는 경로가 실재하는가?** (사실 #4의 "Re-run safety" 주석이 **누군가 그렇게 한다**는 증거)
   - `StartRun` 호출처 전수 조사(`grep -rn "StartRun" Source/`)
   - 디버그 커맨드(`FPSR.EndRun` 등)로 재런이 되는가? 그건 프로덕션 경로인가 디버그 전용인가?
3. **보스가 죽지 않고 런이 끝나는 경로가 있는가?** (전멸 패배 = `EndRun(Defeat)`인데 보스는 살아있음)
   - 이 경우 GameState가 살아남으면 stale 보스 바가 로비/다음 런까지 따라옴
4. **보스 2페이즈(장기 백로그)**: 보스가 죽어도 런이 안 끝나는 미래 설계가 오면 이 clear가 **필수**가 됨 — 지금 안 고치면 그때 터짐

**판정 결과에 따라:**
- **재현됨** → 고친다(§4)
- **현재 재현 불가 + 미래에만 필요** → 그래도 고칠 가치는 있음(계약이 이미 약속됐고 6줄). 단 **커밋 메시지에 "현재 관측 불가, 계약 정합 + 미래 대비"라고 정직하게 쓸 것.** "버그 수정"이라고 과장하지 말 것

## 4. 고친다면 — 어디에 넣을지 (플랜에서 결정할 것)

후보 지점. **하나를 고르는 게 아니라 어느 조합이 옳은지 판정하세요:**

| 후보 | 근거 | 주의 |
|---|---|---|
| **A. `StartRun`에 리셋 추가** | 사실 #4가 가장 강한 근거 — 형제 리셋(`ResetSpawnZones`·`SetMissionProgress(0)`)이 **이미 다 거기 있음**. 대칭적이고 발견하기 쉬움 | 디렉터 자신의 `ActiveBoss` 멤버(`:496`)도 같이 리셋해야 대칭 |
| **B. 런 종료(`EndRun`) 경로에서 clear** | 대조군 `SetActiveMission(nullptr)`이 정리 함수(`DestroyActiveMission`)에서 불림 = 같은 패턴 | `EndRun`은 GameMode에 있고 clear 대상은 GameState — 디렉터를 거칠지 결정 필요 |
| **C. 보스 사망 시점에 clear** | 가장 "즉각적" | ⚠️ **위험**: 승리 연출/결과 화면이 보스 바나 보스 액터를 참조한다면 너무 일찍 지움. `NotifyBossDefeated` → `EndRun(Victory)` 순서 확인 필수 |

**설계 원칙(CLAUDE.md §4 = 제1원리 우선)**: 비자명한 구조 결정이면 플랜에 **3줄** 명시 — ① 제1원리 근거 ② (참고) Lyra/UE표준과 같은가·다른가 ③ 이 프로젝트 제약과의 정합.

**멀티 필수 검토(메모리 `reason-in-multiplayer-terms`)**: 4인 리슨서버 기준으로 —
- 호스트는 OnRep을 못 받으니 세터의 직접 broadcast에 의존(사실 #7) → clear도 같은 경로를 타는가?
- 늦게 접속한 클라(late joiner)가 stale `ActiveBoss`를 초기 복제로 받으면?
- clear 시 `MARK_PROPERTY_DIRTY_FROM_NAME` 빠뜨리면 Push Model에서 **복제가 아예 안 됨**(사실 #8)

## 5. 검증

- 빌드: `"D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\Build.bat" FPSRogueliteEditor Win64 Development -Project="E:\Git_Project\FPSRoguelite\FPSRoguelite.uproject" -WaitMutex -MaxParallelActions=2` → 로그 `Result:`로 판정(래퍼 exit코드 아님, 메모리 `build-octobuild-xge-c1076-flaky`)
- 스모크: `UnrealEditor-Cmd.exe <uproject> -unattended -nopause -nullrhi -nosplash -nosound -ExecCmds="Automation RunTests FPSRoguelite.Smoke.ModuleLoads" -TestExit="Automation Test Queue Empty" -abslog=...` → `Result={Success}`
- ⚠️ **에디터 열려 있으면**: 빌드는 되지만 `-0001.dll` 핫리로드 변형이 나옴 → PIE 실동작 확인 전엔 **에디터 끄고 재빌드**. 머지는 `.uasset`을 안 건드리면 안 막힘(메모리 `ue-editor-file-locks-block-git`)
- PIE 확인 = 사용자(§3에서 재현 경로가 확정된 경우에만 의미 있음)
- Codex 머지게이트: `powershell -File Scripts\codex-review.ps1 -Base main`

## 6. 관련 메모리
`freeze-gate-client-server-symmetry` · `reason-in-multiplayer-terms` · `architecture-decision-first-principles` · `production-structure-first` · `shared-worktree-branch-collision`(커밋 전 `git branch --show-current` 필수) · `ue-editor-file-locks-block-git` · `codex-review-gate`
