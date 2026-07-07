# ToolingBacklog — 개발 툴 백로그 (living doc · 로드맵 전 구간 매트릭스)

> **이 문서는 살아있는 백로그다 — 자유롭게 수정·추가·상태갱신한다.** 프로젝트에 만들 만한 개발/콘텐츠 툴을 실제 시스템(DataAsset 주도 콘텐츠·플로우필드·map-aware allocator·VAT 적·Synty 파이프라인·headless commandlet·GAS)에 그라운딩해 **로드맵 전 phase × 툴 카테고리**로 뽑은 것.
> **출처**: 로드맵 전 구간 10-에이전트 워크플로(9관점 리서치 + 1 종합, 2026-07-05). *(직전 "다음 두 세션 가속" v1을 대체·확장. 시각 quick-view Artifact(next-two-sessions 프레이밍) = `https://claude.ai/code/artifact/58f97f66-f4e7-41a8-8935-06cad14ddcbc` — 이 문서가 로드맵-와이드 정본.)*
> **정렬 원칙**: 각 툴을 **가장 먼저 필요해지는 phase**에 1회 배치(§2). phase 순서와 무관하게 **일찍 만들수록 후속 phase가 상속하는 seam**은 §1에 별도. 전체 아크 = **오버레이(눈) → 하네스(게이트) → 분석(리텐션)**.
>
> **범례** — 공수(UE5 현실): S(작음: headless commandlet·cvar 오버레이·EUW) / M(중간: telemetry·EUW 브라우저·간단 UEdMode) / L(큼: soak/perf 하네스·UEdMode 인터랙티브·커스텀 Asset Editor) · 임팩트: high/med/low(**반복 횟수 반영** — phase에서 몇 번 쓰이나) · **유형**: 저작 / 뷰어 / 검증 / 자동화 / 계측 · **상태**: 📋 백로그 / 🔨 빌드중 / ✅ 완료 / 🗑️ 폐기.
> **[가정]** = 미구현 시스템 의존 등 코드/문서 직접 앵커가 없는 speculative 툴.
>
> **Phase 축**: CORE(P1.5-P4 코어, DONE) · COOP(P5 협동, DONE) · **MM-T0**(다중맵 Tier0, active) · MM-T1/2(다중맵 Tier1/2) · **SYNTY**(에셋교체+VAT, active) · P6-META(메타/보스/클리어) · BALANCE(양산 튜닝) · P7-SHIP(폴리시+§5 perf+출시) · LIVEOPS(출시후 리텐션).
> **상태값 이관 노트(2026-07-05 재작성)**: 직전 백로그 전 툴 = 📋(빌드 착수 0). 흡수 툴은 📋 유지, 신규 툴도 📋로 시작.

---

## 1. 빌드 순서 — Seam 우선 (조기 빌드 = 다수 후속 phase 상속)

> phase 순서와 무관하게 이걸 먼저 만들면 여러 phase가 상속한다. 규칙: S 공수 헤드리스 가드는 near-zero 비용으로 하위 전부를 게이트하니 즉시.

| Seam 툴 | 유형 | 공수 | 임팩트 | 상속 phase | 근거 | 상태 |
|---|---|---|---|---|---|---|
| **FPSR.Perf.SwarmHarness** | 검증 | M | high | SYNTY→P7-SHIP→BALANCE | §5 적500 램프 CSV 하나가 파일럿 게이트·ship perf 패스·양산 회귀 벤치를 전부 태움 | 📋 |
| **Batch DataAsset Validator** | 검증 | S | high | CORE→BALANCE→P6-META | non-zero exit CI로 이후 전 콘텐츠 단계가 red line 상속 | ✅ P0(`FPSRogueliteEditor` 모듈·앵커 검증기 3종·`validate-data.ps1`, main 57270c5) |
| **FPSR.Alloc.Debug** | 계측 | M | high | MM-T0→MM-T1/2 | allocator=심장, per-map 오버레이가 전 다중맵 정책 이터레이션 재사용(+Togetherness 백엔드) | 📋 |
| **Collision-for-FlowField Validator** | 검증 | M | high | MM-T0→SYNTY | ECC_WorldStatic 계약=두 active-next 세션 공통 언블로커 | 📋 |
| **FPSR.Significance.Debug** | 계측 | S | high | MM-T0→P7-SHIP | NetCull pre-cull 베이스라인 + 한 번도 안 한 §5 perf 눈 | 📋 |
| **FPSR.Net.SoakBot** | 검증 | L | high | COOP→MM-T0→P6-META | 봇 클라 soak 하나가 전 서버권위 회귀 게이트(door-break·NetCull·메타 세이브) | 📋 |
| **FPSR.Repro.SeedReplay** | 자동화 | L | med | COOP→BALANCE→MM-T1/2 | 시드 재구동이 밸런스 골든 diff 기반+다중맵/MP 비결정 repro | 📋 |
| **Run Funnel Telemetry** | 계측 | M | high | LIVEOPS 전 미터 | GMS 스파인 per-run JSONL이 전 분석 미터 백본(P6 클리어플로우에 시드) | 📋 |
| **Temp-Test-Value Tracker** | 검증 | S | high | CORE→BALANCE→P7-SHIP | 압축값 매니페스트가 원복 누락 방지 + Ship Cook Gate가 참조 | 📋 |

