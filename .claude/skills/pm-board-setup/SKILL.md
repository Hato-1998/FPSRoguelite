---
name: pm-board-setup
description: Bootstrap the full Notion workspace shape for a project — a PM task board ("작업 보드") plus the reading layer around it (분야 pages, 문서 index DB, 결정 기록 DB, 아이디어·논의 DB) — and wire the repo-side harness: .mcp.json, the pm-board subagent, the /board slash command, the PreToolUse guard hook, the SessionStart hooks (git state + board snapshot), a PROGRESS.md pointer, the protocol doc, and a CLAUDE.md hard-gate paragraph. Use whenever the user wants to set up, install, replicate, or migrate this Notion workflow into a new or existing project — Korean phrasings like PM 보드 만들기, 작업 보드 도입, 노션 보드 세팅, 노션 구조 옮기기, 이 노션 방식 다른 프로젝트에도, 클레임 게이트 도입. Also use when a project already has it and the user asks to repair, audit, or extend the schema, since references/schema.md and references/workspace-structure.md hold the verified DDL and references/notion-tool-traps.md holds the failure modes. Do NOT use for day-to-day board operations (claim/close/audit) in a project that already has /board installed — that is /board's job.
---

# PM 보드 부트스트랩

프로젝트에 **Notion 작업 보드 + 리포 하네스** 세트를 심는 설치기다. 원형은 FPSRoguelite 리포의 `Docs/SSOT/Workflow.md` §6-9(2026-08-07 도입)이고, 이 스킬은 그 형태에서 프로젝트 고유 요소를 벗겨낸 것이다.

**형태를 이루는 요소 (전부 심어야 "같은 형태"다)**

| 쪽 | 요소 | 템플릿 |
|---|---|---|
| Notion | 허브 페이지 → 작업 보드 DB(뷰 7개) → (선택) 마일스톤 DB → 운영 규칙(사람용) 페이지 | `references/schema.md` |
| Notion | **분야 페이지 4개 + 문서 · 결정 기록 · 아이디어·논의 DB**(읽는 문서 층) | `references/workspace-structure.md` |
| 리포 | 프로토콜 정본 문서 | `templates/PMBoard.md` → `Docs/PMBoard.md` |
| 리포 | `.mcp.json` (Notion MCP 전파, 시크릿 없음) | `templates/mcp.json` |
| 리포 | `pm-board` 서브에이전트 (읽기 + 추가만) | `templates/pm-board.agent.md` → `.claude/agents/pm-board.md` |
| 리포 | `/board` 슬래시 커맨드 (에이전트 위임 창구) | `templates/board.command.md` → `.claude/commands/board.md` |
| 리포 | PreToolUse 가드 훅 (에이전트의 덮어쓰기 차단) | `templates/pm-board-guard.ps1` → `Scripts/pm-board-guard.ps1` |
| 리포 | SessionStart 훅 (git 상태 + PROGRESS 포인터 출력) | `templates/session-resume.ps1` → `Scripts/session-resume.ps1` |
| 리포 | **보드 스냅샷 스크립트**(세션 시작 조회를 네트워크에서 뺀다) | `templates/board-snapshot.ps1` → `Scripts/board-snapshot.ps1` |
| 로컬 | `.claude/settings.json` 훅 배선 (gitignore 대상 — 클론마다) | `templates/settings.hooks.json` |
| 리포 | `PROGRESS.md` 포인터 | `templates/PROGRESS.md` |
| 리포 | CLAUDE.md 하드 게이트 문단 | `templates/CLAUDE.snippet.md` |

왜 이 형태인가(제1원리): git 문서는 브랜치-로컬이라 머지 전엔 병렬 세션끼리 서로 안 보인다. **공유 가변 상태(누가 무엇을 하는가)는 git 밖에 살아야** 하고, 그걸 매 세션이 무조건 거치게 만드는 것이 하드 게이트다. 근거 상세 = `references/design-rationale.md`.

🚨 **읽는 문서 층에는 한 줄짜리 헌법이 있다 — 축자 사본을 만들지 마라.** 원형은 repo `.md` 41개를 노션에 그대로 올렸다가 이틀 만에 폐기했다(같은 내용이 두 곳에 있으면 한쪽이 반드시 낡고, 옮겨서 새로 얻는 정보가 0이었다). **repo = 정본 명세 / 노션 = 색인과 「왜」**. 상세 = `references/workspace-structure.md` §A.

