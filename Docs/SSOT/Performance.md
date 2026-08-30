# Performance — 성능 / 네트워크 예산 (FPSRoguelite SSOT 분할)

> `Game.md`(SSOT 허브)의 분할 문서. **섹션 번호(§x)는 원본 그대로 보존** — 소스 주석·교차참조 호환.
> 작업 시작 전 허브 `Game.md` + `PROGRESS.md`를 먼저 읽고, 적 대량화·복제 예산·Significance·플로우필드·프로파일링 관련 작업 시 본 파일을 연다.
> 담는 섹션: §5 성능·네트워크 예산(§5-1 Significance 티어, §5-2 Flow-Field 적 타겟).

---

## 5. 성능 / 네트워크 예산 (⚠️ P2 착수 전 수치 확정·검증 — 최우선 보완)

> 이 프로젝트 최대 리스크는 **적 500마리 협동의 성능/복제 예산이 미수치화된 점**이다. 아래는 잠정값이며 Unreal Insights + NetProfiler로 검증·조정한다.
> **⚠️ 검증 시점 (확정 2026-06-10)**: 당초 P2 예정이었으나 미실시 → **P4-C 무기 콘텐츠(6종) 완료 직후, 코어 재미 게이트(§7-5)와 함께 일괄 측정**한다. 그때까지 본 §5 수치는 *미검증 가정*이며, 그 위에 쌓인 P4 콘텐츠는 검증 결과에 따라 조정될 수 있다.
>
> **⚠️ 측정 미실시 갱신 (2026-06-30, U1 게이트)**: U1은 G1 재미(①~④) + MP 조건으로 **합격**했으나, **§5 적500 정량 측정(Insights/NetProfiler)은 미실시(보류)** — 패키지 재빌드 미수행(사용자 결정). 따라서 **하드캡은 아래 잠정값 유지**, 적500 정량 + 하드캡 확정은 **콘텐츠 밸런싱/U14 perf 패스로 이월**. **코드 실측 확인분(2026-06-30, 6에이전트 read-only 워크플로)**: 하드캡 500(`MaxActiveEnemies` constexpr)·NetUpdateFreq 티어(S0 30/S1 10/S2 5/S3 2Hz, 거리기반·티어변경시만 set)는 코드 일치 / **NetCullDistance는 미구현**(아래 교정) / 복제 = Push Model(`net.IsPushModelEnabled=1`), **RepGraph·Iris 미사용** → 판정 시작점 Push 맞음.
>
> **⚠️ 적 규모 실측 갱신 (2026-07-01, 실플레이)**: 동시 개체수 현실 상한 = **~200-300**(코드 폴백캡 300 부합; 하드캡 `MaxActiveEnemies`=500은 헤드룸으로 유지). Game.md §1 규모 프레이밍을 ~200-300으로 정정. **함의**: perf·가독성 게이트 목표치를 **200-300 기준**으로 재설정 권장(500은 스트레스 상한으로만). 최대 리스크(1인칭 가독성·복제 예산)는 500이 아니라 ~300에서 성립하면 되므로 **리스크 규모가 축소**된다.
>
> **🎯 목표 프레임 예산 확정 (사용자 결정 2026-08-12)** — M0 (b)의 [결정] 선행 해소(`Roadmap.md §7-6`). 기준 = **개발 PC(측정 시 사양 명기) · 리슨서버 호스트 · `L_Map1_City` · 적 300/500 고정 스폰 · 셀셰이딩 on/off 교차**.
> - **적 300(통상)**: 평균 **≥60fps(16.6ms)** · **P95 프레임타임 ≤20ms**(히치 기준) — 합/불 판정선
> - **적 500(스트레스)**: 합불 아님 — **≥30fps 헤드룸 확인**용
> - **스웜 렌더 서브예산**: 베이스패스+Custom Depth+그림자 합 **≤4ms @300** — 인스턴싱/VAT 렌더 경로(M0 (a″) VAT-1) 판정 잣대. 참고 실측: Custom Depth 단독 1.33ms@300(2026-07-10 SRS 파일럿)
> - **측정 빌드 = 패키지 Test 구성**(Shipping급 최적화 + stat/Insights 가능). ⚠️ 패키지에선 Push Model이 꺼진다(N-1, 아래 🔴 주) — **그 상태 그대로 잰다**(현재 유지 아키텍처의 실비용이므로), 측정 기록에 이 전제를 명기. N-1 해소 시 동일 구성 재측정으로 전후 비교.
>   🔴 **정정 2026-08-13**: 런처(Installed) 엔진은 Test 구성 빌드를 **지원하지 않는다**(*"Targets cannot be built in the Test configuration with this engine distribution"* 실측). Shipping은 stat/CSV가 빠져 측정 불가 → **실측은 Development 패키지로 수행**(수치는 Test 대비 5~15% 보수적 — 합격 판정엔 안전한 방향). 소스 엔진 도입 시 Test로 재측정.
>
> **📊 VAT-1 실측 (2026-08-13, `phase/vat-renderpath-spike`)** — 고정 시나리오(`L_Map1_City` 리슨 호스트 · `FPSR.SpawnEnemies N 6000` 60m 링 수렴 · PlayerStart 고정 카메라 · Development 패키지 · `Scripts/measure_swarm_render.ps1`):
> | | **B(CPD 채택)** @300 | A(대조군) @300 | B @500 |
> |---|---|---|---|
> | 프레임 평균 | **5.48ms(182fps)** | 6.48ms(154fps) | 7.11ms(141fps) |
> | P95 | 7.99ms | 8.08ms | 9.68ms |
> | 스웜 렌더 합(BasePass+Shadow+CustomDepth) | **2.05ms** | 2.68ms | 3.03ms |
> | RHI 드로우콜 | 445 | 529 | 562 |
>
> **📊 M0 잠정 베이스라인 실측 (2026-08-28, `perf/enemy-baseline-300-500`)** — 시나리오 = **`L_Arena?listen`**(ADR 0012 지속 레벨, 전투 공간은 스트리밍 서브레벨 `L_Map_1`) · `FPSR.SpawnEnemies N 6000` 60m 링 · PlayerStart 고정 카메라 · **Development 패키지** · `Scripts/measure_swarm_render.ps1`. 셀 축 off = `r.PostProcessing.DisableMaterials 1`(블렌더블만 차단, 톤매핑·블룸·노출 유지 — 패키지 빌드 1개로 교차 성립).
>
> | 구성 | **실제 생존** | 프레임 평균 | P95 | GameThread | GPUTime | 스웜 렌더 합(Base+Shadow+CD) | 드로우콜 |
> |---|---|---|---|---|---|---|---|
> | 300 요청 · 셀 on | **225** | 4.50ms(222fps) | 5.09ms | 2.70ms | 4.08ms | 1.28ms | 159 |
> | 300 요청 · 셀 off | **226** | 4.51ms(222fps) | 5.29ms | 2.98ms | 4.06ms | 1.40ms | 164 |
> | 500 요청 · 셀 on | **394** | 4.96ms(202fps) | 6.29ms | 4.56ms | 4.30ms | 1.46ms | 167 |
> | 500 요청 · 셀 off | **398** | 4.50ms(222fps) | 5.39ms | 3.93ms | 4.02ms | 1.39ms | 161 |
>
> **판정: 통과**(적300 평균 ≥60fps·P95 ≤20ms → 222fps·5.09ms로 **약 3.7배 여유**, 스웜 렌더 ≤4ms → 1.28ms, 적500 ≥30fps → 202fps). **다만 아래 한계 때문에 이 표는 `(b) 공식 베이스라인`이 아니라 하한(floor) 측정이다.**
>
> ⚠️ **한계 4가지(전부 실측 근거 있음 — 다음 세션이 이 표를 과신하지 않게 명시한다)**
> 1. **요청 적 수가 안 나왔다** — 300 요청 → 225·226, 500 요청 → 394·398(약 75~79%). KillZ·스폰 실패 로그 0건이고 생존 수가 캡처 내내 고정이라 처음부터 그만큼만 스폰됐다. **판정선은 "적 300"인데 300에서 잰 것이 아니다.** GameThread 가 적 수에 거의 선형(225→394 에서 2.70→4.56ms)이라 300 환산 ≈4.7ms 로 결론은 안 뒤집히지만, 그건 측정이 아니라 외삽이다. → 후속 행 「디버그 스폰 명령 요청수 미달」. ✅ **원인 규명·해소 2026-08-30**(main 머지 `ac5ff629`): `AcquireEnemy` 의 엘리트 캡이 `curve 0` 이라 로스터가 뽑는 엘리트를 전부 거부했고, 그 경고가 엣지 트리거라 디버그 명령이 겪은 거부는 무음이었다. 명령이 유계 재시도 + 전달수·구성 상시 로그를 갖도록 고쳤다(런타임 검증 `delivered 300/300`).
> 2. **셀 on/off 비용이 측정되지 않았다** — 300 에서 4.50 vs 4.51, 500 에서는 셀 on 이 오히려 느리되 그 차이가 GPU 가 아니라 **GameThread**(4.56 vs 3.93)에서 난다. 같은 GPU 구성의 실행 간 CPU 변동이 15% 라는 뜻이고, 셀의 GPU 비용(0.02~0.28ms)은 그 노이즈에 묻힌다. 스크린샷상 두 구성의 렌더는 확연히 다르므로 **셀은 그려지되 쌀 뿐**이다. 셀 비용을 정말 알려면 반복 실행 + GPU 프로파일 분리가 필요하다.
> 3. **씬이 비어 있다** — 현행 아레나는 블록아웃이라 `GPU/BasePass = 0.07ms`, 즉 래스터라이즈되는 것이 사실상 없다. 드로우콜 159~167 도 VAT-1 도시맵(445)의 3분의 1이다. **출시 아트가 들어오면 이 표의 여유는 줄어든다.**
> 4. **정적·단독 시나리오가 아니다** — 카이팅 없음(고정 카메라), 그리고 측정 중 배경에 브라우저(영상 재생)가 떠 있었다. 후자는 수치를 **나쁜 쪽으로** 밀므로 통과 판정을 뒤집지는 않는다.
>
> ⚠️ **정정 (2026-08-30, 사용자 결정) — 종전 이 자리의 「`(b)` 공식 베이스라인 = 카이팅 실플레이 + 아트 적용 후」는 EC ① 의 요건이 아니다.**
> `Roadmap.md` §7-6 **M0 EC ① 원문**이 요구하는 것은 넷뿐이다 — ① 측정 빌드 구성 명기 ② **적 300/500 실측** 프레임값(추정치 아님)
> ③ (a′)로 확정된 맵 위에서 ④ (a″)로 확정된 렌더 경로가 적용된 상태. **「카이팅」도 「아트 적용」도 원문에 없다.**
> 둘 다 이 블록(2026-08-28)이 사후적으로 도입한 표현인데, 이후 보드 행 로그들이 그대로 인용하며 사실상 사양처럼 굳어
> **M0 ↔ M2 순환 의존**을 만들고 있었다 — 아트는 M2 소속인데 M0 Exit 이 아트를 요구하면 §6-9 (8) 개시 규칙상 M0 을 영원히 못 닫는다.
>
> **사용자 결정(2026-08-30) = 블록아웃 기준으로 EC ① 을 닫는다.** 따라서:
> - 위 표의 EC ① 미달 항목은 **한계 ① 하나뿐**이었고, 그 원인은 2026-08-30 해소됐다(`ac5ff629`).
> - 한계 ③(빈 씬)·④(카이팅 없음)는 **사실로 유지하되 EC ① 의 게이트가 아니다.** 아트 적용 후 재측정 = **M2 EC ③**(보드 행 「성능 재측정」).
> - 이 표는 정확량(적 300/500) 재측정으로 대체되기 전까지 "현 시점 하한"으로만 인용할 것.

