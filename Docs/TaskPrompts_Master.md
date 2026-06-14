# TaskPrompts_Master — 남은 작업 의존성 분해 + 유닛별 실행 프롬프트

> **목적**: 남은 로드맵 전체를 의존성 기준 유닛으로 분해하고, 각 유닛을 **새 세션에 복붙으로 시킬 수 있는 실행 프롬프트**로 제공한다.
> **작성**: 2026-06-13 (전수 분석: SSOT 전 문서 + 코드 grep + Plan 에이전트 적대적 검증 + 사용자 순서 확정).
> **갱신 규칙**: 유닛 완료 시 §B 표의 해당 행에 ✅ 표시 + `PROGRESS.md` 완료 이력 갱신. 설계가 바뀌면 해당 `Docs/SSOT/*.md` 먼저 고치고 이 문서의 프롬프트를 동기화한다.
> **프롬프트 사용법**: §C의 코드블록을 새 세션에 그대로 붙여넣는다. 모든 프롬프트는 플랜모드 우선·HIGH_RISK 승인 원칙(CLAUDE.md)을 전제한다.

---

## §A 현황 스냅샷 (2026-06-13)

### 완료 (main 머지, 실측 확인)
- **P0~P4 전부**: 스캐폴드 / 1P 캐릭터(Separated Arms) / 사격감(반동·확산·ADS·탄약) / 적 스웜(풀 500·플로우필드·Significance·스폰포인트) / 공유XP·카드 시스템(CommonUI 카드 UI·리롤·오프닝 시드) / 런 디렉터(시간 미션 스케줄·전역 프리즈) / 미션 6종+PointSet / 무기 7종 DA(Rifle·BurstRifle·Shotgun·Sniper·Bazooka·Grenade·ChargeLaser)+Knife / Fragment 모디파이어 / 게임필(히트마커·피격방향·원거리경고 소비자)
- **P5 FF(친선사격)**: 통합 데미지 헬퍼 `FPSRCombatStatics`, **아군 데미지 = FF ON일 때 50%**(`FriendlyFireDamageScale=0.5`), 자폭=폭발만 풀, 넉백 데미지 독립, FF 카드 4종 콘텐츠 완료
- **ChargeLaser 재설계**: 클릭 1회=자동 차징 시퀀스(ServerOnly GA), DA 적용 완료
- **P6-A 게임플로우 셸**: 메뉴→런→결과→메뉴 루프 + EndRun 프리즈. 현재 트리거는 디버그 `FPSR.EndRun [victory|defeat]` / `FPSR.ReturnToMenu` 뿐
- **운영**: origin 동기화 완료, phase 브랜치 정리 완료, `.uproject` VibeUE `Optional:true` 커밋 완료(`15d4e34`)

### 미배선 핵심 (게임 루프의 진짜 닫기)
| 항목 | 근거 | 상태 |
|---|---|---|
| 승리 = 보스 처치 | `Source/FPSRoguelite/Private/Run/FPSRRunDirectorSubsystem.cpp:365` — "boss actor is a P6 stub", 보스 게이트 도달 시 타임라인 정지만 | 보스 액터 자체가 없음 |
| 패배 = 전원 사망 | `Source/FPSRoguelite/Private/Hero/FPSRCharacter.cpp:472-476` `HandleOutOfHealth` = 로그만("DBNO/respawn handling is P5") | 사망 처리 플레이스홀더 |
| `EndRun` 호출자 | `Source/FPSRoguelite/Private/Core/FPSRGameMode.cpp:51` (`bRunEnded` 래치) | 디버그 커맨드만 |

### 미실시 검증 게이트
- **§7-5 코어 재미 게이트**(Roadmap) + **§5 성능 검증**(Insights/NetProfiler, 적 500, 호스트 기준 하드캡 확정, Push→RepGraph→Iris 평가 순서)
- **2-client PIE**: FF(아군 50%·아군 런칭), ChargeLaser 서버권위 체크리스트

### 사용자 추가 확정 (2026-06-13)
- **Infima Games — Low Poly Animated Modern Guns Pack**을 기본 비주얼 베이스로 적용. `Content/Assets/LowPolyAnimatedModernGuns`(이미 LFS 커밋, 1,935 uasset). 무기 8종(AG14W/HVG7/LRAF9/MAK12/MR22/RC425/SP60/X13)+`_Melee`+1P팔 메시·애니메이션+머즐 이펙트(`PS_*`=레거시 Cascade)+무기/폭발/캐릭터 사운드 포함. 코드에 무기 메시 배선 부재(무기=순수 DataAsset, `FirstPersonArms`는 메시 미할당) → **V0 유닛 신설**
- **순서 확정**: 게임 플로우 완성(U2→U3→U4)이 먼저, 그 후 W1 전체 검증, 그 후 U1∥V2 게이트(멀티 세팅에서 동시 진행)

### 임시 테스트값 (미원복 — U14에서 원복)
- `DA_RunSchedule.MissionWindows` 윈도우 시간(테스트 압축값) + `BossTime 300s` → 프로덕션 = 미션 300/600/900s·보스 1200s(≈20분 런). 메모리 `p4a-temp-test-values`. **보스 E2E(U11)까지 압축값 유지가 의도**

---

## §B 의존성 DAG (22유닛)

### 실행 순서 (사용자 확정 2026-06-13)

```
[1차 — 게임 플로우 완성 트랙]
V0 무기 비주얼 베이스 ∥ V1 사각 경고 오디오 ∥ V3 기본 크로스헤어 HUD ∥ U16 스핀업 LMG(가속 연사 무기)   (병행 가능, 서로 독립)
        ↓
U2 패배 배선(전멸→EndRun Defeat)  ∥  U3a 약점 부위 데미지 시스템   (서로 독립, 병행 가능)
        ↓
U3 보스 스캐폴드+승리 배선(D4)   (U3a 선행 — 보스가 약점 인프라 소비)
        ↓
U4 보스 콘텐츠+PIE  ←← 여기서 "메뉴→런→미션→보스→승/패→결과→메뉴" 전체 플로우 완성
        ↓
W1 전체 프로젝트 정합 검증 (Fable 전수 + Codex 분할) → 교정(fix/ 브랜치)
        ↓
U1 재미 게이트(§7-5)+성능 검증(§5) ∥ V2 2-client 정합 검증   (같은 멀티 세팅에서 동시, 체크리스트 분리)
        ↓ (게이트 통과 시 해금)
[2차 — 병행 자유 트랙]  U5 원거리 적 / U6 Fragment 마무리 / U8 GMS / U10 SaveGame  (순서 자유)
                        U7 플로우필드 높이 (U1 PIE 관찰 결과로 우선순위 확정)
                        U15 1P 무기 애니메이션 시스템 (U1 손맛 ① 불합격 시 최우선 / 합격 시 폴리시)
        ↓
U9 DBNO (U2 판정식 교체)
        ↓
U11 세션+P7 멀티플레이 루프 (2-PC Steam E2E)
        ↓
U12 UI/필 후속 ∥ U13 VFX/오디오 배선 (U8 소프트 선행)
        ↓
U14 임시값 원복 + §8 플레이스홀더 전환 + 폴리시/패키지 빌드
```

### 유닛 표

| # | 유닛 | 구분 | 선행 | 차단(블록) | 브랜치 |
|---|---|---|---|---|---|
| V0 ✅ | 무기 비주얼 베이스 적용(Infima 팩) — **완료 2026-06-14** (통합 스태틱 무기+머즐+사운드, PIE 통과) | C++/콘텐츠 | — | U1(손맛 판정) | `phase/p6-weapon-visuals` |
| V1 | 최소 사각 경고 오디오(§2-14 당김 확정분) | C++/콘텐츠 | — | U1(판정 ④) | `phase/p5-blindspot-audio` |
| V3 | 기본 크로스헤어 HUD(정적 레티클) | 콘텐츠(+소량 C++) | — (1차 트랙 병행) | U1(손맛 ① 평가에 필수), U12(동적 스프레드 베이스) | `phase/p4d-crosshair` |
| ✅U16 | 스핀업 LMG(연사속도 가속 기관총) — 새 무기 1종 **완료(main 머지 `c0b28a9`, 2026-06-15)** | C++/콘텐츠 | — (1차 트랙 병행, V0/V1 옆) | U1(손맛 평가 포함), W1(검증 포함) | ~~`phase/p4c-spinup-lmg`~~ 정리됨 |
| U2 | 패배 배선: 간이 전멸 판정→EndRun(Defeat) | C++ | — | U3, U9, U11 | `phase/p6-defeat-wiring` |
| U3a | 약점 부위 데미지 시스템(헤드샷/디자이너 지정 존) | C++ | — (U2와 병행 가능) | U3/U4(보스 약점), U1(손맛 판정에 포함) | `phase/p6-weakpoint-damage` |
| U3 | D4 보스 스캐폴드+승리 배선 | C++ | U2, U3a | U4, U11 | `phase/p6-boss-scaffold` |
| U4 | 보스 콘텐츠+PIE(콜리전 정합·체력바) | 콘텐츠 | U3 | W1, U11, U14 | U3 동승 또는 `content/boss` |
| W1 | 전체 프로젝트 정합 검증(Fable+Codex) | 검증 | U4 | U1, V2 | 교정은 `fix/*` |
| U1 | 재미 게이트(§7-5)+성능 검증(§5) | 검증(사용자 PIE) | V0, V1, W1 | 2차 트랙 전체 | (검증·문서) |
| V2 | 2-client PIE(FF·ChargeLaser) — U1과 동시 | 검증(사용자 PIE) | W1 | U11(소프트) | (검증) |
| U5 | B1 원거리 적 AI+경고 생산자 | C++ | U1 게이트 | — | `phase/p4-ranged-enemy` |
| U6 | A4 Fragment 마무리+AvailableModifiers 확장 | C++(+콘텐츠 꼬리) | U1 게이트 | — | `phase/p4b-fragment-finish` |
| U7 | C1 플로우필드 높이/클리어런스 | C++ | U1 관찰 결과 | — | `phase/p2-flowfield-height` |
| U8 | C2 GMS 재구현 | C++ | U1 게이트 | U13(소프트) | `phase/infra-gms` |
| U9 | D2 DBNO 수동부활 | C++ | U2 | — | `phase/p5-dbno` |
| U10 | D3 메타 SaveGame | C++ | U1 게이트 | 메타 콘텐츠 | `phase/p6-savegame` |
| U11 | D5 세션+P7 멀티 루프(2-PC Steam E2E) | C++/콘텐츠/검증 | U2·U3·U4 (소프트 V2) + 메뉴↔로비 사용자 결정 | U14 | `phase/p7-mp-loop` |
| U12 | UI/필 후속(크로스헤어·히트마커·카드 아이콘·무기별 팔 AnimBP) | C++/콘텐츠 | U1 게이트 | — | `phase/p4d-ui-followups` |
| U15 | 1P 무기 애니메이션 시스템(모듈러 부품+노리쇠+팔↔무기 이중 스켈 동기) | C++/콘텐츠 | U1 (조건부 우선순위) | — | `phase/p6-weapon-anim` |
| U13 | VFX/오디오 배선(폭발·빔·핑/Gibs·풀 사각오디오) | C++/콘텐츠 | U8 소프트 | — | `phase/p7-vfx-audio` |
| U14 | 임시값 원복+§8 전환+폴리시/패키지 빌드 | 혼합 | U4·U11 | (출시) | `phase/p7-polish` |

