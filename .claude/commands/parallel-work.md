---
description: 병렬 유닛 작업 셋업 — worktree 분리·핫스팟 직렬·머지-전-검토(§B-2/§B-3). 인자=유닛명(예 V1, U16)
---

이 작업은 **다중 세션 병렬 작업**으로 진행한다. 아래를 순서대로 수행하라.

1. **스킬 적용**: 프로젝트 스킬 `parallel-unit-work`(`.claude/skills/parallel-unit-work/SKILL.md`)를 읽고 그 체크리스트를 그대로 따른다. 이 커맨드는 그 스킬의 슬래시 런처다.

2. **문서 선독**: `Game.md` + `PROGRESS.md` + `Docs/TaskPrompts_Master.md`의 **§B-2(병렬 가이드)·§B-3(검토·머지 프로토콜)** + 진행할 유닛의 §C 프롬프트·도메인 SSOT.

3. **핵심 규칙 (스킬 요약 — 반드시 지킬 것)**:
   - **worktree 분리**: `git worktree add ../FPSR-<키워드> -b phase/<유닛-브랜치> main` 후 그 폴더에서 작업(같은 디렉터리 다중 세션 = 브랜치 전환 충돌).
   - **코드 핫스팟 직렬**: 발사계(`FPSRWeaponFireComponent`·`Hitscan GA`·`FPSRCombatStatics`) = U16·U3a·V3 동시 금지 / `GameMode.cpp` EndRun = U2→U3 순차.
   - **콘텐츠 배타**: `.uasset`/`.umap`은 LFS 머지 불가 → 한 세션만 점유(`BP_FPSRPlayer` 충돌 1순위).
   - **완료 정의**: main 머지가 완료가 아니다 = 자체 빌드(`-WaitMutex`)+헤드리스 스모크 통과+phase push+PR/완료 보고. **main 머지는 사용자 검토·승인 후에만**.
   - **문서를 작업 코드 브랜치에 섞지 말 것**(`--ff-only`로 문서 올리면 미검증 작업 커밋이 딸려감).

4. **진행**: 플랜모드 우선 → 사용자 승인 → 구현(Haiku 위임)/검증(Opus 직접: 빌드+스모크) → push+PR → 승인 후 `--no-ff` 머지 + PROGRESS·TaskPrompts §B ✅ + 콘텐츠 동반 커밋 질문.

인자 `$ARGUMENTS` = 진행할 유닛명(예 `V1`, `U16`). 지정되면 그 유닛의 §C 프롬프트로 바로 착수한다. 비어 있으면 어떤 유닛을 진행할지 사용자에게 묻는다.
