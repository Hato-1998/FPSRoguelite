# FPSRoguelite — 프로젝트 지침 (Codex/에이전트 자동 로드)

> **작업 시작 전 반드시 `Game.md`(SSOT 허브)와 `PROGRESS.md`(진행 현황)를 먼저 읽을 것.**
> 기획·설계·기술스택·코드구조·환경경로·빌드/검증법·모델정책은 `Game.md` 허브의 라우팅 가이드(§0-1)를 따라 **작업에 해당하는 `Docs/SSOT/*.md`** 만 읽는다(문서 분할, 섹션번호 보존).

## ⛔ 절대 금지 / 핵심 3원칙
1. **장르 편향 금지** — 1인칭 FPS × 뱀파이어 서바이벌 × 4인 협동 로그라이트(레퍼런스 The Spell Brigade). Hero Shooter 아님. 적 수백(~500) 싼 액터. Lyra 풀fork·전적 GAS·적별 StateTree/NavMesh·Iris 핵심의존·MassEntity 금지. GAS는 플레이어+보스만. (상세 Game.md §1)
2. **프로덕션 방식** — C++=로직/베이스, 콘텐츠=BP/DataAsset/config. 에셋 경로 C++ 하드코딩 금지. 서버권위+Push Model. 검증 없이 "완료" 보고 금지. (상세 §6 = `Docs/SSOT/Workflow.md`)
3. **핸드오프** — 단계 완료·세션 중단 전 `PROGRESS.md` 갱신·커밋. 설계 변경은 해당 `Docs/SSOT/` 도메인 파일 먼저. 새 작업은 플랜 우선, HIGH_RISK는 승인 후. 구현=Haiku 위임/검증=Opus 직접. (상세 §6 = `Docs/SSOT/Workflow.md`)