### 장기 백로그 (유닛화 보류 — 기록만)
- **복수 authored 고정 맵**(RunFlow §2-1): 맵 수·해금 방식은 콘텐츠 단계(P6+) 확정. U14 이후.
- **보스 2페이즈 본격화**: U3/U4는 "체력만 박스" 스캐폴드(P7-MultiplayerLoop_Plan 확정). 실제 보스(StateTree 패턴·페이즈 전환)는 별도 유닛으로 재기획.
- **에셋 확장**: V0는 무기/팔 한정. 적 메시(§8 큐브 교체)·환경 에셋은 컨셉 확정 후 별도(팩에 적 메시 없음).

---

## §B-2 병렬 작업 가이드 (다중 세션 충돌 방지 — 2026-06-15 수립)

> **여러 세션에서 동시 작업 시 반드시 먼저 읽을 것.** 의존성상 독립이라도 ① 코드 핫스팟 ② 콘텐츠 바이너리 ③ 빌드/PIE 병목 때문에 "전부 동시 병렬"은 머지 충돌·회귀·에셋 유실을 부른다. 아래 트랙 배분과 규칙을 지킨다.

### 절대 규칙 (4개)
1. **worktree 분리**: 각 phase 브랜치는 `git worktree add ../FPSR-<키워드> phase/<브랜치>`로 별도 디렉터리에서 작업. 한 워크트리에서 브랜치만 전환하면 새 UCLASS 다수로 매번 풀 리빌드(Live Coding 불가).
2. **콘텐츠(.uasset/.umap) 동시 수정 금지**: LFS 바이너리는 3-way 머지 불가 → 같은 에셋을 두 세션이 건드리면 한쪽이 통째로 유실. 에셋 단위로 배타 배분(아래 표).
3. **빌드·PIE 검증은 직렬**: 단일 엔진/단일 사용자. 병렬은 "구현"까지만, 빌드(`-WaitMutex`)·헤드리스 스모크·PIE·Codex 머지게이트는 한 번에 하나.
4. **작은 것 먼저 머지 + 머지마다 나머지 브랜치 rebase**: 충돌을 조기 흡수. 머지 순서는 아래 "권장 머지 순서".

### 코드 핫스팟 (같은 파일 = 직렬 강제)
| 충돌 파일 | 건드리는 유닛 | 처리 |
|---|---|---|
| `FPSRWeaponFireComponent.{h,cpp}` | U16(스핀업 케이던스)·V3(IsAiming getter)·U3a(발사 경로 가능) | **발사계 직렬** |
| `FPSRGA_WeaponFire_Hitscan` + `Combat/FPSRCombatStatics.{h,cpp}` | U16(동적 MinInterval)·U3a(약점 판정) | **발사계 직렬** |
| `Weapon/FPSRWeaponTypes.h`(FFPSRWeaponStatBlock) | U16(스핀업 필드 신설) | U16 머지 후 타 유닛 rebase |
| `Core/FPSRGameMode.cpp`(EndRun) | U2(defeat)·U3(victory) | **U2 먼저 머지 → U3** |
| `Run/FPSRRunDirectorSubsystem.cpp`(EnterBoss) | U3(보스 연결) | 단독 |

### 콘텐츠 배타 배분 (동시 수정 금지)
| 에셋 | 소유 유닛 | 비고 |
|---|---|---|
| `WBP_GameHUD` / 크로스헤어 WBP | V3 | U12는 V3 머지 후 확장(동시 X) |
| 보스 BP/DA · L_Sandbox 보스 배치 | U4 | — |
| `BP_FPSRPlayer` | (1세션만) | V0 이후 누구든 건드리면 단독 점유 — 충돌 1순위 |
| 사각 오디오 사운드 에셋 | V1 | PlayerFeedbackComponent 계열 |
| 새 LMG DA(`DA_Weapon_*`) | U16 | 신규 파일이라 충돌 적음 |

### 권장 트랙 배분 (게이트 전)
```
트랙1 (코드·발사계, 직렬):   U16(진행중) → U3a → V3 getter   ← FireComponent/GA/CombatStatics 공유
트랙2 (코드·비발사계, 병렬):  V1 사각 오디오  ∥  U2 패배 배선   ← 발사계 안 건드려 안전. 단 U2는 U3보다 먼저 머지
트랙3 (보스, 순차):           U2 머지 → U3a 머지 → U3 → U4
```
- **안전한 동시 세션**: U16(발사계) + V1 또는 U2(비발사계) = 파일 안 겹침 → OK.
- **직렬 강제**: U16·U3a·V3는 발사 시스템 공유 → 같은 시점 다른 세션 금지.
- **순차 의존**: U2→U3→U4(EndRun/보스 체인).

### 권장 머지 순서 (작은 것·기반 먼저)
`U16(스탯 신설 먼저) → U2(EndRun defeat) → U3a(약점) → V1 → V3 → U3(보스) → U4(보스 콘텐츠) → W1`
- 각 머지 후 미머지 phase 브랜치를 `git rebase main`으로 갱신해 충돌 조기 해소.
- 머지마다 PROGRESS.md 갱신이 겹치므로(텍스트 충돌), **PROGRESS 편집은 머지 직전에 최소 라인만** + 머지 직후 즉시 push.

---

## §B-3 검토·머지 프로토콜 (머지 전 검토 모델 — 2026-06-15 확정)

> **핵심 원칙: "머지 후 확인"이 아니라 "머지 전 확인".** main에 들어온 뒤 문제를 발견하면 그 위에 쌓인 작업 때문에 되돌리기(revert) 비용이 크고 main이 미검증 상태로 노출된다. 머지를 검토 게이트 **뒤**로 두면 같은 목적(사용자 확인 후 수정)을 main 오염 없이 달성한다. (Workflow §6-7 "검증 후 `--no-ff` 머지 + Codex 머지게이트"와 정합)

### "완료"의 정의
세션이 **main에 머지하는 것이 완료가 아니다.** 완료 = **자체 빌드(`-WaitMutex`) + 헤드리스 스모크 통과 + phase 브랜치 push + 완료 보고**. 미검증 상태로 머지하지 않는다. 빌드를 세션이 책임지므로 사용자가 빌드 여러 개를 직접 돌릴 필요가 없다(각 worktree가 자체 산출물로 검증).

### 흐름 (PR 기반)
```
1. 세션: 플랜 확정(사용자 승인) → 구현 → 자체 빌드+헤드리스 스모크 통과
2. 세션: phase 브랜치 push + GitHub PR 생성(gh) + 완료 보고
   (무엇을/어디를/빌드·스모크 결과/PIE 필요 여부 — main 머지는 하지 않음)
3. 사용자 검토 (머지 전):
   - 코드 = PR diff(라인 코멘트) 또는 Codex 머지게이트(codex-review.ps1 -Base main)로 P1/P2 거름
   - 콘텐츠(.uasset/.umap) = PR diff엔 안 보임(LFS 바이너리) → 그 브랜치 worktree에서 PIE 필수
4. 수정사항 → 같은 phase 브랜치에서 세션에 수정 지시 → 재push(PR 자동 갱신) → 3 반복
5. 승인 → --no-ff main 머지 → 머지 후 미머지 phase 브랜치 git rebase main
```

### 규칙
- **머지는 사용자 승인 직후에만**, 한 번에 하나(§B-2 직렬·머지 순서 준수).
- **빌드 큐**: 빌드 필요 유닛(C++)은 worktree별 자체 빌드(산출물 분리, `-WaitMutex` 직렬화 안전). 콘텐츠/문서 유닛은 빌드 불필요(PIE만). PIE는 단일 사용자라 머지-전-검토 큐로 직렬.
- **PR diff 한계**: 콘텐츠 위주 유닛(V0/V3/U4 등)은 PR diff로 안 보이므로 PIE 검토가 게이트. 코드 유닛은 PR diff가 1차 게이트 + Codex.
- **문서/PROGRESS 갱신**: 머지 직전 최소 라인만(텍스트 충돌 최소화) + 머지 직후 즉시 push.

---

## §C 유닛별 실행 프롬프트

> 모든 프롬프트 공통 전제(프롬프트에도 내장됨): `Game.md`+`PROGRESS.md` 선행 독해 → 해당 도메인 SSOT만 추가 독해(§0-1 라우팅) / `phase/` 브랜치 분기(§6-7) / 플랜모드 우선·승인 후 진행 / 구현=Haiku 위임·검증=Opus 직접(§6-5) / **완료 = 자체 빌드(`-WaitMutex`)+헤드리스 스모크 통과 + phase push + PR 생성·완료 보고(§B-3). main 머지는 하지 않고 사용자 검토·승인 후 진행** / Codex 머지게이트(`Scripts/codex-review.ps1 -Base main`) / 승인 후 PROGRESS 갱신+`--no-ff` 머지+브랜치 삭제+사용자 콘텐츠 동반 커밋 여부 질문.
> **⚠️ 다중 세션 동시 작업 중이면 §B-2 병렬 작업 가이드 + §B-3 검토·머지 프로토콜을 먼저 읽을 것**(worktree 분리·핫스팟·머지 전 검토).
> 💡 **간편 호출**: 작업 시작 시 프로젝트 스킬 `parallel-unit-work`(`.claude/skills/`)를 호출하면 §B-2·§B-3 셋업(worktree 분리·핫스팟·완료 정의·머지-전-검토)이 체크리스트로 적용된다. 각 유닛 프롬프트 첫 줄의 ⚠️ 안전장치가 이 스킬을 상정한다.

### V0 — 무기 비주얼 베이스 적용 (Infima Low Poly Animated Modern Guns)

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽고 별도 worktree에서 진행(git worktree add ../FPSR-<키워드> -b <브랜치> main). main 머지는 빌드+스모크 통과·PR·사용자 승인 후.
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 V0를 진행한다.
읽을 SSOT: Docs/SSOT/CombatWeaponCard.md §2-4(무기/DataAsset 구조), Docs/SSOT/PlayerFeel.md §2-9(Separated Arms), Docs/SSOT/Workflow.md §6.

[목표] Content/Assets/LowPolyAnimatedModernGuns (Infima 팩, 이미 LFS 커밋됨)를 프로젝트 기본 비주얼 베이스로 적용.
현재 코드엔 무기 메시 배선이 전혀 없다(무기=순수 DataAsset, AFPSRCharacter::FirstPersonArms는 SkeletalMeshComponent만 생성·메시 미할당, FPSRCharacter.cpp:60).

- 브랜치: phase/p6-weapon-visuals 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증(빌드+스모크+diff 자기비판)

[C++ 산출물]
1. UFPSRWeaponDataAsset에 비주얼/사운드 필드 신설(전부 TSoftObjectPtr/TSubclassOf, 기본 null=현재 동작 무회귀):
   1P 무기 SkeletalMesh, 팔 장착 AnimBP 또는 장착/발사 AnimMontage, 발사 사운드, 머즐 플래시 이펙트, 머즐 소켓명(FName — 팩 메시마다 소켓명이 다를 수 있어 DA 데이터화).
2. 장착 배선: 무기 교체(OnWeaponEquipped/OnRep) 시 FirstPersonArms에 무기 메시 컴포넌트 어태치/교체. 1P 전용(OnlyOwnerSee 등 1인칭 표준 설정 검토).
3. 발사 코스메틱 배선: 발사 시 로컬(오너 클라)에서 몽타주/사운드/머즐 플래시 재생. 기존 발사 경로(FireComponent/GA)에 코스메틱 훅만 추가.

