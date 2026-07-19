# TaskPrompts_Master — 남은 작업 의존성 DAG + 유닛 실행 프롬프트

> **목적**: 남은 로드맵을 의존성 기준 유닛으로 분해하고, 각 유닛을 **새 세션에 복붙으로 시킬 수 있는 실행 프롬프트**로 제공한다.
> **갱신 규칙**: 유닛 완료 시 §B 라이브 표를 완료 아카이브 인덱스로 옮기고 `PROGRESS.md` 완료 이력 갱신. 설계가 바뀌면 해당 `Docs/SSOT/*.md` 먼저 고치고 이 문서의 프롬프트를 동기화한다.
> **프롬프트 사용법**: §C의 코드블록/포인터 문서를 새 세션에 붙여넣는다. 모든 프롬프트는 플랜모드 우선·HIGH_RISK 승인 원칙(CLAUDE.md)을 전제한다. **각 §C 프롬프트는 그 유닛 작업에 자족적**이며, 어느 클론에서 돌릴지(2-클론 배정)는 사용자 오케스트레이션 영역이라 문서에 넣지 않는다.
> **이 문서를 관리하는 역할** = 프롬프트 매니저(`Docs/ProjectPromptManager.md`, 호출 `/pm`). 계획 작성·완료 검증·DAG 최신화를 그 페르소나가 수행한다.
>
> **📌 재앵커 패스 완료 (2026-07-13)**: 완료 §B/§C(V0~U20 + 2차 트랙 + 다중맵 U 아크 + 반동 아크)는 **git 히스토리 + 아카이브 스냅샷**(`Docs/Archive/prompts/TaskPrompts_Master_pretrim-20260713.md`)으로 트림하고, 현재/다음 아크(Synty 아트 파일럿·Synty 전체교체·애니 콘텐츠 저작·메타 확장·RepGraph 후속·성능 정량)를 **라이브 아크 유닛(U21~U25)으로 재앵커**했다. **진행/다음 작업의 진실원본 = `PROGRESS.md`(현재 상태) + 아래 §B 라이브 표 + §E ConsultLoop 인입표**. **다음 착수 = U21 Synty 아트 파일럿 게이트**.

---

## §A 현황 스냅샷 (2026-07-19)

### 완료 (main 머지 — 상세 = `PROGRESS.md` 완료 이력 + `git log`)
- **코드 로드맵 V0~U20 전부 머지 완료** — 1P 슬라이스·사격감·적 스웜(풀500·플로우필드)·공유XP/카드 v2·런디렉터·무기/Fragment·FF·보스 스캐폴드+콘텐츠·로비/세션/트래블·DBNO·원거리 적·GMS·SaveGame 인프라·크로스헤어/설정·통합 애니 코드 인프라. 요약 = §B 완료 아카이브 인덱스.
- **최근 2아크 종결** — 다중맵 U 연속필드(P-0~P-H, `34b5eea`)·반동 CrystalRecoil(P0~P4, `6f1a981`).
- **W2 전수 그라인딩 3아크 종결(2026-07-18~19)** — 유닛 번호를 안 붙인 품질/정확성 감사축이라 아래 라이브 표엔 없다(U 표는 기능 유닛 전용). 상세 = `PROGRESS.md` §⑦⑧⑨.
  - **W2-A 코드 품질**(`f940ed7e`) + 후속 보류정리 4건(`637a56f0`) — 주석 드리프트·§6-2 하드코딩·dead code·태그 SSOT·데이터드리븐.
  - **W2-B 런타임 정확성**(`73f4caa7`) — 원시 14 → 적대검증 생존 9. **P1 = 런 GameMode `Logout` 부재**(마지막 생존자 이탈 시 패배 미선언 + 카드 프리즈 영구 고착 = 시핑 복구불가 데드락). 사용자 PIE 2인 검증 통과.
  - **W2-C 메뉴/세션 서버권위**(`1077eb50`) — 세션 자폭(배포 영향)·클라 Play 하드크래시·메뉴 GM `bUseSeamlessTravel` 누락·GameFlow 트래블 가드. ⚠️**미검증 2건은 Steam 2-PC 때 확인**(⑨-1 세션 자폭은 PIE로 검증 불가 = NULL OSS라 조인 분기 미도달).
- **검증 게이트** — U1 재미 게이트(§7-5 G1) + V2 2-client = ✅합격(2026-06-30). ⚠️**§5 적500 정량 측정은 미실시(보류)** → 라이브 U25로 승격(아래).

### 열린 유닛 / 진행
- **U21 Synty 아트 파일럿 = ✅완료**(2026-07-18 사용자 판정): S1 단차 walkability·S3 셀 아웃라인×VAT 정합·S4 성능 실측 전부 통과 + **SRS 아웃라인 거리 헤이즈 수정**. 아트 방향 = 사전확정 셀/툰 피벗(메모리 `synty-anime-cel-art-pivot`)이 **파일럿으로 검증됨** → U22 게이트 해제. ✅**하위결정 2건도 확정됨**(2026-07-18, 근거 `PROGRESS.md` §⑦ 머리말): ① 무기 백본 = **Synty Military 전환**(→ 후에 SF 무기로 리스킨) ② 캐릭터 애님 = **3인칭 Blu · 1인칭 PWAS**(손저작 AnimBP 대체 → U15 1P·U19 3P 계획이 이 파이프라인에 흡수, U20 적 VAT만 U22 적교체 후 별도 베이크). *(종전 이 줄은 "미확정"이라 적혀 있어 PROGRESS와 모순 — 2026-07-19 정정.)*
- **다음 프론티어 = U22 Synty 전체교체**(게이트 해제). content-pending = U12(잔여)·U15/U19/U20(애니 콘텐츠, HOLD — 이제 U22에 종속). 열린 출시 유닛 = U13·U14. (전부 §B 라이브 표.)

### 임시 테스트값 (미원복 — U14에서 원복)
- `DA_RunSchedule.MissionWindows` 윈도우 시간(테스트 압축값) + `BossTime 300s` → 프로덕션 = 미션 300/600/900s·보스 1200s(≈20분 런). 메모리 `p4a-temp-test-values`.

---

## §B 의존성 DAG

### 완료 아카이브 (V0~U20 + 아크 — 프롬프트 원본 = git 히스토리)
> 완료 유닛의 **실행 프롬프트 원본**은 트림 전 스냅샷 `Docs/Archive/prompts/TaskPrompts_Master_pretrim-20260713.md` + git(`git show <머지해시>:Docs/TaskPrompts_Master.md`)에 보존. 완료 프롬프트는 재사용 안 됨(stale) — 재구현 시 참조만.

