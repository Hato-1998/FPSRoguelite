---
description: 플랜 특화 컨설트 — 작업을 플랜화 → 웹/앱 적대 레드팀(Codex) 수렴 → 결정지원 리포트(결과 플랜 + 쟁점[경위·양측] + 사용자 결정[왜·기준])
---

플랜 컨설트를 수행한다. **반드시 먼저 [`Docs/PlanConsultLoop.md`](Docs/PlanConsultLoop.md) 전체를 읽고 그 프로토콜대로** 진행하라. (그 문서가 SSOT — 공유 골격[범위 게이트·역할/페르소나·종료 권한·호출법]은 [`Docs/ConsultLoop.md`](Docs/ConsultLoop.md)에서 상속하며 여기서 재정의하지 않는다.)

대상 작업: **$ARGUMENTS**
- 인자가 비어 있으면, 무엇을 계획할지 사용자에게 먼저 물어본 뒤 진행한다.

수행 순서 (PlanConsultLoop 요약 — 상세는 그 문서):
1. **범위 게이트 → Intake 판정** — 먼저 ConsultLoop §0-1(= `Docs/SSOT/Workflow.md` §6-5-2 트리거 `T1~T5`)로 `Scope: IN/OUT`을 판정한다. **OUT이면 Codex를 부르지 않고 `Codex 미검증 초안`으로 플랜만 작성해 사용자 승인으로 간다**(NO-GO 아님). IN이면 무게를 `LIGHT / FULL / NO-GO`로 분류(기본 LIGHT; 하드트리거는 FULL; 요구 불명확·HIGH_RISK 승인/SSOT 선행 필요면 NO-GO).
2. **R0 범위설정** — `Game.md`·`PROGRESS.md` + 관련 `Docs/SSOT/*.md`·소스를 읽어 사실 기반 확보. `plan_type`을 판정하고 **먼저 소진할 공격 축**을 정한다(페르소나는 고르지 않는다 — 항상 웹/앱 적대 레드팀 하나).
3. **초안 플랜 작성** — 안건 소유자로서 실행 가능한 플랜 초안(목표·제약·plan·사용자 결정지점·검증법). 이 단계에서 코드/에셋 변경 금지.
4. **Codex 적대 수렴** — `Scripts/consult-codex.ps1 -PromptFile <file> -Title <슬러그>` 로 라운드마다 플랜을 공격받고 조정한다(**역할 프라이머는 프롬프트에 쓰지 마라 — 래퍼가 `Docs/CodexRedTeamPersona.md`를 자동으로 앞에 붙인다**). 질문은 좁힌다: "이 플랜이 과설계인가 / 실행을 막는 결함이 있나 / 사용자가 결정해야 할 쟁점은?". **라운드 정책**(Bounded / Delta-Gated / Deep Delta-Gated / Fixed-Minimum Audit)에 따라 종료. 기본 = Deep Delta-Gated. 각 라운드는 delta(PLAN_DELTA / RISK_ACCEPTANCE / FALSIFIABLE_CHECK) 또는 NO_DELTA로 기록. 5분 내 무출력이면 종료·스킵.
5. **종료 (ConsultLoop §3-1)** — **최종 결정권은 너에게 있다.** 라운드 정책과 무관하게 언제든 끝낼 수 있되, ⓐ `종료 사유` 1줄 + ⓑ **기각 원장**(기각한 지적 전부 + 각각의 근거 = 제1원리 조항 / 코드 인용 / 실측치) + ⓒ 정책 상한 전에 끊었으면 **남긴 축 이름**을 남겨야 성립한다. 근거를 못 대면 기각이 아니라 미해결 쟁점으로 내리고, `[가설]` 태그 지적은 기각 말고 검증 항목으로 옮긴다.
6. **산출** — PlanConsultLoop §5 템플릿(Intake / 결과 플랜 / 수렴 로그 / 🧾기각 원장 / 미해결·사용자결정 / 검증 상태)으로 리포트. FULL은 `Docs/Review/<yyyyMMdd>-plan-<슬러그>.md` 저장 + 채팅 압축요약. LIGHT는 작업 플랜 또는 `PROGRESS.md` 하단에 첨부.

가드레일: **자문 전용 — 코드/에셋 변경 금지.** Codex 미가용이거나 Scope OUT이면 "Codex 미검증 초안"으로 격하한다(수렴 리포트라 부르지 않음). **너의 결정권은 토론을 끝낼 권한이지 실행 권한이 아니다** — 결과는 결정지원 리포트지 실행 지시가 아니며, 구현은 별도 승인 단계(`/pm`·`Docs/TaskPrompts_Master.md` 파이프라인 인입 후보로만). Codex 미설치/미인증이면 `npm install -g @openai/codex` + `codex login` 안내.
