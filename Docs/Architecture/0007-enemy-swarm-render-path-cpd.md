# ADR 0007 — 적 스웜 렌더 경로 = CustomPrimitiveData (MID 폐기 · ISM 기각)

- **상태**: 채택 (2026-08-13, 사용자 승인 스파이크 실측 판정)
- **브랜치**: `phase/vat-renderpath-spike` · 컨설트 = [Review/20260812-plan-vat1-swarm-render-path.md](../Review/20260812-plan-vat1-swarm-render-path.md) (Codex 4R 수렴)
- **보드**: VAT-1 (M0 (a″) 분할 조각 1/4)

## 결정

적 스웜(동시 ~200-300, 캡 500)의 애니메이션 렌더 백엔드는 **개별 `UStaticMeshComponent` 유지 + 공유 머티리얼 + CustomPrimitiveData(CPD)**로 한다. per-actor MID 경로는 폐기, ISM/Niagara 인스턴싱 전환은 기각(컨틴전시로만 보존).

3줄 근거(핵심 4원칙 §4):
1. **제1원리** — 적 300을 싸게: 실측(아래) 스웜 렌더 합 2.05ms@300으로 예산(4ms) 2배 여유. 지배항이라 추정했던 "개별 컴포넌트"는 실측상 지배항이 아니었다(배칭을 깨는 건 컴포넌트 수가 아니라 per-actor MID — R2 전제 뒤집기).
2. **엔진 기본값과의 관계** — UE5 Mesh Draw Command 동적 인스턴싱(기본 on)이 동일 메시+머티리얼 컴포넌트를 병합하며, CPD는 그 병합을 보존하는 per-primitive 데이터 통로다(MID는 머티리얼을 분화시켜 병합을 깬다). 엔진 기본 경로를 그대로 쓰고, 덮지 않는다.
3. **프로젝트 정합** — 기존 인프라(캡슐 콜리전·shadow LOD·계측·per-BP stencil·풀링) 전부 무손실 유지. ISM 전환이 요구했던 수명주기 기구(슬롯/generation 핸들·이주·단일 기록자) 일체 불필요.

## 계약 (구현 배선)

> ℹ️ **정정 (2026-08-24, 사용자 결정 — 절차적 적 형태 프로토 합격)**: 아래 «CPD 슬롯»·«백엔드»·«머티리얼»
> 세 항목은 **스켈레탈(BroBot VAT) 전제 위에 쓰인 것이라 만료됐다.** 적 비주얼이 절차적 스태틱 메시로
> 바뀌면서(`95124189`) VAT 베이크 트랙이 폐기됐고, 그에 따라 헤더는 `FPSRVATAnimParams.h` →
> **`FPSRAnimCPDParams.h`**(네임스페이스 `FPSRVATAnim` → `FPSRAnimCPD`), 백엔드는
> `UFPSREnemyAnimProfile_VAT_CPD` → **`UFPSREnemyAnimProfile_Proc`**, 슬롯 의미는 아래로 바뀌었다:
>
> | 슬롯 | 신 의미 | 구 의미(만료) |
> |---|---|---|
> | 0 | `StateId` (Idle0/Walk1/Attack2/Death3) | StartFrame |
> | 1 | `EnterTime` (상태 진입 월드시각 초) | EndFrame |
> | 2 | `Rate` (루프=배율 / 원샷=1/지속시간) | PlayRate |
> | 3 | `Phase` (0..1) — **불변** | Phase |
> | 4 | `LastHitTime` (0=미피격) | 히트플래시 예약(미사용) |
>
> **이 ADR 이 정한 것 중 살아 있는 것** = 렌더 경로 판정 자체다(개별 SMC + 공유 머티리얼 + CPD, MID 폐기,
> ISM 기각) — 그 결론과 실측치는 절차 메시에도 그대로 유효하다. 만료된 것은 «무엇을 슬롯에 싣는가» 뿐이다.
> **슬롯 계약의 정본은 언제나 `FPSRAnimCPDParams.h`** 다. 아래 «리셋 계약»도 유효(오히려 확장됨 —
> 새 계약은 슬롯 4를 두 리셋 시임에서 명시적으로 0으로 되돌린다).
>
> 참고: CPD 페이로드는 엔진이 float4 9개(=36 슬롯)를 **고정 예약**하므로(`NUM_CUSTOM_PRIMITIVE_DATA`)
> 슬롯 수는 병합·비용에 영향이 없다. 병합을 깨는 것은 머티리얼 분화이며, 그 판정이 이 ADR 의 본론이다.