[콘텐츠 산출물 — 사용자/VibeUE]
- 팔 메시·AnimBP 할당(BP_FPSRPlayer), 무기 DA 8종(Rifle/BurstRifle/Shotgun/Sniper/Bazooka/Grenade/ChargeLaser/Knife)에 팩 무기 8종(AG14W/HVG7/LRAF9/MAK12/MR22/RC425/SP60/X13 + _Melee) 매핑 — 어떤 팩 무기를 어떤 DA에 쓸지는 사용자에게 질문.

[함정/주의 — 반드시 지켜라]
- 트레이스/탄도/데미지는 기존 카메라 뷰포인트 기준 유지. 머즐 소켓은 코스메틱(플래시/빔 시각 원점) 전용. 게임플레이 수치(DA BaseStats)는 1도 바꾸지 마라.
- 에셋 경로 C++ 하드코딩 금지(ConstructorHelpers 금지 — XP 스피어 전철, Game.md §6-2). 전부 DA/BP 참조.
- 팩 파티클은 PS_*(레거시 Cascade) — UE5.7에서 동작 확인 후, 문제 시 머즐 플래시만 Niagara 전환(전환 범위는 플랜에서 보고).
- Demo/Mannequin 폴더는 사용 금지(데모용).
- 3P 무기 메시(타 플레이어 시점)는 이번 범위 밖 — 같은 DA 필드를 재사용하는 후속(U11 전 권장)으로 PROGRESS에 기록만.
- 멀티 코스메틱(다른 클라에서 보이는 발사 이펙트)은 U13(GMS 이후) 범위 — 이번엔 로컬 1P만.

[검증] 빌드+스모크 → 사용자 PIE: 무기 8종 교체 시 메시/팔 표시, 발사 시 사운드+플래시, 기존 수치 무회귀(반동/탄약/데미지). 머지 전 codex-review.ps1 -Base main.
완료 시: PROGRESS 갱신 + TaskPrompts_Master §B에 ✅ + 사용자 콘텐츠 동반 커밋 질문 + --no-ff 머지.
```

> **V0 방향성 결정(2026-06-14 확정)**: 무기 = **통합 Preview 스태틱 메시**(SM_LPAMG_<W>_Preview)를 WeaponMeshStatic1P에 사용. **근거(제1원리+SSOT)**: 이 게임의 손맛 코어는 §2-4-2(절차적 카메라 킥+확산+머즐+사운드)·§2-9(타격감=히트마커/Gibs/핑)로 정의되며 **모듈러 부품·노리쇠 풀 무기 애님은 코어가 아니다**. 따라서 스태틱+팔 애님으로 U1 손맛 게이트 판정이 가능하고, 무거운 무기 애님 투자 여부는 **게이트 결과로 결정**(투기성 선투자 회피). 스켈(WeaponMesh1P) 경로를 코드에 남겨 확장 길을 열어둠.
> **V0 후속(별도 유닛)**: ① 모듈러+노리쇠 풀 무기 애님 = **U15**(U1 게이트 후 조건부). ② 무기별 팔 AnimBP(BlendByEnum 그립) = **U12** 폴리시 편입. ③ 무기별 부착 오프셋(DA 필드) = U15 또는 경량 후속. ④ 범위 밖(기존 유지): 3P 무기 메시(U11 전), 멀티 코스메틱(U13), ChargeLaser 빔 VFX(U13).

### U16 — 스핀업 LMG (연사속도 가속 기관총)

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽고 별도 worktree에서 진행(git worktree add ../FPSR-<키워드> -b <브랜치> main). main 머지는 빌드+스모크 통과·PR·사용자 승인 후.
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U16을 진행한다.
읽을 SSOT: Docs/SSOT/CombatWeaponCard.md §2-4(무기/스탯)·§2-4-1(모디파이어/Fragment)·§2-4-2(사격감/반동), Docs/SSOT/Workflow.md §6.

[목표] 새 무기 1종 — **스핀업 LMG**: 발사 시작 시 연사속도가 느리다가 연속 발사할수록 빨라져 일정 시간(램프) 후 최대 연사속도에 도달하는 기관총. 1차 트랙 병행(V0/V1 옆) — 게임 플로우(U2~U4)와 독립이지만 발사 케이던스 코어를 건드리므로 W1 전체 검증과 U1 손맛 게이트의 평가 대상에 포함되어야 한다.

- 브랜치: phase/p4c-spinup-lmg 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증(발사 케이던스 + 서버권위 = 세밀 검증 필수, Opus 직접 교정 가능)

[설계 — 조사로 확정된 배선 지점]
- **새 Archetype 불필요**: 기존 EFPSRWeaponArchetype::FullAuto 사용. 스핀업은 아키타입 속성이 아니라 "연속 발사 경과 시간에 따른 동적 FireRate"다. ChargeLaser처럼 별도 아키타입 만들지 마라.
- **스탯 필드 신설(FFPSRWeaponStatBlock, Public/Weapon/FPSRWeaponTypes.h)**: bool bHasSpinup + SpinupFireRateStart(시작 연사) + SpinupRampTime(최대 도달까지 시간). EditConditionHides=bHasSpinup(ChargeLaser 필드의 Archetype 게이트 패턴과 동일). BaseStats.FireRate = **최대 연사**로 정의(램프 종점).
- **동적 FireRate 계산**: CurrentRate = Lerp(SpinupFireRateStart, FireRate, Clamp(SpinupElapsed/SpinupRampTime, 0..1)). 한 함수로 빼서 클라/서버가 동일 식 사용(결정성).
- **FireComponent(UFPSRWeaponFireComponent) 클라 케이던스 3곳**(조사 기준 라인, 재확인 후):
  ① TickComponent 자동발사 루프의 Interval 계산(현재 1.0/FireRate)을 동적 CurrentRate로 + 발사 홀드 중 SpinupElapsed += DeltaTime.
  ② FireOneShot의 NextFireReadyTime 스탬프를 동적 CurrentRate로.
  ③ StopFiring / OnWeaponEquipped에서 SpinupElapsed 리셋.
- **서버 권위 일관성(★핵심)**: 클라 케이던스(NextFireReadyTime)와 서버 게이트(UFPSRWeaponInventoryComponent::ServerTryConsumeFireInterval의 MinInterval)가 **같은 스핀업 곡선**으로 계산되어야 한다. 히트스캔 GA(FullAuto, 클라 예측 있음)가 발사할 때 동적 MinInterval을 전달하려면 **서버도 스핀업 진행도를 추적**해야 함 — 서버 스핀 상태(마지막 발사 이후 gap으로 증가/리셋 추론, 멤버 1~2개)를 Inventory 또는 WeaponInstance에 두고 결정적 규칙으로 클라와 일치시킬 것.

[플랜에서 결정할 항목 — 지어내지 말고 사용자 질문]
- 멈추면(StopFiring) 스핀: 즉시 리셋 vs 짧은 유예 후 리셋 vs 점진 스핀다운(decay). LMG 손맛엔 점진 감소가 어울리나 복잡도↑ — 권장안 제시 후 결정.
- FireRate 카드(EFPSRWeaponStat::FireRate)가 무엇을 올리나: 최대 연사(BaseStats.FireRate)만 vs 시작 연사도 함께. 기존 GetResolvedStats 해석 경로 재사용 범위 결정.
- 무기 아키타입: 히트스캔 LMG 확정인지(투사체 LMG는 다른 GA). 기본=히트스캔(FullAuto, UFPSRGA_WeaponFire_Hitscan).

[함정/주의]
- **클라/서버 스핀 desync**가 최대 함정(ChargeLaser 재설계가 cross-channel race를 ServerOnly로 피한 교훈, 메모리 freeze-gate-client-server-symmetry). 결정적 동일 곡선 + 서버 게이트 25% 허용오차 안에 들어오는지 검증.
- 반동/확산은 추가 작업 불필요 — FireRate만 변하면 기존 "복구 빚" 반동·블룸이 자동으로 가속 추종(의도된 효과). 단 최고속에서 반동이 과하지 않은지 DA 튜닝.
- 전역 프리즈(§2-2) 중 SpinupElapsed 진행 정지(다른 무기 게이트와 대칭).
- 무기 교체로 스핀 뱅킹/우회 금지(OnWeaponEquipped 리셋 — equip 케이던스 쿨다운 EquipFireCooldown과 정합).
- AllWeapons 카드 제외축 검토: 필요 시 무기 DA의 AllWeaponsStatExclusions에 FireRate 추가(ChargeLaser=RecoilVertical 선례). 사용자 결정.
- 비주얼: 팩 무기 매핑은 V0 체계 재사용(스핀업 LMG에 맞는 팩 무기 1종 — 예 HVG7류 — 사용자 지정). V0 미완이면 메시 없이 기능만 검증 후 비주얼은 V0 머지 후.

[검증] 빌드+스모크 → PIE: 발사 시작 느림→연속 발사로 가속→램프 시간 후 최대 도달, 멈춤 시 결정 규칙대로 / 2-client에서 클라/서버 케이던스 일치(렉/거부 없이) / 카드(FireRate) 상호작용 / 무기 교체 시 스핀 리셋·무크래시 → codex-review.ps1 -Base main → PROGRESS 갱신+✅+사용자 콘텐츠(DA) 동반 커밋 질문+--no-ff 머지.
```

### V1 — 최소 사각 경고 오디오

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽고 별도 worktree에서 진행(git worktree add ../FPSR-<키워드> -b <브랜치> main). main 머지는 빌드+스모크 통과·PR·사용자 승인 후.
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 V1을 진행한다.
읽을 SSOT: Docs/SSOT/PlayerFeel.md §2-14(게임필 — 사각 오디오 확정분), Docs/SSOT/Performance.md §5-1(Significance 티어), Docs/SSOT/Enemy.md §2-6.

[목표] §2-14 확정(2026-06-10): "최소 사각 경고 오디오(방향성 괴성/경고음)를 P4-D 말~P5로 당겨 코어 루프·재미 게이트(§7-5)와 함께 검증". 재미 게이트 판정 항목 ④(1인칭 사각 위협 체감)의 선행 조건이다. 풀 오디오 폴리시는 U13(P7) — 여기선 최소판만.

- 브랜치: phase/p5-blindspot-audio 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증

[산출물]
- 후방/사각에서 접근하는 적에 대한 방향성(스테레오 패닝/공간화) 경고 사운드. 로컬 전용(기존 UFPSRPlayerFeedbackComponent 패턴 — 비복제·이벤트형 — 재사용 검토).
- 발화 조건: 근접 위협(거리/각도) + 과발화 억제(쿨다운/동시 상한). Significance 티어(§5-1)와 정합 — S2/S3 원거리 적까지 전수 각도 계산 금지.
- 사운드 에셋: Infima 팩 Audio 재사용 가능하면 재사용, 적합한 게 없으면 엔진/플레이스홀더 사운드로 기능 검증(에셋 교체는 U13).

[함정/주의]
- §2-14 확정: 근접/사각 *시각* 위협 표시는 의도적으로 제외됨 — 시각 인디케이터를 부활시키지 마라(사운드로만).
- 전역 프리즈(§2-2) 중 경고음 정지/억제.
- 수백 마리 스웜 — 적 단위 오디오 컴포넌트 상시 부착 금지(로컬 판정 기반 단발 재생 등 싼 구조, 제1원리 3줄 명시).