---

## 2. Phase × Tool 매트릭스

> 각 툴은 **가장 먼저 필요해지는 phase**에 배치(중복 제거·1회). 컬럼: 툴 / 유형 / 공수 / 임팩트 / 왜(병목) / 상태. (필요 phase = 섹션.)

### CORE — P1.5-B/P2/P3/P4 코어 루프 (DONE · 회귀/유지보수 가드)
| 툴 | 유형 | 공수 | 임팩트 | 왜 (병목) | 상태 |
|---|---|---|---|---|---|
| Batch DataAsset Validator | 검증 | S | high | 폴리모픽 카드/풀/무기/스케줄/로스터 29+에셋 부분적용 회귀가 PIE까지 침묵 → 전 DA IsDataValid 헤드리스+non-zero exit CI | ✅ P0(57270c5): 앵커드 검증기 3종+per-asset sanity+고아 경고+커맨드릿(exit) — 라우팅누수/CardFamily/미션튜닝 통합은 후속(§E H2/H3) |
| SSOT-Code Drift Sentinel | 검증 | M | high | SSOT 인용 수치(캡500·NetFreq 30/10/5/2·FF 0.5·토큰 10/3·캡200) vs 실 DA/UCLASS 조용한 드리프트를 헤드리스 diff | 📋 |
| Temp-Test-Value Tracker / Revert Guard | 검증 | S | high | 압축 테스트값(윈도우 50-120/보스 300·U14 하드캡)이 출시에 남는 침묵 리스크 → test/prod diff+원클릭 flip-back | 📋 |
| FPSR.Save.MigrationMatrix | 검증 | S | high | 버전드 세이브 조용한 필드 유실=메타 진행도 손실 → 전 SaveVersion+다운그레이드 무손실 단언(P6 확장 전 차단) | 📋 |
| Handoff/PROGRESS Auto-Sync + Guard | 자동화 | M | high | phase diff→PROGRESS/커밋 초안 + -A 스테이징/LFS smudge/doc-bleed 하드블록(문서클론 LFS 손상 실이력) | 📋 |
| Consult→§E Ingestion Autopiler | 자동화 | M | med | Docs/Review 미인입 리포트→§E 행+§C draft 자동전사(자문전용) → PM 수작업 제거 | 📋 |
| Codex-Review Batching Gate | 자동화 | S | med | 대형 diff 서브시스템별 청크→병렬 codex-review→병합(대형 phase 머지마다) | 📋 |
| FPSR.AttackToken.Debug | 계측 | S | med | 이원 토큰(근접 10/원거리 3, placeholder)이 200-300 스웜서 실제 상한 걸리는지 오버레이(BALANCE 재사용) | 📋 |
| FPSR.Content.AnimNullCheck | 검증 | M | med | VAT ClipIndex=0.0f(DEAD)서 적 애니 무크래시/null=휴면 CI 단언(Synty 베이크 전 회귀 방지) | 📋 |

