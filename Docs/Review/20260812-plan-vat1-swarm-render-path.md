# 플랜 컨설트: VAT-1 적 스웜 인스턴스 렌더 경로 설계 (2026-08-12)

> 종료 사유: **축 소진** — R4 델타 3건이 전부 스펙 게이트(설계 방향 불변) 수준으로 수렴, 8개 재공격 축 전부 1회 이상 소진. 정책(Deep Delta-Gated)의 2연속 NO_DELTA 전에 안건 소유자 권한으로 종료 — 남긴 축 없음(보일러플레이트화는 R3 축 E에서 흡수).

## Intake
- **Scope: IN** — T1(렌더 서브시스템 신설 후보) + T2(여러 클래스 구조 변경) + T3(대량 적 성능 경로). **Mode: FULL**(하드트리거 "대량 적 성능경로 변경").
- `plan_type = backend/system` → 선소진 축: 실패모드·역행·기존 도구 충돌·검증 불가능성. 라운드 정책 = **Deep Delta-Gated**(프로젝트 기본).
- 판정 근거: 읽은 파일 = `Roadmap.md §7-6 M0 (a″)` · `Performance.md §5` · `Enemy.md §2-6` · `FPSREnemyBase.cpp/h` · `FPSREnemySpawnSubsystem.cpp` · `FPSREnemyAnimProfile.cpp` · `FPSRVATAnimParams.h` · `FPSREnemyShadowLODSubsystem.cpp` · `FPSREnemyMetricsSubsystem.cpp` + 적 BP 2종 uasset 네임테이블 + `Content/Assets/Characters/BroBot/VAT/` 실측(Explore 전수 조사, 2026-08-12).
- 총 4라운드(R1 수명주기 6델타 / R2 전제 뒤집기 6건 / R3 검증·도구충돌·스코프 10건 / R4 측정 동일성 3건). 원문 = `Docs/Review/_raw/20260812-15*-vat1-swarm-render-path.md` 4건.

## 📋 결과 플랜 (수렴)

**VAT-1의 정체 재정의: "ISM 설계"가 아니라 "스웜 렌더 경로 대조 판정 실험".**
초안은 ISM 채택을 전제했으나, R2가 전제("개별 컴포넌트 300개 = 지배항")가 미측정 주장임을 지적 — 배칭을 깨는 1차 용의자는 **per-actor MID**(코드 주석이 자인, `FPSREnemyAnimProfile.cpp:28-29`)이고, UE5 Mesh Draw Command dynamic instancing이 동일 메시+머티리얼 컴포넌트를 이미 병합할 수 있다.

### 실험 설계 (3경로 대조)
| 경로 | 내용 | 위상 |
|---|---|---|
| **A (대조군)** | 현행 그대로 — per-actor MID + 개별 `UStaticMeshComponent` | 측정만 |
| **B (1순위 후보)** | MID 폐기 + 공유 머티리얼 + **CustomPrimitiveData**(슬롯 0/1/2 = `FPSRVATAnimParams.h` 계약). 개별 컴포넌트·shadow LOD·metrics·per-enemy stencil(BP 계약) 전부 무손실 유지 | 스파이크에서 구현 |
| **C (2순위)** | ISM 스로어웨이 렌더 하네스(게임플레이 배선 0). **B 불통과 시에만** 측정·채택 검토 | 조건부 |

**채택 규칙**: B가 예산(스웜 렌더 합 ≤4ms@300) 통과 + 드로우 병합 확인 → **VAT-2 = "B 정식화(MID→CPD 전환)"로 대축소**, ISM 설계는 컨틴전시 보존. B 불통과 시에만 C(ISM) 채택 — 이때 R1 수명주기 델타 6건 전부 적용.