**함정 사전** — 설치·운영 중 실제로 밟은 노션 도구 실패형(한글 변조·자식 페이지 삭제·배치 원자성·조회 잘림 등) = `references/notion-tool-traps.md`. **설치 전에 훑고, 막히면 여기부터 본다.**

---

## 0. 착수 전 확인

1. **Notion MCP 연결** — `ToolSearch`로 `select:mcp__notion__notion-fetch,mcp__notion__notion-create-pages,mcp__notion__notion-create-database,mcp__notion__notion-update-data-source,mcp__notion__notion-create-view` 를 **한 번에** 로드. 없으면 사용자에게 연결 요청 후 대기(지어내지 말 것).
2. **`notion-fetch "self"`** 로 워크스페이스 확인 — 사용자가 의도한 워크스페이스인지 이름을 보고한다.
3. **대상 리포** — 작업 디렉터리가 git 리포인지, `.claude/`·`Scripts/`·`Docs/` 관례가 이미 있는지 확인. 이미 `.claude/agents/pm-board.md`가 있으면 **설치가 아니라 수리/확장**이다 → §5로.
4. 이 설치는 **HIGH_RISK**(외부 API 쓰기 + 파일 다수 생성)다. §1의 질문에 답을 받은 뒤 §2~§4를 한 번에 진행하되, 시작 전에 생성할 것 목록을 1회 보여 승인받는다.

## 1. 부트스트랩 질문 (한 번에 묻는다)

`AskUserQuestion` 1회로 아래를 받는다. 기본값이 있는 항목은 기본값을 첫 옵션으로.

| 항목 | 기본값 | 비고 |
|---|---|---|
| 프로젝트 이름 (허브 페이지 제목 접두) | 리포 폴더명 | `{{PROJECT}}` |
| 허브를 만들 Notion 상위 페이지 | 워크스페이스 최상위(private) | URL을 받으면 그 아래 |
| `영역` multi_select 옵션 목록 | 물어야 함 — 프로젝트별 | 8~14개 권장. 병렬 충돌 감지용이라 **코드 모듈 경계**와 맞출 것 |
| `분야` 4분류 | **고정** — 기획 / 아트 / 프로그래밍 / AI·워크플로 | 묻지 않는다. 읽는 문서 층 3개 DB와 분야 페이지가 이 4개를 공유한다. 비게임 프로젝트면 앞 셋의 *이름만* 바꿔 쓰되 **`AI·워크플로`는 그대로 둔다**(AI 작업자 절차서 자리) |
| **마일스톤 축** 사용 여부 + 코드 목록 | 사용 안 함 | 사용하면 마일스톤 DB + `마일스톤` relation + `크기`의 XL 분할 규칙이 켜진다. 코드 예 `M0~M4`. `성격`·`동결선` 열은 출시형 프로젝트 전용 — 필요할 때만 |
| **게이트 갈래 열**(원형의 `추천모델`) 사용 여부 | 사용 안 함 | "이 행이 무거운 리뷰 게이트를 타는가"를 보드에 드러내는 select. 켜면 열 이름·옵션 2개를 받는다 (예 `게이트`: `코어`/`일반`) |
| 프로토콜 문서 경로 | `Docs/PMBoard.md` | `{{PROTOCOL_DOC}}` |
| 커밋 메시지 scope 관례 | `type(scope): …` 의 scope = 행 약칭 | 감사 D2/D4가 이걸로 보드↔git을 대조한다. 관례가 없으면 D2/D4는 "수동"으로 표기 |

> `크기`(S/M/L/XL)는 마일스톤과 무관하게 **항상** 넣는다 — 상대크기 축이라 어느 프로젝트든 쓸모가 있고, 원형에서도 마일스톤과 같은 날 들어왔지만 독립 열이다.

## 2. Notion 생성 (순서 고정 — 의존관계가 있다)

`references/schema.md`의 DDL·뷰 DSL을 **그대로** 쓴다. 뷰 DSL 구분자 등이 불확실하면 먼저 `notion-fetch "notion://docs/view-dsl-spec"`(호출 무제한)을 읽는다.