### COOP — P5 4인 협동+Steam 세션+FF+DBNO 부활 (DONE · MP/서버권위 회귀)
| 툴 | 유형 | 공수 | 임팩트 | 왜 (병목) | 상태 |
|---|---|---|---|---|---|
| FPSR.Net.SoakBot (2-4 client) | 검증 | L | high | 리슨서버 4인 서버권위 불변식(Index+OfferId만·프리즈 대칭·트래블 후 입력/로드아웃 생존) 회귀를 봇 클라 soak 게이트 | 📋 |
| FPSR.Repro.SeedReplay | 자동화 | L | med | 런 시드(미션롤·rarity·스폰RNG·±10%이속) 캡처→재구동 원클릭 repro(밸런스 골든 diff 기반) | 📋 |

### MM-T0 — #3 다중맵 Tier 0 (ACTIVE NEXT · allocator=설계의 심장)
| 툴 | 유형 | 공수 | 임팩트 | 왜 (병목) | 상태 |
|---|---|---|---|---|---|
| FPSR.Alloc.Debug | 계측 | M | high | allocator=심장(R6). per-map alive/target/reserve/drain+뭉침 가중 실시간 — 없으면 1/1/1/1 붕괴 침묵. 최대 단일 레버 | 📋 |
| Collision-for-FlowField Validator | 검증 | M | high | BuildObstacleMask=ECC_WorldStatic 의존 → 미등록 면 조용히 미bake. 놓친 면 좌표+액터명 CI. A·B 공통 언블로커 | 📋 |
| FPSR.Significance.Debug | 계측 | S | high | S0-S3 티어(NetFreq 30/10/5/2) 색분류+헤드카운트 → NetCull 베이스라인 + §5 perf 선행 눈 | 📋 |
| FPSR.Net.Relevancy | 계측 | M | high | NetCull 미구현(~2.25km)→호스트가 클라당 전 적 복제. connection별 relevant vs ~150 → 문제 증명+수정 검증 | 📋 |
| FPSR.MultiMap.AllocatorProbe | 검증 | L | high | MM-T0 검증항목(전역캡200·빈맵드레인·bake/evict·진입시드·grace)을 헤드리스 불변식. T1 refactor 게이트 | 📋 |
| FlowField Bake Auditor (overlay) | 뷰어 | M | high | U7 2.5D 필드(2층·EdgeMask·CellFloorZ) per-cell 오버레이+클릭 인스펙터로 저작시점 검증(FPSR.FlowField.Debug 확장) | 📋 |
| Multi-Map Streaming/Registry Inspector | 뷰어 | L | high | 레지스트리 계약(ULevel*키·문→타깃·bake/evict·잔존 dormant/HISM)을 EUW로 저작시점 노출(맵 추가마다) | 📋 |
| Door-Break Replication Inspector **[가정]** | 검증 | M | high | MM-T0 #8-10(문 서버권위 복제+late-join·stream-in fail fallback). 문/스트리밍 코드 0=가정. SoakBot 연동 | 📋 |
| Spawn-Point Coverage / FOV-Gate Auditor | 검증 | M | med | AFPSREnemySpawnPoint(FOV+MinDist+ring) dead-zone 감사. 진입시드=측후방/문밖 스폰 필요(신규 맵마다) | 📋 |
| FPSR.Spawn.EventLog | 계측 | S | med | spawn/acquire-release/KillZ/recycle 이유 링버퍼 = "왜 안 나오나/사라졌나" 추적(Alloc 이벤트 백엔드) | 📋 |
| 2-Storey Surface Graph Inspector | 뷰어 | M | med | U7 유계 2층(NumLayers=2)이 >2 스택 조용히 드롭 → rank0/1·EdgeMask·경고 시각화 | 📋 |

### MM-T1/2 — #3 다중맵 Tier 1(예산 게임필)+Tier 2(텔포/split/은근한 비효율) · 시스템 미구현이라 대부분 [가정]
| 툴 | 유형 | 공수 | 임팩트 | 왜 (병목) | 상태 |
|---|---|---|---|---|---|
| FPSR.MultiMap.ContentAllocatorSim **[가정]** | 검증 | M | high | R6 핵심: content-aware allocator가 고가치 콘텐츠를 2인+에 집중 안 하면 1/1/1/1 파밍 최적→붕괴. 정책 스윕 헤드리스 시뮬 | 📋 |
| FPSR.MultiMap.RecyclePolicyProbe **[가정]** | 검증 | M | high | T1 silent recycle 안전게이트(NetCull밖·LOS없음·교전 10-15s·drain ≤10-15%/10s·pressure floor) 불변식 CI | 📋 |
| FPSR.MultiMap.BurstReserve.Debug **[가정]** | 계측 | M | high | burst reserve(평시10-15/진입20-30)·전환추적자(cap12)·pressure floor 오버레이(Alloc.Debug 확장) | 📋 |
| FPSR.Teleporter.Debug **[가정]** | 계측 | S | med | 텔포(T2) 개인+shared 쿨타임·채널·전투중 제한 오버레이로 4인 번갈아 남용 회귀 검증 | 📋 |
| FPSR.SplitDetection.Debug **[가정]** | 계측 | M | med | split 감지(솔로정찰태그·최대그룹·15-25s·전환 2중점유 grace) 시각화 | 📋 |

