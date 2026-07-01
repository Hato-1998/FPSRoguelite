# Performance — 성능 / 네트워크 예산 (FPSRoguelite SSOT 분할)

> `Game.md`(SSOT 허브)의 분할 문서. **섹션 번호(§x)는 원본 그대로 보존** — 소스 주석·교차참조 호환.
> 작업 시작 전 허브 `Game.md` + `PROGRESS.md`를 먼저 읽고, 적 대량화·복제 예산·Significance·플로우필드·프로파일링 관련 작업 시 본 파일을 연다.
> 담는 섹션: §5 성능·네트워크 예산(§5-1 Significance 티어, §5-2 Flow-Field 적 타겟).

---

## 5. 성능 / 네트워크 예산 (⚠️ P2 착수 전 수치 확정·검증 — 최우선 보완)

> 이 프로젝트 최대 리스크는 Hero Shooter 과설계가 아니라, **적 500마리 협동의 성능/복제 예산이 미수치화된 점**이다. 아래는 잠정값이며 Unreal Insights + NetProfiler로 검증·조정한다.
> **⚠️ 검증 시점 (확정 2026-06-10)**: 당초 P2 예정이었으나 미실시 → **P4-C 무기 콘텐츠(6종) 완료 직후, 코어 재미 게이트(§7-5)와 함께 일괄 측정**한다. 그때까지 본 §5 수치는 *미검증 가정*이며, 그 위에 쌓인 P4 콘텐츠는 검증 결과에 따라 조정될 수 있다.
>
> **⚠️ 측정 미실시 갱신 (2026-06-30, U1 게이트)**: U1은 G1 재미(①~④) + MP 조건으로 **합격**했으나, **§5 적500 정량 측정(Insights/NetProfiler)은 미실시(보류)** — 패키지 재빌드 미수행(사용자 결정). 따라서 **하드캡은 아래 잠정값 유지**, 적500 정량 + 하드캡 확정은 **콘텐츠 밸런싱/U14 perf 패스로 이월**. **코드 실측 확인분(2026-06-30, 6에이전트 read-only 워크플로)**: 하드캡 500(`MaxActiveEnemies` constexpr)·NetUpdateFreq 티어(S0 30/S1 10/S2 5/S3 2Hz, 거리기반·티어변경시만 set)는 코드 일치 / **NetCullDistance는 미구현**(아래 교정) / 복제 = Push Model(`net.IsPushModelEnabled=1`), **RepGraph·Iris 미사용** → 판정 시작점 Push 맞음.
>
> **⚠️ 적 규모 실측 갱신 (2026-07-01, 실플레이)**: 동시 개체수 현실 상한 = **~200-300**(코드 폴백캡 300 부합; 하드캡 `MaxActiveEnemies`=500은 헤드룸으로 유지). Game.md §1 규모 프레이밍을 ~200-300으로 정정. **함의**: perf·가독성 게이트 목표치를 **200-300 기준**으로 재설정 권장(500은 스트레스 상한으로만). 최대 리스크(1인칭 가독성·복제 예산)는 500이 아니라 ~300에서 성립하면 되므로 **리스크 규모가 축소**된다.

