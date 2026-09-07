# 작업 보드 스키마 — 검증된 DDL · 뷰 · 페이지 본문

원형(FPSRoguelite, 2026-08-07~08-11 실측)을 그대로 옮긴 것이다. 열 이름·옵션은 한글 리터럴이며, 바꾸려면 프로토콜 문서·에이전트·커맨드의 SQL과 함께 바꿔야 한다(세 파일 동시 갱신).

## A. 허브 페이지

- 제목 `{{PROJECT}} PM 보드`, 아이콘 🎛️.
- 본문(콜아웃 📌 blue):
  > **{{PROJECT}} 프로젝트의 작업 조율 허브.** 모든 세션·클론의 작업 현황·우선순위·선행관계는 아래 **작업 보드**가 단일 진실이다.
  > 에이전트 프로토콜 원문 = repo `{{PROTOCOL_DOC}}` · 사람용 요약 = 하위 "운영 규칙" 페이지
- 하위: (마일스톤 DB) → 작업 보드 DB → 운영 규칙 페이지. DB는 `notion-create-database` parent=허브로 만들면 자동으로 하위에 걸린다.

## B. 작업 보드 DB — `notion-create-database`

`parent: {page_id: <허브>}`, `title: "작업 보드"`.

### B-1. 필수 열 (항상)

```sql
CREATE TABLE (
  "작업명"   TITLE,
  "상태"     SELECT('대기':gray, '진행중':blue, '검증중':yellow, '결정대기':orange, '차단':red, '보류':brown, '완료':green, '폐기':default),
  "우선순위" SELECT('크리티컬':red, '하이':orange, '미듐':yellow, '로우':blue, '백로그':gray),
  "영역"     MULTI_SELECT(<프로젝트별 옵션 — §B-3>),
  "담당"     RICH_TEXT COMMENT '<클론>@<브랜치> · MMDD-키워드. 비어 있으면 미클레임',
  "브랜치"   RICH_TEXT,
  "주요경로" RICH_TEXT COMMENT '주요 파일/모듈 — 병렬 충돌 감지용',
  "완료커밋" RICH_TEXT,
  "출처"     SELECT('사용자지시':red, '에이전트발견':blue, '기존이관':gray),
  "크기"     SELECT('S':gray, 'M':blue, 'L':orange, 'XL':red) COMMENT '상대크기(달력 기간 아님). S=1세션 / M=2~3세션 / L=4세션 이상 / XL=쪼개야 함'
)
```

### B-2. 선택 열

- **게이트 갈래**(원형 `추천모델`): `"{{GATE_COL}}" SELECT('{{GATE_OPT_HEAVY}}':purple, '{{GATE_OPT_LIGHT}}':blue)` — "이 행이 무거운 리뷰 게이트를 타는가". 판정 기준은 프로토콜 문서가 아니라 프로젝트의 모델/리뷰 정책 문서가 SSOT여야 하고, 칸과 어긋나면 그 문서가 옳다(이중 SSOT 금지).
- **마일스톤 relation** — DDL에 넣지 말고 §B-4에서 `ADD COLUMN`으로 (마일스톤 DS ID가 먼저 있어야 한다).

### B-3. `영역` 옵션 — 원형 예시 (게임 프로젝트)

`'캐릭터·1인칭팔':purple, '무기':orange, '적·스웜':red, '카드·런플로우':pink, 'UI·HUD':blue, '네트워크':green, '성능':yellow, '애니메이션':purple, '에디터툴':brown, '빌드·인프라':gray, '콘텐츠·에셋':orange, '문서':default, '기획':pink, 'PM·워크플로':blue`

일반 소프트웨어 프로젝트라면 모듈 경계로: 예 `'API':blue, 'DB·스키마':green, '프론트':pink, '인증':red, '인프라·CI':gray, '문서':default, 'PM·워크플로':blue`. **`PM·워크플로`·`문서`는 항상 넣는다**(보드 자체·프로토콜 개정 작업이 여기 들어간다).