[검증] 빌드+스모크 → 사용자 PIE(뒤에서 접근 시 방향 인지 체감) → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### V3 — 기본 크로스헤어 HUD (정적 레티클)

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽고 별도 worktree에서 진행(git worktree add ../FPSR-<키워드> -b <브랜치> main). main 머지는 빌드+스모크 통과·PR·사용자 승인 후.
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 V3를 진행한다.
읽을 SSOT: Docs/SSOT/PlayerFeel.md §2-14(게임필/HUD), Docs/SSOT/CombatWeaponCard.md §2-4-2(확산/ADS), 메모리 [[vibeue-mcp-capabilities]].

[목표] 현재 화면에 조준점이 없어 사격 체감이 어렵다. **기본 정적 크로스헤어(중앙 레티클)**를 Game 레이어 HUD에 추가한다. U1 재미 게이트 손맛 판정 ①(스웜 사격 손맛)의 사실상 전제 — 게이트 전 1차 트랙에서 작업한다. 동적 스프레드(원 확대/축소)는 U12 — 여기선 정적 베이스만.

- 브랜치: phase/p4d-crosshair 분기 (§6-7)
- 주로 콘텐츠(WBP) + 소량 C++(ADS getter BP 노출이 필요한 경우만). VibeUE 또는 사용자 / Opus 직접 권장(콘텐츠 검증 성격)

[배선 — 조사로 확정]
- HUD 인프라 존재: UFPSRGameHUDWidget(UCommonActivatableWidget)가 PlayerController EnsurePrimaryLayout에서 UI.Layer.Game에 push됨(FPSRPlayerController.cpp:75). WBP_GameHUD가 그 BP 구현. 히트마커(WBP_HitMarker)도 같은 Game 레이어 중앙.
- **최소 작업 경로**: WBP_GameHUD에 크로스헤어 위젯을 캔버스 자식으로 중앙 정렬 추가(또는 WBP_BasicCrosshair 신설 후 자식 배치). 신규 코드 없이 콘텐츠만으로 화면 중앙 표시 가능.

[산출물]
1. 정적 크로스헤어 위젯: 중앙 십자/점(플레이스홀더 비주얼 OK — 폴리시는 후속). 히트마커와 중앙 정렬 정합(Z는 히트마커가 위).
2. ADS 시 처리: 조준 중 크로스헤어 숨김 또는 변경. 런타임 상태는 UFPSRWeaponFireComponent::IsAiming()(FPSRWeaponFireComponent.h:36-39) — BP에서 못 읽으면 BlueprintPure 게터/표면화 경로를 소량 C++로 노출(서버 권위 불필요, 로컬 코스메틱).

[함정/주의]
- 동적 스프레드 크로스헤어(원 레티클 확대/축소)는 만들지 마라 — U12 범위. U12가 GetCurrentBloom()(FireComponent.h:32-33)+GetResolvedStats().SpreadDegrees를 읽어 이 베이스 위젯을 확장한다(중복 구현 금지). V3는 정적 + ADS 토글까지만.
- 무기별 다른 크로스헤어(샷건 원 등)도 U12/폴리시 — 여기선 단일 기본형.
- ConstructorHelpers/에셋 경로 하드코딩 금지(§6-2). VibeUE 한계: 위젯바인딩 getter는 컴파일 후(P6-A 교훈).
- 메뉴(Menu 레이어)에선 크로스헤어 비표시 — Game 레이어 전용이라 자동 충족(확인만).

[검증] 빌드+스모크(코드 변경 시) → 사용자 PIE: 크로스헤어 중앙 표시, 발사 시 위치 정합, ADS 시 숨김/변경, 메뉴에선 미표시 → codex-review.ps1 -Base main → PROGRESS 갱신+✅+사용자 콘텐츠 동반 커밋 질문+--no-ff 머지.
```

### U2 — 패배 배선: 간이 전멸 판정 → EndRun(Defeat)

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽고 별도 worktree에서 진행(git worktree add ../FPSR-<키워드> -b <브랜치> main). main 머지는 빌드+스모크 통과·PR·사용자 승인 후.
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U2를 진행한다.
읽을 SSOT: Docs/SSOT/RunFlow.md §2-1(런 루프)·§2-2(전역 프리즈), Docs/SSOT/PlayerFeel.md §2-13(DBNO — 이번엔 미구현, 경계 파악용), Docs/SSOT/Workflow.md §6.

[목표] 게임 루프 "진짜 닫기" 1/2 — 패배. 전원 사망 시 AFPSRGameMode::EndRun(Defeat) 자동 호출.
현재: Source/FPSRoguelite/Private/Hero/FPSRCharacter.cpp:472-476 HandleOutOfHealth가 로그만 남김(주의: 경로가 Private/Hero/다, Player/ 아님). EndRun은 Core/FPSRGameMode.cpp:51, 디버그 FPSR.EndRun으로만 트리거됨.

- 브랜치: phase/p6-defeat-wiring 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증

[산출물]
1. 사망 표식: HandleOutOfHealth(서버 OnOutOfHealth 바인딩)에서 플레이어를 Dead 상태로 마킹(간이판 — 입력/공격 차단 + 적 타게팅 제외 정도. 시체 연출/관전은 범위 밖).
2. 전멸 집계: "생존 플레이어 0명 → EndRun(Defeat)". 집계 함수를 GameMode/GameState에 독립 함수로 분리할 것 — 후속 U9(DBNO)가 판정식을 "전원 Dead 또는 DBNO"로 교체만 하면 되는 구조(P7-MultiplayerLoop_Plan §3-6 'Wiped' 판정의 재료가 됨).
3. 솔로(1인) 플레이 = 그 1인 사망 즉시 전멸.

[함정/주의]
- EndRun은 HasAuthority+bRunEnded 래치로 이중 호출 안전 — 그대로 활용하되 호출은 서버에서만.
- EndRunFreeze는 §2-2 bRunPaused 재사용+bRunEnded 래치로 카드선택 완료가 언프리즈 못 하는 구조(P6-A 구현) — 건드리지 마라. 메모리 [[freeze-gate-client-server-symmetry]]: 클라 입력+서버 RPC 양쪽 게이트 대칭 확인.
- Dead 상태에서 적 공격토큰/접촉 데미지·자석 XP 대상에서 제외되는지 확인(죽은 폰에 데미지 반복 금지).
- DBNO/부활/관전은 만들지 마라(U9 범위). 임시 구조 금지 — 집계 함수는 프로덕션 구조로.

[검증] 빌드+스모크 → PIE: 솔로에서 적에게 사망 → DEFEAT 결과창 → ReturnButton 메뉴 복귀 → 재시작 정상. 2-client(가능하면): 1명 사망=게임 계속, 2명 사망=패배. codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U3a — 약점 부위 데미지 시스템 (헤드샷/디자이너 지정 존)

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽고 별도 worktree에서 진행(git worktree add ../FPSR-<키워드> -b <브랜치> main). main 머지는 빌드+스모크 통과·PR·사용자 승인 후.
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U3a를 진행한다.
읽을 SSOT: Docs/SSOT/Enemy.md §2-6·§2-10(데미지 경로/브릿지), Docs/SSOT/CombatWeaponCard.md §2-4-2(크릿/사격감), Docs/SSOT/PlayerFeel.md §2-14(히트마커), Docs/SSOT/Performance.md §5(스웜 비용 예산).

[목표] 적/보스의 **디자이너 지정 부위**(머리, 부패 부위 등)에 명중 시 추가 데미지("헤드샷/약점 사격") 시스템. 보스(U3/U4)가 1차 소비자이며 스웜 적에도 범용 적용 가능해야 한다(DESIGN-FIRST 범용성 원칙 — Enemy 전용으로 만들지 마라).

- 브랜치: phase/p6-weakpoint-damage 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증(데미지 경로 전수라 세밀 검증 필수)

[산출물]
1. 약점 존 정의(디자이너 주도): BP에서 배치/조정 가능한 컴포넌트 기반(예: 약점 콜리전 셰이프 컴포넌트 + 존별 데미지 배수 프로퍼티) — 디자이너가 위치·크기·배수를 에디터에서 지정. 수치 하드코딩 금지(컴포넌트/DA 프로퍼티).
2. 판정 배선: 히트 결과의 컴포넌트 식별로 약점 여부/배수 결정 — **히트스캔(단일/관통)·투사체 직격·ChargeLaser** 경로. 판정/배수 적용은 FPSRCombatStatics 데미지 헬퍼 흐름에 통합(경로별 중복 구현 금지).
3. 적용 대상: AFPSREnemyBase(스웜 — per-BP/DA 옵트인) + U3 보스 베이스가 그대로 상속·소비할 수 있는 구조.
4. 피드백: 약점 명중 구분 — 기존 NotifyHitMarker(Hit/Crit/Kill) 체계에 약점 타입 확장(전용 마커/사운드 훅, 연출 콘텐츠는 보류).

[플랜에서 결정할 항목 — 지어내지 말고 사용자 질문]
- 크릿과의 스택 규칙: 약점 배수 × 크릿(곱연산) vs 배타 vs 약점=크릿 확정 트리거.
- 근접(Melee)/AOE(폭발)에 약점 적용 여부 — 정밀 조준이 아니므로 비적용이 자연스러우나 확정은 질문.
- ChargeLaser 워밍업 틱(고정 소뎀)에 약점 적용 여부 — 페이오프 빔만 권장(틱뎀 고정화 설계와 일관).

[함정/주의]
- **성능(§5)**: 적 500 × 추가 콜리전 프리미티브 = 쿼리 비용 증가. 스웜은 per-DA 옵트인 + 보스/엘리트 우선 적용을 기본 설계로. 전 스웜 적용 시 비용을 측정해 플랜에 명시(제1원리 3줄).
- **이중 히트 방지**: 약점 컴포넌트가 pawn-gather(AddDamageablePawnObjectTypes — ECC_Pawn+ECC_FPSRPlayerPawn) 멀티트레이스에 본체 캡슐과 함께 잡히면 같은 액터가 2회 데미지/관통 카운트 2회 소모될 수 있다 — per-actor 디듀프(가장 우선 히트 컴포넌트 1개 채택, 약점>본체 우선) + 관통 카운트는 액터당 1회.
- **서버 권위**: 약점 판정은 서버 트레이스/임팩트 결과 기준(클라가 "약점 맞았다" 보고하는 구조 금지).
- FF: 플레이어/아군에는 약점 없음(적/보스 전용).
- 약점 미배치 적 = 현행과 완전 동일(무회귀 — 기본값 경로에 분기 비용 최소).
- 콜리전 프로파일: 약점 셰이프는 데미지 쿼리에만 응답(이동/지면 트레이스/적 분리(separation)·플로우필드에 영향 금지).

[검증] 빌드+스모크 → PIE: 약점 명중=추가 데미지+구분 마커 / 본체 명중=현행 동일 / 관통 무기로 약점+본체 겹침 시 1회 판정 / 스웜 다수 스폰 시 프레임 회귀 없음 → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지. → U3(보스)가 이 인프라를 소비한다.
```

### U3 — 보스 스캐폴드(D4) + 승리 배선

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽고 별도 worktree에서 진행(git worktree add ../FPSR-<키워드> -b <브랜치> main). main 머지는 빌드+스모크 통과·PR·사용자 승인 후.
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U3(코드 백로그 D4)를 진행한다.
읽을 SSOT: Docs/SSOT/RunFlow.md §2-7(보스)·§2-2(프리즈), Docs/P7-MultiplayerLoop_Plan.md §3-5(보스 박스 설계 — 이 유닛과 통합됨), Docs/SSOT/Enemy.md §2-10(데미지 경로), Docs/SSOT/Workflow.md §6.