| 유닛 | 머지/완료 | 유닛 | 머지/완료 |
|---|---|---|---|
| V0 무기 비주얼(Infima) | 2026-06-14 `phase/p6-weapon-visuals` | U6 Fragment 마무리 | `8d71b1c` |
| V1 사각 오디오(최소) | `da76144` | U7 플로우필드 높이+2층 | `8d8e232` |
| V3 크로스헤어(정적) | 2026-06-15 `phase/p4d-crosshair` | U8 GMS 재구현 | 2026-07-02 `phase/infra-gms` |
| U16 스핀업 LMG | `c0b28a9` | U9 DBNO 수동부활 | `e38dfbe` |
| U2 패배 배선 | `3506da9` | U10 메타 SaveGame 인프라 | 2026-07-02 `phase/p6-savegame` |
| U3a 약점 데미지 | `89b535b` | U11b 멀티 보스승리 E2E | 2026-07-01 사용자 2-PC Steam |
| U18a/b/c 카드 v2 | `7a85773`/`78b1bb5`/`f02536a` | U17 설정 + U12 진실크로스헤어 | `36cf3d4` |
| U3 보스 스캐폴드+승리 | `0181ed0` | 통합 애니 패스 코드(U15/U19/U20 인프라) | `e651cce` |
| U4 보스 콘텐츠+PIE | `71c9bde` | **아크** 다중맵 U 연속필드(P-0~P-H) | `34b5eea` |
| U11a 로비/세션/트래블 | `b3b364e` | **아크** 반동 CrystalRecoil(P0~P4) | `6f1a981` |
| W1 전체검증(1·2·3차) | `b2c55d3` | (기타) 룸스폰/밸런스2·오디오설정·Data Editor P0 | `d285c69`·`747a9b2`·`57270c5` |
| U1 재미게이트 + V2 | 2026-06-30 사용자 판정 + `e38dfbe` | U5 원거리 적 | `cd7de43` |

**완료 경로 요약**: V0~V3/U16 선행 → 게임루프 닫기(U2 패배·U3a 약점·U18 카드 v2·U3/U4 보스 승리·U11a 로비) → W1 전수검증 → U1/V2 게이트 합격 → 2차 트랙(U5·U6·U7·U8·U9·U10·U11b·U17/U12) → 통합 애니 코드 인프라 → 다중맵 U 아크·반동 아크. (ASCII 실행순서 원본 = 아카이브 스냅샷.)

### 라이브/다음 아크 유닛 (U21+ — 재앵커 2026-07-13)
> 완료 U1~U20과 물리 분리된 라이브 표. **의존성만 표기**(선행/차단/HOLD 게이트 = 세션이 착수 순서를 아는 데 필요한 전부). 실행 프롬프트는 §C.

| ID | 유닛 | 구분 | 선행/게이트 | 상태 | 실행 프롬프트 |
|---|---|---|---|---|---|
| **U21** | Synty 아트 파일럿 게이트 (셀/툰 렌더러·무기모듈·Blu팔+PWAS·스웜 200-300 perf, throwaway). **산출에 아트 정체성 결정 포함** = Infima 유지 vs Synty Military 전환 vs Blu+PWAS 손저작 AnimBP 대체 | 에셋/PIE | — | ✅**완료**(2026-07-18 사용자 판정: S1/S3/S4 통과·perf OK·SRS 아웃라인 헤이즈 수정. 하위결정 2건은 U22 착수 시 확정) | `Docs/SyntyArtPilot_Scoped_ResumePrompt.md` + `Docs/SyntyArtPilot_S1_CityBuildGuide.md` |
| **U22** | Synty Path A 에셋 전체교체 (환경 3맵·무기 Synty Military·캐릭터 Blu·적 저코스트 VAT·UI/VFX) | 에셋 | U21 ✅통과 | **다음 프론티어**(게이트 해제) | ⚠️ 프롬프트 **재작성 필요**(구 `AssetReplacement_Synty_ResumePrompt.md` = 폐기본) |
| **U15/U19/U20** | 통합 애니 콘텐츠 저작 (A 1P무기·B 3P팀원·C 적VAT+보스스켈). 코드 인프라 ✅, 콘텐츠 미저작 | 콘텐츠 | **⚠️HOLD**: U21 아트정체성 결정 (U20 VAT 베이크 = U22 적교체 후) | HOLD | `Docs/AnimationPass_ContentGuide.md`(자족 A/B/C) |
| **U12** | UI/필 잔여 (카드 아이콘 콘텐츠 · 무기별 팔 AnimBP=U15 흡수 · 서버권위 bloom=장기백로그) | 콘텐츠 | — | 잔여 hold | §C U12(잔여) |
| **U23** | 메타 프로그레션 콘텐츠 (P0-③): U10 인프라 위 실 메타필드/재화/언락트리 + §D6 세이브 테스트갭 2건 | C++/콘텐츠 | U10 ✅ (스키마 P0-③ 확정 선행) | 대기 | §C U23(신규) |
| **U24** | RepGraph 공간그리드 relevancy: NetCull(Tier-0)→per-slot footprint relevancy. NetCull 튜너블=이 유닛 선행/대체 한계 | C++ | 다중맵 U 아크 ✅ | 후속 페이즈 | §C U24(신규) |
| **U25** | §5 성능 정량 게이트 (적500 Insights/NetProfiler·호스트 하드캡·다중맵 상주메모리·RepGraph/Iris 판정) | 검증 게이트 | U22·U20콘텐츠·U24 이후(perf 이동분) | 보류 해제 대기 | §C U25(신규) |
| **U13** | VFX/오디오 배선 (폭발·빔·핑/Gibs·풀 사각오디오). GMS 통복사 수정 + 원거리 경고 생산자(B1)·원격탄 시각예측(A3) 귀속 | C++/콘텐츠 | U8 소프트 | 열림 | §C U13 |
| **U14** | 임시값 원복 + §8 플레이스홀더 전환 + 폴리시/패키지 빌드 (최종/출시) | 혼합 | U22·U13·U25 | 열림 | §C U14 |

**핵심 DAG 통찰**:
- **애니 콘텐츠 ⟂ Synty 피벗 얽힘(HOLD 근거)**: (A) 무기 백본 Infima→Synty Military 전환 가능, (C) 적 VAT 베이크는 U22 적교체 이후라야 재베이크 방지, (B) Blu+PWAS가 손저작 AnimBP 대체 가능 → **U21 파일럿 산출의 아트정체성 결정 전엔 U15/U19/U20 콘텐츠 착수 금지**(중복 저작 유도 차단, PROGRESS 백로그 경고).
- **U24 RepGraph** = 다중맵 U 아크가 명시적으로 "별도 후속 페이즈"로 미룬 것(`Docs/Review/20260707-plan-continuous-field-arch.md` §2-5·§4 D3). 클라 seam pop-in = 수용된 Tier-0 한계.
- **U25 §5 성능 정량** = U1에서 보류된 정량 측정을 독립 게이트로 승격 — Synty(U22)·적 VAT(U20)·RepGraph(U24)가 전부 perf를 이동시키므로 U14 출시 폴리시에 묻지 않고 그 뒤 게이트로 박음.

