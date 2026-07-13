# 플랜 컨설트: PWAS 1인칭 절차 무기애니 — 경로 B(C++ 네이티브 경량) (2026-07-10)

> `/plan-consult` 산출(decision-support). 자문 전용 — 코드/에셋 무변경. 채택 시 별도 승인 단계에서 해당 SSOT 갱신 후 구현.
> Shared Contract: ConsultLoop v1 · Plan Delta: PlanConsult v1. 원시 Codex 응답 = `Docs/Review/_raw/20260710-172034-pwas-path-b-r1.md`, `…-172426-pwas-path-b-r2.md`.

## 1. Intake
- **Mode: FULL** — 플레이어 측 FP 필 시스템의 구조 변경(장기 영향·되돌리기 비용) + 문서화된 아트 스택 변경(PWAS→C++) + 결정축 3개+. 사용자가 적대 게이트 명시 요청.
- **plan_type: mixed** — primary=backend/system 레드팀(계약·확장성·검증성·롤백), secondary 강제=content/feel(애니 패리티, 사용자 1순위).
- **라운드 정책: Deep Delta-Gated** — R1(과설계·실행결함·필패리티·확장성) + R2 divergence(타이밍·스키마·롤백). 7축 소진, 수렴.
- Codex: `codex exec` read-only, 설치·응답 확인(미검증 초안 아님).

## 2. 결과 플랜 (수렴)
**결정 = 경로 B 확정 근거 충분**(조사 3축 + Codex 2R). 정적 Synty 무기(본0)에 절차 트랜스폼 애니를 단일 seam(FPSRCharacter.cpp:1437)에 owner-local 코스메틱으로 합성. 애니메이션(살아있는 총) = PWAS와 동일 클래스, B로 패리티 가능.

**락인된 설계 불변식**
- 단일 seam 합성(1437), owner-local·복제0, cached base 재구성(자기-읽기 피드백 금지).
- **계약 가드레일(Codex C)**: Hip 모션 상태를 발사 GA·발사체 스폰·반동 컴포넌트·복제 스탯·크로스헤어 spread가 **절대 읽지 않는다.** 제거 시 수정파일이 `FPSRWeaponDataAsset.*`+`FPSRCharacter.*`에 갇히면 저비용 롤백; 그 밖으로 번지면 설계가 새는 신호.
- 트레이스/진실 크로스헤어=뷰축 유지, 총구=시각효과만(Codex D).
- 합성 순서 고정: base → ADS blend → hip additive ×(1−α) → kick/dip pivot.
- `ClampedDt=Min(dt,1/30)` (저프레임 FInterpTo 스냅 회피). edge(equip/weapon-change/reload-start/ADS-enter)서 상태(PreviousControlRotation·sway accum·BobPhase·kick·dip) 명시 리셋.
- 스키마 = **`FFPSRProceduralWeaponMotionProfile` USTRUCT**(Hip 전용, DA에 1필드 포함). ADS는 flat 유지(비대칭 명시 수용 — Codex B).

**단계**
- **P0 그립(콘텐츠, 코드0)**: rifle 1정 DA `WeaponAttachSocket='ik_hand_gun'` + BP 상대오프셋 → 손 안착 눈검증. **C++ 합성 진입 전 선통과 필수.**
- **P1 C++ 코어 최소 슬라이스(구조 검증)**: USTRUCT 프로파일 + 런타임 상태 + edge reset + UpdateAimDownSights 힙 레이어(**3효과만: HipLookSway+HipWalkBob+HipFireKick**, breathing·reload dip 제외) + PlayWeaponFireCosmetics 힙 킥 bump. **rifle 1정 + 과장 테스트값**으로 구조(합성순서·edge reset·ADS suppression·fire hook 충돌) 검증. 빌드(-NoXGE)+헤드리스 스모크. **완료조건=사용자 PIE 판정**(60/30/20fps: "조준/탄착 신뢰 무해" 먼저, "살아있나" 다음).
- **P1b breathing + reload dip(별도 게이트)**: reload dip=재장전 몽타주 없는 정적무기 보완용 per-weapon bool(ADS reload-relax 이중하강 회피). PIE.
- **P2 무기별 정밀 튜닝(Blu 팔 도입 후 lock 권장)**: 6종+ Hip* 저작. Blu 도입 시 기준좌표계(BaseArmsRelLoc/Rot·소켓) 변경으로 재튜닝 확률 높음 → 정밀 튜닝은 Blu 이후. P1/P1b는 durable(코드 무수정, DA값만).
- **P3 검증·머지**: Codex 머지게이트 + PIE 최종판정 → phase `--no-ff` main 머지 + SSOT/메모리 갱신(FP팔=Blu+C++절차모션, PWAS=파라미터 레퍼런스 강등).