> **전 게이트 통과(여유 2~3배) → 스웜 렌더 경로 = CPD(경로 B) 채택**(`Docs/Architecture/0007`). 이 표는 (a″) 완료 시점 참고치 — **(b) 공식 베이스라인은 별도 실측**(카이팅 실플레이 포함, 이 표의 시나리오 재사용 권장). 드로우콜 완전 병합 검증(V1)·원격 클라 CPD 리셋 2클라 테스트(V5)는 VAT-2로 이월.

| 항목 | 잠정 목표 | 비고 |
|---|---|---|
| 최대 활성 적 수(서버) | 하드캡 500(`MaxActiveEnemies` constexpr, 코드 실측 2026-06-30), 통상=스케줄 `MaxAliveCount`/`AliveCountByLevel` 주도(코드 폴백캡 300) | 풀 고갈 시 스폰 보류. ⚠️"통상 200~350"은 잠정 문서값 — 실측 통상치는 활성 스케줄 에셋 MaxAliveCount |
| 클라이언트별 관련(relevant) 적 | 상한 ~150 | relevancy cull |
| 적 NetCull | **P-H 구현**(2026-07-10): 멀티슬롯 유니파이드 = footprint 버블 균일 `R=max(무기사거리, min(무기사거리+seam, 슬롯대각+seam))`(현 132m 슬롯 ≈140m, 현 200m 대비 relevant 면적 ~2배↓); 단일맵 = ctor 기본 200m(BP `NetCullRadius` per-archetype 튜닝, BeginPlay 반영) | 대칭 거리컬이라 seam-only 불가(자기슬롯 완전커버 ⟺ 이웃 slot bleed) → **RepGraph=진짜 공간 relevancy 해법·별도 후속**(단계서 per-acquire 반경 적 재-bucket 필요). 클라 far-slot/seam 팝인=수용 D3 한계. 대역폭 PIE 실측=사용자 |
| 적 NetUpdateFrequency | 위협도별 S0 30Hz / S1 10Hz / S2 5Hz / S3 2Hz (코드 실측 일치 2026-06-30) | 거리기반(최근접 플레이어), 티어변경시만 SetNetUpdateFrequency(적500 핫패스 가드, W1 P2) |
| 적 Dormancy | 원거리·비활성 DORMANT, 접근 시 wake | |
| 적 복제 상태 | Transform(위치/Yaw) + `UFPSREnemyHealthComponent`의 **3프로퍼티**(`Health`·`MaxHealth`·`bDead`, 전부 Push Model) | 히트/사망 코스메틱은 GameplayMessage/Cue (복제 액터 상태 아님). **계약 정정 2026-08-11 — 아래 주 참조** |
| XP/픽업 | 개수 cap + 인접 병합, 자석=클라 코스메틱·서버 권위 수령 | |
| 복제 발사체 액터 | **팀별 분리 예산**(U5, 2026-06-30): 플레이어 ≤64 / 적 ≤`FPSR.Enemy.ProjectileBudget`(기본 32, 천장 100) | **⚠️갱신 2026-07-08(무기 전면 투사체화)**: 이제 **차지레이저(히트스캔)·근접 외 전 플레이어 무기 = 복제 발사체**(연사총도 포함). 캡 ≤64 초과 시 팀내 FIFO 회수=총알 조용히 소멸 → **연사 무기는 고속·단수명 필수**로 동시 비행을 낮게 유지(무기별 피크 ≈ `FireRate×Lifetime×Pellet`, `UFPSRWeaponDataAsset::IsDataValid`가 >12 경고). 실측 4인 최악 피크 ≈ 무기당 <8 → 합계 <32(캡 여유). PIE 4인 시뮬 `UFPSRProjectileSubsystem::GetActiveCount()` 피크로 실검증. 팀별 FIFO라 적 사격↔플레이어 상호 잠식 없음. 원격 클라 시각 예측=별도 후속(A3, 호스트/싱글은 즉발) |
| 적 공격 판정 | 서버 배치 처리(거리 체크 배치) + **공격 토큰 상한** | 플레이어당 동시 공격 시도 적 수 제한(§2-6) |
| **호스트(리슨서버) 부하** | 자기 클라 렌더 + 적 500 서버권위 시뮬 **동시 부담 = 최악 케이스** | 전용 서버 없음(§2-10) → 호스트 프레임예산 **별도 측정**, 하드캡은 호스트 기준으로 결정. 부족 시 RepGraph·시뮬 LOD·스폰 보류 우선 |