[목표] 게임 루프 "진짜 닫기" 2/2 — 승리. 보스 스캐폴드(체력만 있는 보스) + 처치 시 EndRun(Victory).
현재: Source/FPSRoguelite/Private/Run/FPSRRunDirectorSubsystem.cpp:365 — BossTime 도달 시 "boss actor is a P6 stub" 로그+타임라인 정지만. 디버그 FPSR.SkipToBoss(:719) 있음.

- 브랜치: phase/p6-boss-scaffold 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증
- 선행: U2 머지 확인(전멸 판정·EndRun 배선 패턴 공유) + U3a 머지 확인(약점 부위 인프라 — 보스가 소비)

[산출물]
1. ABossBase(또는 동급): UFPSREnemyHealthComponent 재사용으로 기존 무기 전 경로(히트스캔/투사체/레이저/근접) 데미지가 신규 코드 0으로 먹히게(P7 플랜 §1 확정). 큰 체력, 이동/공격은 스캐폴드 단계 생략 가능(체력만 박스 = 확정 스코프).
2. UFPSRBossDefinitionDataAsset: 체력/스폰 위치 규칙 등 콘텐츠 주도 필드(에셋 경로 하드코딩 금지). StateTree 골격(빈 상태기계)은 백로그 D4 명세대로 클래스만 — 실제 패턴은 장기 백로그.
2-b. U3a 약점 인프라 소비 확인: 보스 베이스에서 약점 컴포넌트 배치가 동작하는지(배치 자체는 U4 콘텐츠) — 신규 약점 코드를 여기서 또 만들지 마라.
3. RunDirector EnterBoss() 연결: 타임라인 정지 지점에서 보스 스폰/활성화(GameMode 설정 클래스 주입 — BP_Enemy 주입 패턴과 동일).
4. 승리 배선: 보스 OnDeath → 델리게이트로 GameMode에 통지 → EndRun(Victory). 느슨한 결합 필수 — U11(P7 멀티)이 같은 신호를 "결과 표시 후 ServerTravel(Lobby)"로 확장하므로 직접 호출 박지 말고 훅으로.

[함정/주의]
- P7-MultiplayerLoop_Plan §3-5의 AFPSRBossBox를 별도로 만들지 마라 — 이 유닛의 보스 베이스가 그 역할을 대체한다(중복 구현 방지, U11 프롬프트에도 동일 명시됨).
- 콜리전 = "가장 흔한 함정"(P7 플랜 §6): 보스 루트/메시는 ECC_Pawn 오브젝트타입으로 — WorldStatic이면 히트스캔의 Visibility 벽판정에 '벽'으로 잡혀 자기 몸이 탄을 차단한다. pawn-gather 경로는 FPSRCombatStatics::AddDamageablePawnObjectTypes(ECC_Pawn+ECC_FPSRPlayerPawn 양채널)와 정합 확인.
- 보스 페이즈 중에도 레벨업 전역 프리즈(§2-2) 동작 — 보스 로직(스폰 직후 활성화 포함)에 bRunPaused 게이트.
- 보스 비폰이면 넉백 비대상(LaunchCharacter 경로) — 의도된 동작, 버그 아님.
- GAS는 보스에 허용되는 영역(§1)이지만 이번 스캐폴드는 EnemyHealthComponent 확정 — GAS/ASC를 붙이지 마라(확장은 실보스 유닛에서 판단).

[검증] 빌드+스모크 → PIE: FPSR.SkipToBoss → 보스 스폰 → 전 무기로 데미지 확인 → 처치 → VICTORY 결과창 → 메뉴 복귀. U2와 합쳐 승/패 양쪽 E2E. codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U4 — 보스 콘텐츠 + PIE (전체 게임 플로우 완성 지점)

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽고 별도 worktree에서 진행(git worktree add ../FPSR-<키워드> -b <브랜치> main). main 머지는 빌드+스모크 통과·PR·사용자 승인 후.
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U4를 진행한다.
읽을 SSOT: Docs/SSOT/RunFlow.md §2-7, Docs/P7-MultiplayerLoop_Plan.md §3-5·§6, Docs/P6A_GameFlow_UserContent_Guide.md(셸 콘텐츠 전례), 메모리 [[vibeue-mcp-capabilities]].

[목표] U3 코드 베이스 위에 보스 콘텐츠를 만들어 "메뉴→런→미션→보스→승/패→결과→메뉴" 전체 플로우를 PIE로 완성한다. 이 유닛이 끝나면 W1(전체 검증)으로 진입.

- 브랜치: U3 브랜치 동승(코드+콘텐츠 함께 머지) 또는 content/boss 별도 — 착수 시 사용자에게 질문
- VibeUE MCP 사용 가능(에디터 열림+플러그인 활성 시) / Opus 직접 권장(콘텐츠 검증 성격)

[산출물]
- BP_Boss(U3 베이스 상속) + DA_BossDefinition: 체력 등 수치 지정. 메시=플레이스홀더 박스 OK(실보스는 장기 백로그).
- **보스 약점 존 배치(U3a 인프라)**: BP_Boss에 약점 컴포넌트(예: 머리/코어 부위) 위치·크기·배수 지정 — 디자이너 조정 항목.
- L_Sandbox 보스 스폰 위치 지정(맵 중앙 — P7 플랜 확정 "맵 중앙 박스").
- 보스 체력 표시: 디버그 텍스트/단순 ProgressBar로 시작 가능(P7 플랜 §6 허용). 정식 HUD는 U12/U13.

[함정/주의]
- 콜리전 정합 재확인(U3 프롬프트와 동일): 보스 = ECC_Pawn 오브젝트타입. WorldStatic 금지(자기 몸 탄 차단). 적 지면 트레이스는 ECC_WorldStatic만 쿼리하므로 보스 발밑 바닥은 기존 규칙 유지(Mobility=Static+Block, WorldDynamic 금지 — PROGRESS 적 바닥 gotcha).
- 임시 테스트값(BossTime 300s)은 이 단계에서 원복하지 마라 — 빠른 반복 검증용으로 유지(원복은 U14, 메모리 p4a-temp-test-values).
- VibeUE 한계(메모리): 위젯 바인딩 getter는 컴파일 후 / FText는 Conv_StringToText / config는 ini 직접.

[검증 — 전체 플로우 PIE 체크리스트]
① 메뉴→Play→런 시작 ② 미션 발생·클리어·카드 보상 ③ BossTime 도달→보스 스폰 ④ 전 무기(8종)로 보스 데미지 + **약점 부위 명중=추가 데미지·구분 마커 / 본체=기본 데미지** ⑤-a 보스 처치→VICTORY ⑤-b (별도 런) 전원 사망→DEFEAT ⑥ 결과창→메뉴→재시작 무결성(상태 리셋).
완료 시: PROGRESS 갱신+✅ + 사용자 콘텐츠 동반 커밋 질문 + --no-ff 머지. → 다음 = W1 전체 검증.
```

### W1 — 전체 프로젝트 정합 검증 (Fable 전수 + Codex 분할)

**W1-A: Fable(Claude) 전수 검증 세션 프롬프트**

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽을 것. W1은 검증 전용이라 코드 수정 금지(교정은 별도 fix/ 브랜치·PR·승인 후 머지).
Game.md + PROGRESS.md + Docs/TaskPrompts_Master.md(§A·§B·W1)를 먼저 읽어. 이 세션은 구현이 아니라 **전수 검증 전용**이다(유닛 W1). 게임 플로우(메뉴→런→미션→보스→승/패→결과→메뉴)가 완성된 시점에서, 전 코드베이스·콘텐츠 배선·SSOT 정합을 검증하고 교정 백로그를 만든다.

[범위·방법] Docs/SSOT 전 파일(RunFlow/CombatWeaponCard/Enemy/PlayerFeel/Architecture/Performance/Workflow/Roadmap)을 도메인별로 읽고, 대응 코드(Source/FPSRoguelite/{Core,Run,Enemy,Weapon,AbilitySystem,Card,UI,Hero,Combat,Pickup})를 서브시스템 단위로 대조하라. 필요 시 서브에이전트로 영역 분할(단 판정은 직접).

[검증 체크리스트 — 전 영역 공통]
1. 장르 3원칙 위반(Game.md §1): 스웜 적에 GAS/ASC·StateTree·NavMesh 유입 없는지, 적별 비용 증가 코드 없는지.
2. 서버 권위: 데미지/스폰/카드/XP/탄약의 결정이 전부 서버인지, 클라 RPC 입력 검증(안티치트 — offer 캐시/인덱스 패턴) 유지인지.
3. Push Model 일관성: 신규 복제 프로퍼티가 DOREPLIFETIME_WITH_PARAMS_FAST+MARK_PROPERTY_DIRTY 패턴인지.
4. 전역 프리즈 대칭(§2-2, 메모리 freeze-gate-client-server-symmetry): 모든 행동 경로(발사/근접/대시/재장전/교체/보스/미션 타이머)가 클라 입력+서버 게이트 양쪽으로 막히는지, 진행형 FTimerHandle이 PauseTimer 처리되는지, bRunEnded 래치 무결성.
5. 콜리전/채널 정합: ECC_FPSRPlayerPawn(=GameTraceChannel1 "PlayerPawn") 양채널 쿼리(AddDamageablePawnObjectTypes) 전 경로 적용, 보스=ECC_Pawn, 바닥=WorldStatic 규칙.
6. 하드코딩/임시구조: 에셋 경로 C++ 하드코딩(ConstructorHelpers — 알려진 잔존: XP 스피어, U14 전환 예정), 디버그 로그/값 잔존, TODO 주석 전수 grep(TODO/FIXME/HACK/P5/P6/P7) 후 분류.
7. 수치 정합: FF=50%, 임시 테스트값(스케줄/BossTime)=의도된 미원복(U14) — 버그로 분류 금지.
8. 알려진 수용 설계를 재발견해 "버그" 보고 금지: 관통+ExplosiveRounds 탄착점 1회 폭발 / ExplosiveRounds 직격 히트마커 2회 / ChargeLaser 차징 중 프리즈=데미지만 스킵 / ChargeLaser Finding A(원격 클라 코스메틱 레이스, U13에서 notify로 해소 예정).

[절차]
1. 정적 대조(위 체크리스트) → 2. 빌드("D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\Build.bat" FPSRogueliteEditor Win64 Development -Project="E:\Git_Project\FPSRoguelite\FPSRoguelite.uproject" -WaitMutex) + 헤드리스 스모크(FPSRoguelite.Smoke.ModuleLoads) → 3. Codex 분할 리뷰(아래 W1-B 가이드) 실행·결과 병합 → 4. 사용자 PIE 전체 플로우 체크리스트(U4의 ①~⑥) 재실행 의뢰 → 5. 발견사항을 P1(차단)/P2(중요)/P3(개선)으로 분류한 리포트를 Docs/reviews/full-audit-<날짜>.md로 작성(코드 수정은 이 세션에서 하지 마라).

[완료 기준] 리포트 + 교정 백로그 제시. P1/P2 교정은 사용자 승인 후 별도 fix/ 브랜치에서 플랜 우선으로. 교정 완료 후 U1∥V2 게이트 진입 가능 보고.
```