**은닉 백로그 방지 — PROGRESS 백로그 → 귀속 유닛 매핑** (PROGRESS엔 있는데 DAG엔 없는 항목 0 보장):
- RepGraph spatial-grid relevancy → **U24** · NetCull 튜너블 → U24 scope
- 애니메이션 콘텐츠 저작 → **U15/U19/U20 (HOLD)**
- 성능 정량 §5 → **U25**
- 반동 잔여(ADS 확산배수·예측거부 heat 드리프트·레거시 블룸 orphaned·Shotgun/Bazooka 고정확산·ChargeLaser base-only) → **장기백로그**(PvE 코스메틱, 의도·회귀 아님)
- 원거리 적 A3(원격탄 시각예측)·B1(원거리 경고 생산자 `ClientNotifyRangedTarget` 배선) → **U13**

### 장기 백로그 (유닛화 보류 — 기록만)
- **다중맵 심리스**(`RunFlow §2-1` · `Concept §1-C-9` · `Roadmap §8`): 맵 4방향 거대문 부수기 → 심리스 다음 맵. **#3 아키텍처 = 설계 수렴(§E G1-G5)** → **Tier 0 = U 연속필드로 구현·머지 완료**(`34b5eea`, `Architecture §4-1`). RepGraph 후속 = **U24**(라이브). Tier 1(예산 게임필)·Tier 2(rally·인센티브) = 후속(§E G3/G4). 에셋 전체교체(Synty) = **U21/U22**(라이브).
- **보스 2페이즈 본격화**: U3/U4는 "체력만 박스" 스캐폴드(`Docs/Archive/plans/P7-MultiplayerLoop_Plan.md` 확정). 실제 보스(StateTree 패턴·페이즈 전환)는 별도 유닛으로 재기획.
- **서버권위 블룸(원격 클라)**: `CurrentBloom`이 owning-client-local(비복제)라 서버에서 원격 클라의 발사체/히트스캔이 bloom=0(base spread) — HUD는 블룸 콘 표시(코스메틱, PvE 코옵서 익스플로잇 아님). 정식 수정=서버권위/복제 블룸 히트스캔+발사체 공동 적용. Codex U12 머지게이트 P2 이월(2026-07-03), 인코드 주석=`FPSRGA_WeaponFire_Projectile.cpp:91`. (U12 잔여 → 이 백로그로 귀속.)
- **개발 툴 백로그**: [`Docs/ToolingBacklog.md`](ToolingBacklog.md)(living, 📋/🔨/✅) — 로드맵 전 구간 × 툴 매트릭스(65툴 phase 배치 + seam 9 + 관통 테마 5). 착수 시 별도 phase 유닛(§B-3).

---

## §B-3 작업·검토·머지 프로토콜 (트랙 내 순차 · 2트랙 병렬)

> **트랙 내부는 한 유닛씩 순차**, **두 트랙은 별도 클론에서 병렬**(2026-06-16 사용자 결정, 2026-07-13 재확인 = 지금도 클론 2개로 병렬 작업 중). 충돌점=공유파일. 각 클론은 메인에서 phase 브랜치를 분기해 작업하고, 검증 통과 후 머지한 다음 같은 트랙의 다음 유닛으로 넘어간다. **어느 유닛을 어느 클론에서 돌릴지는 사용자가 산정해 각 클론 폴더에 지정**(문서/프롬프트엔 클론 배정 메타를 넣지 않음).

### 순차 흐름
```
1. main에서 phase/<유닛-브랜치> 분기 (§6-7)
2. 플랜 확정(사용자 승인) → 구현(Sonnet 위임)/검증(Opus 직접)
3. 자체 빌드(-WaitMutex) + 헤드리스 스모크 통과   ← 이게 "완료"의 정의. 미검증 머지 금지
4. 사용자 검토: 코드 = diff 또는 Codex 머지게이트(codex-review.ps1 -Base main) / 콘텐츠 = PIE
5. 승인 → --no-ff main 머지 + PROGRESS·TaskPrompts §B 갱신 + 콘텐츠 동반 커밋 질문 → 브랜치 삭제
6. 다음 유닛으로 (1부터 반복)
```

### 핵심 원칙
- **머지 전 검토**: main에 들어온 뒤 문제를 발견하면 되돌리기 비용이 크다. 빌드+스모크 통과 + 사용자 검토를 머지 **앞**에 둔다.
- **순서**: 의존성을 따른다(라이브 표의 선행/게이트).
- **문서/PROGRESS 갱신은 해당 유닛 작업·머지에 포함**(별도 브랜치에 섞지 않음).

---

## §C 유닛별 실행 프롬프트

> 공통 전제(프롬프트에도 내장): `Game.md`+`PROGRESS.md` 선행 독해 → 해당 도메인 SSOT만 추가 독해(§0-1 라우팅) / `phase/` 브랜치 분기(§6-7) / 플랜모드 우선·승인 후 진행 / 구현=Sonnet 위임·검증=Opus 직접(§6-5) / **완료 = 자체 빌드(`-WaitMutex`)+헤드리스 스모크 통과 후 사용자 검토·머지(§B-3). 미검증 머지 금지** / Codex 머지게이트(`Scripts/codex-review.ps1 -Base main`) / 승인 후 PROGRESS 갱신+`--no-ff` 머지+브랜치 삭제+콘텐츠 동반 커밋 여부 질문.

### 완료 유닛 아카이브 인덱스 (V0~U20 — 프롬프트 본문 트림됨)
완료 유닛의 실행 프롬프트 본문은 **재사용 안 됨(stale)**이라 §C에서 제거하고 아래에 보존한다:
- **스냅샷**: `Docs/Archive/prompts/TaskPrompts_Master_pretrim-20260713.md`(트림 직전 전문).
- **git 복구**: `git show <머지해시>:Docs/TaskPrompts_Master.md`(해시 = 위 §B 완료 아카이브 표).
- **완료 상세**: `PROGRESS.md` 완료 이력 + `git log <해시>`.

재구현/참조가 필요하면 위 스냅샷을 열고, 최신 코드 사실은 항상 grep으로 재확인(라인 드리프트 가능).

---

### — 라이브 유닛 (U21+) —

### U21 — Synty 아트 파일럿 게이트

> **실행 프롬프트 원본(자족) = [`Docs/SyntyArtPilot_Scoped_ResumePrompt.md`](SyntyArtPilot_Scoped_ResumePrompt.md)**(+ 맵 작업 시 [`Docs/SyntyArtPilot_S1_CityBuildGuide.md`](SyntyArtPilot_S1_CityBuildGuide.md)) — 그 문서를 새 세션에 붙여넣는다(여기 중복 안 함, SSOT 분리).
> ⚠️ **구 `Docs/SyntyArtPilot_ResumePrompt.md` = 폐기본**(SRS를 "최후 폴백 유료옵션"이라 하는 등 SSOT와 모순) — 읽지 말 것. **Scoped 판이 최신**(스코프 축소 = 단일 CyberCity 화이트박스 + 라이플 + 스웜 200 + SRS 셀 실측).