**정량 패리티 게이트(6요소)**: look sway · movement bob · fire impulse · reload settle · ADS suppression · weapon-specific intensity. P1 슬라이스가 5요소 커버(reload settle=P1b).

**범위 밖(DEFER)**: SceneComponent 오프셋 노드 · AnimBP 포스트프로세스 · ADS→struct 마이그레이션 · curve 기반 sway(코드가 더 쌈) · 벽충돌/크라우치/홀스터 스프링(선택 패리티) · 정적무기 내부파츠 스켈레탈 애니(리그드 에셋 필요=별도 트랙).

## 3. 수렴 로그 (초안→변경)
- **R1**: 초안이 약간 과설계 → **P1 최소 슬라이스**(3효과·1무기·그립 선통과)로 축소[PLAN_DELTA]. 놓친 결함=**base cache 수명 + edge 상태 리셋 + ClampedDt**[PLAN_DELTA]. reload dip=per-weapon bool 별도 게이트[PLAN_DELTA]. 애니 패리티=B로 가능, PWAS 잔여이점=프리셋 폭/에디터 UX(수학 아님)[content 렌즈]. **Hip*를 USTRUCT 프로파일로 묶기**[PLAN_DELTA].
- **R2**: 타이밍=**P1 지금·P2 정밀튜닝 Blu 후**(기준좌표계 변경)[PLAN_DELTA]. 스키마=**Hip만 struct·ADS flat 유지**(ADS 마이그레이션 비용>이익)[RISK_ACCEPTANCE]. 롤백=낮으나 0 아님, **가드레일=Hip 모션이 gameplay contract로 새지 않게**[RISK_ACCEPTANCE]. 총구/크로스헤어 어긋남=코스메틱 수용, 트레이스는 뷰축[NO_DELTA].
- **기각/보류**: ADS struct 마이그레이션(보류=별도 refactor 후보), SceneComponent/AnimBP 대안(보류=결합도·seam 파괴), 물리 스프링 오버슈트(기각=현 목표 과함).

## 4. 미해결 쟁점 · 사용자 결정 필요
1. **경로 확정(B)** — 경위: 조사+R1·R2로 B가 구조·필·확장·롤백 전부 우위 확인. 백엔드: 회귀0·유지보수·정적무기 동작. Codex: 동의(단 슬라이스 축소·가드레일 전제). **사용자 결정 이유**: 구매한 PWAS 미사용 + 아트 스택 문서 변경이라 사람이 확정. 기준: 애니메이션(사용자 1순위)이 B로 충족되는가(=procedural 살아있는 총; 스켈레탈 파츠 애니는 A·B 공통 불가·에셋 문제).
2. **P2 정밀 튜닝 타이밍** — 경위: R2-A. **결정 이유**: Blu 팔 도입 시점/근시일 완성도 체감에 의존(사람 판단). 선택지: (a)지금 플레이스홀더에서 전 무기 튜닝(Blu 후 재튜닝 감수) vs **(b·권장)** P1/P1b로 1무기 구조검증만 지금, 전무기 정밀튜닝은 Blu 후. 기준: 재작업 최소(제1원리) vs 근시일 비주얼 완성도.

## 5. 검증 상태
- **확인됨(코드 관찰)**: 단일 seam(1437)·rigid child·트레이스=뷰축·CrystalRecoil=컨트롤회전·owner-local 가드·기존 ADS 절차 합성 패턴·DA ADS 필드 flat·그립 소켓 폴백(ik_hand_gun 존재). [증거등급: 코드/소스 직접 관찰 + 워크플로 3축 조사]
- **추정**: Blu 팔 도입 시 P0/P2 재튜닝 필요(선례·구조 추론). ClampedDt/edge-reset의 실제 필/프레임 안정성(자문 전용이라 빌드/PIE 미검증).
- **자문 전용**: 빌드/테스트 미실행. **반증가능 예측**: 플랜이 맞다면 Blu 교체 후 C++ 무수정·DA값만 변경(P1 durable). 틀렸다면 첫 PIE 체크포인트에서 저프레임 스냅 또는 reload 이중하강이 보임 → ClampedDt/reload-bool 재설계 신호.

## 📌 PM 인입 후보
- 이 플랜(경로 B) → `Docs/TaskPrompts_Master.md` 신규 유닛(예: U21 절차 무기모션). 채택 시 SSOT(Concept §1-C 아트스택·CombatWeaponCard §2-5 사격감) 먼저 갱신(FP팔=Blu+C++절차모션, PWAS 강등).