### B-4. 릴레이션 — `notion-update-data-source` (DB 생성 직후, 문장 수 최소)

```sql
ADD COLUMN "선행작업" RELATION('<TASKS_DS uuid>', DUAL '후행작업')
```

마일스톤 사용 시 같은 호출에 `;`로 이어서:

```sql
; ADD COLUMN "마일스톤" RELATION('<MILESTONE_DS uuid>', DUAL '작업')
```

🪤 **`후행작업`·`작업`을 직접 ADD하지 말 것.** DUAL이 반대편을 자동 생성한다. 각각 ADD하면 서로 모르는 단방향 2개가 생기고 겉으로는 똑같이 보인다(원형 실사고 — 후행 전부 null). 되읽기에서 두 열의 `propertyUrl`이 같은 relation 쌍인지 확인한다.

🪤 `DROP COLUMN`은 행의 relation 값을 함께 지운다. 재설정하면 전부 다시 연결해야 한다.

### B-5. 되읽기 결과 기대치 (`notion-fetch <DB URL>`)

- SQLite 정의에 `"상태" TEXT, -- one of ["대기","진행중","검증중","결정대기","차단","보류","완료","폐기"]` 처럼 옵션이 **철자 그대로** 보인다. `미듐`을 확인한다.
- `"선행작업"`·`"후행작업"` 둘 다 `JSON array of page URLs relating to {{collection://<TASKS_DS>}}`.
- 마일스톤 사용 시 `"마일스톤"`은 마일스톤 DS를 가리키고, 마일스톤 DB 쪽에 `"작업"`이 자동으로 생겨 있다.

## C. 뷰 7개 — `notion-create-view` (`database_id`=작업 보드, `data_source_id`=TASKS_DS)

DSL 세부(구분자·인용)는 `notion-fetch "notion://docs/view-dsl-spec"`로 먼저 확인한다. 아래는 원형 뷰의 **의도**이며 directive 조합으로 옮긴다.

| # | 이름 | type | 필터 | 정렬/그룹 | SHOW |
|---|---|---|---|---|---|
| 1 | `Default view` (자동 생성, 이름 유지) | table | `상태` ∈ 진행중·검증중·결정대기·보류·대기 | `상태` DESC | 작업명·상태·영역·우선순위·선행작업·크기·후행작업·(마일스톤)·담당·브랜치·완료커밋·주요경로·출처 |
| 2 | `상태 보드` | board | — | GROUP BY `상태` | 작업명 |
| 3 | `🔵 지금 병렬 진행중` | table | `상태` = 진행중 | `우선순위` ASC | 작업명·우선순위·담당·브랜치·영역·주요경로 |
| 4 | `🟠 결정대기 (PM 인박스)` | table | `상태` = 결정대기 | `우선순위` ASC | 작업명·우선순위·영역·주요경로 |
| 5 | `⬜ 대기열 (우선순위순)` | table | `상태` = 대기 | `우선순위` ASC | 작업명·우선순위·영역·선행작업·주요경로 |
| 6 | `⚪ 백로그` | table | `우선순위` = 백로그 | — | 작업명·상태·영역·주요경로 |
| 7 | `✅ 완료 아카이브` | table | `상태` = 완료 | `우선순위` ASC | 작업명·우선순위·담당·완료커밋 |