**⚠️ 적 복제 상태 계약 정정 (사용자 결정 2026-08-07 · 반영 2026-08-11)** — 종전 계약은 "Transform만 최소 복제, 체력=서버 권위"였는데 **실제로는 3개**를 복제하고 있었다(`FPSREnemyHealthComponent.cpp` 실측). 결정 = **코드 유지 + 계약을 실제에 맞춤**. 각각 남는 이유:
- `Health` — 원래 계약에 있던 것.
- `MaxHealth` — 클라가 체력바 **비율**을 계산하려면 필요. **스폰 때 한 번만** 바뀌므로 지속 비용은 사실상 0.
- `bDead` — `Health <= 0`에서 파생 가능하지만, **풀 재사용 때 `true→false` 전이를 클라가 놓치지 않게 하는 엣지 감지**용이다(`OnRep_bDead`). 클라에서 `Health` 엣지를 추론하는 안은 기각 — **비트 하나보다 엣지 감지 정확성이 우선**(적을 재사용하는 구조라 놓친 전이는 곧 유령 시체·체력바 잔존으로 나타난다).

**리플리케이션 도구 평가 순서**: Push Model(기본) → 부하 시 **Replication Graph**(spatial grid relevancy, 검증된 도구) → 그래도 부족 시 Iris(Beta) 평가. **Iris를 1순위로 두지 않음**(RepGraph가 다수 액터·연결별 relevancy 병목에 더 직접적).

