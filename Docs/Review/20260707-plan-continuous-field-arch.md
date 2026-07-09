# Plan-Consult: 다중맵 "연속 크로스도어 플로우필드" (3×3 그리드) 아키텍처 (2026-07-07)

> `/plan-consult` 산출 = **결정지원 리포트**(실행 지시 아님). 채택 시 해당 `Docs/SSOT/` 먼저 갱신 후 구현(§6-3). 자문 전용 — 빌드/PIE 미검증.

## 1. Intake
- **Mode: FULL** — 하드트리거 다수(서버권위/복제·대량 적 성능경로[pathing/AI recompute]·phase 경계 구조변경·결정축 3+). 되돌리기 비용 큼.
- **plan_type = backend/system** → Codex 렌즈 **제2 아키텍트/레드팀**(계약·확장성·검증성·운영비용·되돌리기·파이프라인 충돌) 3R 고정.
- **라운드 정책 = Deep Delta-Gated** — 3R 실행(전 라운드 실질 delta, 마지막은 새 fork 아닌 사용자결정 착지 → 종료). 근거: 되돌리기 비용↑·장기영향·조기합의 의심.
- **초안 = 사내 멀티에이전트 워크플로(3렌즈 설계+적대 critique+수렴)** 하드닝본 = **대안 P(portal-seed per-map)**. 이걸 Codex로 외부 교차검증.

## 2. 결과 플랜 (수렴) — **대안 U(unified 단일 그리드)로 전환**

> **핵심 반전**: 사내 워크플로는 "3×3 단일 그리드 = 축당 768셀 > 캡 256 → 불가"를 근거로 P를 택했으나, **그 768셀은 맵당 최대캡 가정일 뿐 실측 아님**(Codex R1). 화이트박스 맵 ≈3000cm → 3×3 = ~45셀/축 ≪ 캡. **현실 스케일에서 U가 P를 제1원리·단순성·검증성에서 지배**(Codex R2). U는 사용자의 literal "무 특수처리"에 가장 근접.

### 2-1. 메커니즘 (U)
- **고정 3×3 world extent를 프리사이즈한 단일 `UFPSRFlowFieldComputer`.** 맵별 stream-in 시 그 슬롯 셀 구간만 shared grid에 **증분 bake**(원자적 staging commit). 미로드 슬롯 셀 = blocked.
- **문 부수면**: 그 벽의 door cells `blocked→open` **stamp** + 단일 `RunBFS`(재트레이스 없음) + field generation bump.
- 적은 **단일 필드**를 O(1) 샘플 → 열린 문 넘어 seamless 추격. **MapId·portal registry·전환 추적자·seam 타이브레이크·per-map 전투게이트 전부 불필요.**

### 2-2. 신규 1차 요구사항 (R2/R3 잠금 — U 구현의 계약)
1. **Subregion bake atomic commit** — staging buffer; 언로드 시 `CellFloorZ`/`BlockedField`/`EdgeMask`를 경계 **양쪽 edge** 클리어(ghost path 방지). (`BuildFromWorldTrace`는 현재 전체교체 → 부분교체로 확장.)
2. **Door-cell stamping** — 문 leaf=`ECC_FPSRPlayerPawn`·blocker=`WorldDynamic`이라 flow의 `WorldStatic` bake에 **안 잡힘** → door cells를 명시적 blocked→open stamp(P는 MapId gate가 덮던 것).
3. **Origin-aware grid-connectivity 전투게이트** — MapId gate 제거 시 폭발이 닫힌 벽 너머 반경피해(현 `ApplyExplosion` overlap-only, LOS 없음). → `CanAffectTarget`을 **instigator 원점셀 ↔ 타겟셀이 현재 open grid로 연결됐나**로 대체. **시그니처/호출부 계약변경**(explosion은 별도 `Center` 원점).
4. **FrontId/PocketId** — open-door connectivity 컴포넌트. allocator `GroupBonus` 키를 MapId→**FrontId**. ⚠️새 실패모드: all-open 후반 멀리 1/1/1/1이 한 front로 오인 → 보너스에 **graph-distance/전투포켓/미션앵커 근접 가중**(연결=전투가능 gate, 보너스=실제 같은 전투권). **`FrontId`를 gameplay 소비 추상화로 유지**(단일배열 index를 gameplay에 누수 금지 = U↔P 회귀 비용 中 유지).
5. **연결성-인지 trickle drain** — front-pressure + stale-age + corridor-exemption. token accumulator 전역 2/sec 기본, front deficit 심하면 4/sec. **빈 후방이 cap을 잡아먹어 front target 못 찰 때만 가속**(현 `EmptyMapDrainPerTick=4 @0.1s`=40/sec hard drain이라 trickle로 교체).
6. **TopologyGeneration 버전** — door commit/subregion ready 시 서버 증가. **late-join**: late client는 generation ack 전까지 allocator occupant/target/damage 제외(또는 spawn protection). **freeze**: 프리즈 중 door 변경=dirty 큐 → **unfreeze 직전 즉시 RunBFS**(또는 generation bump). **reset**: `ResetToBakedBaseline`이 door stamps/blocked-open delta/dirty subregion/cached BFS/front ids/generation 원자적 초기화.
7. **per-slot occupancy Schmitt** — 단일 그리드라 flow/combat의 MapId flap은 **자연 소멸**(하나의 grid·하나의 WorldToCellIndex). 단 allocator/drain용 슬롯 배정은 PlayerState에 stateful Schmitt(현 슬롯 유지, 임계 넘을 때만 flip). P보다 단순(occupancy 전용).
8. **NetCull/RepGraph** — U/P 동일. Tier-0=NetCull을 죽은 offset contract에서 실 slot footprint로 재튜닝. **RepGraph(공간 grid relevancy)=프로덕션 해법, 별도 페이즈.** 클라 seam pop-in=수용된 Tier-0 한계.