| 항목 | 잠정 목표 | 비고 |
|---|---|---|
| 최대 활성 적 수(서버) | 하드캡 500(`MaxActiveEnemies` constexpr, 코드 실측 2026-06-30), 통상=스케줄 `MaxAliveCount`/`AliveCountByLevel` 주도(코드 폴백캡 300) | 풀 고갈 시 스폰 보류. ⚠️"통상 200~350"은 잠정 문서값 — 실측 통상치는 활성 스케줄 에셋 MaxAliveCount |
| 클라이언트별 관련(relevant) 적 | 상한 ~150 | relevancy cull |
| 적 NetCullDistance | **미구현**(엔진 기본 ~2.25km 사용 — 코드 실측 2026-06-30) | ⚠️ relevancy cull 없음 → 적500서 호스트가 클라당 전 적 복제. **RepGraph 이전 1순위 후보 레버**(net-cull 도입/relevancy) |
| 적 NetUpdateFrequency | 위협도별 S0 30Hz / S1 10Hz / S2 5Hz / S3 2Hz (코드 실측 일치 2026-06-30) | 거리기반(최근접 플레이어), 티어변경시만 SetNetUpdateFrequency(적500 핫패스 가드, W1 P2) |
| 적 Dormancy | 원거리·비활성 DORMANT, 접근 시 wake | |
| 적 복제 상태 | Transform(위치/Yaw)만 최소 복제, 체력=서버 권위 | 히트/사망 코스메틱은 GameplayMessage/Cue (복제 액터 상태 아님) |
| XP/픽업 | 개수 cap + 인접 병합, 자석=클라 코스메틱·서버 권위 수령 | |
| 복제 발사체 액터 | **팀별 분리 예산**(U5, 2026-06-30): 플레이어 ≤64 / 적 ≤`FPSR.Enemy.ProjectileBudget`(기본 32, 천장 100) | 히트스캔/연사=비복제 코스메틱. 팀별 FIFO 회수라 적 사격↔플레이어 AOE 상호 잠식 없음 |
| 적 공격 판정 | 서버 배치 처리(거리 체크 배치) + **공격 토큰 상한** | 플레이어당 동시 공격 시도 적 수 제한(§2-6) |
| **호스트(리슨서버) 부하** | 자기 클라 렌더 + 적 500 서버권위 시뮬 **동시 부담 = 최악 케이스** | 전용 서버 없음(§2-10) → 호스트 프레임예산 **별도 측정**, 하드캡은 호스트 기준으로 결정. 부족 시 RepGraph·시뮬 LOD·스폰 보류 우선 |

**리플리케이션 도구 평가 순서**: Push Model(기본) → 부하 시 **Replication Graph**(spatial grid relevancy, 검증된 도구) → 그래도 부족 시 Iris(Beta) 평가. **Iris를 1순위로 두지 않음**(RepGraph가 다수 액터·연결별 relevancy 병목에 더 직접적).

**🔒 1인칭 가독성 USP 게이트 (P0 · 컨설트 2026-07-01 F6 — `Docs/Review/20260701-concept-conclusions.md`)**: Concept §1-C-3 USP("1인칭=협동 강제")의 *성립 조건*. perf 수치만으론 "에임이 의미있나 vs 난사"를 못 잡으므로, 아래 **최소 5지표**(1인 팀 계측 싼 것)를 P0 게이트로 둔다 — **미통과 시 aim uplift/오탐율 등 상위 측정으로 가지 말 것**:

| # | 지표 | 통과 임계 |
|---|---|---|
| ① | 활성 적(서버) | 실전 200~300 / 스트레스 500(붕괴 확인용) |
| ② | 클라 relevant 적 | P90 ≤ 150 |
| ③ | 화면 내 보이는 적 | P50 ≤ 40, P90 ≤ 70 |
| ④ | 15m 내 즉시 위협 적 | P90 ≤ 25 |
| ⑤ | 시각 위협 큐 동시표시(§PlayerFeel 2-14 F3) | ≤ 3 (+후방집계 1, 45도 병합) |

5개 통과 = USP **"성립 가능성" 통과**(확정 아님). 최종 성립 = 10~15분 플레이에서 **"4솔로 아니라 서로 커버했다"가 반복**되는지 별도 확인(정량 5개 + 짧은 플레이 기록 리뷰). 참고(텔레메트리 필요, 5개 통과 후): aim uplift ≥1.35x·무음 원거리차징 70%가 1.5s내 반응·오탐율 ≤20%.

### 5-1. Significance 티어 (플러그인 enable ≠ 최적화)
적/VFX/SFX/anim tick/mesh/healthbar를 단계별로 다운. **AI update budget에도 연동.**
- **S0** 근접 위협: full update
- **S1** 근거리: 저빈도 update
- **S2** 중거리 군집: anim·VFX 축소
- **S3** 원거리: coarse movement·no cosmetic