> 🔴 **이 "복제 = Push Model" 전제는 출시(패키지) 빌드에선 성립하지 않는다** — Push Model이 컴파일 단계에서 빠져 비교 기반 복제로 폴백한다(동작은 정상, 의도한 CPU 절감이 없음). 미해결·결정 대기 = **`Docs/OpenIssues_Network.md` N-1**. 호스트 프레임 예산을 계산할 때 이 차이를 빼고 세지 말 것. ✅ *측정 항목 등록 완료 2026-08-19(U14R)* — 메트릭·시나리오·잠정 판정 기준 = `Docs/Specs/U14R_PerfMeasureRegistry.md` §5-A, 실측 = §5-3 레지스트리.

**다중맵 예산 모델 (설계 수렴 2026-07-05, `Docs/Review/20260705-multimap-budget-regroup.md`)** — ⚠️ **per-map 레지스트리·map-aware allocator 기구는 U 연속필드로 대체됨**(다음 문단; **전역 공유 캡 원칙은 U에 계승**). (원안 참고) 심리스 다중맵에서도 예산은 **전역 공유 캡**(맵 수 무관 호스트 전역 상한 — per-map 캡 금지=붕괴; 잠정 전역 200, perf 후 확정). 단일 `FPSREnemySpawnSubsystem` → **map-aware allocator**(점유맵 배분, "2인+ 맵 > 솔로 맵" 가중, 빈 맵 target=0+하드 드레인), U7 플로우필드 → **per-map 레지스트리**(`ULevel*` 키·stream-in bake·stream-out evict, bake는 ECC_WorldStatic 의존이라 콜리전 등록 후). 새 맵 진입 공백은 **예약 헤드룸(진입 시드) + 백그라운드 silent recycle**(Kill 아님·NetCull밖·LOS없음·최근교전/미션/엘리트 보호·drain rate ≤10-15%/10s·local pressure floor)로 채움. 복제 = **NetCull 구현(§5 1순위 미구현 레버) → RepGraph 앞당김**(다중맵+분산이 connection별 relevancy를 필수화). **map-aware allocator = 적 예산 + 콘텐츠(미션/보스/엘리트/이벤트) 배분 공동**(디렉터 결정 2026-07-05: 그룹 버프 전면 폐기 → "뭉치면 효율"은 고가치 콘텐츠를 2인+ 그룹에 집중시켜야만 성립, allocator가 설계의 심장). **맵 잔존(언로드X·LOD컬)**: 픽업/문/상자는 dormant/HISM 경량 잔존(이관/소멸 로직 불요, 백트래킹 유혹 방지 위해 큰 성장은 미션/보스/상자), **적만** 빈 맵 하드 드레인(예산 회수). Tier 0(코어)/1(예산 게임필·콘텐츠 allocator)/2(텔레포터·은근한 비효율) 스코프·시작 수치 = 리포트.