### 2-3. 유지 / 제거
- **유지**: `MapStreamSubsystem` collision-ready polling + fail-closed; Door stream trigger; allocator 전역 cap/seed reserve; per-slot occupancy(→FrontId 키).
- **제거**: per-map registry(`Computers`/`BakeDiscoveredMap`/`EvictMap` 의미), movement same-map target gate, 전환 추적자 전부, enemy MapId sync, combat MapId gate(→origin connectivity), 맵간 갭.

### 2-4. 단계별 구현 (각 build+smoke 검증 가능, **트래커 제거는 연속성 증명 후**)
| P | 변경 | 검증 |
|---|---|---|
| **P-0 반증 게이트** | 슬롯 셀상한 산술검증 커맨드릿: `3·SlotX≤256`, `3·SlotY≤256`, `9·SlotX·SlotY≤40000`(200cm→슬롯 ≤~132m/변). 콘텐츠 계약 잠금. | fail-fast 통과 = U 유효. **못 잠그면 U 무효→P/tiled 회귀 결정.** |
| **P-A 단일 그리드+증분 bake** | 고정 3×3 extent 단일 computer + subregion 원자 bake + 통합 WorldToCellIndex. | 2슬롯을 한 그리드에 bake → 단일 필드가 양 슬롯 커버(FlowField.Unit 확장). |
| **P-B door stamping** | 문 부수면 door cells open stamp + generation bump + 단일 RunBFS. | 문 열면 빈 이웃 슬롯 적이 필드 따라 물리적 크로스. 트래커 잔존(belt). |
| **P-C 전투 게이트** | origin-aware connectivity `CanAffectTarget`(+explosion 원점, 호출부); same-map target gate → open-connectivity. | 닫힌 문=벽 너머 데미지/폭발 0. 열린 문=크로스 타격. |
| **P-D FrontId occupancy** | open-door connectivity FrontId + allocator GroupBonus(FrontId+근접가중) + slot Schmitt. | 2+ 열린문 연결=GroupBonus, 닫힌문 사이=별개 front. |
| **P-E trickle drain** | front-pressure+stale-age+corridor token drain(2~4/sec). | 후방 오래비운 슬롯만 trickle, active corridor 보존, cap200 front 안 굶음. |
| **P-F TopologyGeneration** | generation 버전 + late-join ack gate + freeze pre-unfreeze RunBFS + ResetToBakedBaseline. | late-join 미ack시 무피해, unfreeze 필드 최신, 리셋 후 baseline. |
| **P-G 트래커/registry 제거** | 전환 추적자 + per-map registry + 갭 삭제(**마지막**). grep 0 references. | 풀빌드 클린, 크로스도어 추격 유지(필드로), half-wired 0. |
| **P-H NetCull 재튜닝** | NetCull → 실 footprint UPROPERTY. **RepGraph=별도 후속 페이즈.** | seam 근처만 복제, 대역폭 PIE 실측. 클라 pop-in=문서화된 한계. |

### 2-5. 범위 밖
- **RepGraph**(프로덕션 relevancy 해법) = 별도 페이즈.
- **World Partition Data Layers** = 미채택(스트리밍/가시성 직교, per-enemy relevancy·flow 도구 아님).
- 프로시저럴 맵 생성.

## 3. 수렴 로그 (초안 P → 결과 U)
- **R1**: Codex가 **전제 뒤집음** — "단일 그리드 불가(768셀)"는 실측 아닌 맵당-최대캡 가정. + BFS hop-order≠shortest(정확성 버그) + seam은 정준 타이브레이크만으론 flap 안 잡힘(stateful Schmitt 필요) + drain connectivity가 all-open서 front 굶김 + late-join per-client + combat 시그니처 계약. **code fact 대조**: 1건 표현 수정(RunBFS는 rank pick 안 함, PickRankForFootZ가 함), 나머지 정확.
- **R2 (divergence: U vs P fork)**: **U 채택 결정** — 현실 스케일서 U가 P 지배, U 막을 엔진/성능 이유 약함. 신규 요구 3개(atomic bake / door stamping / origin combat gate). drain=front-pressure+age+corridor token. NO_DELTA 확인: net-relevancy U/P 동일, 스트리밍 메모리는 지오/텍스처 지배(flow array 무시가능).
- **R3 (final divergence: 반증가능성/allocator-front/U-late-join)**: 3 PLAN_DELTA — 셀상한 산술 게이트 먼저 / FrontId 보너스 근접가중 / TopologyGeneration(late-join·freeze·reset) + **FrontId 추상화 유지=U↔P 회귀 저비용** 통찰. 새 fork 없음 → 종료.
- **Codex 지적 처리**: 수용 대부분(위 요구사항 반영). 기각 없음(모두 코드사실 근거). 보류=사용자결정(§4).