**W1-B: Codex 분할 리뷰 실행 가이드** (Fable 검증 세션이 3단계에서 수행)

```
Codex(gpt-5.5) 전체 검증 — Scripts/codex-review.ps1은 diff 기반(review 서브커맨드, scope 플래그와 커스텀 프롬프트 양립 불가)이므로 전체 코드 검증은 두 방법을 병용한다. 사용량 한도(메모리 codex-review-gate) 고려해 영역 분할·세션 분산 실행.

[방법 A — 권장: 누적 diff 리뷰] 게임 플로우 완성분 전체를 베이스라인 대비로 리뷰:
  powershell -File Scripts\codex-review.ps1 -Base <검증 기준점 커밋/태그>
  기준점은 직전 전체검증 시점(최초면 P6-A 머지 4dacec1 이전의 적절한 SHA). diff가 너무 크면 Codex 컨텍스트 한도 — 그 경우 방법 B로 전환.
[방법 B — 영역 분할 직접 호출] codex exec(비대화)로 영역 스코프 프롬프트 실행(절대경로 C:\Users\koras\AppData\Local\OpenAI\Codex\bin\codex.exe, -C 리포루트, -s read-only, -a never — pwsh 없음·PS5.1 주의):
  영역 순서(중요도순): ① Core/Run(GameMode/GameState/RunDirector/미션) ② Enemy(스폰/플로우필드/HealthComp) ③ Weapon+AbilitySystem(FireComponent/GA 4종/CombatStatics/Projectile) ④ Card+UI ⑤ Hero/Pickup.
  각 영역 프롬프트: "Source/FPSRoguelite/<영역> 전 파일을 AGENTS.md 3원칙+Game.md 설계 기준으로 리뷰. 서버권위/Push Model/프리즈 게이트/채널 정합/하드코딩 중심. P1/P2/P3 분류."
[병합] 각 결과(stdout 또는 Docs/reviews/ 저장본)를 Fable 리포트의 'Codex 소견' 절에 영역별로 병합하고, Fable 자체 발견과 중복/상충을 판정해 단일 교정 백로그로 통합.
```

### U1 — 재미 게이트(§7-5) + 성능 검증(§5) ※ V2와 같은 멀티 세팅에서 동시 진행

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽을 것. U1·V2는 사용자 PIE 주도 판정이라 worktree 코드작업 아님 — 검증 기록만(코드 교정은 별도 유닛).
Game.md + PROGRESS.md + Docs/SSOT/Roadmap.md §7-5 + Docs/SSOT/Performance.md §5 전체를 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U1(+동시 진행 V2)을 진행한다. 이 유닛은 코드 작성이 아니라 **판정 게이트**다 — 사용자 PIE 주도, 세션은 측정 세팅·체크리스트·기록을 담당.

[선행 확인] V0(무기 비주얼)·V1(사각 오디오)·W1(전체 검증·교정) 완료 — 손맛 판정은 실제 총기 메시/애님/사운드와 완성된 플로우에서만 유효하다. 미완이면 중단하고 보고.

[판정 항목 — §7-5 ①~⑤ 전부, 누락 금지]
① 스웜 사격 손맛(타격감·반동·학살 쾌감, §2-4-2) ② 카드 선택의 의미(빌드 체감 차이, §2-3 시너지) ③ 프리즈 페이싱 수용성(§2-2 — 비동기 Q/E 하이브리드 전환 필요성 판단) ④ 1인칭 사각 위협 체감(V1 사각 오디오) ⑤ 적 500 체감 성능.

[성능 검증 — §5, 동시 수행]
- Unreal Insights + NetProfiler로 적 500 시나리오 측정. **호스트(리슨서버)=최악 케이스 — 하드캡은 호스트 기준으로 확정**.
- 판정 순서 준수: Push Model(현행) → 부족 시 Replication Graph → 그래도 부족 시 Iris(Beta) 평가. Iris를 1순위로 꺼내지 마라(§3 — 디폴트 OFF).
- 산출물: 측정 수치 + 하드캡 확정값 + RepGraph/Iris 도입 여부 판정 → Docs/SSOT/Performance.md §5 갱신(SSOT 먼저 원칙).

[V2 동시 진행 — 같은 2-client 세팅에서, 단 체크리스트 분리(실패 원인 격리)]
- FF: FPSR.SetFriendlyFire 1 → 아군 사격=50% 데미지(10% 아님 — Roadmap §7-3의 10%는 stale), FF OFF=무피해, 바주카/유탄 아군 런칭(넉백은 데미지 독립).
- ChargeLaser(서버권위, PROGRESS 체크리스트): 클릭1회→자동차징(틱 소뎀 연속)→완료 본뎀1발 / 차징 중 조준이동=빔 추적 / 차징 중 재클릭 무시 / 무기교체 시 시퀀스 취소·무크래시 / 적·아군(FF)·관통 각각 판정.
- 알려진 한계 재발견 시 버그 보고 금지: Finding A(원격 클라 코스메틱 반동/게이트 레이스 — 데미지 무영향, U13에서 해소 예정), 플레이어 넉백 오너클라 스무딩 후속.

[불합격 처리 — §7-5 확정 규칙] 어떤 항목이든 불합격이면 **다음 콘텐츠 진행 전 루프 재설계 우선**: ①→§2-4 사격, ③→§2-2 프리즈(비동기 선택 하이브리드), ⑤→§5 성능 재검토를 트리거. 합격/불합격과 근거를 PROGRESS+해당 SSOT에 기록.