**다중맵 U 대전환 — 단일필드 재계산 예산 (설계 2026-07-07 `Docs/Review/20260707-plan-continuous-field-arch.md` · 구현 완료·main 머지 `34b5eea`)**: 위 per-map 레지스트리 → **U(고정 3×3 단일 flow 그리드)**로 피벗(구조 `Architecture.md §4-1`, allocator FrontId=`RunFlow.md §2-1`). **재계산 예산**: 단일 그리드 셀수 = (3·슬롯셀/축)². **D1 확정 슬롯 100~132m/변** → 200cm 셀 기준 **100m=22,500셀 / 132m=39,204셀**(단일 30m맵 2,025셀의 **11~19배 BFS**). 문 열림마다 이 크기의 단일 `RunBFS`(+generation bump). **P-0 합성 벤치**(worldless ≈39k셀 `RecomputeField` 타이밍)로 콘텐츠 전 조기 반증 완료 → **프로덕션 near-cap/실맵 재계산 ms 정량은 실콘텐츠 perf 패스로 이월**(§5 적500 정량과 함께). 셀상한 게이트 = `MaxTotalCells=40000`·`MaxGridDimPerAxis=256`(코드 상수 `FPSRFlowFieldComputer.h`) **fail-fast 잠금**(오늘은 초과 시 조용히 CellSize coarsen=품질↓ → U는 콘텐츠 계약으로 차단). **NetCull(P-H 구현 2026-07-10)**: 죽은 offset contract 제거 → **Option A 교전/무기사거리 버블**(footprint 상한, 멀티슬롯 균일 `R=max(WeaponRange, min(WeaponRange+seam, SlotDiag+seam))`; 단일맵=ctor 200m). ⚠️**적대게이트 확증**: NetCull은 대칭 거리컬이라 "seam-only 복제"는 수학적 불가(자기슬롯 완전커버 R≥슬롯대각 ⟺ 그 R이 이웃 슬롯 통째 bleed) → 순수 NetCull은 교전버블+무기사거리 floor까지가 한계. **RepGraph(spatial grid relevancy) = 프로덕션 해법·별도 페이즈**(per-acquire NetCull 반경 적 재-bucket 핸드오프); 클라 far-slot/seam pop-in = 수용된 Tier-0 한계(서버 추격은 심리스, 클라 시각 이슈지 로직버그 아님). **World Partition Data Layer=미채택**(스트리밍/가시성 직교, per-enemy relevancy·flow 도구 아님).