### 경로 B 계약 (스파이크 스펙)
1. **CPD 슬롯 계약**: 0=AnimIndex · 1=PlayRate · 2=Phase(+3 히트플래시 예약) — `FPSRVATAnimParams.h`가 단일 편집점. 머티리얼 파라미터명 불일치(C++ `PlayRate/Phase/AnimationIndex` vs 머티리얼 `Playrate/TimeOffset/Index` — **현행 애님 드라이버 사실상 무동작**)를 이 기회에 교정.
2. **CPD 리셋 계약**: 서버 `Activate`(풀 재사용)에서 전 슬롯 초기화 **+ 클라 측 리셋**(R4-2 [가설]: CPD는 로컬 렌더 데이터라 서버 리셋이 원격 클라에 전파 안 됨 → 복제 상태 엣지(OnRep hidden/bDead)에서 클라도 리셋). 2클라 리슨 테스트로 검증.
3. **BP 가드**: 적 BP에서 `CreateDynamicMaterialInstance` 금지 계약 명문화(후속 코스메틱은 CPD 슬롯으로).
4. **범위 울타리**: 머티리얼 재저작은 기존 walk 클립 1개의 CPD 읽기 전환 + 파라미터명 계약 확정까지만. 클립 인덱스 정책·death dwell·재베이크 = **VAT-3**.
5. **측정 fixture 고정**(R4-1): 300스폰 전원이 `BP_EnemyMeleeBase/Ranged` + `SM_BroBot_VAT` + VAT 머티리얼 + AnimProfile_VAT 할당 상태임을 측정 전 검증(AnimProfile null이면 MID/CPD 경로를 아예 안 탄다 — 무효 측정).

### 측정 프로토콜 (판정 재현성)
- PIE = 기능 스모크만. **채택 판정 = 패키지 Test 구성 · 리슨서버 호스트 · L_Map1_City · 적 300 고정 스폰 · 셀셰이딩 on — A/B 두 빌드.**
- 고정 카메라 좌표/FOV/시나리오 · 10초 워밍업 + 45~60초 캡처.
- 기록: `stat unit`(프레임 분해) + `stat gpu`(BasePass+CustomDepth+Shadow 합) + **`stat scenerendering`/Insights MeshDrawCommands(드로우 병합 검증 — GPU ms 단독 판정 금지)** + Insights P50/P95.
- 500·cel off는 헤드룸 참고 1회씩만(측정 매트릭스 폭주 금지, R3-10).
- C 측정이 열리면 **동등성 게이트**(R4-3): CustomDepth+stencil+shadow 혼합을 A/B와 동등 재현해야 채택 근거 성립(BasePass만 빠른 건 하한 측정).

### 검증 항목 (가설 이관 — 기각 아님)
| # | 가설 | 반증 신호 |
|---|---|---|
| V1 | UE5 dynamic instancing이 B에서 실제 병합 | draw 수가 적 수 비례로 잔존하는데 GPU ms만 통과 |
| V2 | shadow LOD의 CastShadow 혼합이 shadow pass draw group을 분열 | ShadowDepths draw ∝ 그림자 밴드 내 적 수 |
| V3 | SRS 아웃라인 출력 A/B 동일 | 패키지 캡처에서 아웃라인/PP 분류 상이 |
| V4 | 전투 중 MID 재생성으로 배칭 회귀(BP 코스메틱) | 전투 30초 후 draw/MID 수가 A 수준 회귀 |
| V5 | 원격 클라 CPD 잔존(R4-2) | 2클라 리슨에서 kill→reuse 첫 가시 프레임에 이전 life 상태 |

### 산출물
- `Docs/Architecture/` ADR(경로 판정 + CPD/BP 가드/리셋 계약) + `Performance.md §5` 실측 기입 + VAT-2/3/4 보드 행 스코프 재정의.

### 범위 밖으로 뺀 것
- ISM 수명주기 기구 상세(R1 델타 6건: 클라 슬롯 이벤트 소스 / 리슨서버 단일 기록자 / generation 핸들 / 그림자 히스테리시스 상태 승격 / near-far 이주 CPD 원자성 / 이중 백엔드) — **C 채택 시에만 유효한 컨틴전시 설계**로 본 리포트에 보존.
- 계측(`FPSREnemyMetricsSubsystem`) CPU 프러스텀 재작성 — C 채택 시에만 필요(B는 무손실). 베이스라인 측정 전 완료 필수라는 순서 제약만 기록.