- **목적**: 대량 채택·콘텐츠 저작 **전에** 셀/툰 아트 스택이 제1원리(적 200-300 싸게)를 지키는지 + 룩을 실측/육안 게이트. throwaway 파일럿, 통과분만 LFS 커밋.
- **범위**: Step A 무기 모듈 조립(U15 검증)·Step B FP팔+PWAS·Step C 셀/툰 렌더러 육안(무료 우선)·Step D 스웜 200-300 perf(아웃라인 고정비용 실증).
- **어디서**: 활성 코드 클론(에셋·PIE·LFS 대용량). SyntyPass 보유.
- **착수 게이트**: 다음 착수 유닛(선행 없음). **산출에 아트 정체성 결정 필수** = Infima 유지 / Synty Military 전환 / Blu+PWAS 손저작 AnimBP 대체 — 이 결정이 U22·U15/U19/U20 HOLD 해제 조건.
- **완료 시**: 파일럿 수치·채택 렌더러·채택 에셋을 PM 보고 → PM이 SSOT 갱신(렌더러 확정: `Concept §1-C-9`·`Roadmap §8`) + 채택분 LFS 커밋.

### U22 — Synty Path A 에셋 전체교체

> ⚠️ **실행 프롬프트 = 재작성 필요.** 구 `Docs/AssetReplacement_Synty_ResumePrompt.md`는 **폐기본**(SSOT와 모순, 읽지 말 것). U22는 **U21 파일럿의 아트 정체성 결정이 나온 뒤** 그 결정을 반영해 새로 작성한다(결정 없이 착수 = 중복 저작).

- **목적**: 전체 셀/툰 통일 룩으로 아트 스택 개편(환경 3맵·무기 Synty Military 모듈+사이버 리스킨·캐릭터 Blu·적 저코스트 VAT·UI Synty Soldier HUD·VFX).
- **선행**: U21 파일럿 통과(렌더러·아트정체성 확정).
- **⚠️ 시퀀싱(핵심)**: **Step 3 적 교체 → U20 VAT 베이크 선행**(지금 Paragon으로 베이크하면 재베이크). 적 교체 완료 후 Paragon 미니언 제거.
- **함정**: U7 플로우필드 `BuildObstacleMask`=ECC_WorldStatic 다운트레이스 → Synty 임포트 콜리전 확인. Nanite OFF. 다중맵 상주 메모리 측정(맵 언로드 안 함). 통과 전 커밋 금지, 채택분만 LFS.
- **완료 시**: 파일럿/채택 목록 PM 보고 → PM이 `Roadmap §8` 확정 스택 기록.

### U15/U19/U20 — 통합 애니메이션 콘텐츠 저작 (⚠️HOLD)

> **실행 가이드(자족 A/B/C) = [`Docs/AnimationPass_ContentGuide.md`](AnimationPass_ContentGuide.md)** — 코드 인프라는 완료(`e651cce`), 이 유닛 = **콘텐츠 저작(에디터)**뿐.

- **⚠️HOLD 조건**: U21 파일럿의 아트 정체성 결정 대기. 도메인별 종속:
  - A(1P 무기): 무기 백본 Infima 유지 vs Synty Military 전환에 종속(모듈 파츠/스켈 구조 다름).
  - B(3P 팀원): 캐릭터 Blu+PWAS 채택 여부에 종속(손저작 BroBot AnimBP 대체 가능).
  - C(적 VAT): **U22 Step3 적 교체 이후** 다중 시퀀스 베이크(재베이크 방지). 엘리트 Siege=VAT 유지, 보스만 스켈.
- **착수 전**: U21 결과가 "Infima 유지 + BroBot AnimBP + Paragon VAT"면 가이드 그대로, "Synty 전환"이면 가이드의 에셋 대상만 교체 후 진행. **U21 결정 전 착수 = 중복 저작**(금지).
- **주의**: 에디터 편집 후 같은 세션 PIE=World Leak → 편집 후 에디터 재시작 후 PIE([[vibeue-buildgraph-pie-worldleak]]). 코드는 전부 null-safe·휴면이라 콘텐츠 채우기 전 무회귀.

### U23 — 메타 프로그레션 콘텐츠 (P0-③)

```
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U23을 진행한다.
읽을 SSOT: Docs/SSOT/RunFlow.md §2-11(메타 프로그레션·저장 정책), Docs/SSOT/Architecture.md §4-1(MetaProgression/ 폴더), Docs/SSOT/Workflow.md §6.

[작업] U10이 착지한 범용 버전드 SaveGame 인프라 위에 **실제 메타 스키마·콘텐츠**를 얹는다(P0-③). U10은 인프라+최소 스키마만 착지했고 실 메타필드는 "미커밋"으로 남겨둠 — 이 유닛이 그 공백을 메운다.

[선행/스코프 제약]
- U10 인프라(✅ main 머지): URogueliteSaveGame(USaveGame, SaveVersion+MigrateIfNeeded 버전사다리) / UFPSRSaveGameSubsystem(강제경유·per-player 로컬소유·async·프라이머리→백업→기본값 복구) / UFPSRSaveSettings(슬롯 config) / 카드 안정키 CardId / EndRun→ClientCommitMetaSave 배선 / ApplyMetaProgressionEffects(GAS 런시작 시임, 빈 본체).
- **P0-③ 스키마 확정이 선행**(재화 종류·언락트리·캐릭터·난이도 승급) — 미확정이면 플랜에서 사용자에게 질문. 지어내기 금지.

[산출물]
1. URogueliteSaveGame에 실 메타필드 추가(UPROPERTY(SaveGame)) + SaveVersion bump + MigrateIfNeeded에 새 버전 케이스. 인프라/스키마 분리 유지(필드 추가+버전 bump만으로 확장).
2. ApplyMetaProgressionEffects 실 본체(InitAbilityActorInfo 직후 메타 보너스 GE/스탯 적용). 실제 GE·어트리뷰트 배선.
3. 메타 획득/소비 경로: 런 결과(EndRun) → 재화 적립 → 로비/메뉴에서 언락 소비. per-player 로컬 소유(서버는 신호만) 유지.
4. 콘텐츠: 언락트리/재화 DataAsset, 메타 GE(경로 C++ 하드코딩 금지).

[§D6 세이브 테스트갭 2건 — 반드시 착지]
- ⓐ 손상 세이브 fallback 미테스트(현재 '부재'만 커버) → 손상 더미 세이브 → fallback 정책 검증 자동화 테스트.
- ⓑ 다운그레이드(신버전 세이브를 구버전 코드가 로드) 마이그레이션 방어 미테스트 → 방어 동작 자동화 테스트.
- 기존 IMPLEMENT_SIMPLE_AUTOMATION_TEST 패턴(Source/FPSRoguelite/Private/Tests/) 재사용.

[모델정책] 구현=Sonnet 위임 / 검증=Opus 직접. ⚠️ 세이브 소유권(per-player 로컬)·버전 마이그레이션·세션/트래블 대칭 = Opus 직접(하위모델 위임 금지, [[haiku-delegation-security-wiring]]).

[함정/주의]
- 메타=각 플레이어 로컬 계정 자산 — 협동에서 서버가 타인 세이브 소유/기록 금지(SaveManager 단일 경유).
- U17 GameUserSettings(로컬설정)·U18b WeaponUnlockMilestones(런레벨 언락, config)와 혼동 금지 — 메타 아님.
- 임시·미루기 스키마 금지 — 확정된 P0-③ 필드만 커밋, 미정 필드 지어내기 금지.

[검증] 빌드+스모크 + 세이브/로드/버전 마이그레이션 + 손상/다운그레이드 방어 자동화 테스트 → 사용자 PIE(런 완료→재화 적립→언락 소비→세션영속) → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U24 — RepGraph 공간그리드 relevancy (다중맵 U 아크 후속 페이즈)

```
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U24를 진행한다.
읽을 SSOT: Docs/SSOT/Performance.md §5(다중맵 예산·NetCull 재튜닝·RepGraph 별도), Docs/SSOT/Architecture.md §3-4(다중맵 심리스), Docs/SSOT/RunFlow.md §2-1.
근거 리포트: Docs/Review/20260707-plan-continuous-field-arch.md §2-2(요구8: NetCull/RepGraph)·§2-4(P-H)·§2-5(범위밖)·§4 D3(RISK 수용).