**🎯 1인칭 가독성 설계 목표 (가드레일 · 컨설트 2026-07-01 F6 — `Docs/Review/20260701-concept-conclusions.md`; 2026-07-02 게이트→목표 강등, 1인칭 확정)**: Concept §1-C-3대로 1인칭은 **확정**(시점 폴백 없음)이라 아래 5지표는 go/no-go 게이트가 아니라 **HUD·오디오·적 밀도 튜닝의 설계 목표(가드레일)**다 — 벗어나면 시점 교체가 아니라 가독성 설계로 되돌린다. perf 수치만으론 "에임이 의미있나 vs 난사"를 못 잡으므로, 아래 **최소 5지표**(1인 팀 계측 싼 것)를 상시 참조 목표로 둔다:

| # | 지표 | 통과 임계 |
|---|---|---|
| ① | 활성 적(서버) | 실전 200~300 / 스트레스 500(붕괴 확인용) |
| ② | 클라 relevant 적 | P90 ≤ 150 |
| ③ | 화면 내 보이는 적 | P50 ≤ 40, P90 ≤ 70 |
| ④ | 15m 내 즉시 위협 적 | P90 ≤ 25 |
| ⑤ | 시각 위협 큐 동시표시(§PlayerFeel 2-14 F3) | ≤ 3 (+후방집계 1, 45도 병합) |

5지표 충족 = 1인칭 가독성 설계가 **목표선 안**(1인칭 확정이라 시점 재검토는 없음 — 목표 이탈 시 HUD 큐/오디오/밀도로 교정). 체감 성립 = 10~15분 플레이에서 **"4솔로 아니라 서로 커버했다"가 반복**되는지 확인(5지표 + 짧은 플레이 기록 리뷰). 참고(텔레메트리 필요): aim uplift ≥1.35x·무음 원거리차징 70%가 1.5s내 반응·오탐율 ≤20%.

### 5-1. Significance 티어 (플러그인 enable ≠ 최적화)
적/VFX/SFX/anim tick/mesh/healthbar를 단계별로 다운. **AI update budget에도 연동.**
- **S0** 근접 위협: full update
- **S1** 근거리: 저빈도 update
- **S2** 중거리 군집: anim·VFX 축소
- **S3** 원거리: coarse movement·no cosmetic