1. **허브 페이지** — `notion-create-pages` (아이콘 🎛️, 제목 `{{PROJECT}} PM 보드`, 본문 = schema.md §A 콜아웃). 반환 URL = `{{HUB_URL}}`.
2. **(선택) 마일스톤 DB** — 작업 보드보다 **먼저** 만든다(작업 보드의 `마일스톤` relation이 이 DS ID를 필요로 한다). `notion-create-database` parent=허브. 반환 `collection://…` = `{{MILESTONE_DS}}`. 이어서 `notion-create-pages`로 코드별 행 생성 → **행 page ID를 전부 받아 둔다**(프로토콜 문서의 정적 표에 박는다 — 세션이 마일스톤 DB를 조회하지 않고도 relation 값을 쓰기 위해).
3. **작업 보드 DB** — `notion-create-database` parent=허브, schema.md §B DDL(선택 열은 답에 맞춰 넣고 빼기). 반환 `collection://…` = `{{TASKS_DS}}`, DB URL = `{{TASKS_DB_URL}}`.
4. **자기 참조 듀얼 릴레이션** — `notion-update-data-source`로 **`ADD COLUMN "선행작업" RELATION('{{TASKS_DS}}', DUAL '후행작업')` 한 문장만** 실행한다. 🪤 `선행작업`·`후행작업`을 각각 ADD하면 **독립 단방향 2개**가 생겨 동기화가 안 된다(원형 실사고 — 후행이 전부 null). 마일스톤을 쓰면 같은 호출에 `ADD COLUMN "마일스톤" RELATION('{{MILESTONE_DS}}', DUAL '작업')`.
5. **뷰 7개** — `notion-create-view` × 7 (schema.md §C). 기본 뷰는 DB 생성 시 자동 생성되므로 이름만 맞춘다.
6. **운영 규칙(사람용 요약) 페이지** — 허브 아래 `notion-create-pages`, 본문 = schema.md §D. 콜아웃에 "정본 = repo `{{PROTOCOL_DOC}}`, 충돌 시 repo가 이긴다"를 반드시 남긴다(이중 SSOT 방지).

7. **읽는 문서 층** — `references/workspace-structure.md` 를 따라 만든다. 순서: 분야 페이지 4개(§B) → 결정 기록 DB(§D) → 문서 DB(§C) → 아이디어·논의 DB(§E) → **릴레이션 배선(§F, DB 3개가 다 생긴 뒤 한 번에)**.
   - 🪤 릴레이션 반대편을 직접 ADD 하지 말 것(DUAL 이 자동 생성). 두 번 부르면 중복쌍이 생긴다.
   - ⚠️ DUAL 이름(`관련결정`·`관련문서`·`관련아이디어`)이 **작업 보드에 새 열로 생긴다** — (2) 조회 SELECT 에는 넣지 않는다(세션이 안 쓰는 열).
   - **AI 분야 페이지는 형식이 다르다**(§B-1) — 첫 화면만 사람용, 하위 「온보딩·착수 절차」·「함정 사전」·「보드 스냅샷 설치」는 AI 가 읽는 절차서다.

**되읽기 검증(필수)** — `notion-fetch {{TASKS_DB_URL}}`로 스키마를 다시 받아 ① 열 개수·select 옵션 철자(`미듐` — 듐/듄 오타 실사고) ② `후행작업`이 **자동 생성**됐고 `선행작업`과 같은 relation 쌍인지 ③ 뷰 7개 이름을 확인한다. 이어 테스트 행 2개를 만들고 A의 `선행작업`=B로 두 뒤 B를 fetch해 `후행작업`에 A가 자동으로 들어왔는지 본다. 확인 후 테스트 행은 **사용자에게 알리고** 남겨 두거나 상태=폐기로 둔다(삭제는 사용자 몫).

## 3. 리포 파일 생성

`templates/`를 읽어 플레이스홀더를 치환하고 조건 블록을 정리해 쓴다. 치환은 수동이다(스크립트 없음) — 파일마다 아래 표를 끝까지 적용했는지 `grep "{{"`로 확인한다.

