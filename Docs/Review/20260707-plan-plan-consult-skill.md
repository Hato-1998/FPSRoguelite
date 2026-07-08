# 플랜 컨설트: `/plan-consult` 스킬 설계 (2026-07-07)

> 이 리포트는 `/plan-consult` 스킬을 **자기 자신으로 도그푸드**한 산출물이다. 대상 작업 = "plan-consult 스킬 설계". Codex 5라운드 적대 토론(R1~R5) 수렴 결과.
> 원문: `Docs/Review/_raw/20260707-102349-plan-consult-skill-r1.md` … `-102617-…-r2` … `-102844-…-r3` … `-103110-…-r4` … `-103322-…-r5.md`.

## 1. Intake
- **Mode: FULL** — 하드트리거 해당(개발 워크플로 전반에 장기 영향 + 결정축 다수: 스킬 경계·라운드 정책·문서 구조·출력 계약).
- **plan_type: tooling/workflow** → Codex 렌즈 = 툴 설계 레드팀(R0 고정, 5R 유지).
- **라운드 정책: Deep Delta-Gated** (사용자 "5회 이상/깊게" 요구) — 실제 5R, 매 R 실질 delta.

## 2. 결과 플랜 (수렴된 v1 설계)
신규 2파일(consult 무수정 재사용):
- `.claude/commands/plan-consult.md` — 얇은 커맨드.
- `Docs/PlanConsultLoop.md` — ConsultLoop 참조형 델타 프로토콜.

핵심 흐름: task → Intake(LIGHT/FULL/NO-GO) → R0 `plan_type`·렌즈 고정 → 초안 플랜 → Codex 적대 수렴(delta-gated) → 5섹션 결정지원 리포트.
범위 밖(DEFER): 점수표 · telemetry · drift 스크립트 · Delta 태그 원장 · 다중 렌즈 분기표 · trial 롤아웃 (PlanConsultLoop §7).

## 3. 수렴 로그 (R1~R5 delta)
- **R1** (수렴-빠름): 별도 스킬 정당화(입력/종료 계약) + Codex 미가용 명명 격하 + 렌즈 R0 고정 + Divergence 고정축 + 실패모드 3종 방어. → PLAN_DELTA.
- **R2** (divergence: 전제 뒤집기 / 검증 불가능성): "쟁점은 변환될 때만 성과" 원칙 + Round Delta 등급(PLAN_DELTA/RISK_ACCEPTANCE/NO_DELTA) + Verification Status / Falsifiable Predictions 필드. → PLAN_DELTA.
- **R3** (divergence: 스코프 / 비용 / 역행): intake gate(LIGHT/FULL/NO-GO) + 경량 모드 + 문서 경계(ConsultLoop 참조형 델타) + rollback/telemetry 개념. → PLAN_DELTA.
- **R4** (통합·티어링): 과설계 자기비판 → 린 v1(점수표·telemetry·drift스크립트·Delta태그·다중렌즈·Divergence수치 DEFER) + hard stop budget = MUST. → PLAN_DELTA.
- **R5** (정책 충돌 화해): "5회 이상(깊이)" vs "극장 방지 상한" 충돌 해소 → 라운드 정책 4종 + delta-gated + `--deep`. 출시 차단 결함 = 없음. → PLAN_DELTA.

## 4. 미해결 쟁점 · 사용자 결정 필요
### 결정 D1 — 라운드 정책 기본값
- **왜 사람이 정하나**: 사용자의 명시 요구("5회 이상/깊게" = 깊이·관점 커버리지)와 Codex의 원칙("라운드 강제 → NO_DELTA → 컨설팅 극장")이 정면 충돌. 어느 쪽을 기본값으로 둘지는 취향·용도 판단.
- **선택지**: Bounded / Delta-Gated(권고) / Deep Delta-Gated / Fixed-Minimum Audit(N=5 강제).
- **선택 기준(rubric)**: 되돌리기 비용↑·불확실성↑·영향범위↑ → Deep/Fixed. 반복·저위험·검증 쉬움 → Bounded/Delta-Gated. '검토 기록 자체가 증거로 필요'하면 Fixed-Minimum Audit.
- 결정(2026-07-07, 사용자): 기본값 = **Deep Delta-Gated**. 단순·저위험 작업은 Intake가 LIGHT/Bounded로 자동 강등.

### 쟁점 C1 — 적응 렌즈 범위 (잠정 해소)
- **발생 경위**: R1에서 Codex가 "적응형은 좋지만 R0 고정 아니면 약해진다"고 지적, R4에서 다중 렌즈 분기표를 DEFER로 분류.
- **백엔드 이유**: 임의 작업 범용성 위해 적응 필요. **Codex 이유**: 분기표는 관료화 위험, R0 선택+잠금이면 충분.
- **현재 상태**: v1 = 렌즈 '선택'만 적응(R0 후 잠금), 분기표 DEFER. 사용자 "적응형" 의도 충족.

### 쟁점 C2 — LIGHT 산출물 위치 (잠정 해소)
- **발생 경위**: R3에서 경량 모드 산출을 어디 둘지. 사용자는 "Docs/Review 재사용" 선택, Codex는 LIGHT는 plan/PROGRESS 첨부 권고.
- **현재 상태**: FULL → `Docs/Review/`(사용자 선택 존중), LIGHT → 작업 플랜/PROGRESS 하단(경량 유지)로 절충.

## 5. 검증 상태
- **확인됨(코드 관찰)**: `Scripts/consult-codex.ps1` 5회 실호출 성공(Codex 왕복 작동). 재사용 경로 존재(`ConsultLoop.md`·`Docs/Review/`·`Docs/TaskPrompts_Master.md`).
- **추정(추론+선례)**: 스킬이 실사용에서 반복 채택될지 — 선례는 `/consult` 정착.
- **검증 필요**: 첫 2~3회 실사용 후 NO_DELTA 비율·PM 미인입 여부(§7 telemetry는 DEFER라 수동 회고).
- **자문 전용**: 이 설계 토론은 코드/에셋 미변경, 빌드/테스트 미검증. **반증가능 예측**: "틀렸다면 첫 2회 사용 내에 `/pm`과 이중 플래닝을 만들거나, LIGHT로 충분한 작업을 FULL로 돌린다."

## 액션 (PM 인입 후보)
- (구현 — 이 세션 완료) `.claude/commands/plan-consult.md` + `Docs/PlanConsultLoop.md` 생성.
- 실사용 3회 후 경량/Full 사용 비율·NO_DELTA율 회고 → 필요 시 §7 DEFER 항목 승격.