### 5-2. Flow-Field 적 타겟 (P2)
**고정맵 grid + 단일 목표점 field(가장 가까운 플레이어) + local separation steering**으로 시작. 타겟 규칙(가까운/위협도/파티중심/미션목표)은 데이터로 전환 가능하게. Flow-Field를 플레이어별로 둘지/파티중심·목표점별로 둘지 결정 필요. 동적 장애물은 비용 대비 나중에. separation batch update 주기·충돌 처리 범위 정의.
- **구현 보강(P4, 2026-06-09 main 머지 — `phase/p4-enemyspawnpoints` 코드분, Codex 5R 하드닝)**: ① **디자이너 배치 스폰포인트** `AFPSREnemySpawnPoint`(Weight/ZoneTag/MinPlayerDistance/bEnabled) + `UFPSREnemySpawnSubsystem`이 전 플레이어 비가시(FOV)+거리 게이트로 가중랜덤 선택(후보 0 시 링 폴백 → 미배치 맵도 동작). 디자이너 지점은 권위적 위치라 ground-snap 생략(실내/지붕 천장 스냅 방지). `SetActiveSpawnZone` 훅은 TimeGate(§2-8) 후속. ② **플로우필드 장애물 마스크 + BFS 라우팅 + 적 중력/지면추종**(`AFPSREnemyBase`, 경사/계단 소폭 보정). 하드닝: 월드 밖 추락 적 **KillZ 회수**(슬롯 영구점유 방지), 접촉 데미지 **수직 게이트**(바닥 관통 타격 방지), 동일 스폰지점 중첩은 **분리(separation)의 동일위치 결정적 푸시**로 근본 해소(스폰 위치 비이동). **후속(C1, `phase/p2-flowfield-height`, PIE 의존)**: 멀티레벨/높이 인지 BFS + 플로우필드 셀 **클리어런스 인지 프로브**(현 전셀 오버랩은 경계 벽 양쪽 셀 차단 → 좁은 통로 과차단 트레이드오프). 콘텐츠 스폰포인트 배치(~~L_Sandbox~~ → **`L_Map1_City`**, 🔁 *정정 2026-08-13*: `L_Sandbox`는 실재하지 않는다)는 사용자 PIE 후 별도 머지 — ✅ **완료 2026-08-12**(적 스폰포인트 18개 배치, `Roadmap.md` §7-6 M0 (a′)).
- **U7 = C1 높이/클리어런스 구현(코드 완료·검증, `phase/p2-flowfield-height`, 2026-07-01)**: **2.5D 높이 인지 플로우필드**. `BuildObstacleMask`에서 셀당 **Z-스텝 반복 다운트레이스**(ECC_WorldStatic, 컬럼 적층 표면 전부 포집·병합메시 포함)로 up-facing(normal.Z≥0.71 walkable) 후보 수집 → **지면(`GridOrigin.Z`)에서 클라이머블 스텝 플러드필**로 셀별 **도달 가능 walking surface**(`CellFloorZ`) 확정(램프/계단은 오르되 벽/천장 윗면·절벽엔 안 감). 점유/edge 프로브를 **셀 자기 바닥높이**에서 수행 + **스텝게이트를 `EdgeTraversable`에 굽기**(연속 램프=경사 상한 `ActiveCellSize·tan(max각)`, 평지 단차=`ClimbableStepHeight`, `MaxStepHeight=45` 미러; **`GroundSnapTolerance`=60으로 클램프**). 대각선 흐름은 2×2 코너 4-edge 요구(높이-게이트 우회 차단). **Part B(좁은통로)**: edge 박스 전셀폭→**footprint폭**(과차단 해소). **핵심 성능계약**: 모든 신규 비용=**1회성 BuildObstacleMask**, 0.2s 멀티소스 BFS·steepest-descent·500적 샘플=**순수 배열연산 유지**(월드쿼리 0, RecomputeED에 가드주석). **평지 무회귀**(CellFloorZ≈GridOrigin.Z). **바운드볼륨 per-map 오버라이드**: `ClimbableStepHeightOverride`·`ProbeApexAboveOriginOverride`(apex는 천장 아래로). **수용 한계(당시)**: 동일 XY 수직중첩(다리 위 통로)=단일 표면(최상단 아닌 지면 우선) → **아래 "U7 멀티레이어" 불릿에서 유계 2층으로 확장**(제1원리 = *무제한* 3D/레이어 그리드는 여전히 거부, 층수 유계). 검증=빌드×8+헤드리스 스모크×7+**Codex 머지게이트 7R**(구현 P2 6건 전부 교정, 7R=설계 경계 문서화). **실맵 PIE(계단·단차 추격, 좁은통로)=사용자**(~~L_Sandbox 현재 평지라 계단/램프/플랫폼 배치 필요~~ → 🔁 *정정 2026-08-13*: `L_Sandbox`는 실재하지 않고, 현행 **`L_Map1_City`는 이미 2단 지면**이다 — 주 지면 Z=200 + 고가 지면 Z≈800, 단차 600cm라 계단·다리로만 연결(`Roadmap.md` §7-6 M0 (a′) 실측). 배치 요구는 **충족됐고 PIE 검증만 남는다**).
- **U7 멀티레이어(유계 2층) 확장(코드, `phase/p2-flowfield-height`, 2026-07-01)**: 같은 XY에 수직중첩된 두 walkable 표면(예: 지면 + 겹친 2층 덱/메자닌) 지원 → 2층 플레이어를 적이 계단/램프로 추격. **서피스 그래프 설계**(독립 3설계×적대 2비평 워크플로 수렴 = Design3 base): XY 셀수 불변 + 고정 `constexpr NumLayers=2`(`static_assert`로 잠금; 3층=상수+`EdgeMask` 타입 확장). 셀당 최대 2 **랭크 서피스**(rank0=최하단), `Surf(Cell,Rank)=Cell*NumLayers+Rank`(dense 인터리브). per-surface `CellFloorZ/BlockedField/DistField/FlowField` + `SurfaceFlags`(bValid/bSloped/경사방향) + `EdgeMask`(uint8 `[NumCells*2]`, 바이트당 `ra*NumLayers+rb` 비트=경계별 전 랭크쌍 연결, 캐노니컬). 연결성=**서피스↔서피스**(계단이 rank 전이적 상승, 별도 층간 edge 無; `MaxTraverseDelta` 게이트로 지면↔덱 직결 차단). `BuildObstacleMask`=후보 클러스터링→rank 배정(>2층 드롭+로그)→**`Surf` 키 flood**(bare-cell 키 금지=단일층 회귀 방지)→per-surface 점유/edge 베이크. `RecomputeField`=플레이어 foot-Z rank 시드·`EdgeMask` BFS·이웃 서피스 steepest-descent. `SampleFlowDirection`=`FootZ=Z-EnemyStandOffset(HalfHeight+GroundRestClearance)`로 rank 픽(타이=결정적 하위·착지면 최근접). **수직 stop-gate**(`FPSREnemySpawnSubsystem`): StopDistance 정지에 `AttackVertGap≤AttackVerticalRange(150)` AND 추가 → 2층 플레이어 밑에서 멈추지 않고 flow 따라 계단 등반(평지 vertGap≈0=무회귀). **하드닝(Codex 로버스트니스 3)**: R1 경사면 점유박스 `floorZ+RampAllowance` 상향(기둥/난간 포집·tread 오탐 0)·R2 연속 경사벡터 `dot(edge,slope)>cos45°` edge만 grade(램프 옆 절벽 오연결 차단)·R3 PlayerStart 트레이스 `StartFloorZ` 시드. **성능계약**: 신규 비용 전부 1회성 `BuildObstacleMask` 유지, 0.2s BFS·샘플=순수 배열(`!bValid` early-out=평지 무회귀); 스캔 슬롯 ≤×NumLayers(base-cell 캡 `MaxTotalCells` 불변, ~2.4MB). **예외(정직 기록)**: `RecomputeField`의 `FindNearestOpenCell` 플레이어-소스 스냅 LOS 트레이스는 **기존부터 있던 플레이어 수 바운드**(≤파티원, per-enemy 아님) — 유일한 비배열 op, 레이어 스냅도 동일 바운드 유지. **U14 perf 패스(적 ~200-300 실측·§5 P0 가독성 게이트)로 ×2 스캔 재측정 이월**. **콘텐츠 제약**: 중첩은 storey급(≥~1층)으로 저작(storey 미만 근접 2면=단일 계단면, 진동 회피); 3층+·동일 XY 두 경사면 교차=미지원(스토리 저작)·동적 수직 지오메트리 제외. 검증=빌드+헤드리스 스모크+Codex 플랜게이트(P2 3핀 교정)+머지게이트. **실맵 PIE(겹친 2층 추격)=사용자**(~~L_Sandbox 2층 배치~~ → 🔁 *정정 2026-08-13*: 현행 **`L_Map1_City`가 이미 2단 지면**이라 배치는 완료됐다 — 고가 지면 Z≈800이 면적 20%, `NumLayers=2` 한도에 정확히 맞음. **남은 것은 PIE 검증뿐**).