[작업] 다중맵 U 연속필드 아크(P-0~P-H, 34b5eea)가 **명시적으로 "별도 후속 페이즈"로 미룬** 프로덕션 relevancy 해법을 착지한다. 현 Tier-0 = NetCull(대칭 거리컬 교전버블, `NetCull` 균일 사이징)이라 플레이어가 seam 근처 접근 시 적이 **클라에서 팝인**(서버 추격은 심리스). RepGraph 공간 grid relevancy로 이를 해소한다.

[선행/현 상태]
- 다중맵 U 연속필드 = 구현·머지 완료(✅ 34b5eea). Tier-0 NetCull = per-slot footprint로 재튜닝됨(P-H, 1ebe3b2).
- NetCull 튜너블(NetCullWeaponRangeCm=10000·NetCullSeamMarginCm=4000)이 현 대체 한계 — 이 유닛이 그 위를 대체.

[산출물]
1. RepGraph(공간 grid relevancy) 도입: 적을 per-acquire NetCull 반경으로 재-bucket(리포트 §2-2 요구8·D3). 클라 seam pop-in 제거.
2. Performance §5 예산 재측정 반영(RepGraph 도입 전후 대역폭 PIE 실측).
3. SSOT 선갱신(원칙3): Performance §5(RepGraph 도입·NetCull 대체)·Architecture §3-4.

[모델정책] Opus 직접(복제·relevancy·서버권위·적500 성능경로 = 전 항목 카브아웃, [[haiku-delegation-security-wiring]]). 구현 위임 시에도 relevancy 계약·복제 배선은 Opus.

[함정/주의]
- 적 복제=Transform만(§5 계약) 유지 — RepGraph는 relevancy(누구에게 복제하나)만 바꾸고 복제 페이로드를 늘리지 말 것.
- 단일 연속필드/allocator/FrontId(U 아크 산출)와 충돌 없이 얹을 것 — MapId 개념은 이미 제거됨(U가 단일 그리드).
- Iris는 1순위 아님(§3 디폴트 OFF) — RepGraph로 충분한지 먼저 판정, 부족 시에만 Iris 평가.
- 범위 밖(리포트 §2-5): World Partition Data Layers, 프로시저럴 맵 생성.

[검증] 빌드+스모크 → 2-client 이상 PIE(seam 근처 적 팝인 제거 확인) + 대역폭 실측(NetProfiler, RepGraph 전후) → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지. **정량 프레임/대역폭 = U25 성능 게이트와 연계**.
```

### U25 — §5 성능 정량 게이트

```
Game.md + PROGRESS.md + Docs/SSOT/Performance.md §5·§5-1 전체를 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U25를 진행한다. 이 유닛은 코드 작성이 아니라 **판정/측정 게이트**다(U1 재미게이트와 동형) — 사용자 PIE 주도, 세션은 측정 세팅·체크리스트·기록·SSOT 반영 담당.

[배경] U1(2026-06-30 합격) 때 §5 적500 정량 측정은 **미실시 보류**로 남겼다(하드캡 잠정값 유지). Synty(U22)·적 VAT 콘텐츠(U20)·RepGraph(U24)가 전부 perf를 이동시키므로, 출시 폴리시(U14)에 묻지 않고 그 뒤 **독립 게이트**로 승격한다.

[선행 확인] U22(Synty 에셋)·U20(적 VAT 콘텐츠)·U24(RepGraph) 중 perf에 영향 준 유닛이 랜딩된 뒤 측정해야 유효. 미완이면 부분 측정 후 보고(무엇이 빠졌는지 명시).

[측정 항목 — §5]
- Unreal Insights + NetProfiler로 적 500 시나리오. **호스트(리슨서버)=최악 케이스 — 하드캡은 호스트 기준 확정**.
- 다중맵 상주 메모리(맵 언로드 안 함 — RunFlow §2-1): 3~4맵 동시 상주 시 메모리 견디는지.
- §5-1 Significance 티어(S0~S3) 실효성 + §5 1인칭 가독성 5지표(F6: 활성 200-300·relevant P90≤150·화면내 P50≤40/P90≤70·15m 즉시위협 P90≤25·시각 큐 동시≤3).
- 판정 순서: Push Model → RepGraph(U24 반영) → 부족 시 Iris(Beta) 평가. Iris 1순위 금지(§3 디폴트 OFF).

[산출물]
- 측정 수치 + 하드캡 확정값(잠정→확정) + RepGraph 효과 정량 + Iris 도입 여부 판정 → Docs/SSOT/Performance.md §5 갱신(SSOT 먼저 원칙).
- 미달 지표는 트리거: perf 재검토(§5)·스웜 아웃라인 게이팅·LOD 조정 등 후속 유닛 등재.

[완료 시] 판정 기록 커밋(문서) + TaskPrompts §B 갱신 + 하드캡 확정 보고 → U14 최종 폴리시/패키지 진입 가능.
```

### U12 — UI/필 잔여 (완료분 트림, 잔여만)

> **완료분**(진실 크로스헤어·동적 스프레드·히트마커) = U17 동반 머지 `36cf3d4`. 아래는 **잔여 콘텐츠/코스메틱**만.

- **카드 UI 소속 무기 표시**(콘텐츠): `UFPSRWeaponDataAsset.Icon` 필드 + `UFPSRCardEntryWidget`에 `FFPSRCardDraw.TargetWeapon` 바인딩(아이콘+무기명). TargetWeapon=서버 세팅·클라 위조 불가 유지.
- **무기별 팔 AnimBP** → **U15(애니 콘텐츠)로 흡수**(BlendByEnum 중복 구현 금지, `ArmsAnimInstanceClass` 재사용).
- **서버권위 bloom(원격 클라)** → **장기백로그**(PvE 코스메틱, 인코드 주석 `FPSRGA_WeaponFire_Projectile.cpp:91`).
- 함정: VibeUE 위젯바인딩 getter는 컴파일 후 / FText=Conv_StringToText / fragment 카드=카테고리 라벨(등급/수치 표시 금지).

### U13 — VFX/오디오 배선

```
Game.md + PROGRESS.md 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U13을 진행한다.
읽을 SSOT: Docs/SSOT/PlayerFeel.md §2-14, Docs/SSOT/Performance.md §5(코스메틱=GMS/Cue 계약), Docs/SSOT/Enemy.md §2-10.

