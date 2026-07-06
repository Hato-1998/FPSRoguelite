# ToolingBacklog — 개발 툴 백로그 (living doc)

> **이 문서는 살아있는 백로그다 — 자유롭게 수정·추가·상태갱신한다.** 프로젝트에 만들 만한 개발/콘텐츠 툴을 실제 시스템(DataAsset 주도 콘텐츠·플로우필드·map-aware allocator·VAT 적·Synty 파이프라인·headless commandlet)에 그라운딩해 뽑은 것.
> **출처**: 8관점 SSOT-그라운딩 리서치(9-에이전트 워크플로, 2026-07-05). 시각 카탈로그(Artifact) = `https://claude.ai/code/artifact/58f97f66-f4e7-41a8-8935-06cad14ddcbc`.
> **정렬 원칙**: 임팩트 × 공수, 그리고 **다음 두 세션(A 다중맵 Tier 0 · B Synty 교체) 가속** 순서. 전체 아크 = **오버레이(눈) → 하네스(게이트) → 분석(리텐션)**.
>
> **범례** — 공수: S(작음)/M(중간)/L(큼) · 임팩트: high/med/low · **상태**: 📋 백로그 / 🔨 빌드중 / ✅ 완료 / 🗑️ 폐기 (툴이 진행되면 상태만 갱신).

---

## 1. 빌드 순서 (현 로드맵 가속)

> 규칙: 오버레이(눈) → 하네스(게이트) → 분석(리텐션). S 공수 헤드리스 가드는 near-zero 비용으로 하위 전부를 게이트하니 즉시.

### 세션 A — #3 다중맵 Tier 0 가속 (먼저/함께)
1. **FPSR.Alloc.Debug** `M/high` — **최우선**. allocator="설계의 심장", Tier 0 임시 "2인+ 맵>솔로" 가중치 PIE 검증 필수. per-map target/actual/cap-headroom/drain 없으면 1/1/1/1 파밍 붕괴가 안 보임.
2. **Collision-for-FlowField Validator** `M/high` — 프롬프트 명시 gotcha(ECC_WorldStatic 없으면 필드 안 구워짐). 화이트박스 2맵 bake 배선 전.
3. **FPSR.Significance.Debug** `S/high` + **FPSR.Net.Relevancy** `M/high` — Tier 0 항목 #4=NetCull 구현. pre-cull 베이스라인 증명 + 수정 검증.
4. **FPSR.MultiMap.AllocatorProbe** `L/high` — 프롬프트 검증 항목을 헤드리스 불변식으로(오버레이로 눈 생긴 뒤 하네스로 굳힘).