### 5-3. U14 perf 패스 측정 항목 레지스트리 (등록 2026-08-19, U14R)

> **M0 EC ① 정량 베이스라인 패스가 실행할 측정 항목의 인덱스.** 항목 정의의 정본은 각 칸의 링크가 가리키는 문서다(여기 복제 금지 — 이중 SSOT 방지). 측정 구성 = **Development 패키지**(위 🔴 정정 2026-08-13) · 고정 시나리오 = VAT-1(`L_Map1_City?listen` · `FPSR.SpawnEnemies N 6000` · `Scripts/measure_swarm_render.ps1`) · 분석 = `Scripts/analyze_swarm_csv.py`.

| # | 항목 | 정본 / 판정 잣대 | 비고 |
|---|---|---|---|
| 1 | 프레임 예산 (avg·P95 @300, ≥30fps @500) | 위 🎯 목표 프레임 예산(2026-08-12) | 합/불 판정선. **M0 잠정 실측(2026-08-28) 통과**(222fps·P95 5.09ms) — 단 실제 생존 225/394 로 요청수 미달, 하한 측정 |
| 2 | 스웜 렌더 서브예산 (≤4ms @300) | 위 🎯 + VAT-1 참고 실측(2026-08-13) + **M0 잠정 실측(2026-08-28) = 1.28ms** | ✅ 잠정 통과. (b) 공식 베이스라인은 카이팅 실플레이 + 아트 적용 후 별도 실측 |
| 3 | 1인칭 가독성 5지표 | 위 🎯 가드레일 표 · 계측 = `UFPSREnemyMetricsSubsystem`(`FPSREnemy/*` CSV) | 게이트 아닌 설계 목표 |
| 4 | **N-1 복제 폴백 비용** (신규 등록) | **`Docs/Specs/U14R_PerfMeasureRegistry.md` §5-A** — `Exclusive/GameThread/ServerReplicateActors`, 4인 리슨서버(`-ClientCount 3`), 잠정 ≤1.5ms @300×4 | 결과가 **소스 엔진 전환 여부**(N-1) 결정 — 사용자 비준 |
| 5 | **GMS §7-7 브로드캐스트 비용** (신규 등록) | **`Docs/Specs/U14R_PerfMeasureRegistry.md` §5-B** — `FPSRMsg/*` 4스탯, 잠정 ≤0.2ms @300 | 초과 시에만 재설계 코어 행 생성. 🔴 **U13 발행/구독 배선 후에만 실측 유의미**(2026-08-19 현재 발행처 0 — 0 캡처는 "측정 불가"로 기록, §5-B 선행조건) |
| 6 | 플로우필드 멀티레이어 ×2 스캔 재측정 | §5-2 "U7 멀티레이어" 불릿(이월 명시분) | |
| 7 | U 연속필드 near-cap 재계산 ms | 위 "다중맵 U 대전환" 문단(P-0 합성 벤치 후 이월분) | |