| 플레이스홀더 | 값 |
|---|---|
| `{{PROJECT}}` | 프로젝트 이름 |
| `{{HUB_URL}}` `{{TASKS_DB_URL}}` `{{TASKS_DS}}` | §2에서 받은 값 |
| `{{MILESTONE_DB_URL}}` `{{MILESTONE_DS}}` `{{MILESTONE_TABLE}}` | 마일스톤 사용 시. `MILESTONE_TABLE` = 코드↔page ID 표(markdown) |
| `{{GATE_COL}}` `{{GATE_OPT_HEAVY}}` `{{GATE_OPT_LIGHT}}` | 게이트 갈래 열 사용 시 |
| `{{PROTOCOL_DOC}}` | 예 `Docs/PMBoard.md` |
| `{{SCOPE_RULE}}` | 커밋 scope 관례 한 줄 (없으면 `관례 없음 — D2/D4 수동`) |
| `{{DATE}}` | 오늘 (YYYY-MM-DD) |
| `{{TASKS_DS_ID}}` | `{{TASKS_DS}}` 에서 `collection://` 접두어를 뗀 **UUID만**. `board-snapshot.ps1` 이 REST 엔드포인트에 쓴다 |

**조건 블록** — 템플릿 안의 `<!-- IF:MILESTONE -->…<!-- /IF:MILESTONE -->`, `<!-- IF:GATE -->…<!-- /IF:GATE -->` 는 해당 옵션을 켰으면 마커만 지우고, 껐으면 **블록 전체를 지운다**. 남은 마커가 없는지 `grep "IF:"`.

생성 대상과 경로:

| 템플릿 | 대상 | 비고 |
|---|---|---|
| `PMBoard.md` | `{{PROTOCOL_DOC}}` | 프로토콜 정본. 에이전트·커맨드는 이 문서를 읽고 따른다(절차 복제 금지) |
| `pm-board.agent.md` | `.claude/agents/pm-board.md` | ⚠️ 새 에이전트 정의는 **세션 시작 때만 로드** — 이 세션에선 `Agent type not found`가 정상 |
| `board.command.md` | `.claude/commands/board.md` | |
| `pm-board-guard.ps1` | `Scripts/pm-board-guard.ps1` | 내용 무치환(범용). `Scripts/`가 없으면 프로젝트 관례 폴더로 옮기고 settings 경로도 맞춘다. **UTF-8 BOM을 유지해 복사**(바이트 복사) — PowerShell 5.1은 BOM 없는 스크립트를 ANSI로 읽어 거부 사유의 한글이 깨진다 |
| `session-resume.ps1` | `Scripts/session-resume.ps1` | 리포에 이미 SessionStart 훅이 있으면 그 스크립트에 PROGRESS 출력 3줄만 합친다 |
| `board-snapshot.ps1` | `Scripts/board-snapshot.ps1` | **UTF-8 BOM 유지 복사.** 토큰이 없으면 안내 한 줄 + `exit 0` 이라 심어 두어도 무해하다 — 사용자가 나중에 토큰을 넣으면 그때부터 동작한다. 토큰 파일 경로를 `.gitignore` 에 **명시**할 것 |
| `mcp.json` | `.mcp.json` | 이미 있으면 `mcpServers.notion` 키만 병합 |
| `settings.hooks.json` | `.claude/settings.json` | **로컬 파일** — 기존 hooks에 병합. 배선 없으면 가드는 규칙일 뿐 강제가 아니다 |
| `PROGRESS.md` | `PROGRESS.md` | 기존 PROGRESS/TODO 문서가 있으면 내용을 보드로 옮긴 뒤 포인터로 교체할지 사용자에게 묻는다 |
| `CLAUDE.snippet.md` | `CLAUDE.md` 상단 | 기존 CLAUDE.md의 관련 문단(작업 추적·핸드오프)이 있으면 그 자리에 |

**.gitignore** — `.claude/*`를 무시하는 리포면 `!.claude/agents/`·`!.claude/commands/`를 추가해 에이전트·커맨드가 공유되게 한다(`settings.json`은 무시 유지 — 머신 로컬).

## 4. 설치 검증 + 인계