[선행 확인] U8(GMS) 머지 권장(소프트 선행) — 히트/사망 코스메틱은 GMS 경유가 §5 계약. U12에서 차징 게이지 (a)/(b) 어느 쪽을 택했는지 PROGRESS 확인.

- 브랜치: phase/p7-vfx-audio 분기 (§6-7)
- 플랜 우선 → 승인 후 Sonnet 구현 / Opus 검증. 에셋: Infima 팩 Effects/Audio 우선 재사용(PS_*=Cascade — UE5.7 동작 확인, 필요 시 Niagara 전환).

[산출물]
1. 투사체 폭발 VFX 배선(로켓/유탄 — 코드 훅, 콘텐츠 가이드 §4 이월).
2. ChargeLaser 빔 VFX + **Finding A 해소**: 서버 차징 시작/종료 클라 notify 신설(빔 VFX와 동일 신호 — PROGRESS 확정 해법). 원격 클라 코스메틱 레이스(반동 램프/게이트)가 이 notify로 정리되는지 확인. 데미지는 이미 서버권위 — 재설계 금지.
3. 핑/Gibs: GMS(U8) 경유 경량 코스메틱(§5 — 복제 액터 상태 금지, Gibs=과도 연산 금지 §2-14).
4. 풀 사각 오디오 폴리시(V1 최소판 위에 에셋/믹스 확장) + 히트스캔 트레이서(비복제 코스메틱).
5. **원거리 경고 생산자 배선(B1)·원격탄 시각예측(A3)**: 원거리 적 차징 시 `ClientNotifyRangedTarget` 실배선(현재 디버그 `FPSR.TestRangedWarn`만) + 원격 클라 총알 시각예측(PROGRESS 피드백 후속 귀속).

[함정/주의 — 알려진 수용 설계, "버그"로 고치지 마라]
- 관통(MaxPenetration>1)+ExplosiveRounds = 탄착점 1회만 폭발(설계 수용, Codex P2 문서화됨) — 폭발 VFX도 탄착점 1회만.
- ExplosiveRounds 적 직격 히트마커 2회(직격+스플래시) = 알려진 폴리시.
- ChargeLaser 차징 중 프리즈 = 타이머 계속+데미지만 스킵(의도적 단순화) — PauseTimer로 "고치지" 마라.
- ⚠️ **[U8 GMS 핫패스 후속 — 산출물 3 배선 전 필독]** `FPSRGameplayMessageSubsystem::BroadcastMessageInternal`(cpp:37)이 재진입 안전용으로 **브로드캐스트마다 리스너 `TArray`를 통복사**한다. U8은 무publisher라 현재 무해하나, **U13(핑/Gibs)이 적 사망(`NotifyKill`/HealthComponent death)에 상주 리스너를 붙이는 순간** 적 사망 ×수백/프레임에서 이 복사가 §5 "발행 핫패스 힙할당 금지"를 **위반**한다(U20 적 애님도 동일 소비처=같은 breach — 먼저 리스너 붙이는 쪽이 GMS 수정 소유). → **상주 리스너 배선 전에 GMS 통복사를 재진입 가드 in-place 순회 또는 예약 스크래치 버퍼로 교체** + 채널 단위 early-out 검토.

[검증] 빌드+스모크 → PIE(+2-client에서 Finding A 해소 확인: 원격 클라 차징 표시/빔 정합) → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

### U14 — 임시값 원복 + §8 플레이스홀더 전환 + 폴리시/패키지 빌드

```
Game.md + PROGRESS.md + Docs/SSOT/Roadmap.md §8(플레이스홀더 인벤토리) 먼저 읽어. Docs/TaskPrompts_Master.md의 유닛 U14를 진행한다. 메모리 [[p4a-temp-test-values]] 원복 의무 이행 유닛이다.

[선행 확인] U22(Synty 에셋)·U13(VFX/오디오)·U25(성능 게이트) 완료 — 압축 스케줄 값은 E2E 검증까지 쓰고 원복하는 게 확정 운영. 최종/출시 수렴 유닛.

- 브랜치: phase/p7-polish 분기 (§6-7). 원복은 content 커밋으로 분리.
- 플랜 우선 → 승인 후 Sonnet 구현 / Opus 검증

[산출물]
1. **임시값 프로덕션 원복(사용자에게 노티 후)**: DA_RunSchedule.MissionWindows 윈도우 시간(테스트 압축값 예 50~120/240~300s) → 프로덕션 미션 300/600/900s, BossTime 300s → 1200s(≈20분 런). 원복 후 풀 길이 런 1회 PIE.
2. **§8 인벤토리 전수 전환**: 발사/근접 DrawDebug → #if !UE_BUILD_SHIPPING 게이트(또는 VFX 대체 확인) / FPSR.* 콘솔 커맨드 shipping 제외 / AFPSRXPPickup의 ConstructorHelpers 스피어 제거(§6-2 하드코딩 금지 위반 잔존 — DA/BP 참조로) / 적 큐브·FP팔 등 잔여 플레이스홀더 메시 상태 점검(Synty 교체분 = U22 반영, 미교체분만 목록화).
3. **Roadmap §7-3 P7 폴리시**: CommonUI 폴리시(입력 라우팅/패드), 오디오/이펙트 폴리시 잔여(U13과 분담 — U13=배선, 여기=마감), Insights 최종 측정(U25 게이트 반영), README, **패키지 빌드**(Win64 Shipping까지).

[함정/주의]
- 원복 커밋 전 반드시 사용자 노티(메모리 규칙 — 노티·원복 의무).
- shipping 게이트 후 스모크/패키지에서 디버그 의존 코드 컴파일 깨짐 주의(에디터 전용 분리).
- 패키지 빌드에서 LFS 에셋 쿠킹 시간/용량 — 미사용 폴더(Demo 등) 쿠킹 제외 설정 검토.

[검증] 빌드+스모크+패키지 빌드 성공+패키지 실행 E2E(메뉴→런→보스→결과) → codex-review.ps1 -Base main → PROGRESS 갱신+✅+--no-ff 머지.
```

---

## §D 문서 정합 메모 (후속 정리 대상)