### 5-2. Flow-Field 적 타겟 (P2)
**고정맵 grid + 단일 목표점 field(가장 가까운 플레이어) + local separation steering**으로 시작. 타겟 규칙(가까운/위협도/파티중심/미션목표)은 데이터로 전환 가능하게. Flow-Field를 플레이어별로 둘지/파티중심·목표점별로 둘지 결정 필요. 동적 장애물은 비용 대비 나중에. separation batch update 주기·충돌 처리 범위 정의.
- **구현 보강(P4, 2026-06-09 main 머지 — `phase/p4-enemyspawnpoints` 코드분, Codex 5R 하드닝)**: ① **디자이너 배치 스폰포인트** `AFPSREnemySpawnPoint`(Weight/ZoneTag/MinPlayerDistance/bEnabled) + `UFPSREnemySpawnSubsystem`이 전 플레이어 비가시(FOV)+거리 게이트로 가중랜덤 선택(후보 0 시 링 폴백 → 미배치 맵도 동작). 디자이너 지점은 권위적 위치라 ground-snap 생략(실내/지붕 천장 스냅 방지). `SetActiveSpawnZone` 훅은 TimeGate(§2-8) 후속. ② **플로우필드 장애물 마스크 + BFS 라우팅 + 적 중력/지면추종**(`AFPSREnemyBase`, 경사/계단 소폭 보정). 하드닝: 월드 밖 추락 적 **KillZ 회수**(슬롯 영구점유 방지), 접촉 데미지 **수직 게이트**(바닥 관통 타격 방지), 동일 스폰지점 중첩은 **분리(separation)의 동일위치 결정적 푸시**로 근본 해소(스폰 위치 비이동). **후속(C1, `phase/p2-flowfield-height`, PIE 의존)**: 멀티레벨/높이 인지 BFS + 플로우필드 셀 **클리어런스 인지 프로브**(현 전셀 오버랩은 경계 벽 양쪽 셀 차단 → 좁은 통로 과차단 트레이드오프). 콘텐츠 스폰포인트 배치(L_Sandbox)는 사용자 PIE 후 별도 머지.
- **U7 = C1 높이/클리어런스 구현(코드 완료·검증, `phase/p2-flowfield-height`, 2026-07-01)**: **2.5D 높이 인지 플로우필드**. `BuildObstacleMask`에서 셀당 **Z-스텝 반복 다운트레이스**(ECC_WorldStatic, 컬럼 적층 표면 전부 포집·병합메시 포함)로 up-facing(normal.Z≥0.71 walkable) 후보 수집 → **지면(`GridOrigin.Z`)에서 클라이머블 스텝 플러드필**로 셀별 **도달 가능 walking surface**(`CellFloorZ`) 확정(램프/계단은 오르되 벽/천장 윗면·절벽엔 안 감). 점유/edge 프로브를 **셀 자기 바닥높이**에서 수행 + **스텝게이트를 `EdgeTraversable`에 굽기**(연속 램프=경사 상한 `ActiveCellSize·tan(max각)`, 평지 단차=`ClimbableStepHeight`, `MaxStepHeight=45` 미러; **`GroundSnapTolerance`=60으로 클램프**). 대각선 흐름은 2×2 코너 4-edge 요구(높이-게이트 우회 차단). **Part B(좁은통로)**: edge 박스 전셀폭→**footprint폭**(과차단 해소). **핵심 성능계약**: 모든 신규 비용=**1회성 BuildObstacleMask**, 0.2s 멀티소스 BFS·steepest-descent·500적 샘플=**순수 배열연산 유지**(월드쿼리 0, RecomputeED에 가드주석). **평지 무회귀**(CellFloorZ≈GridOrigin.Z). **바운드볼륨 per-map 오버라이드**: `ClimbableStepHeightOverride`·`ProbeApexAboveOriginOverride`(apex는 천장 아래로). **수용 한계(제1원리 = 3D/레이어 그리드 거부)**: 동일 XY 수직중첩(다리 위 통로)=단일 표면(최상단 아닌 지면 우선)→후속 시임 Z-레이어/포털 그리드. 검증=빌드×8+헤드리스 스모크×7+**Codex 머지게이트 7R**(구현 P2 6건 전부 교정, 7R=설계 경계 문서화). **실맵 PIE(계단·단차 추격, 좁은통로)=사용자**(L_Sandbox 현재 평지라 계단/램프/플랫폼 배치 필요).
