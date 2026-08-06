# Review — ConsultLoop 토론 결과 (추적, 프롬프트 매니저 인입원)

안건 소유자(Claude) × 웹/앱 적대 레드팀(Codex) 컨설팅 토론의 산출 리포트를 모은다. 프로토콜: [`Docs/ConsultLoop.md`](../ConsultLoop.md).
**프롬프트 매니저([`Docs/TaskPrompts_Master.md`](../TaskPrompts_Master.md) §E)가 이 폴더를 읽어** 각 리포트의 `📌 액션 아이템`을 백로그 유닛으로 인입한다.

- **명명**: `<yyyyMMdd>-<주제슬러그>.md` (예: `20260616-lmg-spinup-feel.md`)
- **포맷**: ConsultLoop §5 템플릿(종료사유/범위/📋안건/🕸️레드팀 공격/토론 요약/✅합의/🧾기각 원장/⚖️쟁점/🙋사용자결정/📌액션).
- **추적함** — 설계 의사결정 기록 + 프롬프트 매니저 입력원이라 git에 남긴다.
- `_raw/` = Codex 원시 응답(verbose) — **gitignore**(감사용 로컬 보관).
- ⚠️ **2026-08-07 이전 리포트는 옛 역할 배치**(백엔드 렌즈 × 클라이언트/콘텐츠 렌즈)로 작성됐다. 실제 오간 기록이므로 **소급 개정하지 않는다.**

> ⚠️ 폴더명 주의: `Docs/codex-reviews/`(gitignore)는 **2026-08-07 이전** 외부 Codex diff 리뷰 덤프의 로컬 보관소다(Windows 대소문자 미구분 때문에 `Docs/Review`와 이름을 분리했다). 머지 게이트가 **내부 Fable 레드팀**으로 옮겨간 뒤로는 새 파일이 쌓이지 않으며, 리뷰 결과는 머지 커밋 또는 `Docs/Specs/<유닛>.md` §13에 남는다(`Workflow.md` §6-6-1).
