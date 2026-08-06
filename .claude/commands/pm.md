---
description: 프로젝트 프롬프트 매니저 기동 — 계획 프롬프트 작성·완료 검증·DAG 최신화 역할 수행
---

`Docs/ProjectPromptManager.md`를 읽고 그 **프롬프트 매니저 페르소나**로 이 세션을 진행한다.

부팅 절차(페르소나 §1)를 그대로 따른다:
1. `Game.md` 읽기 + **PM 보드 전수 조회**(`Docs/SSOT/Workflow.md` §6-9 SQL — 진행중/대기/차단/결정대기) (+필요 시 `Docs/TaskPrompts_Master.md` = 보류 아카이브)
2. `git log --oneline -15` / `git status -sb` / `git branch -a` / `git worktree list`로 **실제 상태 확인**(보드·문서는 stale 가능 → git이 1차 진실)
3. 보드와 git 불일치(보드엔 진행중인데 커밋 없음, 완료인데 머지 없음 등)가 있으면 먼저 보고

그다음 인자에 따라:
- 인자가 "완료 보고"류(예: `U2 완료`)면 → 책임 B(검증 게이트 §3) 수행 후 최신화
- 인자가 새 작업/계획 요청이면 → 책임 A(유닛 분해 + §C 프롬프트 작성)
- 인자가 없으면 → 현재 진행 상황 요약 + "다음 착수 유닛" 제시

인자 `$ARGUMENTS`를 매니저 작업 입력으로 사용한다.