| # | 이슈 | 위치 | 내용 |
|---|---|---|---|
| 1 | **"P7" 명칭 충돌** | `Docs/SSOT/Roadmap.md` §7-3 vs `Docs/Archive/plans/P7-MultiplayerLoop_Plan.md` | Roadmap의 P7=폴리시/빌드(U14), 플랜 문서의 P7=멀티 루프(U11). 멀티 루프는 Roadmap상 P5 행 영역. 본 문서는 유닛 번호(U11/U14)로 구분 — Roadmap 개정 시 명칭 정리 권장 |
| 2 | ~~**FF 10% stale**~~ ✅해소(2026-07-01) | `Docs/SSOT/Roadmap.md` §7-3 P5 행 · `Docs/SSOT/Enemy.md` §2-10 | 확정값=50%(`FriendlyFireDamageScale=0.5f`). **기본 ON 설계확정 2026-07-01**(Concept §1-C-6; ⚠️코드 `bFriendlyFireEnabled` false→ON 전환 후속). 10% 인용 금지 |
| 3 | ~~**Fragment 훅 stale**~~ ✅해소 | `PROGRESS.md` | `ModifyChargeTime`/`OnProjectileSpawn` 훅은 A3a/A3b에서 구현 완료 — 재구현 금지 |
| 4 | **U10 SaveGame 감사 후속(PM 2026-07-02)** → **U23 선행** | `FPSRSaveGameSubsystem.cpp` · `FPSRSaveGameTest.cpp` | PM 검증 감사(계약=PASS·인프라=CONCERNS·저위험): **테스트 공백 2건(MED)** — ⓐ손상 세이브 fallback 미테스트 ⓑ다운그레이드 마이그레이션 방어 미테스트 → **P0-③(U23)가 스키마 확장하기 전 추가**. 견고성 저위험 3건(저장 재시도 PostLoadMap 의존·in-flight 재저장 가드·백업 미러 실패 무관측). **데이터 유실 없음**. |
| 5 | **PM 페르소나 §4 stale** | `Docs/ProjectPromptManager.md` §4 | "순차 진행(병렬 폐지 2026-06-15)" 문구는 현재 **2-클론 병렬 라이브**(2026-07-13 사용자 확정)와 stale — 페르소나 개정 시 §B-3 최신 문구로 정합 |

---

## §E ConsultLoop 결과 인입 (`Docs/Review/` → 백로그)

> **이 프롬프트 매니저는 `Docs/Review/`의 컨설팅 리포트를 읽어** 각 리포트의 `📌 액션 아이템`을 아래 인입 표로 받고, 대상 유닛(§C)·백로그(§B)에 반영한다. 프로토콜=[`Docs/ConsultLoop.md`](ConsultLoop.md), 채널 설명=`Docs/SSOT/Workflow.md` §10.
>
> **인입 규칙**:
> 1. 새 컨설팅 리포트가 `Docs/Review/`에 생기면 그 `📌 액션 아이템`·`🙋 사용자 결정 필요`를 아래 표에 한 줄씩 등재(리포트 링크 + 대상 유닛 + 상태).
> 2. **자문 전용** — 등재만으로 코드/에셋을 바꾸지 않는다. 채택 시 해당 `Docs/SSOT/*.md`를 먼저 갱신(원칙3) 후 대상 유닛 프롬프트(§C)에 반영.
> 3. 유닛에 흡수되면 상태를 `→Uxx 반영`으로, 폐기 시 `보류/기각(사유)`으로 갱신.
> 4. **제외**: `Docs/Review/20260707-plan-plan-consult-skill.md` = 프로세스/스킬 저작 문서(게임 백로그 아님) → §E 미등재.