완료 시: 판정 기록 커밋(문서) + TaskPrompts_Master §B에 ✅ → 2차 트랙(U5/U6/U8/U10, U7) 해금 보고.
```

### V2 — (U1에 통합 실행) 2-client 정합 검증

> V2의 체크리스트는 U1 프롬프트에 내장되어 같은 멀티 세팅에서 동시 실행된다(사용자 확정 2026-06-13). 별도 세션으로 분리할 경우에만 U1 프롬프트의 `[V2 동시 진행]` 블록을 독립 프롬프트로 사용.

### U5 — B1 원거리 적 AI + 사전경고 생산자

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽고 별도 worktree에서 진행(git worktree add ../FPSR-<키워드> -b <브랜치> main). main 머지는 빌드+스모크 통과·PR·사용자 승인 후.
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U5(코드 백로그 B1)를 진행한다.
읽을 SSOT: Docs/SSOT/Enemy.md §2-6(원거리 아키타입·공격토큰)·§2-10(투사체), Docs/SSOT/PlayerFeel.md §2-14(원거리 경고 소비자 — 이미 구현됨), Docs/SSOT/Performance.md §5.

- 브랜치: phase/p4-ranged-enemy 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증. 콘텐츠 보류분(적 메시/투사체 VFX)은 만들지 마라.

[산출물]
1. 원거리 적 아키타입: 경량 유지(GAS 금지 — §1). 사거리 내 정지→차징(경고)→발사 사이클.
2. 투사체: A1 AFPSRProjectile+UFPSRProjectileSubsystem 재사용, Team=Enemy 경로(P5 FF에서 보존됨 — Enemy→플레이어 ApplyContactDamage 브릿지)+IsHostileTarget 친화/instigator 차단 그대로.
3. 사전경고 생산자: 차징 시작/종료 시 대상 플레이어 PC의 ClientNotifyRangedTarget 호출(P4-D 소비자 — Reliable·다수소스 id별 TMap — 그대로 소비, 현재 디버그 FPSR.TestRangedWarn만 있음).
4. 공격토큰: 기존 토큰 상한 시스템에 원거리 토큰 통합(§2-6 — 무한 포격 방지).

[함정/주의]
- 복제 투사체 ≤64 캡 FIFO 강제회수를 플레이어 AOE와 **공유**한다(§5) — 원거리 적 다수 시 풀 고갈 설계(발사 빈도 상한/적 투사체 비복제 검토 등)를 플랜에 명시.
- 전역 프리즈(§2-2): 차징/발사 사이클 타이머 모두 게이트+PauseTimer.
- 스폰 비율/아키타입 혼합은 DA/스케줄 데이터 주도(하드코딩 금지).
- ±10% 이속 편차·분리(separation) 등 기존 스웜 규칙과 충돌 없는지 확인.

[검증] 빌드+스모크 → PIE: 원거리 적 차징 시 화면 경고 방향 표시(기존 위젯), 피격, FF OFF 시 적탄 아군 오발 없음 → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U6 — A4 Fragment 마무리

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽고 별도 worktree에서 진행(git worktree add ../FPSR-<키워드> -b <브랜치> main). main 머지는 빌드+스모크 통과·PR·사용자 승인 후.
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U6(코드 백로그 A4)를 진행한다.
읽을 SSOT: Docs/SSOT/CombatWeaponCard.md §2-4-1(Fragment 훅 구조)·§2-3(카드), Docs/SSOT/Workflow.md §6.

- 브랜치: phase/p4b-fragment-finish 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증. 교체 UI 위젯 콘텐츠는 보류(레이아웃만 후속).

[산출물]
1. Melee fragment: UFPSRGA_WeaponMelee에 fragment 훅(PreFire/OnHitActor/PostFire 상당) 배선 — 형제 GA(Hitscan/Projectile/ChargeLaser) 패턴 대칭.
2. Fragment 제거/교체 서버 로직: 슬롯 상한 도달 시 교체 플로우. 기존 안티치트 패턴 준수 — 서버가 발급한 offer 캐시+인덱스 검증(클라는 인텐트만, 임의 fragment 적용 불가).
3. 콘텐츠 꼬리: 무기별 AvailableModifiers 확장(현재 Rifle 중심+FF 카드 4종) — 어떤 무기에 어떤 fragment를 열지 사용자에게 질문 후 등록.

[함정/주의 — stale 정보]
- PROGRESS 구절 "ModifyChargeTime/OnProjectileSpawn 훅 미완"은 **stale — 둘 다 A3a/A3b에서 이미 구현됨**. 재구현하지 마라(grep으로 확인 후 재사용).
- MaxStacks 규칙: 훅은 스택마다 적용(MultiShot 2스택=3발), MultiShot은 펠릿당 탄약 소모(잔량 클램프) — 기존 의미 유지.
- fragment 카드 UI 규칙: 등급 대신 카테고리 라벨(FragmentCategoryText), 수치 빈칸(P4-B-2 확정).
- 제거/교체는 ThisWeapon 귀속(FFPSRCardDraw.TargetWeapon 서버 세팅) 구조 유지.

[검증] 빌드+스모크 → PIE: 칼에 fragment 적용·발동, 슬롯 초과 시 교체 동작, 교체 악용(임의 인덱스) 서버 거부 → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U7 — C1 플로우필드 높이/클리어런스

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽고 별도 worktree에서 진행(git worktree add ../FPSR-<키워드> -b <브랜치> main). main 머지는 빌드+스모크 통과·PR·사용자 승인 후.
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U7(코드 백로그 C1)을 진행한다.
읽을 SSOT: Docs/SSOT/Performance.md §5-2(플로우필드), Docs/SSOT/Enemy.md §2-6.

[선행 확인] U1 게이트의 PIE 관찰(계단/높이 주행, 좁은 통로)에서 실제 문제가 확인됐는지 먼저 PROGRESS에서 확인 — 관찰 결과가 없으면 사용자에게 우선순위 재확인 후 착수(PIE 의존 유닛).

- 브랜치: phase/p2-flowfield-height 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증

[산출물]
1. 멀티레벨/높이 인지 BFS 샘플링(계단·단차에서 적 경로 품질).
2. 셀 클리어런스 인지 프로브: 현재 전셀 오버랩 방식이 경계 벽 양쪽 셀을 차단(좁은 통로 과차단) — 해소.

[함정/주의]
- AFPSREnemyBase::ApplyGravity down-trace는 **의도적으로 ECC_WorldStatic만** 쿼리(폰/투사체 위 착지 방지) — 바닥 콘텐츠 규칙(Mobility=Static+Block)은 유지, **WorldDynamic 추가 금지**(비행 투사체 위 착지 부작용).
- 0.2s 간격 멀티소스 BFS 예산(§5-2) 초과 금지 — 높이 샘플링 추가 비용을 측정해 플랜에 명시.
- 그리드/셀 크기(200cm) 변경은 전 맵 영향 — 변경 시 제1원리 3줄 명시.

[검증] 빌드+스모크 → PIE: 계단/단차 추격, 좁은 통로 통과 → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U8 — C2 GameplayMessageSubsystem 재구현

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽고 별도 worktree에서 진행(git worktree add ../FPSR-<키워드> -b <브랜치> main). main 머지는 빌드+스모크 통과·PR·사용자 승인 후.
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U8(코드 백로그 C2)을 진행한다.
읽을 SSOT: Docs/SSOT/Architecture.md §3·§4(기술 채택/구조), Docs/SSOT/Performance.md §5(코스메틱 계약).

- 브랜치: phase/infra-gms 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증

[산출물]
- GameplayMessageSubsystem(또는 동급 경량 메시지 버스) + Payload struct 정의. GameplayTag 채널 기반 publish/subscribe.
- 기존 GameplayEvent 태그(GameplayEvent.EnemyKilled/LevelUp/MissionComplete/MissionStart/PickupCollected — DefaultGameplayTags.ini에 이미 정의됨) 활용 설계.

[함정/주의]
- §5 계약: "히트/사망 코스메틱은 GameplayMessage/Cue — 복제 액터 상태 아님". U13(VFX/Gibs/핑)이 이 버스의 소비자다 — Payload struct에 U13 요구(위치/적 타입/킬 여부 등)를 미리 반영하되 과설계 금지.
- 수백 적 스케일: 메시지 발행 비용 최소(스택 할당 payload, 구독자 없으면 zero-cost).
- 엔진 플러그인 GameplayMessageRouter(Lyra 발췌) 사용 가능 여부를 먼저 엔진 소스에서 확인(§6-3 — UE5.7에 플러그인 존재 여부 grep) 후 자체 구현과 비교해 플랜에 명시.

[검증] 빌드+스모크+발행/구독 유닛 경로 로그 확인 → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U9 — D2 DBNO 수동 부활

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽고 별도 worktree에서 진행(git worktree add ../FPSR-<키워드> -b <브랜치> main). main 머지는 빌드+스모크 통과·PR·사용자 승인 후.
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U9(코드 백로그 D2)를 진행한다.
읽을 SSOT: Docs/SSOT/PlayerFeel.md §2-13(생존/DBNO), Docs/SSOT/RunFlow.md §2-2, Docs/P7-MultiplayerLoop_Plan.md §3-6(전멸 판정 연계).

- 브랜치: phase/p5-dbno 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증. 다운 애니메이션/부활 UI 콘텐츠는 보류(디버그 표시로 검증).

[산출물]
1. Alive/DBNO/Dead 상태기계(서버권위): 체력 0 → DBNO(다운) → 아군 근접 상호작용 부활 게이지 → 부활 또는 블리드아웃 → Dead.
2. U2의 전멸 집계 판정식 교체: "전원 Dead" → "전원 Dead 또는 DBNO"(P7 플랜 §3-6) — U2가 분리해 둔 집계 함수만 수정.
3. 솔로 플레이: DBNO 없이 즉사 또는 즉시 전멸 — 정책을 플랜에서 사용자에게 질문.

[함정/주의]
- 블리드아웃 시간/전원 다운 처리 세부 밸런스는 문서 미확정 — 지어내지 말고 플랜에서 사용자 질문.
- 전역 프리즈(§2-2) 중 부활 게이지/블리드아웃 진행 정책도 문서 미정 — 사용자 질문 항목(권장: PauseTimer 대칭).
- 플레이어 캡슐=ECC_FPSRPlayerPawn 별도 채널 — DBNO 상태에서 콜리전 변경 시 FF 경로(AddDamageablePawnObjectTypes 양채널 쿼리)가 깨지지 않는지 확인.
- DBNO 중 적 공격토큰 대상 제외 여부(다운 킬 정책)도 질문 항목.

[검증] 빌드+스모크 → 2-client PIE: 다운→아군 부활→복귀 / 전원 다운→DEFEAT / 부활 게이지 서버권위(클라 조작 불가) → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U10 — D3 메타 SaveGame

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽고 별도 worktree에서 진행(git worktree add ../FPSR-<키워드> -b <브랜치> main). main 머지는 빌드+스모크 통과·PR·사용자 승인 후.
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U10(코드 백로그 D3)을 진행한다.
읽을 SSOT: Docs/SSOT/RunFlow.md §2-11(메타 프로그레션·저장 정책), Docs/SSOT/Architecture.md §4.

- 브랜치: phase/p6-savegame 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증. 메타 업그레이드 트리 콘텐츠/UI는 보류(저장 인프라만).

[산출물]
1. URogueliteSaveGame: 버전 필드 + 마이그레이션 경로(§2-11 정책 전항목 준수).
2. SaveManager(UGameInstanceSubsystem): 모든 저장/로드는 이 서브시스템 경유 강제(UI/Actor 직접 SaveGameToSlot 금지). AsyncSaveGameToSlot 사용.
3. 저장 시점 구분: 런 중 vs 로비/메뉴(§2-11). 런 종료(EndRun) 시 재화/기록 영속화 훅.
4. 영구 스탯 적용 경로: 런 시작 시 GE로 ASC 적용(§2-11 — GAS는 플레이어 영역).

[함정/주의]
- Steam Cloud 호환 고려(파일 슬롯 구조) — U11 세션 작업과 충돌 없게.
- 호스트/클라 구분: 메타 저장은 각 로컬 플레이어 소유(서버가 타인 세이브 쓰지 않게).
- 검증 없이 마이그레이션 코드 방치 금지 — 구버전 더미 세이브로 로드 테스트.

[검증] 빌드+스모크+저장/로드/버전 마이그레이션 자동화 경로 → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U11 — D5 세션 + P7 멀티플레이 루프 (2-PC Steam E2E)

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽고 별도 worktree에서 진행(git worktree add ../FPSR-<키워드> -b <브랜치> main). main 머지는 빌드+스모크 통과·PR·사용자 승인 후.
Game.md + PROGRESS.md + Docs/P7-MultiplayerLoop_Plan.md **전문**을 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U11을 진행한다.
주의: 이 "P7"은 멀티플레이 루프 플랜 문서의 P7이다 — Roadmap §7-3의 P7(폴리시/빌드)과 무관(명칭 충돌, §D 참조).

[선행 확인] U2(전멸 판정)·U3/U4(보스) 머지 완료 + V2(2-client 정합) 통과 권장. 미완이면 중단·보고.

- 브랜치: phase/p7-mp-loop 분기 (§6-7)
- **Opus 직접 구현**(플랜 문서 §6 확정 — 세션/트래블/권위는 Haiku 위임 금지 영역, 메모리 haiku-delegation-security-wiring)

[착수 전 사용자 결정(문서 공백 — 지어내지 마라)]
P6-A 메인메뉴(L_MainMenu, GameDefaultMap)와 P7 로비(L_Lobby)의 관계: Play 버튼=솔로 런 직행 vs 호스트(로비行)인지, 결과창 복귀처=메뉴 vs 로비 분기인지 — P7 플랜이 P6-A 셸보다 먼저 작성돼 양쪽 문서에 답이 없다. 플랜 단계에서 반드시 질문.

[산출물 — 플랜 문서 §4 구현 순서 준수]
① Steam OnlineSubsystem 설정+UFPSRSessionSubsystem(Host/Find/Join/초대) ② L_Lobby+LobbyGameMode(플레이어 목록/시작) ③ Seamless travel 골격(TransitionMap, GetSeamlessTravelActorList — 연결/PS 유지) ④ 보스 연결(U3 베이스 — **AFPSRBossBox 신규 작성 금지**, 플랜 §3-5는 U3가 대체) ⑤ 전멸→Wiped→로비 복귀(U2 판정 재사용) ⑥ 런 리셋(로비 입장 시 XP/레벨/카드/무기/프리즈 상태 풀 초기화 — §6 확정) ⑦ 콘텐츠(로비 맵/UI) ⑧ 2-PC Steam E2E.

[함정/주의]
- app id 480(스페이스워)=공용 — 같은 480을 쓰는 타인 세션과 충돌 가능, 소인원 테스트로 한정(플랜 §3-1).
- Steam 테스트=클라이언트 실행 상태에서 패키지 빌드 2-PC. PIE 폴백=세션 없이 트래블만 부분 검증(플랜 §4 팁).
- Seamless travel 불안정 시 non-seamless 폴백 플랜 준비(플랜 §6).
- 호스트 마이그레이션 없음=호스트 종료=세션 종료(확정) — 구현하지 마라.
- 보스 콜리전 정합 재확인(플랜 §6 "가장 흔한 함정").
- 승리 흐름 확장: U3의 OnDeath 훅을 "결과 표시→ServerTravel(Lobby)"로 연장(EndRun 셸과 양립 — 플랜과 P6-A 셸의 접합부를 플랜 단계에서 설계).

[검증] 단계별 빌드+스모크, 최종 2-PC Steam E2E(초대→로비→인게임→미션/카드→보스→승/패→로비 복귀→재시작) → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U12 — UI/필 후속 묶음

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽고 별도 worktree에서 진행(git worktree add ../FPSR-<키워드> -b <브랜치> main). main 머지는 빌드+스모크 통과·PR·사용자 승인 후.
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U12를 진행한다.
읽을 SSOT: Docs/SSOT/PlayerFeel.md §2-14, Docs/SSOT/CombatWeaponCard.md §2-4-1·§2-4-2, Docs/P4-C_WeaponContent_SpecSheet.md §4, 메모리 [[vibeue-mcp-capabilities]].

- 브랜치: phase/p4d-ui-followups 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현(위젯 C++)/콘텐츠는 VibeUE 또는 사용자 / Opus 검증

[산출물 — 순서 고정]
1. 동적 스프레드 크로스헤어(원 레티클): **V3 기본 크로스헤어를 베이스로 확장**(V3는 정적+ADS 토글 완료). 발사 랜덤(SpreadDegrees+VRandCone)은 이미 구현됨 — **시각만 신규**: GetCurrentBloom()(FireComponent.h:32-33)+GetResolvedStats().SpreadDegrees 읽어 원 크기 변조 + 무기별 크로스헤어(샷건 원 등). 발사 로직 중복 구현 금지(스펙시트 §4). V3 위젯을 새로 만들지 말고 확장.
2. 히트마커 최종 연출 재확인 — 크로스헤어(V3/동적) 작업 **후**(P4-D 이월 순서 고정).
3. 카드 UI 소속 무기 표시: UFPSRWeaponDataAsset.Icon 필드 신설 + UFPSRCardEntryWidget에 FFPSRCardDraw.TargetWeapon 바인딩(아이콘+무기명). TargetWeapon=서버 세팅·클라 위조 불가 구조 유지.

[차징 게이지 HUD — 양자택일을 플랜에서 결정]
차징 게이지는 U13의 "서버 차징 시작/종료 클라 notify"(ChargeLaser Finding A 해소·빔 VFX와 같은 신호)를 쓰는 게 적절하다(PROGRESS 확정 — "같은 신호"). 선택지: (a) notify 배선을 이 유닛으로 당겨 게이지까지 완성 (b) 게이지를 U13으로 이월. 로컬 추정으로 임시 구현하지 마라(U13에서 재배선됨 — 임시 구조 금지).

[함정/주의]
- VibeUE 한계: 위젯바인딩 변수 getter는 컴파일 전 생성 불가(P6-A 교훈 — PreConstruct 파라미터화 회피, per-WBP 베이크), FText=Conv_StringToText.
- fragment 카드=카테고리 라벨 규칙 유지(등급/수치 표시 금지).

[검증] 빌드+스모크 → PIE: 샷건/바주카 크로스헤어 원 확대·축소, 카드 UI 무기 아이콘, (선택 시) 차징 게이지 → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U13 — VFX/오디오 배선

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽고 별도 worktree에서 진행(git worktree add ../FPSR-<키워드> -b <브랜치> main). main 머지는 빌드+스모크 통과·PR·사용자 승인 후.
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U13을 진행한다.
읽을 SSOT: Docs/SSOT/PlayerFeel.md §2-14, Docs/SSOT/Performance.md §5(코스메틱=GMS/Cue 계약), Docs/SSOT/Enemy.md §2-10.

[선행 확인] U8(GMS) 머지 권장(소프트 선행) — 히트/사망 코스메틱은 GMS 경유가 §5 계약. U12에서 차징 게이지 (a)/(b) 어느 쪽을 택했는지 PROGRESS 확인.

- 브랜치: phase/p7-vfx-audio 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증. 에셋: Infima 팩 Effects/Audio 우선 재사용(PS_*=Cascade — UE5.7 동작 확인, 필요 시 Niagara 전환).

[산출물]
1. 투사체 폭발 VFX 배선(로켓/유탄 — 코드 훅, 콘텐츠 가이드 §4 이월).
2. ChargeLaser 빔 VFX + **Finding A 해소**: 서버 차징 시작/종료 클라 notify 신설(빔 VFX와 동일 신호 — PROGRESS 확정 해법). 원격 클라 코스메틱 레이스(반동 램프/게이트)가 이 notify로 정리되는지 확인. 데미지는 이미 서버권위 — 재설계 금지.
3. 핑/Gibs: GMS(U8) 경유 경량 코스메틱(§5 — 복제 액터 상태 금지, Gibs=과도 연산 금지 §2-14).
4. 풀 사각 오디오 폴리시(V1 최소판 위에 에셋/믹스 확장) + 히트스캔 트레이서(비복제 코스메틱).

[함정/주의 — 알려진 수용 설계, "버그"로 고치지 마라]
- 관통(MaxPenetration>1)+ExplosiveRounds = 탄착점 1회만 폭발(설계 수용, Codex P2 문서화됨) — 폭발 VFX도 탄착점 1회만.
- ExplosiveRounds 적 직격 히트마커 2회(직격+스플래시) = 알려진 폴리시.
- ChargeLaser 차징 중 프리즈 = 타이머 계속+데미지만 스킵(의도적 단순화) — PauseTimer로 "고치지" 마라.

[검증] 빌드+스모크 → PIE(+2-client에서 Finding A 해소 확인: 원격 클라 차징 표시/빔 정합) → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U14 — 임시값 원복 + §8 플레이스홀더 전환 + 폴리시/패키지 빌드

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽고 별도 worktree에서 진행(git worktree add ../FPSR-<키워드> -b <브랜치> main). main 머지는 빌드+스모크 통과·PR·사용자 승인 후.
Game.md + PROGRESS.md + Docs/SSOT/Roadmap.md §8(플레이스홀더 인벤토리) 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U14를 진행한다. 메모리 [[p4a-temp-test-values]] 원복 의무 이행 유닛이다.

[선행 확인] U4(보스)·U11(멀티 E2E) 완료 — 압축 스케줄 값은 E2E 검증까지 쓰고 원복하는 게 확정 운영.

- 브랜치: phase/p7-polish 분기 (§6-7). 원복은 content 커밋으로 분리.
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증

[산출물]
1. **임시값 프로덕션 원복(사용자에게 노티 후)**: DA_RunSchedule.MissionWindows 윈도우 시간(테스트 압축값 예 50~120/240~300s) → 프로덕션 미션 300/600/900s, BossTime 300s → 1200s(≈20분 런). 원복 후 풀 길이 런 1회 PIE.
2. **§8 인벤토리 전수 전환**: 발사/근접 DrawDebug → #if !UE_BUILD_SHIPPING 게이트(또는 VFX 대체 확인) / FPSR.* 콘솔 커맨드 shipping 제외 / AFPSRXPPickup의 ConstructorHelpers 스피어 제거(§6-2 하드코딩 금지 위반 잔존 — DA/BP 참조로) / 적 큐브·FP팔 등 잔여 플레이스홀더 메시 상태 점검(교체 에셋 없으면 목록화만).
3. **Roadmap §7-3 P7 폴리시**: CommonUI 폴리시(입력 라우팅/패드), 오디오/이펙트 폴리시 잔여(U13과 분담 — U13=배선, 여기=마감), Insights 최종 측정, README, **패키지 빌드**(Win64 Shipping까지).

[함정/주의]
- 원복 커밋 전 반드시 사용자 노티(메모리 규칙 — 노티·원복 의무).
- shipping 게이트 후 스모크/패키지에서 디버그 의존 코드 컴파일 깨짐 주의(에디터 전용 분리).
- 패키지 빌드에서 LFS 에셋(Infima 팩) 쿠킹 시간/용량 — 미사용 폴더(Demo 등) 쿠킹 제외 설정 검토.

[검증] 빌드+스모크+패키지 빌드 성공+패키지 실행 E2E(메뉴→런→보스→결과) → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U15 — 1P 무기 애니메이션 시스템 (모듈러 + 노리쇠)

```
⚠️[병렬 작업 시] 다중 세션이면 이 문서 §B-2(병렬 가이드)·§B-3(검토·머지) 먼저 읽고 별도 worktree에서 진행(git worktree add ../FPSR-<키워드> -b <브랜치> main). main 머지는 빌드+스모크 통과·PR·사용자 승인 후.
Game.md + PROGRESS.md + Docs/V0_WeaponVisual_UserContent_Guide.md(V0 베이스/스켈 경로) 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U15를 진행한다.
읽을 SSOT: Docs/SSOT/PlayerFeel.md §2-9(Separated Arms)·§2-14, Docs/SSOT/CombatWeaponCard.md §2-4-2(사격감).

