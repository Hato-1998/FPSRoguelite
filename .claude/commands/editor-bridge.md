---
description: UE 에디터 작업 시도(VibeUE MCP → headless commandlet) → 연결/실행 불가 시 핸드오프 + 새 세션 재개 프롬프트 작성
---

UE 에디터가 필요한 작업(DataAsset·카드·GE 배선, 위젯 저작, 에셋 인스펙션 등)을 **연결 시도 → 실행, 안 되면 핸드오프**로 처리한다. `$ARGUMENTS`에 작업 내용이 오면 그것을, 없으면 현재 대화 맥락의 에디터 작업을 대상으로 한다.

## 1. 작업 파악 + 안전 게이트
- 수행할 에디터 작업과 대상 에셋 경로(`/Game/...`)를 명확히 한다.
- **파괴적 작업(콘텐츠 삭제·배열 제거·덮어쓰기)은 먼저 read-only 인스펙션**으로 현황을 확인한 뒤 진행. 삭제는 사용자 승인 없이 하지 않는다(비파괴 우선 = 추가/수정, 제거는 플래그).

## 2. 연결 경로 결정 (순서대로 시도)
- **(A) VibeUE MCP** — 세션에 VibeUE MCP 툴이 로드돼 있고(ToolSearch로 확인) **에디터가 열려 있을 때만**(127.0.0.1:8088, 등록명 `VibeUE-Claude`). 위젯/BP 그래프·라이브 편집에 적합. `mcp__unreal_editor__*`는 죽은 Aura 잔상이니 쓰지 말 것. [[unreal-editor-mcp-vibeue]] [[vibeue-mcp-capabilities]] — 컨테이너 위젯 프로그래매틱 compile/save는 크래시/손상 위험이니 회피. [[vibeue-render-target-gpu-hazard]]
- **(B) Headless commandlet** — 에디터가 **닫혀 있어도** 되고, DataAsset/카드/GE 배선·인스펙션에 가장 안정적. [[headless-gas-content-authoring]]
  - 실행: `"D:/UnrealEngine/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<uproject>" -unattended -nopause -nullrhi -nosplash -nosound -ExecutePythonScript="<script.py>" -abslog="<log>"`
  - 스크립트는 스크래치패드에 작성. Python: `unreal.EditorAssetLibrary.load_asset/save_asset`, `get/set_editor_property`. 배열 수정 = 기존 리스트 복사 → append → `set_editor_property` → `save_asset(path, only_if_is_dirty=False)`.
  - **먼저 read-only 인스펙션 스크립트**로 에셋이 로드되는지·현황을 출력해 검증(로그 grep은 `LogPython:` 접두사 때문에 `^` 앵커 쓰지 말 것) → 그 다음 write 스크립트.
  - **주의**: 에디터가 **열려 있으면** .uasset 락으로 저장 충돌 → `tasklist | grep -i unreal`로 확인 후 닫고 실행. LFS·`*_BuiltData` gitignore 유지. [[ue-editor-file-locks-block-git]]

## 3. 실행 + 검증
- 스크립트 실행 후 로그의 성공 마커(예: `SAVED=True`, `=== *_END ===`) 확인.
- write면 **재-인스펙션(또는 로그의 최종 상태)**으로 결과를 검증하고, `git status --short`로 변경된 `Content/*.uasset`이 의도와 일치하는지 본다(리다이렉터·스트레이 없는지).
- 코드가 얽힌 작업이면 빌드/스모크 검증(검증 없이 "완료" 보고 금지).

## 4. 성공 시
- 변경 요약(무엇을, 어느 에셋에) + 비파괴로 남긴 항목·플래그를 보고.
- 콘텐츠 커밋은 프로젝트 규칙대로: 코드 완료 시 동반 콘텐츠 커밋 여부 확인 [[phase-end-commit-user-content]], 커밋 메시지 `content(<유닛>): ...`. **PIE 검증은 사람이 하는 단계**이므로, 남으면 5의 재개 프롬프트로 넘긴다.

## 5. 실패 시(연결/실행 불가) → 핸드오프 + 재개 프롬프트
연결 불가(MCP 미연결+에디터 못 엶)·commandlet 에러·에디터 락 등으로 진행 불가하면:
- **원인 1줄 기록**(락/미연결/스크립트 에러 등)과 **어디까지 됐는지**.
- **PROGRESS.md 상단 핸드오프 갱신**(`handoff` 스킬과 동형): ① 현재까지 한 일 ② **남은 에디터 작업(구체적: 대상 에셋 경로 + 작성해 둔 스크립트 경로 + 매핑/의도)** ③ 블로커 ④ 미커밋 `Content/` 목록.
- **코드/문서만 커밋**(`Source/`·`Scripts/`·`*.md`·`.claude/`), `Content/`는 사용자 몫으로 남김. 검증 없이 커밋 금지. 준비한 Python 스크립트가 재사용 가능하면 `Scripts/`로 옮겨 커밋(재현성).
- **새 세션 재개 프롬프트 출력**(복붙용, 아래 형식):
  ```
  [<유닛> 에디터 작업 이어서]  브랜치: <branch>
  ■ 먼저 읽기: PROGRESS.md 상단 핸드오프 + Game.md(§0-1) + 관련 Docs/SSOT
  ■ 에디터 작업: <대상 에셋/작업> — 스크립트 <경로>(있으면). 연결 = 에디터 열고 VibeUE MCP,
     또는 닫고 headless commandlet(-ExecutePythonScript). 파괴적이면 read-only 인스펙션 먼저.
  ■ 검증: 재-인스펙션 + git status. (코드 얽히면 빌드+스모크)
  ■ 완료 후: <다음 단계 = 사용자 PIE / 머지 / 등>
  ```
- **push 여부 질문**: 다른 기기에서 이어가려면 push 필요함을 알리고 원하면 `git push origin <branch>`.

## 참고
- 이 스킬은 "에디터 연결 시도"가 핵심 — 되면 실행, 안 되면 깔끔한 인계로 다음 세션이 즉시 재개하게 하는 것이 목적. 모델 정책: 스크립트 작성=Haiku 위임 가능하나 서버권위/파괴적 작업·검증은 Opus 직접.
- 성공 예(레퍼런스): U6 콘텐츠 배선 — read-only 인스펙션(`inspect_frags.py`)으로 현황 확인 → 비파괴 additive write(`wire_frags.py`)로 무기별 `UnlockableFeatures` 배선 → 재-inspect·`git status` 검증 → `content(U6)` 커밋.
