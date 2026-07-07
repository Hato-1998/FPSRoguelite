---
description: 플랜 특화 컨설트 — 작업을 플랜화 → Codex 적대 수렴 → 결정지원 리포트(결과 플랜 + 쟁점[경위·양측] + 사용자 결정[왜·기준])
---

플랜 컨설트를 수행한다. **반드시 먼저 [`Docs/PlanConsultLoop.md`](Docs/PlanConsultLoop.md) 전체를 읽고 그 프로토콜대로** 진행하라. (그 문서가 SSOT — 공유 토론 골격은 [`Docs/ConsultLoop.md`](Docs/ConsultLoop.md)에서 상속하며 여기서 재정의하지 않는다.)

대상 작업: **$ARGUMENTS**
- 인자가 비어 있으면, 무엇을 계획할지 사용자에게 먼저 물어본 뒤 진행한다.

수행 순서 (PlanConsultLoop 요약 — 상세는 그 문서):
1. **Intake 판정** — 작업을 `LIGHT / FULL / NO-GO` 로 분류(기본 LIGHT; 하드트리거는 FULL; 요구 불명확·HIGH_RISK 승인/SSOT 선행 필요면 NO-GO).
2. **R0 범위설정** — `Game.md`·`PROGRESS.md` + 관련 `Docs/SSOT/*.md`·소스를 읽어 사실 기반 확보. `plan_type` 판정 후 Codex 적대 렌즈를 R0에서 고정한다.
3. **초안 플랜 작성** — 백엔드 렌즈로 실행 가능한 플랜 초안(목표·제약·plan·사용자 결정지점·검증법). 이 단계에서 코드/에셋 변경 금지.
4. **Codex 적대 수렴** — `Scripts/consult-codex.ps1 -PromptFile <file> -Title <슬러그>` 로 라운드마다 플랜을 공격받고 조정. **라운드 정책**(Bounded / Delta-Gated / Deep Delta-Gated / Fixed-Minimum Audit)에 따라 종료. 기본 = Deep Delta-Gated(깊이 우선; 서로 다른 divergence 축을 소진한 뒤 종료). 반복·저위험 작업은 Intake가 LIGHT로 자동 강등하거나, 명시 시 Delta-Gated/Bounded로 낮춘다. 각 라운드는 delta(PLAN_DELTA / RISK_ACCEPTANCE / FALSIFIABLE_CHECK) 또는 NO_DELTA로 기록한다.
5. **산출** — PlanConsultLoop §5 템플릿(Intake / 결과 플랜 / 수렴 로그 / 미해결·사용자결정 / 검증 상태)으로 리포트. FULL은 `Docs/Review/<yyyyMMdd>-plan-<슬러그>.md` 저장 + 채팅 압축요약. LIGHT는 작업 플랜 또는 `PROGRESS.md` 하단에 첨부.

가드레일: **자문 전용 — 코드/에셋 변경 금지.** Codex 미가용이면 "Codex 미검증 초안"으로 격하한다(수렴 리포트라 부르지 않음). 결과 = 결정지원 리포트지 실행 지시가 아니다(구현은 별도 승인 단계, `/pm`·`Docs/TaskPrompts_Master.md` 파이프라인 인입 후보로만). Codex 미설치/미인증이면 `npm install -g @openai/codex` + `codex login` 안내.