## 4. 미해결 쟁점 · 사용자 결정 필요
### 🙋 D1. 슬롯 셀상한 콘텐츠 계약 (U를 가르는 KEY 결정)
- **경위**: R1서 "단일 그리드 불가" 전제가 미검증으로 뒤집힘 → R2 U 채택 → R3서 "U 유효조건 = 슬롯 셀상한을 콘텐츠 계약으로 잠글 수 있나"로 수렴.
- **백엔드 이유**: 고정 200cm 셀 품질 유지 시 3×3 단일 그리드는 **슬롯 ≤ ~132m/변**(정사각 총셀 기준)여야 캡(40000/256축) 안. 이걸 커맨드릿 fail-fast로 잠그면 U 성립.
- **Codex 이유**: 못 잠그고 맵이 계속 커지면 U 장점 소멸 → P/tiled/hierarchical 회귀. **`FrontId` 추상화만 유지하면 회귀 비용 中**(allocator/drain 생존, flow registry만 되돌림); 단일배열 index를 gameplay 누수시키면 급증.
- **사용자 결정**: **전투 가능한 slot footprint(미술 크기 아님)를 ~132m/변(200cm 셀 기준) 이하로 잠글 수 있나?** 기준: 테마맵의 실 전투공간이 그 안에 들어오나(비전투 미술/배경은 flow grid 밖 확장 가능). YES → U 확정. NO/불확실 → P 유지 또는 CellSize 상향(품질↓ trade).

### 🙋 D2. Tier-2 추격 판타지 범위
- **경위**: R1 Q1 — "全 open-door graph에서 9맵 전체 추격"인지 "최근 통과 corridor 안 끊김"인지.
- **결정**: 현 U=전자(전 그리드 연속) 지원. 후자면 door-local 국소만으로 더 쌈. 기준: 원하는 체감이 "런 후반 뚫린 맵들 전역에서 몰려옴"인가 "방금 넘은 문 근처만 이어짐"인가.

### ⚖️ D3. RISK 수용 — 클라 seam pop-in
- NetCull 재튜닝만으론 플레이어 seam 접근 시 적이 클라에서 팝인(서버 추격은 심리스). **RepGraph가 진짜 해법(별도 페이즈)**. Tier-0 한계로 수용할지.

## 5. 검증 상태
- **확인됨(코드/소스 관찰 — Codex+워크플로 file:line 대조)**: RunBFS seed 0 강제·uniform cost(FlowFieldComputer.cpp:68,113) / CanAffectTarget unset allow(FPSRCombatStatics.cpp:49) / FindMapIdForLocation TMap 순회·half-open(FlowFieldSubsystem.cpp:180) / IsLocationInMap 양쪽 200cm(:163) / Door owner map 없음(FPSRDoor.h:39) / 문leaf·blocker 비-WorldStatic / EmptyMapDrainPerTick=4@0.1s / 프리즈 recompute skip.
- **추정(추론)**: 화이트박스 맵 ≈3000cm(PROGRESS 인용) → 3×3 ~45셀/축. 프로덕션 슬롯 크기는 **미정(D1)**. U 재계산 ms=모델링만(측정 필요).
- **검증 필요(자문 전용, 빌드/PIE 미실행)**: subregion 원자 bake 정확성 / door-toggle re-BFS / origin connectivity gate 벽너머 차단 / trickle drain front-굶음 방지 / late-join generation gate / 단일필드 재계산 ms @ all-9-connected+300적.
- **반증가능 예측**: U가 맞다면 P-0 산술게이트 통과 + P-B에서 빈 이웃슬롯 적이 필드만으로 크로스; **틀렸다면** P-0에서 슬롯이 캡 초과(→P 회귀) 또는 P-A에서 subregion bake가 경계 ghost path 생성(atomic commit 결함 신호).

## 6. 📌 액션 아이템 (PM 인입 후보 — 실행 편성은 `/pm`·TaskPrompts §E)
- SSOT 갱신 후보(채택 시 먼저): `RunFlow §2-1`(front=FrontId 연결포켓+근접가중) / `Architecture §3-4`(단일 3×3 그리드+door stamping+origin connectivity gate; per-map registry·tracker·gap 제거) / `Performance §5`(단일필드 재계산 예산·NetCull 재튜닝·RepGraph 별도).
- 구현 유닛: P-0~P-H (§2-4). **P-0(슬롯 셀상한 게이트)가 선행 — D1 결정 후.**
- 현 커밋된 전환 추적자(feat P8 `44f7b4f`)는 U 채택 시 **P-G에서 제거 대상**(폐기 아님, 문제 실증·검증 역할 완료).