### SYNTY — Synty Path A 3맵 환경교체 + Paragon→Synty 적 + VAT 베이크 (ACTIVE NEXT)
| 툴 | 유형 | 공수 | 임팩트 | 왜 (병목) | 상태 |
|---|---|---|---|---|---|
| SyntyImportConditioner | 자동화 | M | high | Synty 임포트 계약(ECC_WorldStatic·Nanite OFF·LOD·atlas) per-asset pass/fail. 3맵×수백 조각 수동불가. 최대 언블로커 | 📋 |
| VATBakePipeline | 자동화 | L | high | 적 애니 DEAD(ClipIndex=0.0f). AnimToTexture 1 VAT 세트+FPSRVATAnimParams write-back. 공유 스켈 1베이크=전 적 | 📋 |
| FPSR.Perf.SwarmHarness | 검증 | M | high | §5 적500 한 번도 미측정. 100/200/300/500 램프 avg/p99 프레임+메모리 CSV. SyntyPilotBench 백본+하드캡 근거 | 📋 |
| SyntyPilotBenchHarness | 검증 | L | high | Roadmap §8 게이트(임포트+적300+U7+20분+3-4맵 상주메모리) pass/fail. SwarmHarness 선행(후보 팩마다) | 📋 |
| VATMaterialParamValidator | 검증 | S | med | VAT 스칼라 실명 없으면 SetScalarParameter no-op(정지·에러0) → 실명 존재 단언(베이크 짝 가드) | 📋 |
| MixamoRetargetBatch | 자동화 | M | med | Mixamo/Paragon→공유 마네킹 배치 리타겟(VAT 입력). 1회=전 호드 소스 | 📋 |
| PlaceholderSwapTracker | 검증 | S | med | §8 플레이스홀더(큐브적·스피어XP·FP팔·화이트박스·DrawDebug) 체크리스트+TODO 마커(스트레이 큐브 출시 방지) | 📋 |
| LFSAssetSizeAuditor | 검증 | S | med | Synty 대량 임포트 LFS 추적/예산 감사. -A/smudge 풋건(문서클론 손상 실이력)+잔존 디스크 감시 | 📋 |
| DrawCallAtlasReporter | 계측 | M | med | 드로우콜 소스별 버킷+atlas 병합 실패 노출 → Path A 로우폴리 제1원리 실측(3맵/프롭마다) | 📋 |
| Mission PointSet Editor / Reachability | 저작 | M | med | AFPSRMissionPointSet child 순서=미션 포인트. Synty 3맵 6미션 재저작 시 재정렬+경로+walkability | 📋 |

### P6-META — 메타 프로그레션(통화/업그레이드 트리/해금)+보스 콘텐츠+클리어 플로우
| 툴 | 유형 | 공수 | 임팩트 | 왜 (병목) | 상태 |
|---|---|---|---|---|---|
| Unlock Tree Editor (P6 meta) | 저작 | L | high | P6 메타(통화/트리/해금)=SaveGame 확장+런시작 GE. EUW 트리 에디터+순환/도달불가/orphan 검증(Meta-Tree 통합). RunFlow §2-11 | 📋 |
| CardPool Coverage & Dedup Auditor | 검증 | M | high | 추첨정합 사일런트 실패(중복/누락 CardId·orphan·Family충돌·가중편향) 감사. WeaponUnlock 누수 §2-3-4 불변식(P6 풀 확장) | 📋 |
| Boss Definition & Phase Validator **[가정]** | 저작 | M | med | P6 보스(2페이즈·약점·GAS)에서 BossDefinition 체력/약점/페이즈·BossTime·클리어플로우 정합 검증(페이즈 저작UI=가정) | 📋 |
| Clear-Flow State Machine Tracer **[가정]** | 계측 | S | med | 클리어플로우(종료→결과→정산→로비) 전이 로그로 종료사인/정산 누락=진행도유실 추적(시스템 미구현=가정) | 📋 |