1. `grep -rn "{{\|IF:" <생성 파일들>` → 0건.
2. `.claude/settings.json`이 유효 JSON인지 파싱(PowerShell `ConvertFrom-Json`).
3. **읽는 문서 층 되읽기** — DB 3개를 `notion-fetch` 로 받아 ①`분야` 옵션 4개의 철자가 세 DB에서 동일한가(특히 `AI·워크플로` 의 가운뎃점 U+00B7) ②`문서상태`·`논의상태` 가 `상태` 로 잘못 만들어지지 않았나 ③릴레이션 반대편이 자동 생성됐나.
4. **스냅샷 스크립트 무토큰 드라이런** — 토큰 없이 실행해 `exit 0` + 안내 1줄 + 파일 미생성인지 확인. BOM 확인: `Get-Content Scripts/board-snapshot.ps1 -Encoding Byte -TotalCount 3` → `239 187 191`.
5. 가드 훅 드라이런: `echo '{"agent_type":"pm-board","tool_input":{"command":"update_properties"}}' | powershell -NoProfile -File Scripts/pm-board-guard.ps1` → `permissionDecision: deny` JSON. `insert_content`로 바꾸면 출력 없이 종료.
6. 사용자에게 보고할 것: 허브 URL · DB URL(4개) · 생성 파일 목록 · **다음 세션에서 `/board`로 첫 조회를 해 보라**(에이전트 로드 확인) · settings.json은 클론마다 수동 배선 · 기존 작업 목록을 보드로 옮기는 것은 `/board`가 아니라 사용자 또는 이관 세션이 `notion-create-pages`로 한다(`출처=기존이관`).
7. 커밋은 사용자가 요청할 때만. 커밋 시 `.mcp.json`은 시크릿이 없으므로 포함해도 된다(OAuth는 사용자 레벨).

## 5. 이미 설치된 보드의 수리/확장

- **열 추가** — `notion-update-data-source` `ADD COLUMN`. 릴레이션은 반드시 한쪽만 DUAL로. 열을 추가한 뒤 프로토콜 문서 → 에이전트 → 커맨드 **세 파일을 같은 커밋에서** 갱신한다(원형 실사고: 문서만 고치고 에이전트 체크리스트가 신설 규칙의 반대를 지시했다).
- **열 삭제** — `DROP COLUMN`은 **행의 값도 함께 죽인다**. 릴레이션을 재설정하면 기존 연결이 전부 사라지므로, 백업(전수 SQL 1회로 덤프)을 먼저 받고 사용자 승인 뒤에.
- **전수 백필** — (2)의 작업집합 조회를 기준으로 삼지 말 것. 목적에 맞게 `상태` 필터를 다시 짜고 **`has_more`가 `false`가 될 때까지 `LIMIT/OFFSET`으로 페이지네이션한다**(원형에서 `검증중` 12행을 놓칠 뻔했고, 나중엔 실제로 100행 잘림으로 진행중 행을 놓쳤다 — `references/notion-tool-traps.md`).
- **마일스톤 축 후속 도입** — §2-2 → §2-4의 `마일스톤` ADD → 활성 행만 백필(완료·폐기는 소급 배정 안 함) → 프로토콜 문서 (8)절 블록 복원.

## 원칙

- **절차 복제 금지** — 에이전트·커맨드는 `{{PROTOCOL_DOC}}`을 읽고 따르며, 규칙 원문은 그 문서 한 곳에만 산다. 이 스킬의 템플릿도 설치 후에는 정본이 아니다.
- **에이전트는 추가만** — 신규 행·로그 append. 기존 행의 속성 변경은 메인 세션. 훅이 이걸 강제하고, 훅이 없으면 규칙일 뿐이다 — 설치 보고에 반드시 적는다.
- **한글 select 값은 리터럴로** 쓴다. 유니코드 이스케이프로 쓰다 두 번 틀렸다.
- **SQL 조회는 무료 플랜 한도가 있다**(세션당 1~2회)이고 **100행에서 조용히 잘린다**. 조회는 `references/schema.md` §F 의 셋(작업집합·표적·대기열)만 쓰고, **`has_more`를 반드시 확인한다.** 판정 기준 = *조회 비용은 활성 행 수와 무관해야 한다.*
- 상시 폴링(크론 감시)은 원형에서 **미채택 확정**이다. 세션 시작 1회 + 커밋 전 1회 무조건 부르므로 폴링이 더해 주는 것이 없다. 제안하지 말 것.