예 (#3): `configure: FILTER "상태" = "진행중"  SORT BY "우선순위" ASC  SHOW "작업명", "우선순위", "담당", "브랜치", "영역", "주요경로"` — directive 사이 구분은 spec을 따른다.

원형의 Default view는 마일스톤 relation으로 현행 슬라이스(M0·M1)만 보이게 추가 필터가 걸려 있었다 — 마일스톤을 쓰면 `FILTER "마일스톤" = <현행 마일스톤 행 URL>`을 얹는다(relation 필터는 이름이 아니라 **페이지 URL/UUID**).

## D. 운영 규칙 (사람용 요약) 페이지 — 허브 하위, 아이콘 📖

콜아웃(⚠️ yellow): **규칙 원문(SSOT) = repo `{{PROTOCOL_DOC}}`.** 이 페이지는 사람이 빠르게 보는 요약이다. 둘이 충돌하면 repo가 이긴다.

본문 골격(원형 그대로, 프로젝트 표현만 바꿈):

- **핵심 규칙(하드 게이트)** — ① 모든 세션은 작업 전 클레임·작업 후 갱신, Notion MCP 미연결 세션은 착수 금지 ② 유일한 예외 = 사용자의 명시적 "보드 없이 진행" ③ 읽기 전용·질답·한 줄 수정은 등록 대상이 아님.
- **우선순위 5단계 표** — 크리티컬(사용자 전용) / 하이 / 미듐 / 로우 / 백로그.
- **상태** — 대기 · 진행중(담당 필수) · 검증중(구현 끝, **사람의 직접 확인** 대기) · 결정대기(PM 인박스) · 차단 · 보류 · 완료(커밋 해시) · 폐기(사유 본문).
- **에이전트가 하는 일** — 작업 전 7단계 / 작업 후 5단계 요약.
- **PM(사용자)이 보는 곳** — 뷰 3~7 이름 나열.

## E. 마일스톤 DB (선택) — `notion-create-database` parent=허브, title `마일스톤`

```sql
CREATE TABLE (
  "마일스톤" TITLE,
  "코드"     SELECT('M0':gray, 'M1':blue, 'M2':purple, 'M3':green, 'M4':yellow) COMMENT '체인 순서',
  "상태"     SELECT('미착수':gray, '진행중':blue, '완료':green),
  "요약"     RICH_TEXT COMMENT '한 줄 라벨. Exit Criteria 아님 — 정본은 repo 로드맵 문서'
)
```

출시형 프로젝트 전용 추가 열(원형): `"성격" SELECT('기반':gray, '수직슬라이스':blue, '출시':orange)`, `"동결선" SELECT('없음':gray, 'EA 동결':orange, '1.0 동결':red)`.

- `작업` relation은 만들지 않는다 — 작업 보드 쪽 `마일스톤` DUAL이 자동 생성한다.
- 코드별 행을 `notion-create-pages`로 만들고 **page ID를 전부 기록** → 프로토콜 문서 (8)절 정적 표. 이 표가 있어야 세션이 마일스톤 DB를 열지 않고 relation 값을 쓸 수 있다(무료 플랜 SQL 예산 보호).
- Exit Criteria는 Notion에 복제하지 않는다 — 정의는 repo 로드맵, Notion은 상태·연결만(이중 SSOT 금지).

## F. 조회 레시피 (프로토콜 문서에도 실린다)

- **전수(세션 시작 1회)**: `notion-query-data-sources`
  `SELECT "작업명","상태","우선순위","크기","담당","브랜치","선행작업","주요경로",url FROM "collection://<TASKS_DS>" WHERE "상태" IN ('진행중','대기','차단','결정대기')`
  (마일스톤·게이트 열을 켰으면 SELECT에 추가 — 같은 1회라 비용 증가 0.) ⚠️ 무료 플랜 호출 한도 — 세션당 1~2회.
- **표적(무제한)**: `notion-search` + `data_source_url`.
- **행 본문(무제한)**: `notion-fetch <행 URL>`. DB URL을 fetch하면 스키마·뷰만 나오고 행은 안 나온다.
- **쓰기(무제한)**: 신규 행 `notion-create-pages`(parent=`data_source_id`) / 속성 `notion-update-page` `update_properties` / 로그 `notion-update-page` `insert_content`(append).
- relation 값 = 상대 행 **page ID 배열**. `선행작업`만 쓰면 `후행작업`은 자동.