### BALANCE — 콘텐츠 양산 튜닝(무기/카드/적/보스), G1 게이트 후
| 툴 | 유형 | 공수 | 임팩트 | 왜 (병목) | 상태 |
|---|---|---|---|---|---|
| Card Catalog & Balance Matrix | 뷰어/저작 | M | high | 폴리모픽 카드 magnitude가 인라인 서브오브젝트에 묻힘 → 그리드+편집. §2-3-8 | ✅ P1(그리드 in-place, `c4e0d77`) + ✅ **P2①(티어 생성/삭제=카드레벨 per-rarity + 산술 일괄연산 ×N/+N, 효과별 단위시임으로 +N 혼합거부, `3fb7da6`)**. 딜리버리=EUW 아닌 **C++ Slate 하이브리드**. 이상치뷰=후속 |
| Archetype TTK/DPS Matrix | 자동화 | M | high | 7 아키타입 DPS/STK/TTK vs 적 HP 커브 헤드리스 모델 → PIE 눈대중 대체(balance_dump/apply 확장) | 📋 |
| 20-min Run Pacing Simulator | 자동화 | M | high | MissionWindows 랜덤 롤이라 20분 페이싱 저작만으론 안 보임 → 다수 시드 타임라인 분포 | 📋 |
| FPSR.Balance.SimRun | 자동화 | M | med | 고 SetTimeScale 헤드리스 런→골든 CSV diff로 밸런스 변경 회귀(SeedReplay 결합) | 📋 |
| New Card/Fragment Scaffolder | 자동화 | M | med | §2-3-8 '새 카드 5분/Fragment 15분' → 템플릿 복제+CardId 시드+RarityTiers 채움+명명(u10 재사용) | 📋 |
| EnemyRoster Composition Auditor | 검증 | S | med | EnemyRoster 폴리모픽 SpawnRule(class+weight) 근접/원거리/스페셜 비율·빈풀 폴백 감사. §2-6 규격 | 📋 |
| Mission Content Scaffolder & Reward-Wiring Validator | 자동화 | M | med | 미션6종+DA+PointSet 저작 반복 → 새 미션 시드+보상카드가 해금풀 라우팅되는지(누수 방지) | 📋 |
| Card/Fragment Synergy Matrix | 뷰어 | M | med | G2 대상. rarity×Group/Family×Fragment 상호작용 축으로 시너지/dead-card. §2-3-9·G2 판정 지원 | 📋 |
| XP/Level-Up Curve & Freeze Tuner | 저작 | M | med | 레벨업 프리즈(bRunPaused)가 20분에 몇 번 끊나 XP곡선과 시각화. §2-2 후반 가팔라짐 타깃 | 📋 |
| Run Schedule Timeline Editor | 저작 | M | med | MissionWindows+BossTime 타임라인. §2-8 | 🔒H3=미션튜닝 DA-소유(RunFlow §2-8-1). ✅**P2③a(런타임: 폴리모픽 `UFPSRMissionTuning` DA소유 마이그레이션 소프트)** + ✅**P2③c(에디터: 타임라인 바 인터랙티브 드래그 편집=윈도우 가장자리 리사이즈·몸통 이동, `SetScheduleWindowTime`)**. 남은 **③b 콘텐츠(미션DA에 Tuning 저작+PIE)만**. 시간 타이핑·MissionPool=IDetailsView 무료(과설계 게이트) |
| Data Wiring / Orphan Editor | 저작 | M | high | 고아(카드/미션/무기)를 앵커 배열에 배선=역참조 편집(내장 Reference Viewer는 읽기전용). route-인지 preflight | ✅ P1(FPSR Data Editor guided-add, 고아 3축, `c4e0d77`) + ✅ **P2②(라우팅 누수 검증기: 카드 풀/무기 검증기가 부적격 배선을 H2 하드에러로 차단, `UFPSRWeaponValidator` 신설, 런타임 U18b 경로 제거). 첫 스모크서 실누수 1건 검출**. P0 서비스 재사용 |
| Card Description Snapshot Test | 검증 | S | med | GetDescription 자동생성이 효과/rarity 튜닝에 조용히 깨짐 → 골든 텍스트 diff | 📋 |
| DataAsset→SSOT Doc Generator | 자동화 | S | med | 저작 실값→SSOT fenced 블록 자동갱신(Drift Sentinel write-back 짝, 밸런스 패스마다) | 📋 |
| Enemy Scaling-Curve Editor | 저작 | L | med | ScalingProfile/Roster HP/공격력 시간커브 편집+per-archetype TTK 라이브(TTK Matrix 연동, L이라 이후) | 📋 |
| Card Effect Composer (wizard) | 저작 | L | med | Instanced Effects[] 다중효과 조립(5종)+rarity 프리뷰 위저드 → correct-by-construction 양산(L이라 카탈로그/스캐폴더 이후) | 📋 |
| First-Principle Variant Generator **[가정]** | 자동화 | L | low | 카드/적 후보 생성+제1원리 위반 자기필터. 앵커 없는 speculative=가정, 최저 신뢰 | 📋 |