[목표] V0는 통합 스태틱 메시 + 팔 애님(게이트 검증용)으로 마감했다. 이 유닛은 Infima 팩이 완전 지원하는 **모듈러 SK 무기 + 노리쇠(Bolt) 본 + 팔↔무기 이중 스켈레탈 동기 애님**으로 무기 피델리티를 올린다(A_FP_WEP_*_Fire_Bolt/Reload 등).

[착수 조건 — U1 게이트 결과로 우선순위 확정]
- U1 손맛 판정 ①(사격 손맛)이 **불합격**이면 이 유닛이 §2-4 사격 보강의 일부로 2차 트랙 최우선.
- **합격**(스태틱+팔 애님으로 손맛 충분)이면 피델리티 폴리시로 후순위(U12 이후). PROGRESS에서 U1 판정 확인 후 착수, 미정이면 사용자에게 우선순위 질문.

- 브랜치: phase/p6-weapon-anim 분기 (§6-7)
- 플랜 우선 → 승인 후 Haiku 구현 / Opus 검증

[산출물]
1. SK 무기 경로 활성화: V0가 남겨둔 WeaponMesh1P(스켈) 사용 + 무기 AnimBP(노리쇠/재장전 몽타주).
2. 모듈러 부품 동적 부착: DA 부품 목록(배럴/탄창 등) → 소켓 부착(에셋 경로 하드코딩 금지, DA 데이터).
3. 팔↔무기 이중 스켈 동기: 발사/장전 시 팔 AnimBP와 무기 AnimBP 동기 재생.
4. (선택) 무기별 부착 오프셋 DA 필드 — V0 후속 ③ 흡수.

[함정/주의]
- 게임플레이 무영향: 트레이스/탄도/데미지/케이던스는 V0와 동일하게 카메라 뷰포인트 기준 유지. 애님은 코스메틱 전용(머즐 소켓=활성 메시).
- 재장전 애님 길이와 ReloadTime 스탯 정합(애님이 스탯보다 길면 무기 사용 가능 타이밍 어긋남) — 애님은 ReloadTime에 맞춰 스케일/동기.
- 전역 프리즈(§2-2) 중 몽타주 정지.
- Demo/Mannequin 폴더 사용 금지. 에셋 경로 하드코딩 금지(§6-2).
- 멀티(타 플레이어가 보는 3P 무기 애님)는 별도(U11 전 3P 메시와 함께) — 이번엔 1P만.

[검증] 빌드+스모크 → PIE: 발사 시 노리쇠 왕복, 재장전 애님↔스탯 정합, 무기별 부품 정확 부착, 게임플레이 수치 무회귀 → codex-review.ps1 -Base main → PROGRESS 갱신+✅+사용자 콘텐츠 동반 커밋 질문+--no-ff 머지.
```

---

## §D 문서 정합 메모 (후속 정리 대상 — 발견: 2026-06-13 전수 분석)

| # | 이슈 | 위치 | 내용 |
|---|---|---|---|
| 1 | **"P7" 명칭 충돌** | `Docs/SSOT/Roadmap.md` §7-3 vs `Docs/P7-MultiplayerLoop_Plan.md` | Roadmap의 P7=폴리시/빌드(U14), 플랜 문서의 P7=멀티 루프(U11). 멀티 루프는 Roadmap상 P5 행(4인 협동+세션) 영역. 본 문서는 유닛 번호(U11/U14)로 구분 — Roadmap 개정 시 명칭 정리 권장 |
| 2 | **FF 10% stale** | `Docs/SSOT/Roadmap.md` §7-3 P5 행, PROGRESS 백로그 D1 행 | 확정값=50%(`FriendlyFireDamageScale=0.5`). 10%를 인용하지 말 것 |
| 3 | **Fragment 훅 stale** | `PROGRESS.md` "Fragment 후속(미완)" 구절 | `ModifyChargeTime`/`OnProjectileSpawn` 훅은 A3a/A3b에서 구현 완료 — U6에서 재구현 금지 |
| 4 | **origin 미푸시/.uproject 경고 해소** | `PROGRESS.md` P5·P6-A 절 | 실측(2026-06-13): origin 동기화 완료, `.uproject` VibeUE Optional 커밋(`15d4e34`) 완료 — 경고 stale |
| 5 | **메뉴↔로비 설계 공백** | P6-A 셸 vs P7 플랜 | P7 플랜(06-11)이 P6-A 셸(06-12)보다 먼저 작성됨. U11 착수 시 사용자 결정 필요(U11 프롬프트에 내장) |