### 세션 B — Synty 에셋 교체 가속 (B 전)
5. **SyntyImportConditioner** `M/high` (+ #2 Collision Validator) — 임포트 메시 ECC_WorldStatic/Nanite-off/atlas 계약 강제·검증. Synty 최대 언블로커.
6. **VATBakePipeline** `L/high` (+ **VATMaterialParamValidator** `S/med`) — 적 애니가 죽어있음(ClipIndex_*=0.0f). 공유 마네킹 스켈이라 1베이크=전 적.
7. **SyntyPilotBenchHarness** `L/high` — Roadmap §8 하드 게이트 + 한 번도 안 한 §5 측정. 백본 **FPSR.Perf.SwarmHarness** `M/high` 선행.

### Seams — 지금 심어두기 (싸고 둘 다 보호)
8. **Batch DataAsset Validator** `S/high` + **Temp-Test-Value Tracker** `S/high` — Build.bat/Smoke + codex-review 게이트에 배선하면 이후 전 단계 상속.
9. **FPSR.Save.MigrationMatrix** `S/high` — 싸고 직교, 아무 때나.

### Defer — 두 세션 이후 (배포된 설계를 측정)
- 전 **라이브옵스/분석**(Run Funnel Telemetry·Togetherness Meter·F6 가독성·Death-Cause).
- 콘텐츠 저작 UMG(Card Catalog·Composer)·나머지 밸런스 시뮬 → 다음 콘텐츠/밸런스 단계.

---

## 2. 우선순위 (퀵윈 + 전략 16)

### 퀵윈 (high 임팩트 · S/M 공수)
| 툴 | 관점 | 공수 | 임팩트 | 왜 (병목) | 상태 |
|---|---|---|---|---|---|
| FPSR.Alloc.Debug | Debug | M | high | allocator=심장, Tier0 v0 가중치 PIE 검증, 붕괴 불가시 | 📋 |
| Collision-for-FlowField Validator | Level | M | high | ECC_WorldStatic 없으면 필드 안 구워짐(A·B 둘 다 의존) | 📋 |
| SyntyImportConditioner | Asset | M | high | 임포트 메시 콜리전/Nanite/atlas 계약 강제 | 📋 |
| Batch DataAsset Validator | Content | S | high | 전 DA IsDataValid 일괄→부분적용 회귀 red CI | 📋 |
| Temp-Test-Value Tracker | Balance | S | high | 압축 테스트값 출시 방지·원클릭 flip-back | 📋 |
| FPSR.Significance.Debug | Debug | S | high | S0-S3 티어 가시화(§5 perf 선행) | 📋 |
| FPSR.Save.MigrationMatrix | QA | S | high | 세이브 마이그레이션 무손실 증명(메타 유실 방지) | 📋 |
| Card Catalog & Balance Matrix | Content | M | high | U18d 미구현, 29에셋 흩어진 magnitude 한눈에 | 📋 |

### 전략 (공수 크나 고레버리지)
| 툴 | 관점 | 공수 | 임팩트 | 왜 | 상태 |
|---|---|---|---|---|---|
| FPSR.Perf.SwarmHarness | QA | M | high | §5 적500 미측정 종결·100/200/300/500 램프 CSV | 📋 |
| FPSR.MultiMap.AllocatorProbe | QA | L | high | Tier0 allocator 불변식 머신체크 | 📋 |
| VATBakePipeline | Asset | L | high | 죽은 적 애니 살림·공유 스켈 1베이크=전 적 | 📋 |
| FPSR.Net.Relevancy | Debug | M | high | NetCull 미구현 증명+수정 검증(pre-RepGraph 1순위) | 📋 |
| 20-min Run Pacing Simulator | Balance | M | high | 랜덤 롤 스케줄 실제 20분 모양 가시화 | 📋 |
| Archetype TTK/DPS Matrix | Balance | M | high | 7 아키타입 TTK 모델(PIE 시행착오 대체) | 📋 |
| Run Funnel Telemetry | Live-Ops | M | high | D1/D7·90런 계측(런 어디서 죽나) | 📋 |
| Co-op Togetherness Meter | Live-Ops | M | high | "솔로=정찰/2+=본게임" 명제 검증/반증 유일 계측 | 📋 |

---

## 3. 관점별 전체 (8 dimension · 중복 제거)

### CONTENT — 콘텐츠 저작
> 흩어진 폴리모픽 카드 시스템을 legible·correct-by-construction으로(집계 뷰 + 헤드리스 가드 + 스캐폴딩, EditorAssetLibrary 패턴).

| 툴 | 공수 | 임팩트 | 내용 | 상태 |
|---|---|---|---|---|
| Batch DataAsset Validator | S | high | 전 Card/Pool/Weapon/Schedule/Roster IsDataValid+non-zero exit CI | 📋 |
| Card Catalog & Balance Matrix | M | high | 전 카드 그리드(Group/CardId/rarity magnitude)+이상치(Blutility) | 📋 |
| CardPool Coverage & Dedup Auditor | M | high | 중복/누락 CardId·orphan·Family 충돌·가중 히스토그램 | 📋 |
| New Card/Fragment Scaffolder | M | med | 템플릿 복제+CardId 시드+RarityTiers 채움 "5분 새 카드" | 📋 |
| Card Description Snapshot Test | S | med | GetDescription 골든 텍스트 diff | 📋 |
| Run Schedule Timeline Editor | M | med | MissionWindows+boss+density 타임라인, prod/test 프리셋 | 📋 |
| Card Effect Composer (wizard) | L | med | Instanced Effects[] 손배선 없이 다중효과 조립+rarity 미리보기 | 📋 |

### LEVEL — 레벨/맵 디자인
> 안 보이는 flow-field bake/spawn-gate/다중맵 레지스트리 계약을 저작 시점에 노출(Synty·다중맵 세션 최고 레버).

| 툴 | 공수 | 임팩트 | 내용 | 상태 |
|---|---|---|---|---|
| Collision-for-FlowField Validator | M | high | bake가 놓칠 non-WorldStatic/무콜리전 면을 좌표+액터명 | 📋 |
| FlowField Bake Auditor (overlay) | M | high | PIE 없이 BuildObstacleMask+per-cell 오버레이+클릭 인스펙터 | 📋 |
| Multi-Map Streaming/Registry Inspector | L | high | 서브레벨·ULevel 키·문/텔포→타깃레벨·bake/evict 시뮬 | 📋 |
| Spawn-Point Coverage/FOV-Gate Auditor | M | med | FOV+MinPlayerDistance 통과 지점·dead zone·ring 폴백 | 📋 |
| Mission PointSet Editor/Reachability | M | med | AFPSRMissionPointSet 재정렬·경로 미리보기·walkability | 📋 |
| 2-Storey Surface Graph Inspector | M | med | NumLayers=2 rank0/1·EdgeMask·>2층 경고 | 📋 |

### BALANCE — 밸런스/튜닝
> 임시값 대상 PIE 시행착오를 헤드리스 모델로(balance_dump/apply 확장).

| 툴 | 공수 | 임팩트 | 내용 | 상태 |
|---|---|---|---|---|
| Temp-Test-Value Tracker / Revert Guard | S | high | 압축값 매니페스트+test/prod diff+원클릭 flip-back | 📋 |
| Archetype TTK/DPS Matrix | M | high | 7 아키타입 DPS/STK/TTK vs 적 HP 스케일링 | 📋 |
| 20-min Run Pacing Simulator | M | high | 랜덤 롤 타임라인+density/party-level 오버레이 | 📋 |
| XP/Level-Up Curve & Freeze Tuner | M | med | 프리즈가 20분에 몇 번 끊나+후반 가팔라짐 타깃 | 📋 |
| Card/Fragment Synergy Matrix | M | med | rarity magnitude×Group/Family×Fragment 시너지 축 | 📋 |
| Enemy Scaling-Curve Editor | L | med | HP/damage 곡선 편집+per-archetype TTK 라이브 | 📋 |

### DEBUG — 디버그/프로파일링
> 서버권위·cvar 게이트 오버레이로 스웜 비용 모델·다중맵 allocator를 PIE서 관측(한 canvas HUD 공유).

| 툴 | 공수 | 임팩트 | 내용 | 상태 |
|---|---|---|---|---|
| FPSR.Alloc.Debug | M | high | per-map alive/target/reserve/drain+group 가중+recycle | 📋 |
| FPSR.Significance.Debug | S | high | S0-S3 티어 색분류+헤드카운트+NetUpdateFreq 재색 | 📋 |
| FPSR.Net.Relevancy | M | high | connection별 relevant/replicated+NetCull+dormancy vs ~150 | 📋 |
| FPSR.FrameBudget.HUD | M | high | 리슨호스트 프레임을 movement/separation/flow/replication/render 버킷 | 📋 |
| FPSR.Spawn.EventLog | S | med | spawn/pool acquire-release/KillZ/recycle 링버퍼+이유 | 📋 |
| FPSR.AttackToken.Debug | S | med | 근접(10)/원거리(3)/발사체 예산 압력 vs 블라인드 튜닝값 | 📋 |

### ASSET — 에셋 파이프라인 (Synty + VAT)
> Synty Path A 임포트 계약 자동화 + 죽은 Stage-3 VAT 베이크 완성(Synty 세션 정확한 블로커).

| 툴 | 공수 | 임팩트 | 내용 | 상태 |
|---|---|---|---|---|
| SyntyImportConditioner | M | high | ECC_WorldStatic·Nanite OFF·LOD·per-asset pass/fail | 📋 |
| VATBakePipeline | L | high | AnimToTexture 1 VAT 세트+FPSRVATAnimParams write-back | 📋 |
| SyntyPilotBenchHarness | L | high | Roadmap §8 게이트(임포트+적300+플로우+20분) pass/fail | 📋 |
| VATMaterialParamValidator | S | med | VAT 스칼라 파라미터 실명 존재 단언(no-op 방지) | 📋 |
| PlaceholderSwapTracker | S | med | Roadmap §8 인벤토리+TODO 마커 unswapped 체크리스트 | 📋 |
| MixamoRetargetBatch | M | med | Mixamo/Paragon→공유 마네킹 배치 리타겟(VAT 입력) | 📋 |
| LFSAssetSizeAuditor | S | med | Synty 바이너리 LFS/예산 감사(−A·smudge 풋건 방지) | 📋 |
| DrawCallAtlasReporter | M | med | 드로우콜 소스별 버킷·atlas 병합 실패 노출 | 📋 |

### QA — QA/자동화 테스트
> FPSRoguelite.Smoke.* 하네스를 SSOT 최상위 리스크(미측정 perf·서버권위 MP)+다중맵 불변식+세이브 마이그레이션 게이트로 확장.

| 툴 | 공수 | 임팩트 | 내용 | 상태 |
|---|---|---|---|---|
| FPSR.Save.MigrationMatrix | S | high | 전 SaveVersion+다운그레이드 무손실 마이그레이션 단언 | 📋 |
| FPSR.Perf.SwarmHarness | M | high | 100/200/300/500 램프 avg/p99 프레임+메모리 CSV | 📋 |
| FPSR.MultiMap.AllocatorProbe | L | high | 2-3맵 스트림+allocator 불변식 머신체크 | 📋 |
| FPSR.Net.SoakBot (2-4 client) | L | high | 헤드리스 봇 클라+서버권위 불변식 soak | 📋 |
| FPSR.Balance.SimRun | M | med | 고 SetTimeScale 헤드리스 런→골든 CSV diff | 📋 |
| FPSR.Content.AnimNullCheck | M | med | 전 anim profile×EFPSRAnimState 무크래시·null=휴면 | 📋 |
| FPSR.Repro.SeedReplay | L | med | 런 시드 캡처→동일 시드 재구동(원클릭 repro) | 📋 |

### AI/META — AI 워크플로 / 메타툴
> docs↔code 드리프트 루프 닫기 + PM이 손으로 하는 ConsultLoop→§E/핸드오프 전사 자동화.

| 툴 | 공수 | 임팩트 | 내용 | 상태 |
|---|---|---|---|---|
| SSOT-Code Drift Sentinel | M | high | 실 DataAsset/UCLASS 값 vs SSOT 인용 수치 diff | 📋 |
| Handoff/PROGRESS Auto-Sync + 가드 | M | high | phase diff→PROGRESS delta+커밋 메시지, −A/LFS·doc-bleed 하드블록 | 📋 |
| Consult→§E Ingestion Autopiler | M | high | Docs/Review 미인입 리포트→§E 행+§C draft(자문전용) | 📋 |
| DataAsset→SSOT Doc Generator | S | med | 저작 콘텐츠 실값→자동갱신 fenced 블록 | 📋 |
| Codex-Review Batching Gate | S | med | 대형 diff 서브시스템별 청크→병렬 codex-review→병합 | 📋 |
| First-Principle Variant Generator | L | med | 카드/적 후보 생성+제1원리 위반 자기필터 | 📋 |

### LIVE-OPS — 라이브옵스/분석/플레이테스트 (두 세션 이후)
> 리텐션 퍼널·co-op togetherness 명제를 near-free GameplayMessage 위에 계측(1인 팀 검증 유일 길).

| 툴 | 공수 | 임팩트 | 내용 | 상태 |
|---|---|---|---|---|
| Run Funnel Telemetry | M | high | 런 생애 이벤트(미션/프리즈/보스/종료+사인) JSONL per run | 📋 |
| Co-op Togetherness Meter | M | high | 근접·공유맵·back-to-back→togetherness 비율(명제 검증) | 📋 |
| FPSR.F6 Readability Auto-Capture | S | high | §5 가독성 5지표 N프레임 샘플+P50/P90 | 📋 |
| Death-Cause & Wipe Attribution | S | high | DBNO/사망/와이프 원인+맥락(화면 적수·산개·런클럭) | 📋 |
| Card/Weapon Pick Distribution | S | med | 텔레메트리→픽률·승률상관·dead-card 표 | 📋 |
| Playtest Session Recorder | M | med | 런클럭 타임라인+위치/맵+프리즈+사망+스샷 트리거 | 📋 |

---

## 4. 관통 테마 5
1. **침묵하는 실패를 가시화** — 이 게임 최악의 버그는 조용하다(안 구워지는 flow-field·부분적용 카드·출시된 압축 테스트값·`SetScalarParameter` no-op). 고임팩트 툴 대부분이 "PIE까지 침묵"을 저작/베이크/CI 시점 red line으로.
2. **allocator가 심장** — #3 다중맵 콘텐츠-aware allocator="설계의 심장·디버그 난해". 오버레이(눈)+불변식 하네스(게이트)+togetherness(검증) 3단. 다음 세션 최대 단일 레버.
3. **§5 perf는 한 번도 측정 안 됨** — ~200-300/캡500·NetUpdateFreq·미구현 NetCull은 가정. SwarmHarness+오버레이+Synty 벤치가 리슨호스트 최악 케이스 실 코드경로에 비용 귀속.
4. **죽어있는 VAT 파이프라인** — 적 애니 C++ 완전 배선인데 무동작(`ClipIndex_*=0.0f`, TODO Stage 3). VAT 베이크가 Synty 적 재저작 전체를 여는 빠진 Stage-3, 공유 스켈이라 1베이크=전 적.
5. **재발명 말고 확장** — headless `-run=pythonscript`·`FPSRoguelite.Smoke.*`·cvar 오버레이(`FPSR.FlowField.Debug`)·GMS 스파인·codex-review/consult가 이미 있음 → 얇은 확장이 최고 ROI(다수 S/M).

---

## 5. 이 문서 갱신 방법
- 툴 진행 시 **상태 컬럼만 갱신**(📋→🔨→✅) 또는 🗑️(폐기 사유 병기).
- 새 툴 발견 시 해당 카테고리 표에 행 추가(공수/임팩트/내용/상태). 우선순위 바뀌면 §1 빌드 순서·§2 우선순위 표 재정렬.
- 실제 빌드 착수 시엔 별도 phase 브랜치 유닛으로(§B-3 프로토콜). 이 문서는 백로그(계획)이지 실행 로그 아님 — 완료 상세는 git log.
