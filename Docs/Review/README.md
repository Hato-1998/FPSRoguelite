# Review — ConsultLoop 토론 결과 (추적, 프롬프트 매니저 인입원)

백엔드(Claude)×클라이언트(Codex) 컨설팅 토론의 산출 리포트를 모은다. 프로토콜: [`Docs/ConsultLoop.md`](../ConsultLoop.md).
**프롬프트 매니저([`Docs/TaskPrompts_Master.md`](../TaskPrompts_Master.md) §E)가 이 폴더를 읽어** 각 리포트의 `📌 액션 아이템`을 백로그 유닛으로 인입한다.

- **명명**: `<yyyyMMdd>-<주제슬러그>.md` (예: `20260616-lmg-spinup-feel.md`)
- **포맷**: ConsultLoop §5 템플릿(범위/양측 입장/토론 요약/✅합의/⚖️쟁점/🙋사용자결정/📌액션).
- **추적함** — 설계 의사결정 기록 + 프롬프트 매니저 입력원이라 git에 남긴다.
- `_raw/` = Codex 원시 응답(verbose) — **gitignore**(감사용 로컬 보관).

> ⚠️ 폴더명 주의: 코드 diff 리뷰 덤프는 별도 `Docs/codex-reviews/`(gitignore)다. Windows 대소문자 미구분으로 `Docs/Review`와 충돌하지 않도록 이름을 분리했다.