### P7-SHIP — 폴리시 + §5 미측정 perf 패스(적500) + 패키지/출시 빌드
| 툴 | 유형 | 공수 | 임팩트 | 왜 (병목) | 상태 |
|---|---|---|---|---|---|
| FPSR.Insights.Enemy500Pass | 계측 | L | high | Insights+NetProfiler 자동캡처로 §5 적500 정량 종결(U1→U14 부채). 하드캡·RepGraph 도입 근거 | 📋 |
| FPSR.FrameBudget.HUD | 계측 | M | high | 리슨호스트 최악(자기렌더+적500 시뮬) 프레임을 movement/separation/flow/replication/render 버킷 → RepGraph 근거 | 📋 |
| FPSR.F6 Readability Auto-Capture | 계측 | S | high | 1인칭 가독성 5지표(F6) N프레임 P50/P90 자동캡처 → §5 가드레일 상시 참조 | 📋 |
| Ship Build Cook/Package Gate | 자동화 | M | high | 쿡/패키지+shipping 게이트로 DrawDebug/콘솔/테스트값 #if !SHIPPING 실제 제외를 쿡시점 검증(§8·Temp-Value 연동) | 📋 |

### LIVEOPS — 출시 후 텔레메트리/리텐션(D1 35%/D7 15%·~90런)
| 툴 | 유형 | 공수 | 임팩트 | 왜 (병목) | 상태 |
|---|---|---|---|---|---|
| Run Funnel Telemetry | 계측 | M | high | 리텐션(D1 35/D7 15·90런) 토대. 런 생애 이벤트 per-run JSONL(GMS 스파인 near-free). LIVEOPS 미터 백본 | 📋 |
| Co-op Togetherness Meter | 계측 | M | high | 핵심 명제(솔로=정찰/2인+=본게임) 검증/반증 유일 계측. 근접/공유맵/back-to-back 비율 | 📋 |
| Death-Cause & Wipe Attribution | 계측 | S | high | DBNO/사망/와이프 원인+맥락(적수·산개·클럭·FF) → FF치사·스페셜 압박 의도 검증 | 📋 |
| Card/Weapon Pick Distribution | 계측 | S | med | 픽률·승률상관·dead-card 표 → G2 빌드다양성 출시후 데이터 검증 | 📋 |
| Playtest Session Recorder **[가정]** | 계측 | M | med | 런클럭 타임라인+위치/맵+프리즈+사망+스샷. 녹화 인프라 없음=가정, 편의성 | 📋 |

---