- **CPD 슬롯** (`FPSRVATAnimParams.h` = 단일 편집점): 0=StartFrame · 1=EndFrame · 2=PlayRate · 3=Phase(머티리얼 `TimeOffset`) · 4=히트플래시 예약.
  ⚠️ AnimToTexture에 "AnimationIndex" 파라미터는 **존재하지 않는다** — 클립 = [StartFrame, EndFrame] 프레임 구간(스파이크 중 발견, 종전 MID 드라이버는 이름 불일치로 무동작이었음).
- **백엔드** = `UFPSREnemyAnimProfile_VAT_CPD`(폴리모픽 프로파일 서브클래스, 중앙 switch 없음). 클립 구간은 프로파일의 `FFPSRVATClipRange` 프로퍼티(디자이너 저작, 하드코딩 금지).
- **머티리얼** = 플러그인 함수를 프로젝트로 복제해 CPD 플래그만 켠 변형(`MF_FPSR_GetFrameSwitch_CPD` → 4개 스칼라 파라미터 `bUseCustomPrimitiveData`+슬롯, `MF_FPSR_BoneAnimation_CPD`, `M_BroBot_VAT_CPD`, `MI_BroBot_VAT_Enemy_CPD`). **엔진 플러그인 콘텐츠는 불변**.
- **리셋 계약** — 풀 재사용 시 이전 생 상태 오염 방지: 서버 = `Activate()`(기존), 클라 = `SetActorHiddenInGame` 오버라이드(bHidden은 OnRep이 없고 복제 적용이 이 가상 setter 경유 — `ActorReplication.cpp:155-160` 실측).
- **BP 가드** — 적 BP에서 `CreateDynamicMaterialInstance` 금지(후속 코스메틱은 CPD 슬롯으로).

## 실측 (판정 근거, 2026-08-13)

Development 패키지(런처 엔진이 Test 미지원) · L_Map1_City 리슨 호스트 · 60m 링 스폰 수렴 · 고정 카메라:

| | B(CPD)@300 | A(현행)@300 | B@500 |
|---|---|---|---|
| 프레임 평균 / P95 | **5.48ms(182fps)** / 7.99ms | 6.48ms(154fps) / 8.08ms | 7.11ms(141fps) / 9.68ms |
| BasePass+Shadow+CustomDepth | **2.05ms** | 2.68ms | 3.03ms |
| RHI 드로우콜 | 445 | 529 | 562 |

- 사용자 실플레이 2판(카이팅) 육안: 애니 정상 · 두 빌드 정상 작동. A의 "정상 애니"는 머티리얼 AutoPlay 기본값(상태 제어 불가) — B만 상태 전환 요건 충족.
- 상세 원장 = `Packaged/Measurements/` · 러너 = `Scripts/measure_swarm_render.ps1` · 분석 = `Scripts/analyze_swarm_csv.py`.

## 기각/보존

- **ISM 전환(원 VAT-2) 기각** — B가 예산 통과 + 전 지표 우세라 채택 조건(B 불통과) 미성립. R1 수명주기 설계 6건(클라 슬롯 이벤트 소스 / 리슨 단일 기록자 / generation 핸들 / 그림자 상태 승격 / 이주 CPD 원자성 / 이중 백엔드)은 컨설트 리포트에 **컨틴전시로 보존** — 훗날 수천 단위로 규모가 바뀌면 그 문서부터.
- **Niagara 메시 렌더러 기각** — 신규 모듈 의존 + 위치가 게임스레드 소스라 이점 소멸(R2).

## 이월 검증 (VAT-2)

- V1: 드로우 완전 병합 분석(`stat scenerendering`/MeshDrawCommands) — 실측 드로우콜이 가시 적 수에 부분 비례(445@300→562@500). 예산 여유로 채택엔 무영향.
- V5: 원격 클라 CPD 리셋 2클라 리슨 테스트(kill→reuse 첫 가시 프레임).
- 부수 발견(별도 정리 대상): 멜리 BP stencil=1 vs 랭드 0 (WorkLog 기록과 상이한 콘텐츠 드리프트) · 셀↔아웃라인 stencil 상호배타 규약은 여전히 미확정.