## 토론 로그 요약
- **R1** (축: 렌더 슬롯 소유권/수명주기) — 6건 전부 PLAN_DELTA 수용. ISM 채택 시의 필수 기구가 초안에 전부 누락돼 있었음이 드러남.
- **R2** (Divergence, 축: 전제 뒤집기) — **플랜 대개편**. "지배항" 주장이 미측정임(Performance.md:14가 자인), MID가 1차 용의자, ISM은 쓰기 빈도를 오히려 늘릴 위험(현행은 이벤트/저빈도 기반). 플랜 B(= 경로 B) 채택.
- **R3** (축: 검증 불가능성/기존 도구 충돌/스코프 폭주) — 10건. PIE 상대비교도 무효(판정=Test 패키지), GPU ms 단독 판정 금지(드로우 수 병행), CPD 풀 리셋, shadow pass 분열 가설, C 하네스 격하, 측정 매트릭스 축소.
- **R4** (축: 측정 대상 동일성) — 3건: fixture 고정 게이트, 원격 클라 CPD 리셋, C 동등성 게이트. 방향 변경 없음 → 종료.

## 🧾 기각 원장
**기각 0건.** Codex 지적 25건 전부 수용(PLAN_DELTA/스펙 게이트) 또는 검증 항목 이관(V1~V5) 또는 사용자 결정 상신. `[가설]` 태그 지적은 규칙대로 기각하지 않고 V1~V5로 옮겼다.

## ⚖️ 미해결 쟁점 · 🙋 사용자 결정 필요
1. **B 통과 시 ISM 트랙 처리** (발생: R1-6 롤백 논의 → R2 전제 뒤집기로 조건부화)
   - 쟁점: B가 예산을 통과하면 VAT-2 원안(ISM 전환)이 불필요해진다. 보드 행을 폐기할지, 스코프만 갈아끼울지.
   - 왜 사람이 정하나: 보드 구조(우산 행의 조각 구성) 변경 = 작업 추적 SSOT 변경.
   - 선택지: ⓐ VAT-2를 "B 정식화"로 스코프 교체(권고 — 행 재사용, 릴레이션 유지) ⓑ VAT-2 폐기 + 신규 행.
   - 기준: 릴레이션(VAT-4가 VAT-2를 선행으로 묾) 보존 비용.
2. **B 불통과·C 채택 시 이중 백엔드 vs 완전 교체** (발생: R1-6, Codex 권고 = 얇은 `IEnemyRenderBackend` 이중 경로가 싼 보험)
   - 쟁점: cvar 뒤 ActorMesh/ISM 이중 경로는 롤백 보험이지만 "임시·미루기 구조 금지" 원칙과 긴장.
   - 왜 사람이 정하나: 롤백 비용 對 유지보수 부채의 가치 판단.
   - 기준: 계측/그림자/애니 계약이 한 번에 통과되기 전까지의 전환 기간 길이. Codex 권고는 이중 경로, 통과 후 제거.
   - ※ 조건부 결정이므로 **스파이크 측정 결과가 나온 뒤 상신**해도 늦지 않다.

## 📌 액션 아이템
- VAT-1 스파이크 실행(경로 B 구현 + A/B Test 패키지 측정) — 본 리포트가 스펙. **구현 승인 = 사용자 게이트.**
- 결과에 따라 VAT-2/3/4 보드 행 스코프 재정의 + `Docs/Architecture/` ADR 작성 + `Performance.md §5` 실측 기입.

## 검증 상태
- **확인됨(코드/문서 직접 관찰)**: 메시 NoCollision·캡슐 콜리전 분리, per-actor MID 생성, 파라미터명 불일치(무동작 드라이버), CPD 슬롯 계약 예약, shadow LOD 구조, 계측 의존성, Niagara/ISM 런타임 부재, SRS stencil 계약, 거리밴드 stride.
- **추정(검증 필요 = V1~V5)**: dynamic instancing 실병합, shadow pass 분열, 원격 클라 CPD 잔존, SRS 출력 동일성, MID 회귀.
- **미검증**: 본 리포트는 자문 전용 — 빌드/측정 미실시. 4ms 합불은 스파이크 실측으로만 판정.
- **반증가능 예측**: 이 플랜이 맞다면 B의 BasePass 드로우콜이 적 수 비례에서 상수 근처로 떨어지고 스웜 렌더 합이 ≤4ms@300에 든다. 틀렸다면(=MID가 지배항이 아니었다면) B의 A 대비 delta가 0.5ms 미만으로 나오고 C 하네스 측정이 열린다.