## 3. 관통 테마 (전 로드맵 5)
1. **침묵하는 실패를 저작/베이크/CI red line으로 당김** — 최악 버그는 조용하다(ECC_WorldStatic 없어 미bake·부분적용 카드·출시된 압축 테스트값·SetScalarParameter no-op·세이브 필드 유실). 고임팩트 툴 대부분(Collision Validator·Batch DataAsset Validator·VATMaterialParamValidator·Temp-Value Tracker·Save.MigrationMatrix·Card Description Snapshot)이 "PIE까지 침묵"을 non-zero exit로 앞당긴다.
2. **allocator가 심장 — 오버레이(눈)→불변식 하네스(게이트)→togetherness(검증) 3단** — R6: 그룹 버프 전면 폐기로 "뭉치면 효율"은 오직 content-aware allocator가 미션/보스/엘리트를 2인+ 그룹에 집중시키는 것으로만 성립. `FPSR.Alloc.Debug`+`AllocatorProbe/ContentAllocatorSim/RecyclePolicyProbe`+`Co-op Togetherness Meter`가 다중맵 전 티어를 관통하는 최대 단일 레버 체인.
3. **§5 적500 perf는 한 번도 측정 안 됨 — 램프 하네스가 Synty 채택·하드캡·출시를 태운다** — ~200-300/캡500·NetUpdateFreq·미구현 NetCull은 전부 가정. `FPSR.Perf.SwarmHarness`가 SyntyPilotBench 백본·§5 P7 종결·양산 회귀 벤치를 태우고, Significance/Net.Relevancy/FrameBudget.HUD/Insights.Enemy500Pass가 리슨호스트 최악 케이스를 실 코드경로에 귀속.
4. **죽어있는 VAT 파이프라인 = Synty 적 재저작 전체의 빠진 Stage-3** — 적 애니 C++ 완전 배선인데 `ClipIndex_*=0.0f`(TODO Stage3=DEAD)로 무동작. `MixamoRetargetBatch`(입력)→`VATBakePipeline`(베이크)→`VATMaterialParamValidator`/`AnimNullCheck`(no-op·크래시 가드)가 한 체인으로 죽은 적을 살린다.
5. **재발명 말고 얇게 확장 — 기존 하네스가 최고 ROI** — headless `-run=pythonscript`(u10/balance_dump·apply)·`FPSRoguelite.Smoke.*`·cvar 오버레이(`FPSR.FlowField.Debug`)·`GameplayMessageSubsystem` 스파인·`codex-review/consult-codex.ps1`이 이미 존재. 다수 S/M 툴(Scaffolder·Snapshot·SimRun·Telemetry·Batching Gate·Drift Sentinel)이 이 인프라의 얇은 확장으로 near-zero 비용·고레버리지.

---

## 4. 통계
- **총 65개**(dedup 후, §2 phase 매트릭스 기준. §1 seam 9개는 그 부분집합이라 별도 카운트 아님). 유형별(대표, 겸용 툴=대표 유형 1회 계상): **검증 20 · 계측 18 · 자동화 15 · 저작 7 · 뷰어 5**.
- **[가정] 10개**: Door-Break Replication Inspector · ContentAllocatorSim · RecyclePolicyProbe · BurstReserve.Debug · Teleporter.Debug · SplitDetection.Debug · Boss Definition&Phase Validator · Clear-Flow State Machine Tracer · First-Principle Variant Generator · Playtest Session Recorder (미구현 Tier1/2·P6 시스템 의존).
- **신규(비흡수)**: Unlock Tree Editor · Boss Definition&Phase Validator · EnemyRoster Composition Auditor · Mission Content Scaffolder · MM-T1/2 4종 · Clear-Flow Tracer · Insights.Enemy500Pass · Ship Cook Gate 등. 나머지 다수는 직전 백로그 흡수(상태 📋 이관).

---

## 5. 이 문서 갱신 방법 (living doc 규칙)
- 툴 진행 시 **상태 컬럼만 갱신**(📋→🔨→✅) 또는 🗑️(폐기 사유 병기). 상태값은 재작성/재정렬 시에도 **반드시 이관**.
- 새 툴 발견 시 **가장 먼저 필요해지는 phase** 표에 행 추가(유형/공수/임팩트/왜/상태). 여러 phase 상속이면 §1 seam에도 등재.
- 우선순위·순서 바뀌면 §1 seam·§2 phase 배치 재조정. [가정] 툴은 의존 시스템 구현되면 [가정] 제거 + 근거 갱신.
- 실제 빌드 착수 = 별도 phase 브랜치 유닛(§B-3 프로토콜). **이 문서는 백로그(계획)이지 실행 로그 아님** — 완료 상세는 git log.
- 재작성 시: 기존 상태값 추출(`grep -oE '📋|🔨|✅|🗑️'`) → 신규 문서 이관 후 덮어쓰기(별도 v2 파일 금지).