| # | 출처 리포트 | 액션/결정 | 종류 | 대상 유닛 | 상태 |
|---|---|---|---|---|---|
| C1 | [Review/20260616-lmg-spinup-feel.md](Review/20260616-lmg-spinup-feel.md) | `UFPSRWeaponFireComponent::GetSpinupAlpha()` BlueprintPure 노출(사운드/크로스헤어/애님 단일 콘텐츠 훅) | 코드(소) | U13 또는 U15 합류 | 신규 |
| C2 | 〃 | 발사 루프 사운드 피치/볼륨 램프 + spin-down/brake 사운드 3종(탄소진/재장전/교체) | 콘텐츠 | U13(오디오) | 신규 |
| C3 | 〃 | 크로스헤어 스핀업 미세 피드백(V3 재사용, Alpha→gap/두께, 실제 Bloom과 일치) | 콘텐츠/UI | U12(UI·필 잔여) | 신규 |
| C4 | 〃 | DA 곡선 단계 노출(`SpinupAudioPitch/Volume`, `SpinupCrosshair*`, `SpinupBarrelSpin*`, 선택 `SpinupRecoil/BloomScaleCurve`) — 부담 시 Alpha 노출만으로 1차 충족 | 코드/콘텐츠 | U13/U15 | 신규 |
| C5 | 〃 | **스핀업 곡선 튜닝값 확정**: Start=최대 FireRate 50~65%, RampTime 0.8~1.2s, 최고속 5~7s+ 지속(RampTime ≤ 탄창소모시간 20~25%) | 🙋사용자결정(DA) | U16 후속 튜닝 | 사용자 |
| C6 | 〃 | **사망 경로는 U2에서 해소** — `HandleOutOfHealth`가 `StopFiring()`+`CancelAllAbilities()` 호출. **남은 것 = U9 DBNO/부활**: 다운→부활 전이에서도 발사상태·스핀업 리셋 보장 | 코드 | U9(DBNO) ✅ | ✅해소(U9 머지) |
| D1 | [Review/20260616-volumeup-design.md](Review/20260616-volumeup-design.md) | 재미 게이트 §7-5를 **G1(손맛/페이싱/성능)+G2(빌드다양성/시너지)** 2분할 기재 | 문서(SSOT) | Roadmap §7-5 | ✅반영(2026-06-16) |
| D2 | 〃 | **EnemyDefinition DataAsset + 시간 가중 로스터 디렉터** 골격. 기존 B1(원거리 적) 흡수 | 코드(골격) | 신규 U(적시스템) | ⏸️보스/로비 후(결정1=B) |
| D3 | 〃 | **경량 적 상태이상 서브시스템** C++ 베이스. 적500 예산 건드림=HIGH_RISK. Enemy/Combat/Performance SSOT 선갱신 | 코드(골격) | 신규 U(상태계) | ⏸️G1 후·🙋결정대기 |
| D4 | 〃 | **프리즈 하이브리드 최소 프로토**(일반 수치카드=Q/E 비동기, 무게큰선택=프리즈). RunFlow §2-2 선갱신 | 코드+문서 | 신규 U(프리즈) | ⏸️🙋결정대기 |
| D5 | 〃 | **G1 게이트 계측 기준 고정 + 실행** | 검증 마일스톤 | U1 ✅ / 정량=U25 | ✅G1합격·정량→U25 |
| D6 | 〃 | **빌드 시너지 축 정의 패스**(상태이상 4종 + Fragment 물림 매트릭스). D3 의존 | 문서/설계 | Combat §2-3 | ⏸️D3 의존 |
| D7 | 〃 | 맵 선택/정의 데이터 골격 + 메타 SaveManager/보스정의 골격 | 코드(골격) | U3/U4/U11a ✅ | ✅진행정합(완료) |
| E1 | [Review/20260629-build-structures.md](Review/20260629-build-structures.md) | **건설 구조물 시스템 = 개념 채택**(사용자 2026-06-29). ⚠️아이디어 풀로만 보관(사용자 지침). 코어 잠금 후 정식화 검토 | 💡아이디어 | (미등재) | 💡아이디어 풀(파킹) |
| E2 | 〃 | **MVP = 센트리 포탑 + 메디컬 비콘 2종**(비파괴/비차단/충전) + 미션보상 grant + 폴리모픽 효과. 미션프레임워크+카드v2 재사용 | 💡아이디어 | (미등재) | 💡아이디어 풀(파킹) |
| E3 | 〃 | ❌ **XP 타워/리파이너 기각** / ❌ **바리케이드(차단형) 기각**(사용자 2026-06-29) | — | — | 🗑️기각 |
| F1 | [Review/20260701-concept-conclusions.md](Review/20260701-concept-conclusions.md) | **USP 문장 확정** = "스웜은 각자 갈아내고, 팀은 서로의 사각·원거리·다운을 커버" | 문서(SSOT) | Concept §1-C-1/§1-C-3 | ✅반영(2026-07-01) |
| F2 | 〃 | **FF 최종 = ✅ 치사 ON 50% 확정**(사용자 2026-07-01): 데미지 50%·아군 다운 가능. 공개매칭 미고려(솔로+친구). ⚠️코드 flip(`bFriendlyFireEnabled`) 잔여 | ✅결정+반영 | Concept §1-C-6·Enemy §2-10 | ✅반영(2026-07-01) |
| F3 | 〃 | **HUD 위협 큐 원칙**: 잡몹 개별 UI 금지, 시각 큐 동시 ≤3+후방집계1, 오디오 동시 1·쿨다운 1.0~1.5s, 우선순위·팀 인지+핑 | 문서+콘텐츠 | PlayerFeel §2-14 | ✅SSOT반영·구현=후속 |
| F4 | 〃 | **초반 4솔로 공백 방지**: 0~60s 개인 손맛, 60s+ 양방향 스폰, 75~180s 첫 원거리/스페셜, 팀 위협 비트 하한 | 코드+콘텐츠 | RunFlow §2-8·Enemy §2-6 | ✅SSOT반영·구현=후속 |
| F5 | 〃 | **협동유도 스페셜 적**(개별 AI 불요): `IsolationScore` 타겟팅 + 고립자 측후방 스폰 가중 + 연출. MVP=기존 원거리 재색칠 1종 | 코드+콘텐츠 | Enemy §2-6 / 신규 U(적) | ✅SSOT반영·구현=후속 |
| F6 | 〃 | **1인칭 가독성 USP 게이트 최소 5지표**(활성 200-300·relevant P90≤150·화면내 P50≤40/P90≤70·15m P90≤25·시각큐≤3) | 검증 게이트(P0) | Performance §5 → **U25** | ✅SSOT반영·측정=U25 |
| F7 | 〃 | **FF→회복 카드 제약**: 오버힐 없음·틱 상한·다운직전 세이브 용도. 협동 가성비 = 스페셜 적 > 어그로 룰/카드 > FF→회복 카드 | 콘텐츠(제약) | CombatWeaponCard §2-3 | ✅SSOT반영 |
| G1 | [Review/20260705-multimap-budget-regroup.md](Review/20260705-multimap-budget-regroup.md) | **#3 다중맵 아키텍처 설계 수렴**(Codex 5R): 전역 공유 캡 allocator · per-map 필드 · NetCull→RepGraph→스트리밍 · Tier 0/1/2 | 문서(SSOT) | Performance §5·Architecture §3-4·RunFlow §2-1 | ✅반영(2026-07-05) |
| G2 | 〃 | **Tier 0 실행 프롬프트**(코어 인프라) | 코드(유닛) | #3 Tier 0 = U 연속필드 `34b5eea` | ✅ 완료 |
| G3 | 〃 | **Tier 1**(예산 게임필: 동적 burst reserve · silent recycle · local pressure floor · 최대그룹크기 정책) | 코드 | 후속 유닛(다중맵 Tier1) | 파킹(Tier0 후) |
| G4 | 〃 | **Tier 2**(rally pad · split 감지→솔로 억제 · 양성 인센티브 · 보스 rally · 문리셋 debt) | 코드+콘텐츠 | 후속 유닛(다중맵 Tier2) | 파킹(콘텐츠/밸런스) |
| G5 | 〃 | **디렉터 결정 6항목 확정(2026-07-05, R6)**: 솔로=정찰 · 2인+ 효율 · 그룹버프 폐기 · 텔포=장치 · 문=혼자어려운체력 · 맵=언로드X·잔존 | ✅결정+SSOT반영 | RunFlow §2-1·Performance §5 | ✅(2026-07-05) |
| H1 | [Review/20260707-data-tooling-p0.md](Review/20260707-data-tooling-p0.md) | **P0 데이터 검증 = `FPSRogueliteEditor` 에디터 모듈 신설**(Codex 5R): 앵커 교차검증기 3종+per-asset sanity+`ValidateAnchoredDataCommandlet`+`validate-data.ps1`+메뉴명령 | 코드(신규 모듈) | `phase/editor-tooling-seam` | ✅main 머지(`57270c5`) |
| H2 | 〃 | 🙋**라우팅 스펙 정합 선행**: 행동프래그먼트/feature-unlock/무기-unlock/레벨업 surface를 코드↔SSOT↔[[card-pool-routing]] 확정 후에만 "라우팅 누수" 검증 추가 | 🙋사용자결정+문서 | CombatWeaponCard §2-3-4 | 🙋결정대기 |
| H3 | 〃 | 미션 튜닝(ZoneRadius/HoldSeconds) BP CDO→DA-소유 통합 여부. 미션 타임라인 편집(P2)의 선행 | 문서(SSOT)/백로그 | RunFlow §2-8 / ToolingBacklog | 파킹(P0 후) |
| H4 | [Review/20260707-data-tooling-p1.md](Review/20260707-data-tooling-p1.md) | **P1 편집 툴 = FPSR Data Editor**(Codex 5R): 단일 도킹 탭·route-인지 preflight·카드 magnitude 그리드·미션 read-only 타임라인·헬퍼 라운드트립 테스트2. 하이브리드 C++Slate | 코드(모듈 확장) | FPSR Data Editor(구현·머지 완료 `57270c5`·`c4e0d77`·`3fb7da6`) | ✅ 완료 |
| I1 | [Review/20260707-plan-continuous-field-arch.md](Review/20260707-plan-continuous-field-arch.md) | **다중맵 U 연속필드 아키텍처 수렴**(plan-consult, 대안 U 채택). **아크 구현·머지 완료**(`34b5eea`). 잔여 = **RepGraph 공간그리드 relevancy(§2-2 요구8·§2-5·§4 D3)** = 별도 후속 페이즈 | 코드(후속) | **U24**(라이브) · SSOT갱신후보 Performance §5·Architecture §3-4·RunFlow §2-1 | ✅아크완료 / RepGraph→U24 |

> **✅ G1 SSOT 반영 완료 (2026-07-05)**: #3 다중맵 설계 수렴을 Performance §5·Architecture §3-4·RunFlow §2-1에 반영. Tier 0 = U 연속필드 구현·머지 완료(`34b5eea`). RepGraph 후속 = U24(라이브).

> **✅ F1·F3~F7 SSOT 반영 완료 (2026-07-01)**: Concept §1-C·PlayerFeel §2-14·Enemy §2-6·Performance §5·CombatWeaponCard §2-3-5. **구현은 해당 후속 유닛에서.** F2=FF 치사 ON 50% 확정. F6 정량 측정 = U25.
