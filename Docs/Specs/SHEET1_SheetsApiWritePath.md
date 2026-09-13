# SHEET1 — 저작 시트 정본 복귀 + Sheets API(서비스 계정) 행 단위 쓰기 경로

> 코어 갈래 명세(§6-5-2). 이 유닛은 **게임 런타임 코드가 아니라 저작 툴체인**(Python/PowerShell)이다 —
> 템플릿의 C++·복제 칸은 툴체인에 맞게 옮겨 적었고, 해당 없는 칸은 이유를 적었다.

---

## 1. 메타

| 항목 | 값 |
|---|---|
| 유닛 ID / 이름 | SHEET1 / 저작 시트 정본 복귀 + Sheets API 쓰기 경로 |
| 브랜치 | `main` (트렁크 기반, `Workflow.md` §6-7) |
| 작성 모델 | `claude-opus-5` — §6-5-2 개정(2026-08-26): 설계 = Opus, 검증 = Fable G1·G2 |
| 작성일 / 최종 갱신 | 2026-09-13 / 2026-09-13 (G1 1회차 반려 반영 = 개정 1 · G1 2회차 통과 후 지적 반영 = 개정 2 · C2 착수 전~C3 대조 중 Opus 명확화·교정 20건 = 개정 3, 부록 I) |
| 상태 | `확정` — G1 2회차 통과. C2 구현 진행(2026-09-13) |
| 보드 행 | https://app.notion.com/p/3da3972ddd8881739717cd4236aa3d8c |
| 관련 SSOT | `Docs/SSOT/Localization.md` L-1·L-3·L-5 · `Docs/SSOT/CombatWeaponCard.md` §2-3-10 · `Docs/SSOT/Workflow.md` §6-5-2 |
| 관련 명세 | `Docs/Specs/LOC0_StringTablePipeline.md`(sync 원설계) · `Docs/Specs/CARDCSV_ImporterPipeline.md`(임포터) |
| 관련 메모리 | [[card-csv-asset-must-stay-in-sync]] · [[shared-worktree-branch-collision]] · [[ps51-hook-script-needs-bom]] |
| 사용자 결정 | ① 정본 = 구글 시트(2026-09-13) ② 쓰기 방법 = A안 Sheets API + 서비스 계정(2026-09-13) ③ 비용: 결제 계정 미연결(2026-09-13) ④ 키 파일은 바탕화면에 둔다(2026-09-13, "옮기기" 거절) |
| 게이트 이력 | **G1 1회차(2026-09-13) = 반려** — P1 1 · P2 5 · P3 6, **전부 수용**. 결론을 뒤집을 수 있는 주장 4건(P1 · P2-1 · P2-5 · P3-1)은 Opus 가 로컬 재현으로 확인(부록 F #12~#15). 처리 = 부록 G<br>**G1 2회차(2026-09-13) = 통과** — P1 0 · P2 2 · P3 9(추측 지점 6 포함). 전부 수용·반영, 구조 불변이라 재제출 불요(Fable 판정). 처리 = 부록 H |

## 2. 목표 / 비목표

**목표**
1. 자동화(Claude)가 서비스 계정 자격으로 저작 시트 4종(`Cards`·`CardCatalog`·`ST_UI`·`ST_CardEffect`)의 행을 **추가·수정·삭제**한다. 입력은 **기존 변경셋 JSON 형식 그대로**, 대상만 리포 CSV → 시트로 바뀐다.
2. 쓰기가 사람·다른 세션의 편집을 **조용히** 덮지 않는다 — 자동화 간 **잠금 파일** · 변경셋의 선택적 `expect` · **쓰기 직전 재조회** · 시트당 **1회 원자적** `spreadsheets.batchUpdate` · 쓰기 후 **되읽기 검증**(키 기준 차이 보고).
3. 쓴 값은 **문자열로** 시트에 들어간다(자동 변환 없음 — §12 #5 가 판정).
4. 쓰기가 끝나면 리포 스냅샷(`Content/**/*.csv` + manifest)이 **기존 정식 pull 경로**(`Scripts/sync-authoring-csv.ps1`)로 자동 갱신되고, 그 manifest 가 Python 이 읽는 manifest 와 **같은 파일**임을 런타임에 확인한다.
5. 1회 이관 `seed` — 시트가 **마지막 pull 이후 내용 무편집일 때만**(또는 완전히 빈 새 시트일 때만) 리포 CSV 로 시트를 통째 교체한다.
6. `status` — 시트 export · manifest · 리포 CSV 3방향 대조. **자격 불요.**
7. `doctor` — 자격·접근·탭 수·헤더 + **대조군(API 서식값 == export)** 점검.
8. sync 의 로컬-앞섬 가드를 **fail-closed** 로 만든다(manifest 를 못 찾으면 덮지 않는다).
9. Apps Script(B안) 경로를 **완전히 제거**하고, 문서가 새 방향을 가리킨다.

**비목표(Non-goals)**
- ❌ **게임 시작 시 시트 런타임 읽기** — 사용자 결정 대기. `Localization.md` L-1 "런타임·gather·패키징은 `Content/` 물리 파일만 읽는다"와 충돌한다. 별도 행.
- ❌ 에디터 "시트 불러오기" 버튼 — 사용자 구조 작업.
- ❌ API 기반 pull 신설 — 정식 pull 은 export 경로 하나로 유지한다(스냅샷 직렬화가 둘로 갈리면 manifest 해시 가드가 거짓 판정을 낸다).
- ❌ 헤더·열 추가 마이그레이션 명령 — 사람이 시트 UI 에서 열을 삽입한다. **후속 단순확장**(`insertDimension` 1요청).
- ❌ 시트 비공개 전환 — pull 이 무인증 export 에 의존한다(공개 보기는 유지, **공개 편집은 금지** — §8).
- ❌ manifest 해시 의미 변경(원시 바이트 유지) — §11 R3.
- ❌ 숫자형·수식형 셀 쓰기 — 전부 문자열(§5-4, §11 R2).
- ❌ 자격 없는 `--dry-run`(export 로 대체 읽기) — 행동을 한 갈래로 유지한다.
- ❌ 사람 편집에 대한 잠금·행 밀림 완전 차단 — Sheets API 에 조건부 쓰기가 없다. 재조회·되읽기·문서 규칙으로 좁힌다(§11 R1).
- ❌ 키 파일 이동·환경변수 영구 설정 — 사용자 결정(키는 바탕화면). 도구는 `FPSR_SHEETS_SA_KEY` 를 읽기만 한다.
- ❌ 시트 데이터 정리(예: `DA_CardModifiers_BonusShot`.E1_Attr 끝 공백) — 사용자 확인 사항(U4)으로만 보고.
- ❌ 시트→리포 자동 폴링, C++ 임포터·CSV 스키마·`Config/AuthoringSheets.json` 항목 필드 변경.
- ❌ 과거 명세(`CRIT1`·`CRIT2`·`VIT1` 등)의 옛 절차 서술 수정 — 기록물이다.

## 3. 제1원리 3줄 (핵심원칙 4)

1. **제1원리 근거** — 게임 런타임에 닿지 않는 툴체인이다. 근거는 **콘텐츠 = 데이터(핵심원칙 2)** · **카드 물량 30→70 자동화 병목(`Roadmap.md` §7-6)** · **빌드 재현성(L-1: 런타임·패키징은 `Content/` 물리 파일만)**. 시트가 정본이면 사람 편집이 쉽고, git 스냅샷 커밋이 재현성을 지킨다. 2026-09-05 역전의 **유일한 사유가 "자동화가 시트에 못 쓴다"** 였으므로, 쓰기 경로만 확보하면 원설계(L-1)로 복귀한다.
2. **엔진 기본값·기존 인프라와의 관계** — 엔진 무관. **그대로 쓰는 것** = 변경셋 형식·병합 의미(`authoring_sheet.py apply`) · 매핑(`Config/AuthoringSheets.json`) · 정식 pull·manifest(`sync-authoring-csv.ps1`). **덮는 것** = `apply` 의 대상(CSV → 시트) · `push`(Apps Script) 폐기 · sync 가드의 manifest 부재 처리(fail-open → fail-closed, G1 P1-1). 외부 표준 = Google Sheets API v4 `spreadsheets.batchUpdate` — 공식: *"If any request is not valid then the entire request will fail and nothing will be applied"*, *"Requests will be applied in the order they are specified"* · `google-auth` 서비스 계정.
3. **프로젝트 제약과의 정합** — 워크트리를 여러 세션이 공유하고([[shared-worktree-branch-collision]]) 사람도 같은 시트를 편집하며, **GitHub 리포가 공개**라 시트 ID 도 공개다 → **조용한 손실·외부 변조 방지가 1순위**(잠금 · expect · 재조회 · 원자성 · 되읽기 · fail-closed 가드 · seed 가드 · 공개 링크 뷰어 전용). 자격증명은 리포 밖(전역 금칙). 비용 0(표준 사용 무료, 결제 계정 미연결). 테이블 추가(L-3) = 매핑 1항목 + 시트를 서비스 계정에 편집자로 공유.

## 4. 파일 목록

| 경로 | 구분 | 한 줄 설명 |
|---|---|---|
| `Scripts/sheets_api.py` | 신규 | 서비스 계정 자격 해석 + Sheets REST 얇은 클라이언트(읽기 재시도 / 쓰기는 429 만 재시도 / 예외 계약 §5-1) |
| `Scripts/authoring_sheet.py` | 수정 | `apply` = 시트 대상 · `seed`·`doctor` 신설 · `status` 3방향 · 잠금 · `push` 삭제 · 전역 `--mapping` |
| `Scripts/sync-authoring-csv.ps1` | 수정 | `-MappingPath` · manifest 경로 파생(§5-5 식) · `[manifest]` 출력 · **가드 fail-closed(유일한 가드 변경)** · 절대 target 허용 · 방향 문구. **UTF-8 BOM 유지** |
| `Scripts/requirements-sheets.txt` | 신규 | `google-auth==2.58.0` · `requests==2.34.2` (2026-09-13 이 머신에 설치·동작 확인 — 부록 F #16) |
| `Scripts/tests/test_authoring_sheet.py` | 신규 | 오프라인 단위테스트(stdlib `unittest`, 네트워크 불요, google-auth 유무와 무관하게 통과) |
| `Scripts/AppsScript/AuthoringSheetWriter.gs` | **삭제** | B안 폐기 (`Scripts/AppsScript/` 폴더가 비면 폴더째) |
| `Config/AuthoringSheets.writeback.example.json` | **삭제** | B안 폐기 |
| `.gitignore` | 수정 | writeback 절(53~56행) → 서비스 계정 키 방어 패턴(부록 D) |
| `Docs/AuthoringSheetWriteback.md` | 전면 개정 | 부록 A 구조대로 |
| `Docs/SSOT/Localization.md` | 수정 | L-1·L-3·L-5 (부록 B) |
| `Docs/SSOT/CombatWeaponCard.md` | 수정 | §2-3-10 진실 사슬 131행 (부록 C) |
| `Content/Authoring/{Cards,CardCatalog}.csv` · `Content/StringTables/{ST_UI,ST_CardEffect}.csv` · `Config/AuthoringSheets.manifest.json` | 수정(이관 산출물) | **C3 이관 단계에서만**, 사용자 승인 후 `seed` → pull 결과. C2 구현 커밋에는 넣지 않는다 |

## 5. 인터페이스 선언

> C2 착수 전~C3 대조 중 명확화·교정 20건 = **부록 I** — 이 절과 함께 읽는다(본문과 어긋나 보이면 부록 I 가 뒤에 쓴 결정이다).

### 5-1. `Scripts/sheets_api.py`

모듈 최상단에서 `google.*`·`requests` 를 **import 하지 않는다**.

```python
SCOPES = ("https://www.googleapis.com/auth/spreadsheets",)
API_ROOT = "https://sheets.googleapis.com/v4/spreadsheets"
KEY_ENV = "FPSR_SHEETS_SA_KEY"
DEFAULT_KEY_PATH = os.path.join(os.path.expanduser("~"), ".fpsr", "sheets-service-account.json")
REQUEST_TIMEOUT_SEC = 60
RETRY_DELAYS_SEC = (1, 2, 4, 8, 16)          # 구조 상수 — API 정책 연동값, 콘텐츠 조정값 아님
RETRYABLE_READ_STATUS = (429, 500, 502, 503, 504)
SETUP_DOC = "Docs/AuthoringSheetWriteback.md"


class SheetsSetupError(Exception):
    """자격 파일 없음 / 리포 안에 있음 / 라이브러리 미설치 / 자격 갱신 실패. str() 가 사람이 할 일을 담는다."""


class SheetsApiError(Exception):
    """HTTP 비-2xx 또는 전송 실패. 필드: status(int — 전송 실패는 0), reason(str — 응답 JSON error.message,
    없으면 본문 앞 300자, 전송 실패는 예외 문자열), method(str), url(str)."""
    def __init__(self, status: int, reason: str, method: str, url: str): ...


class SheetsWriteOutcomeUnknown(SheetsApiError):
    """batchUpdate 가 5xx·전송 실패로 끝났다 — 적용됐는지 알 수 없다."""


def is_inside(path: str, root: str) -> bool:
    """realpath·normcase 후 판정. os.path.splitdrive 의 드라이브가 다르면 False(= 밖).
    같으면 os.path.commonpath([p, r]) == r. (commonpath 는 드라이브가 다르면 ValueError — G1 P2-1, 부록 F #13)"""


def resolve_key_path(repo_root: str) -> str:
    """env KEY_ENV 가 비어 있지 않으면 그 경로, 아니면 DEFAULT_KEY_PATH.
    - is_inside(path, repo_root) 면 SheetsSetupError("키 파일이 리포 안에 있다 — 리포 밖으로 옮겨라(커밋 사고 방지): <경로>")
    - 파일이 없으면 SheetsSetupError("서비스 계정 키가 없다: <경로>. 키를 이 경로에 두거나 환경변수 FPSR_SHEETS_SA_KEY 에 전체 경로를 넣어라. 1회 설정 = SETUP_DOC §1")
    - 파일 내용은 읽지 않는다."""


class SheetsClient:
    def __init__(self, key_path: str, *, _session=None, _sleep=time.sleep, _auth_error_types=None):
        """_session 이 None 이면 여기서 지연 import:
             from google.oauth2 import service_account
             from google.auth.transport.requests import AuthorizedSession
             from google.auth.exceptions import GoogleAuthError
           ImportError → SheetsSetupError("python -m pip install -r Scripts/requirements-sheets.txt").
           credentials = service_account.Credentials.from_service_account_file(key_path, scopes=list(SCOPES))
           (ValueError·KeyError·OSError(권한·잠김) → SheetsSetupError("키 파일을 읽을 수 없다 — 서비스 계정 JSON 키인지·읽기 권한이 있는지 확인: <예외>"))
           self._session = AuthorizedSession(credentials); self._email = credentials.service_account_email
           self._auth_error_types = (GoogleAuthError,)
         _session 이 주어지면(테스트): 그대로 쓰고 _email = "test@example.invalid",
           self._auth_error_types = _auth_error_types if _auth_error_types is not None else ().
         _sleep 은 재시도 대기 주입점."""

    @property
    def service_account_email(self) -> str: ...

    def get_sheet_properties(self, spreadsheet_id: str) -> list[dict]:
        """GET {API_ROOT}/{spreadsheet_id}  params={"fields": "sheets.properties(sheetId,title,index,gridProperties(rowCount,columnCount))"}
        반환: [{"sheetId": int, "title": str, "index": int, "rowCount": int, "columnCount": int}, ...] (index 오름차순)."""

    def get_values(self, spreadsheet_id: str, a1_range: str) -> list[list[str]]:
        """GET {API_ROOT}/{spreadsheet_id}/values/{urllib.parse.quote(a1_range, safe='')}
               params={"valueRenderOption": "FORMATTED_VALUE", "majorDimension": "ROWS"}
        반환: 응답 "values"(없으면 []). 모든 셀을 str(v) 로 정규화."""

    def batch_update(self, spreadsheet_id: str, requests: list[dict]) -> dict:
        """POST {API_ROOT}/{spreadsheet_id}:batchUpdate  json={"requests": requests}. 응답 JSON 반환."""
```

**세션 계약** — 모든 HTTP 는 `self._session.request(method, url, params=..., json=..., timeout=REQUEST_TIMEOUT_SEC)` 한 곳으로 나간다. 반환 객체는 `status_code: int` · `text: str` · `json() -> dict` 만 쓴다(`AuthorizedSession` 은 `requests.Session` 이라 성립, 테스트 가짜 세션도 이 셋만 구현).

**예외 계약(고정)** — 요청 1회를 감싸는 순서:
1. `except self._auth_error_types as e` → `SheetsSetupError("자격 갱신 실패 — 네트워크 연결 · 키 삭제/무효 · PC 시계를 확인: <e>")`(부록 I-10). **토큰 갱신은 요청 전에 일어나므로 미적용 확정** — 읽기·쓰기 모두 재시도하지 않는다. (`except ()` 는 아무것도 잡지 않는다 = 테스트 기본.)
2. `except OSError as e` → **전송 실패**(`requests.exceptions.RequestException` 은 `IOError` 파생, 내장 `ConnectionError`·`TimeoutError` 도 `OSError` 파생) → status=0.
3. 그 밖의 예외는 **잡지 않는다**(코드 버그를 "결과 불명"으로 위장하지 않는다).
4. **try 범위 = `self._session.request(...)` 호출 한 줄뿐**(G1 2회차 P3-G②). 응답 본문 파싱(`response.json()`)은 try **밖**에서 한다 — `requests.exceptions.JSONDecodeError` 는 `OSError` 파생이라(G1 2회차 실측 MRO) 같은 try 안에 두면 쓰기 성공이 "결과 불명"으로 위장된다. 파싱 실패(`ValueError`) 처리: 읽기 → `SheetsApiError(status=<응답 코드>, reason="응답 JSON 파싱 실패: <본문 앞 300자>")`(재시도 없음) / `batch_update` 가 2xx 인데 파싱 실패 → **이미 적용된 것**이므로 `{}` 반환.

**재시도 규칙(고정)**
| 호출 | 재시도 대상 | 재시도 금지 → 결과 |
|---|---|---|
| `get_sheet_properties`·`get_values` | `RETRYABLE_READ_STATUS` · 전송 실패 → `RETRY_DELAYS_SEC` 순서로 대기 후 최대 5회 | 소진 시 마지막 실패로 `SheetsApiError` |
| `batch_update` | **429 만**(쿼터 거부 = 미적용 확정) → 같은 대기열 | 5xx · 전송 실패 → **즉시** `SheetsWriteOutcomeUnknown`. 그 밖의 4xx → `SheetsApiError` |

> 왜 쓰기를 재시도하지 않는가: 응답만 실패하고 요청은 적용됐을 수 있다. 재시도하면 `appendCells` 가 **행을 중복 추가**하고 `deleteDimension` 이 **엉뚱한 행을 지운다**(인덱스가 이미 밀렸다).

### 5-2. `Scripts/authoring_sheet.py` — CLI

```
python Scripts/authoring_sheet.py [--mapping PATH] status
python Scripts/authoring_sheet.py [--mapping PATH] doctor
python Scripts/authoring_sheet.py [--mapping PATH] apply <changeset.json> [--dry-run]
python Scripts/authoring_sheet.py [--mapping PATH] seed --sheet <이름> [--confirm-replace] [--dry-run]
```

- `--mapping` 기본 = `<REPO_ROOT>/Config/AuthoringSheets.json`(절대경로로 정규화). manifest = `manifest_path_for(mapping)`(기본 = `Config/AuthoringSheets.manifest.json`).
- mapping 항목의 `target` 은 **절대경로면 그대로**, 상대면 `REPO_ROOT` 기준.
- `push` 서브커맨드·`WRITEBACK_PATH`·Apps Script 안내 문구는 **삭제**한다.
- `LOCK_PATH = os.path.join(os.path.expanduser("~"), ".fpsr", "authoring-sheet.lock")` — `apply`(dry-run 포함) · `seed`(dry-run 포함)가 **명령 진입 직후(매핑·manifest·리포 CSV 를 읽기 전)** 잡고 마지막 검증 뒤 `finally` 로 놓는다(G1 2회차 P3-A). 보유 중에는 **모든 네트워크 동작 직전**(SheetsClient 메서드 호출 · export GET · sync 서브프로세스 시작)에 `touch_lock` 으로 진행 신호를 갱신한다(P2-A). `status`·`doctor` 는 잡지 않는다. 같은 머신의 모든 클론·세션이 공유하고, `sync-authoring-csv.ps1` 도 이 잠금을 존중한다(§5-5).

**종료 코드(고정)**
| 코드 | 의미 |
|---|---|
| 0 | 성공(NO-OP·DRY-RUN 포함) |
| 1 | 검증·데이터·전제조건·잠금 오류 — **아무것도 쓰지 않고** 중단 |
| 2 | 설정 미비(`SheetsSetupError`) |
| 3 | 쓰기가 일어났거나 일어났을 수 있는데 그 뒤가 실패 — **사람 확인 필요**(결과 불명 · 재조회 불일치로 뒤 시트 중단 · 되읽기 실패/불일치 · export 수렴 실패 · sync 실패 · 사후 manifest 대조 실패) |

**쓰기 전 예외 총칙**(G1 2회차 P3-G①): 첫 `batch_update` 전에 난 `SheetsApiError`(403·404·재시도 소진 등) → 1 · `SheetsSetupError` → 2 · `LockBusy`·`PlanError` → 1.

### 5-3. `Scripts/authoring_sheet.py` — 순수 함수(단위테스트 대상, 네트워크 없음)

```python
class PlanError(Exception):
    """쓰기 전에 잡는 변경셋·시트 데이터 오류. 메시지에 시트·키·컬럼을 담는다."""

class LockBusy(Exception):
    """다른 프로세스가 잠금을 쥐고 있다. 메시지에 pid·host·command·started_utc."""

@dataclass
class RowUpdate:
    row_index: int            # 0-based 데이터 행 인덱스(헤더 제외). 그리드 행 = row_index + 1
    key: str
    before: list[str]         # header 폭으로 패딩
    after: list[str]
    changed_cols: list[int]   # 오름차순

@dataclass
class RowAppend:
    key: str
    after: list[str]          # header 폭

@dataclass
class RowDelete:
    row_index: int
    key: str
    before: list[str]

@dataclass
class Plan:
    sheet: str
    key_col: str
    updates: list[RowUpdate]
    appends: list[RowAppend]
    deletes: list[RowDelete]
    unchanged: int
    missing_deletes: list[str]
    def touched(self) -> int: ...   # len(updates) + len(appends) + len(deletes)

def manifest_path_for(mapping_path: str) -> str:            # os.path.splitext → root + ".manifest.json"
def resolve_target(repo_root: str, target: str) -> str:     # 절대면 그대로, 아니면 join
def export_url(sheet_id: str, gid) -> str:                  # ".../export?format=csv" (+ "&gid=<gid>" if gid)
def parse_csv_bytes(raw: bytes) -> list[list[str]]:         # utf-8 디코드 → csv.reader(io.StringIO(text, newline=""))
def normalize_rows(rows: list[list[str]]) -> list[list[str]]:  # 각 행 후행 "" 제거 → 후행 빈 행([]) 제거
def rows_equal(a: list[list[str]], b: list[list[str]]) -> bool: # normalize_rows(a) == normalize_rows(b)
def sha256_bytes(raw: bytes) -> str:                        # 대문자 hex
def classify_status(sheet_state: str, repo_state: str) -> str
def apply_precondition(sheet: str, manifest_sha, repo_sha) -> str | None   # None = 통과, 아니면 오류 문구
def seed_precondition(sheet: str, manifest_sha, export_sha, current_rows: list[list[str]], repo_csv_exists: bool) -> str | None
def validate_changeset(doc: dict, mapping: dict) -> list[dict]
def plan_changes(sheet: str, header: list[str], data_rows: list[list[str]], change: dict) -> Plan
def expected_after(header: list[str], data_rows: list[list[str]], plan: Plan) -> list[list[str]]
def keyed_diff(key_col: str, expected: list[list[str]], actual: list[list[str]]) -> list[str]
def encode_cell(value: str) -> dict
def build_apply_requests(sheet_gid: int, plan: Plan) -> list[dict]
def build_seed_requests(sheet_gid: int, grid_rows: int, grid_cols: int,
                        current_rows: list[list[str]], new_rows: list[list[str]]) -> list[dict]
def acquire_lock(lock_path: str, command: str, *, stale_after_sec: int = 900, _now=time.time) -> str   # 반환 = token
def touch_lock(lock_path: str) -> None                     # os.utime(lock_path, None) — 진행 신호
def release_lock(lock_path: str) -> None
```

**`classify_status(sheet_state, repo_state)`** — `sheet_state ∈ {SAME, CHANGED, FETCH-FAIL, NO-PROV}`, `repo_state ∈ {SAME, CHANGED, MISSING, NO-PROV}`:
| 조건(위에서부터 첫 일치) | 결과 |
|---|---|
| 어느 쪽이든 `NO-PROV` | `NO-PROV` |
| `repo_state == MISSING` | `MISSING` |
| `sheet_state == FETCH-FAIL` | `UNKNOWN` |
| SAME / SAME | `IN-SYNC` |
| CHANGED / SAME | `SHEET-AHEAD` |
| SAME / CHANGED | `LOCAL-AHEAD` |
| CHANGED / CHANGED | `DIVERGED` |

**`apply_precondition(sheet, manifest_sha, repo_sha)`** — `repo_sha is None` → `"[<sheet>] 리포 스냅샷 CSV 가 없다(MISSING) — 시트에 헤더를 넣고 sync 로 첫 스냅샷을 만든 뒤 apply"` / `manifest_sha is None` → `"[<sheet>] manifest 항목이 없다(NO-PROV) — sync 로 첫 스냅샷을 만든 뒤 apply"` / 둘이 다르면 `"[<sheet>] 리포 CSV 가 manifest 와 다르다(LOCAL-AHEAD) — 정본은 시트다. 리포 CSV 를 직접 고쳤다면 git checkout 으로 되돌리고 그 변경을 변경셋으로 옮겨라"` / 같으면 None.

**`seed_precondition(sheet, manifest_sha, export_sha, current_rows, repo_csv_exists)`** — `not repo_csv_exists` → `"[<sheet>] seed 원본(리포 CSV)이 없다"` / `manifest_sha is None`: `normalize_rows(current_rows) == []`(완전히 빈 새 시트)면 None, 아니면 `"[<sheet>] manifest 항목이 없는데 시트가 비어 있지 않다 — seed 가 사람 편집을 지울 수 있다. 먼저 sync 로 첫 스냅샷"` / `export_sha is None` → `"[<sheet>] export 를 받지 못했다 — seed 는 시트 무편집을 확인할 수 있을 때만 돈다"` / `export_sha != manifest_sha` → `"[<sheet>] 시트가 마지막 pull 이후 편집됐다 — seed 는 그 편집을 지운다. 먼저 sync 로 당겨 리포에 합친 뒤 다시"` / 그 외 None.

**`validate_changeset(doc, mapping)`** — 반환 = `doc["changes"]`.
- `"changes"` 키가 없거나 리스트가 비었으면 `PlanError("changes[] 가 없다 — asset_edit.py 형식(edits[])의 변경셋인가?")`.
- 항목마다 `"sheet"` 필수·매핑에 존재. **같은 `sheet` 항목이 2개 이상이면** `PlanError("[<sheet>] 한 변경셋에 같은 시트 항목이 둘 — 하나로 합칠 것")`.

**`plan_changes(sheet, header, data_rows, change)`** — 종전 `cmd_apply` 의 병합 의미를 그대로 옮기고 `expect` 를 더한다.
1. `key_col = change.get("key", header[0])`, 헤더에 없으면 `PlanError`.
2. `data_rows` 의 어떤 행이든 `len(row) > len(header)` 이고 초과분에 비어 있지 않은 셀이 있으면 `PlanError("[<sheet>] 헤더 밖 셀에 값이 있다: 데이터 행 <i+1>")`.
3. 비어 있지 않은 키가 2행 이상에 있으면 `PlanError("[<sheet>] 중복 키: <k1>, <k2> …")`(정렬).
4. `upsert` 레코드마다:
   - `expect` 를 뺀 컬럼 중 헤더에 없는 것 → `PlanError`. 키 값이 비었으면 `PlanError`.
   - `expect`(선택, `dict[str,str]`)의 컬럼이 헤더에 없으면 `PlanError`.
   - 키가 시트에 있으면: `expect` 의 모든 컬럼이 **현재 값(이 변경 안에서 앞 레코드가 이미 병합한 값 포함)** 과 같아야 한다. 아니면 `PlanError("[<sheet>] expect 불일치 <key>.<col>: 현재='<v>' 기대='<e>'")`. 같은 키가 다시 오면 같은 `RowUpdate` 에 누적 병합. 최종 `after == before`(원본 시트 값)면 `RowUpdate` 를 만들지 않고 unchanged 1.
   - 키가 시트에 없으면: `expect` 가 비어 있지 않으면 `PlanError("[<sheet>] expect 가 있는데 행이 없다: <key>")`. 아니면 `RowAppend(after=[record.get(c, "") for c in header])`. 같은 새 키가 다시 오면 그 `RowAppend.after` 에 병합.
5. `delete` 키마다: 같은 변경의 `upsert` 에도 있으면 `PlanError("[<sheet>] 같은 변경에서 upsert 와 delete 가 겹친다: <key>")`. 시트에 있으면 `RowDelete`, 없으면 `missing_deletes`.
6. `updates`·`deletes` 는 `row_index` 오름차순, `appends` 는 등장 순.

**`expected_after(header, data_rows, plan)`** — 데이터 행을 header 폭으로 패딩 → `updates` 의 `after` 로 교체 → `deletes` 의 인덱스 제거 → `appends.after` 를 끝에 이어붙임 → `[header] + 결과` 반환.

**`keyed_diff(key_col, expected, actual)`** — 두 표(헤더 포함)를 **각자의 헤더 컬럼 이름**으로 정렬해 키 기준으로 비교. 반환 줄: `"  ! 기대에만 있는 키: <k>"` · `"  ! 실제에만 있는 키: <k>"` · `"  ! <k>.<col>: 기대='<e>' 실제='<a>'"`(공통 컬럼만) · 헤더가 다르면 맨 앞에 `"  ! 헤더: 기대에만=<cols> 실제에만=<cols>"`. 키 순서는 기대 표 등장 순 → 실제에만 있는 키는 실제 표 등장 순. 한 표 안에서 같은 키가 2행 이상이면 그 표 이름으로 `"  ! 중복 키(기대): <k>"` 또는 `"  ! 중복 키(실제): <k>"` 를 내고(부록 I-16) **첫 등장 행**으로 비교한다. 키 기준으로 차이가 없으면 `[]`. (되읽기 불일치·seed 계획 출력에 쓴다 — 행이 밀려도 사람이 읽을 수 있게, G1 P2-3·P3-6.) **호출부 규칙**(G1 2회차 P3-C): 위치 비교(`rows_equal`)는 다른데 `keyed_diff` 가 `[]` 면 `"  (키 기준 내용 동일 — 행 순서·빈 행만 다름)"` 한 줄을 대신 출력한다.

**`acquire_lock(lock_path, command, stale_after_sec, _now)`** — `os.makedirs(dirname, exist_ok=True)` → `os.open(lock_path, O_CREAT|O_EXCL|O_WRONLY)` 성공 시 JSON `{"pid", "host"(socket.gethostname()), "command", "started_utc", "token"(uuid4 hex)}` 기록 후 token 반환. `FileExistsError` 면: **마지막 진행 신호**(파일 mtime)가 `_now() - stale_after_sec` 보다 오래됐으면 `"WARN 15분간 진행 신호가 없는 잠금을 치웠다: <내용>"` 출력 → 삭제(`FileNotFoundError` 는 무시 — 다른 프로세스가 먼저 치운 것) → **1회만** 다시 시도(또 실패하면 `LockBusy`). 신선하면 `LockBusy("다른 세션이 시트를 쓰는 중이다: <내용> — 끝난 뒤 다시")`. `touch_lock` = `os.utime(lock_path, None)`(파일이 없으면 `FileNotFoundError` 를 그대로 올린다 — 잠금을 잃은 것이다). `release_lock` = 파일이 있으면 삭제(없으면 무시).
> 왜 생성 시각이 아니라 진행 신호인가(G1 2회차 P2-A): 명세 상수로 최악 시간을 계산하면 읽기 1회가 재시도 포함 약 391초, 시트 1개 apply 가 40분을 넘을 수 있다 — 생성 시각 기준 15분이면 **살아 있는 잠금을 치운다.** 네트워크 동작 직전마다 갱신하면 갱신 간격의 최악 = 동작 1회(≈391초) < 900초.

### 5-4. 요청 형태 (고정)

**`encode_cell(value)`** — `""` → `{}`(필드 마스크 `userEnteredValue` 에 없으므로 칸이 비워진다) / 그 외 → `{"userEnteredValue": {"stringValue": value}}`.
숫자처럼 보여도(`"0.03"`·`"1.0"`) 수식처럼 보여도(`"=SUM(A1)"`) 앞뒤 공백이 있어도(`"   [READY]"`) **문자열로 보낸다**. 공식 문서가 확인해 주는 것은 `ExtendedValue` 가 값 종류별 필드를 따로 둔다는 것과 `stringValue` 가 사용자가 `'123` 을 쳤을 때 `123` 으로 표현되는 문자열 필드라는 것까지다 — **"stringValue 로 보낸 값이 변환 없이 저장되고 서식값·export 로 그대로 나온다"는 미검증**이며 §12 #5 스크래치 테스트가 판정한다. 변환이 관측되면 명세 갭으로 멈춘다.

**`build_apply_requests(sheet_gid, plan)`** — 순서 = ① 수정 ② 삭제 ③ 추가 (한 `batchUpdate` 안에서 순서대로 적용된다):
```python
# ① RowUpdate 마다, changed_cols 의 칸 하나당 1요청 (바뀐 칸만 쓴다 — 사람이 입력한 나머지 칸의 형식을 건드리지 않는다)
{"updateCells": {"range": {"sheetId": gid, "startRowIndex": r + 1, "endRowIndex": r + 2,
                           "startColumnIndex": c, "endColumnIndex": c + 1},
                 "rows": [{"values": [encode_cell(after[c])]}],
                 "fields": "userEnteredValue"}}
# ② RowDelete 를 row_index 내림차순으로 (뒤에서부터 지워야 앞 인덱스가 안 밀린다)
{"deleteDimension": {"range": {"sheetId": gid, "dimension": "ROWS",
                               "startIndex": r + 1, "endIndex": r + 2}}}
# ③ appends 가 있으면 1요청
{"appendCells": {"sheetId": gid,
                 "rows": [{"values": [encode_cell(v) for v in a.after]} for a in plan.appends],
                 "fields": "userEnteredValue"}}
```
> ① 은 삭제 전 인덱스를 쓰고 ② 는 ① 뒤에 적용되므로 인덱스가 어긋나지 않는다. `appendCells` 는 *"Adds new cells after the last row with data in a sheet, inserting new rows into the sheet if necessary"*(공식) — `expected_after` 의 "끝에 이어붙임"과 같다.

**`build_seed_requests(sheet_gid, grid_rows, grid_cols, current_rows, new_rows)`** — `current_rows`·`new_rows` 는 헤더 포함 전체 표.
```python
H = max(len(current_rows), len(new_rows))
W = max(max((len(r) for r in current_rows), default=0), max((len(r) for r in new_rows), default=0))
requests = []
if H > grid_rows: requests.append({"appendDimension": {"sheetId": gid, "dimension": "ROWS", "length": H - grid_rows}})
if W > grid_cols: requests.append({"appendDimension": {"sheetId": gid, "dimension": "COLUMNS", "length": W - grid_cols}})
# new_rows 를 H×W 로 패딩("")해 전 칸을 명시 — 옛 값이 남는 칸이 없게 한다
requests.append({"updateCells": {"range": {"sheetId": gid, "startRowIndex": 0, "endRowIndex": H,
                                           "startColumnIndex": 0, "endColumnIndex": W},
                                 "rows": [{"values": [encode_cell(v) for v in padded_row]} for padded_row in padded],
                                 "fields": "userEnteredValue"}})
```

### 5-5. `Scripts/sync-authoring-csv.ps1` 변경

```powershell
param(
    [string]$SheetName,
    [switch]$Force,
    # 매핑 파일 경로. 기본 = <리포>\Config\AuthoringSheets.json.
    # manifest = 같은 폴더의 "<매핑 파일명(확장자 제외)>.manifest.json" — authoring_sheet.py manifest_path_for 와 같은 규칙.
    [string]$MappingPath
)
```
- `$MappingPath` 미지정 → `Join-Path $RepoRoot 'Config\AuthoringSheets.json'`. 지정 → `$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($MappingPath)`(PS 현재 위치 기준 — `[IO.Path]::GetFullPath` 는 PS `Set-Location` 을 안 따라간다).
- **`$ManifestPath = Join-Path (Split-Path -Parent $MappingPath) ([System.IO.Path]::GetFileNameWithoutExtension($MappingPath) + '.manifest.json')`**. ⛔ `[IO.Path]::ChangeExtension($p, $null)` 금지 — PS 5.1 이 `$null` 을 빈 문자열로 넘겨 `AuthoringSheets..manifest.json`(점 두 개)이 된다(G1 P1-1, 부록 F #12 재현).
- 매핑 로드 직후 `Write-Host "[manifest] $ManifestPath"` 1줄.
- `$TargetPath` = `[System.IO.Path]::Combine($RepoRoot, $TargetRelative)` (절대 target 이면 그대로 — `Join-Path` 는 절대 둘째 인자를 이어붙여 깨진다).
- **가드 fail-closed — 이 명세가 가드에 가하는 유일한 변경**: 기존 가드 조건 `if (-not $Force -and (Test-Path $TargetPath) -and $ManifestEntries.ContainsKey($Name))` 앞에 다음을 둔다:
  ```powershell
  # target 은 있는데 이 시트의 manifest 기록을 못 찾으면(파일 없음·파싱 실패·항목 없음) 가드가 판정할 근거가 없다 → 덮지 않는다.
  # (종전엔 이 경우 가드를 통과해 덮어썼다 = fail-open. manifest 경로가 틀려도 조용히 리포 CSV 를 잃던 계급을 막는 유일한 지점. SHEET1 G1 P1-1)
  if (-not $Force -and (Test-Path $TargetPath) -and -not $ManifestEntries.ContainsKey($Name)) {
      Write-Warning "[$Name] manifest 기록이 없어 로컬 CSV 가 앞서 있는지 판정할 수 없습니다 — 덮어쓰기를 거부합니다(기존 파일 무접촉). manifest: $ManifestPath"
      Write-Warning "         처음 받는 테이블이면 target CSV 가 없어야 합니다. 로컬 CSV 를 버리고 시트로 덮으려면: -Force"
      $bHadFailure = $true
      continue
  }
  ```
  target 이 없는 새 테이블의 첫 sync 는 영향 없음. 기존 LOCAL-AHEAD 가드·저장 로직은 그대로.
- **manifest 쓰기 조건**(G1 2회차 P2-B — 가드 외 두 번째이자 마지막 동작 변경): manifest 파싱에 실패했거나(`$bManifestParseFailed`) 이번 실행에서 target 을 **하나도 저장하지 않았으면**(`$SavedCount -eq 0`) manifest 파일을 **쓰지 않는다** — `Write-Host "[manifest] 기록 안 함(파싱 실패 또는 저장 0건) — $ManifestPath"`. (종전엔 끝에서 무조건 다시 써서, 파싱 실패 시 `[]` 로 provenance 전부와 머지 충돌 마커를 지웠다.)
- **잠금 존중**(G1 2회차 P3-B): 매핑 로드 직후, `Join-Path $env:USERPROFILE '.fpsr\authoring-sheet.lock'` 이 있고 mtime 이 900초 이내이며 그 JSON 의 `token` 이 환경변수 `FPSR_AUTHORING_LOCK_TOKEN` 과 다르면 `Write-Warning "authoring_sheet.py 가 시트를 쓰는 중입니다(<command>, pid <pid>) — 끝난 뒤 다시 실행하세요"` 후 **종료 1**(아무것도 안 씀). 900초보다 오래됐으면 경고만 하고 진행. 잠금 JSON 파싱 실패는 신선한 잠금으로 본다(거부). apply·seed 는 서브프로세스 환경에 자기 token 을 넣어 이 검사를 통과한다. `-Force` 는 이 검사를 우회하지 않는다(막힌 잠금은 사람이 파일을 지운다).
- `.DESCRIPTION`·경고 문구 = 부록 E.
- **UTF-8 BOM 유지**([[ps51-hook-script-needs-bom]]).

## 6. 함수별 계약 (명령 단위)

| 명령 | 자격 | 잠금 | 전제조건 | 동작 | 실패 시 |
|---|---|---|---|---|---|
| `status` | 불요 | 안 잡음 | 매핑 로드 | 시트마다 export GET(30s) → sha / manifest sha / 리포 CSV sha → `classify_status` → 표 + 범례(§5-6) | export 실패는 `FETCH-FAIL`, 종료 0 |
| `doctor` | 필요 | 안 잡음 | 키 해석 OK | 서비스 계정 이메일 → 시트마다 ① 탭 수 ② 헤더 == expectedHeader ③ **CONTROL**: API 전체 서식값 vs export 파싱값 `rows_equal` | 설정 미비 2 / 접근 실패·탭≠1·CONTROL 불일치·**CONTROL 판정 불가(`-` = export 실패)** 중 하나라도 1(G1 2회차 P3-F) / 헤더 불일치만이면 0 |
| `apply` | 필요 | 잡음 | `validate_changeset` · 대상 시트마다 `apply_precondition`(dry-run 은 `WARN` 출력 후 계속, 실행은 1) | 아래 알고리즘 | 종료 코드 표 |
| `seed` | 필요 | 잡음 | `--sheet` 필수 · `--confirm-replace`(dry-run 제외 필수) · 리포 CSV 헤더 == expectedHeader · `seed_precondition` | 아래 알고리즘 | 종료 코드 표 |

**`apply` 알고리즘**
1. **`acquire_lock` 먼저**(`LockBusy` → 1, 이후 전 과정 `try/finally: release_lock`) → 매핑·manifest·변경셋 로드 → `validate_changeset` → 대상 시트마다 `apply_precondition`. (잠금을 먼저 잡아야 다른 세션의 sync 가 CSV 는 썼고 manifest 는 아직인 순간을 LOCAL-AHEAD 로 오판하지 않는다 — G1 2회차 P3-A.)
2. `SheetsClient(resolve_key_path(REPO_ROOT))`.
3. **모든 대상 시트를 먼저 읽고 계획한다**(변경셋 순서): `get_sheet_properties` → 탭 ≠ 1 이면 1 · `get_values("'<title 의 ' 를 '' 로>'")` → 첫 행 ≠ expectedHeader 면 1 · `plan_changes` → `PlanError` 면 1. 계획에 쓴 값 표 `planned_rows` = **이 단계 `get_values` 반환값 그대로(헤더 포함)** 를 시트별로 보관(G1 2회차 P3-G④). export sha ≠ manifest sha 면 `WARN [<sheet>] 스냅샷에 없는 시트 편집이 있다 — 이번 pull 에 함께 들어간다`(중단하지 않음).
   → **데이터·검증 오류는 전부 여기서 나고, 이 시점까지 아무것도 쓰지 않았다.**
4. 계획 출력(§5-6). `--dry-run` 이면 종료 0. 모든 시트 `touched()==0` 이면 `NO-OP` 종료 0.
5. `touched()>0` 인 시트마다 순서대로:
   - a0. **쓰기 직전 재조회** `get_values` → `rows_equal(current, planned_rows)` 아니면 `keyed_diff` 출력 + `[<sheet>] 계획 뒤 시트가 바뀌었다(사람·다른 도구) — 다시 실행` → 이미 쓴 시트가 있으면 **3**, 없으면 **1**. (G1 P2-3: 계획 읽기 ↔ 쓰기 사이의 창을 시트당 1초 이내로 좁힌다.)
   - a. `batch_update(build_apply_requests(...))`. `SheetsWriteOutcomeUnknown` → 되읽기를 1회 시도해 `keyed_diff(expected_after, readback)` 출력(되읽기도 실패하면 `되읽기 실패: <오류>` 만) → **3**. `SheetsSetupError` → 이미 쓴 시트가 있으면 3, 없으면 2. 그 밖의 `SheetsApiError` → 이미 쓴 시트가 있으면 3, 없으면 1(403 이면 `시트를 서비스 계정에 편집자로 공유했는지 확인` 안내).
   - b. 되읽기 `get_values` — 실패하면 **3**. `rows_equal(readback, expected_after(...))` 아니면 `keyed_diff` 출력 후 **3**(이후 시트 중단, pull 안 함).
6. 쓴 시트마다 pull:
   - a. **export 수렴 대기** — 최대 15회 × 2초: export GET(실패는 미수렴으로 센다) → `rows_equal(parse, readback)` 이면 통과. 소진 → **3**(`시트엔 반영됨 — 잠시 뒤 powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\sync-authoring-csv.ps1 -SheetName <이름>`).
   - b. `subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", <REPO_ROOT>\Scripts\sync-authoring-csv.ps1, "-SheetName", <이름>, "-MappingPath", <mapping 절대경로>], cwd=REPO_ROOT, capture_output=True, text=True, errors="replace", env={**os.environ, "FPSR_AUTHORING_LOCK_TOKEN": <acquire_lock 반환 token>})` — 시작 직전 `touch_lock`. 종료 코드 ≠ 0 → 출력 끝 20줄과 함께 **3**.
   - c. **사후 대조**: 리포 CSV 바이트 sha == `manifest_path_for(mapping)` 의 그 시트 항목 sha, 그리고 `rows_equal(parse(CSV), readback)`. 하나라도 아니면 **3**(`sync 가 다른 manifest 를 썼거나 스냅샷이 시트와 다르다`). (G1 P1-1 ③: PS·Python 경로 파생 드리프트를 런타임에 잡는다.)
7. 끝 안내: `다음: 카드 시트면 임포터(Tools > FPSR > 카드 CSV 임포트 / -run=FPSRImportCards) · 스냅샷 커밋(CSV + Config/AuthoringSheets.manifest.json)`. 종료 0.

**`seed` 알고리즘**
1. 인자 확인 → **`acquire_lock`**(`LockBusy` → 1, 이후 `finally: release_lock`) → 리포 CSV 존재·`parse_csv_bytes` → `new_rows`, 헤더 == expectedHeader(아니면 1). 8단계 sync 서브프로세스도 apply 6-b 와 같이 `env` 에 token 을 넣는다.
2. export GET(실패 = `export_sha None`) → 클라이언트 → 탭 = 1 → 현재값 `current_rows` → `seed_precondition(...)` → 문구가 있으면 1.
3. 계획 출력: `[<sheet>] SEED 현재 <행>×<열> → 새 <행>×<열>` + `keyed_diff(header[0], new_rows, current_rows)`(= 시트에서 사라질 키·새로 생길 키·바뀔 칸). `--dry-run` 이면 0.
4. **쓰기 직전 재조회** → `rows_equal(now, current_rows)` 아니면 1(`계획 뒤 시트가 바뀌었다`).
5. `batch_update(build_seed_requests(...))` — `SheetsWriteOutcomeUnknown` → 3 · `SheetsSetupError` → 2 · 그 밖 `SheetsApiError` → 1.
6. 되읽기(실패 → 3) → `rows_equal(readback, new_rows)` 아니면 `keyed_diff` 후 3.
7. export 수렴 대기(apply 6-a, 비교 대상 = `new_rows`).
8. **여기서만 `-Force` 로 sync** — 6·7 에서 시트 내용 == 리포 CSV 내용을 증명했으므로 가드를 우회해도 잃는 것이 없다(가드는 리포 CSV 바이트 ≠ 옛 manifest, 또는 manifest 항목 없음 때문에 막는다). 종료 코드 ≠ 0 → 3.
9. 사후 대조(apply 6-c, 비교 대상 = `new_rows`) 아니면 3.
10. 끝 안내: `스냅샷이 export 원시 바이트(CRLF·끝 줄바꿈 없음)로 다시 쓰였다. git status 엔 M 으로 보일 수 있다. 내용 대조 = git diff --ignore-cr-at-eol --stat -- <target> 이 비어야 한다`. 종료 0.

### 5-6. 출력 형식 (고정 — 사람과 자동화가 같이 읽는다)

- `status`: 머리줄 `"%-14s %-11s %-10s %-12s %s" % ("SHEET", "SHEET-STATE", "REPO-STATE", "OVERALL", "TARGET")` → 시트당 1줄 → 범례(7줄 전부):
  ```
  IN-SYNC     = 시트 · 스냅샷(manifest) · 리포 CSV 일치.
  SHEET-AHEAD = 시트에 스냅샷에 없는 편집이 있다 → powershell -File Scripts\sync-authoring-csv.ps1 -SheetName <이름>
  LOCAL-AHEAD = 리포 CSV 를 직접 고쳤다(정본 위반) → 그 변경을 변경셋으로 옮겨 apply 하고 리포 파일은 git checkout 으로 되돌린다. (이관 전이면 seed)
  DIVERGED    = 둘 다 바뀌었다 → 사람이 판단한다. 시트 편집을 먼저 살릴 것.
  NO-PROV     = manifest 기록이 없다 → 새 테이블이면 시트에 헤더를 넣고 sync 로 첫 스냅샷.
  MISSING     = 리포 스냅샷 CSV 가 없다 → sync 로 받는다.
  UNKNOWN     = 시트 export 를 받지 못했다(네트워크·공유 설정) → 다시 시도하고, 계속되면 시트 공개 링크가 "뷰어"인지 확인.
  ```
- `doctor`: `service account: <email>` → 시트당 `"%-14s ACCESS %s · TABS %s · HEADER %s · CONTROL %s"` (값 = `OK`/`FAIL`, 탭 수 숫자 또는 `-`, `OK`/`MISMATCH`/`-`, `OK`/`FAIL`/`-`).
- `apply` 계획: `[<sheet>] PLAN created=%d updated=%d unchanged=%d deleted=%d` + 줄마다 `  ~ <key>.<col>: '<before>' -> '<after>'` / `  + <key>` / `  - <key>` / `  ? 지울 키가 이미 없음(무시): <keys>`. 진행: `[<sheet>] WROTE requests=%d` · `[<sheet>] VERIFY OK` · `[<sheet>] SNAPSHOT OK`. dry-run: `[<sheet>] DRY-RUN (쓰지 않음)`.

## 7. 복제표

**해당 없음 — 게임 런타임 코드가 아니다.** 대신 이 유닛의 동시성·원자성 표:

| 경합 | 방어 | 남는 창 |
|---|---|---|
| 자동화 세션 둘이 동시에 apply/seed, 또는 apply 중 사람이 sync | `~/.fpsr/authoring-sheet.lock` 잠금(명령 진입 ~ 마지막 검증, 네트워크 동작마다 진행 신호) · sync 도 신선한 잠금이면 거부(§5-5) | 같은 머신에선 없음 — 단 진행 신호가 15분 끊긴 잠금은 치워진다(보유자가 멈춘 것으로 간주). 다른 머신의 자동화는 대상 아님 |
| 사람이 **같은 칸**을 편집 | 변경셋 `expect`(선택) + 쓰기 직전 재조회 | 재조회 ↔ batchUpdate 사이 1초 미만 — 이 칸은 우리가 마지막에 쓴다(§11 R1) |
| 사람이 **행 삽입·삭제·정렬** | 쓰기 직전 재조회(표 전체 비교) + 되읽기(키 기준 차이) + 문서 규칙 "apply 중 행 구조 변경 금지" | 재조회 ↔ batchUpdate 사이 1초 미만 — 이때 밀리면 **엉뚱한 행에 쓰일 수 있고** 되읽기가 3 으로 잡는다(사후 탐지, §11 R1) |
| 한 시트 안의 부분 적용 | `spreadsheets.batchUpdate` 원자성(공식) | 없음 |
| 여러 시트에 걸친 부분 적용 | 모든 시트를 **먼저 읽고 계획·검증**한 뒤 쓴다 → 데이터 오류로 인한 부분 적용 0 | 뒤 시트의 재조회 불일치·I/O 오류 → 종료 3 + 시트별 보고 |
| 응답 유실 후 재시도로 중복 적용 | 쓰기 5xx·전송 실패 재시도 금지(§5-1) | 없음 |
| 리포 CSV 직접 편집이 pull 로 소실 | sync LOCAL-AHEAD 가드 + **manifest 부재 fail-closed** + apply 전제조건 | 없음 |
| PS·Python 의 manifest 경로 불일치 | §5-5 파생식 + `[manifest]` 출력 + apply/seed 사후 대조(6-c) | 없음(불일치 시 3) |
| seed 가 사람 편집을 지움 | `seed_precondition`(export sha == manifest sha, 또는 완전히 빈 시트) + 쓰기 직전 재조회 | 재조회 ↔ batchUpdate 사이 1초 미만 |
| 외부인이 시트를 편집 | 공개 링크 권한 = **뷰어**(§8, §12 #7-0 확인) | 없음 |

## 8. 수명주기 · 소유권

- **구글 클라우드 프로젝트**: `fpsrproject` (2026-09-13 사용자 생성). 서비스 계정 `fpsr-sheets-writer@fpsrproject.iam.gserviceaccount.com`. **결제 계정 미연결**(청구 경로 없음).
- **키 파일**: 사용자 소유. 이 머신의 현재 위치 = 바탕화면(`fpsrproject-<hex>.json`, 사용자 결정 2026-09-13 — OneDrive 동기화 폴더 아님 실측). 도구는 `FPSR_SHEETS_SA_KEY` → 기본 `~/.fpsr/sheets-service-account.json` 순으로 찾는다. 리포 안 경로면 거부. **Claude 는 키 파일을 Read/cat 하지 않는다** — `private_key` 가 대화 기록에 남는다. 비밀이 아닌 필드 확인이 필요하면 `type`·`project_id`·`client_email` 세 필드와 `private_key` 존재 여부(bool)만 출력하는 한 줄 스크립트로만 한다(부록 F #18 이 이 방법 — G1 2회차 P3-D). 이 머신의 세션은 호출 전 `$env:FPSR_SHEETS_SA_KEY = (Get-ChildItem "$env:USERPROFILE\Desktop\fpsrproject-*.json" | Select-Object -First 1).FullName` (P3-G⑥).
- **유출 시**: 구글 클라우드 콘솔 > IAM 및 관리자 > 서비스 계정 > 해당 계정 > 키 탭에서 그 키 **삭제** → 새 키 발급(부록 A §1-4 에 명시).
- **접근 범위**: 시트 4종에 **파일 단위** 편집자 공유(폴더 공유 아님 — 2026-09-13 실측, 폴더 공유보다 좁다). **새 테이블은 그 시트를 따로 공유**해야 쓰기가 된다.
- **공개 링크 권한**: 🚨 2026-09-13 실측 = 폴더와 시트 4종 모두 **"링크가 있는 모든 사용자 = 편집자"** 였다. 리포가 공개(GitHub `Hato-1998/FPSRoguelite`)이고 `Config/AuthoringSheets.json` 에 시트 ID 가 있으므로 인터넷의 누구나 정본을 편집할 수 있었다. → **같은 날 사용자가 뷰어로 전환, Drive 권한 재조회로 폴더·시트 4종 `anyone=reader` 확인**(서비스 계정 writer 유지, 읽기 프로브·공개 export 정상 — 부록 F #19). pull 은 공개 보기로 충분하고, 리포가 공개라 보기 공개로 새로 새는 정보는 없다. §12 #7-0 은 이관 직전 **재확인** 조건으로 남긴다.
- **잠금 파일**: `~/.fpsr/authoring-sheet.lock` — 도구가 만들고 지운다. 비정상 종료로 남으면 15분 뒤 자동으로 치운다.
- **토큰**: google-auth 가 프로세스 메모리에서만 발급·갱신. 디스크 캐시 없음.

## 9. 데이터드리븐 경계

| 값 | 나가는 곳 | 기본값 | 비고 |
|---|---|---|---|
| 시트 목록·ID·gid·target·헤더 | `Config/AuthoringSheets.json` | 기존 | 불변 |
| 키 파일 경로 | 환경변수 `FPSR_SHEETS_SA_KEY` | `~/.fpsr/sheets-service-account.json` | 이 머신은 키가 바탕화면 → 호출 시 환경변수 지정 |
| 매핑 경로 | `--mapping` / `-MappingPath` | `Config/AuthoringSheets.json` | 스크래치 테스트·향후 별도 시트 묶음 |
| 재시도 대기열·타임아웃·export 수렴(15회×2초)·잠금 만료(900초)·잠금 경로 | 코드 상수 | §5-1·§5-3 | 구조 상수 — API 정책·도구 동작 연동값이지 콘텐츠 조정값이 아니다 |

## 10. 성능 예산

- **게임 런타임 비용 0**(툴체인).
- **Sheets API 쿼터**(공식, 2026-09-13): 프로젝트당 분당 읽기·쓰기 각 300 / **사용자(=서비스 계정)당 분당 각 60**. 초과 = 429.
- 명령당 요청 수: `apply` 시트당 읽기 4(속성·값·재조회·되읽기) + 쓰기 1 → 4시트 최대 읽기 16·쓰기 4. `seed` 시트당 읽기 4·쓰기 1. `doctor` 시트당 읽기 2. export(`docs.google.com`)·sync 는 Sheets API 쿼터 밖.
- 과금: 구글 공지 — 2026 하반기부터 표준 한도 초과분이 **결제 계정**에 청구될 예정 → §8 결제 계정 미연결로 차단.

## 11. 미결정 항목 · 리스크 · 명세 갭 처리

**미결정**
- **U1** 게임 시작 시 시트 런타임 읽기 — 사용자 결정 대기(비목표).
- **U2** 보드 행 마일스톤 배정 — 사용자.
- **U3** 이관(`seed`) 실행 시점 — 사용자와 조율: 사용자가 시트 편집 중이 아니고, 다른 세션이 카드·문자열 CSV 작업 중이 아닐 때. 실행 직전 `status` 로 4시트 `LOCAL-AHEAD`(시트 == manifest) 재확인.
- **U4** `Cards`·`DA_CardModifiers_BonusShot`.E1_Attr = `'weapon.frag.bonusshot '`(끝 공백) — 저작 사고로 보인다. 정본으로 굳히기 전 사용자 확인(이 유닛은 고치지 않는다). `ST_UI` 의 `Widget.Lobby.ReadyMark`(앞 공백 3)·`HUD.Run.LevelLabel`(`'Level : '`)은 UI 서식 의도로 보고 그대로 둔다.

**수용한 리스크**
- **R1** 재조회 ↔ batchUpdate 사이(1초 미만)에 사람이 ① 같은 칸을 편집하면 우리가 덮는다(마지막 쓰기 = 우리, 되읽기로도 못 잡음) ② 행을 삽입·삭제·정렬하면 인덱스가 밀려 **엉뚱한 행의 칸에 쓰일 수 있다**(되읽기가 키 기준 차이로 잡아 3 — 사후 탐지, 복구는 사람). Sheets API 에 조건부 쓰기가 없다. 완화 = 잠금(자동화 간 0) · 재조회(창 축소) · 문서 규칙(apply 중 행 구조 변경 금지) · 자동화 변경셋은 수정 칸에 `expect`.
- **R2** 모든 쓰기가 문자열 칸 → Claude 가 쓴 숫자는 시트에서 텍스트(왼쪽 정렬, `SUM` 제외). export·스냅샷·임포터는 영향 없음. 수정은 **바뀐 칸만** 쓰므로 사람이 입력한 칸은 그대로다.
- **R3** manifest 해시 = export **원시 바이트**. 이 머신은 `core.autocrlf=true` + `* text=auto` 에서 끝 줄바꿈 없는 CRLF 커밋이 체크아웃 시 바이트 동일 복원됨(G1 실측) → 안정. 설정이 다른 머신에서는 거짓 `LOCAL-AHEAD` 가능 — **막히는 쪽으로 실패**(손실 없음).
- **R4** 콤마·따옴표·앞뒤 공백 셀의 서식값 vs export: **읽기 대조는 실데이터로 확인**(앞뒤 공백 셀 7개 포함, 부록 F #16). 콤마·따옴표와 **쓰기** 왕복은 미검증 → §12 #5.
- **R5** 키 유출 = 시트 4종 편집권 유출. 리포 안 경로 거부 · `.gitignore` 방어 · 폐기 절차 문서화. 키가 바탕화면에 있는 것은 사용자 결정.
- **R6** 공개 링크 편집 권한(§8) — 2026-09-13 뷰어 전환 완료·확인. 누가 다시 편집자로 바꾸면 재발하므로 이관 직전 재확인(§12 #7-0). 새 테이블 시트를 만들 때도 뷰어로 둔다(부록 A §1-7).

**갭 처리 규칙(고정)**: 구현 중 명세에 없는 판단이 필요해지면 **추측해서 채우지 말고 멈추고 "명세 갭"으로 보고**한다. 갭은 C1 으로 돌아가 Opus 가 명세를 고치고, 구조가 바뀌면 G1 을 다시 태운다.

## 12. 검증 기준

| # | 검사 | 통과 조건 |
|---|---|---|
| 1 | 명세 대조 | §5 시그니처·CLI·종료 코드·요청 형태·예외/재시도 계약·출력 형식이 코드와 1:1 |
| 2 | 단위테스트 | `python -m unittest discover -s Scripts/tests -v` 전부 통과(§12-1 목록 전부 존재). google-auth 가 설치된 이 머신에서도, `sys.modules` 차단으로 부재를 흉내 낸 테스트(#16)에서도 |
| 3 | 빌드 | 해당 없음 — C++ 무변경. `git diff --stat` 에 `Source/` 가 없음을 확인 |
| 4 | PS1 무회귀·가드 | ⛔ **실제 매핑으로 라이브 sync 금지**(이관 전엔 4/4 LOCAL-AHEAD). 스크래치패드에 `AuthoringSheets.json` 이라는 **같은 파일명**의 스크래치 매핑을 두고 `-MappingPath` 로: ① `[manifest]` 줄 = `<스크래치>\AuthoringSheets.manifest.json` ② target 이 있고 manifest 가 없으면 **거부**(파일 무접촉, 종료 1) ③ target 없으면 첫 sync 성공 + manifest 생성 ④ LOCAL-AHEAD 거부 유지 ⑤ BOM 유지 ⑥ `localization-gather.ps1` 호출부 무변경(`git diff`) ⑦ 스크래치 manifest 를 일부러 깨뜨리면 target·manifest 파일 모두 바이트 무변경 ⑧ 저장 0건 실행 뒤 manifest 바이트 무변경 ⑨ 신선한 잠금 + 다른 token → 종료 1·무접촉 / 같은 token(`FPSR_AUTHORING_LOCK_TOKEN`) → 진행 / 900초 지난 잠금 → 경고 후 진행 |
| 5 | 스크래치 라이브 테스트 | **사용자 승인 후**: ① Drive 커넥터로 스크래치 시트 생성(헤더+샘플 행 CSV 업로드) ② 서비스 계정에 편집자 공유 ③ 스크래치 매핑·target(스크래치패드) ④ sync 로 첫 스냅샷 ⑤ `apply`: 수정·추가·삭제 · `expect` 불일치 → **무쓰기 1** · 값 왕복(`1.0` · `0.03` · `=SUM(A1)` · `'x` · `a,b` · `a"b` · `" x"` · `"x "` · `"   [a]"` · 빈칸) · 되읽기 · export 수렴 · 사후 대조 · `status` IN-SYNC ⑥ 잠금: 잠금 파일을 손으로 만들어 두면 apply 가 1 ⑦ `seed`: 스크래치 CSV 를 바꿔 seed → IN-SYNC / 시트를 API 로 직접 한 칸 고친 뒤 seed → 거부 1 ⑧ 끝나면 스크래치 시트 휴지통 |
| 6 | doctor | 4시트 `ACCESS OK · TABS 1 · CONTROL OK` (이관 전 Cards `HEADER MISMATCH` 는 정상). 2026-09-13 사전 프로브 = 이 조건 충족(부록 F #16) |
| 7-0 | 공개 링크 권한 | Drive 커넥터 `get_file_permissions` — 폴더·시트 4종의 `type=anyone` 권한이 `reader`(편집자 아님). **이관의 전제조건** |
| 7 | 이관(사용자 승인 후) | `seed` ×4 → `status` 4×`IN-SYNC` · `git diff --ignore-cr-at-eol --stat -- <4 target>` **빈 출력** · 시트 행 수 = 리포 행 수 |
| 8 | 회귀 | `Scripts\run_crit2_tests.bat FPSRoguelite.Editor.CardCsv.RoundTrip` · `… FPSRoguelite.Editor.Localization.StringTableCsv` 통과(판정 = stdout `Result={Success}`, 종료 코드 아님) |
| 9 | 레드팀 게이트 | `Workflow.md` §6-6-1. P1 잔존 시 푸시 금지 |
| 10 | 사용자 스모크 | ① 시트에서 Cards 39장·`BuildTags` 열·ST_UI `HUD.*` 11키 확인 ② 아무 칸이나 고친 뒤 `status` → `SHEET-AHEAD` → sync → `IN-SYNC` ③ Claude 가 apply 한 변경이 시트에 보이는지 |

### 12-1. 단위테스트 목록 (`Scripts/tests/test_authoring_sheet.py`, 전부 필수)

`sys.path` 에 `Scripts/` 를 넣고 `import authoring_sheet, sheets_api`. 파일 쓰기는 `tempfile` 안에서만. 네트워크 0.

1. `manifest_path_for` — `Config/AuthoringSheets.json` → `Config/AuthoringSheets.manifest.json`(점 하나).
2. `resolve_target` — 절대 그대로 / 상대 결합.
3. `parse_csv_bytes` — CRLF·LF 같은 결과 · 인용된 콤마 셀 · 앞뒤 공백 보존.
4. `rows_equal` — 후행 빈 칸·빈 행 무시 · 실제 차이 검출 · 앞뒤 공백 차이는 차이로 검출.
5. `classify_status` — §5-3 표의 7행 전부.
6. `apply_precondition` — MISSING · NO-PROV · LOCAL-AHEAD · 통과.
7. `seed_precondition` — CSV 없음 · manifest 없음+빈 시트(통과) · manifest 없음+비지 않은 시트 · export 실패 · export≠manifest · 통과.
8. `validate_changeset` — `changes` 없음 · 빈 리스트 · 모르는 시트 · 같은 시트 2항목.
9. `plan_changes` — 준 컬럼만 병합 · unchanged · 새 키 추가 · 같은 새 키 두 번 병합 · 같은 기존 키 두 번 누적 병합 · 삭제 · 없는 키 삭제 → `missing_deletes` · 모르는 컬럼 · 키 누락 · 중복 키 · upsert+delete 겹침 · `expect` 일치 · `expect` 불일치 · `expect` 모르는 컬럼 · 새 행에 `expect` · 헤더 밖 셀 · 커스텀 `key`.
10. `expected_after` — 수정+삭제+추가 동시(삭제 전 인덱스 의미).
11. `keyed_diff` — 같음 `[]` · 기대에만/실제에만 키 · 칸 차이 · 행 순서만 다름(= `[]`) · 헤더 컬럼 차이 줄 · 중복 키 줄(첫 등장 행으로 비교).
12. `encode_cell` — `""` → `{}` · `"1.0"`·`"0.03"`·`"=SUM(A1)"`·`"'x"`·`"   [a]"` → `stringValue` 가 입력과 정확히 같음.
13. `build_apply_requests` — 바뀐 칸만 · 행 +1 오프셋 · 삭제 내림차순 · appends 1요청 전 폭 · 순서 ①②③ · 변경 없으면 `[]`.
14. `build_seed_requests` — 그리드 부족 시 `appendDimension`(행/열) · 그리드 충분 시 없음 · 범위 = max(현재, 새) · 패딩 `{}` · 빈 현재 표.
15. `sheets_api.is_inside`·`resolve_key_path` — env 우선 · 리포 안 경로 거부 · **리포와 다른 드라이브 = 밖**(테스트 드라이브 문자는 tempfile 드라이브와 다른 문자로 골라 이식성 유지) · 파일 없음.
16. `SheetsClient` 라이브러리 부재 — `unittest.mock.patch.dict(sys.modules, {"google": None, "google.oauth2": None, "google.auth": None, "google.auth.transport": None, "google.auth.transport.requests": None, "google.auth.exceptions": None})` → `SheetsSetupError`.
17. 예외·재시도 계약 — 가짜 세션(`request()` 가 순서대로 응답 또는 예외) + `_sleep` 주입(대기 0): `get_values` 503→200 성공(호출 2회) · `get_values` `ConnectionError`→200 성공 · `get_values` 503×6 → `SheetsApiError` · `batch_update` 429→200 성공 · `batch_update` 503 → `SheetsWriteOutcomeUnknown`(**호출 1회**) · `batch_update` `TimeoutError` → `SheetsWriteOutcomeUnknown(status=0)`(호출 1회) · `batch_update` 400 → `SheetsApiError`(`SheetsWriteOutcomeUnknown` 아님) · `_auth_error_types=(FakeAuthError,)` 주입 후 `FakeAuthError` → `SheetsSetupError`(재시도 없음) · `request()` 가 던진 `ValueError` 는 그대로 전파 · 읽기 200 + 본문 JSON 깨짐 → `SheetsApiError`(재시도 없음) · `batch_update` 200 + 본문 JSON 깨짐 → `{}` 반환(예외 아님). 추가로 google-auth 가 import 가능할 때만(`unittest.skipUnless`): 깨진 키 파일로 `SheetsClient(...)` → `SheetsSetupError`.
18. `acquire_lock`·`touch_lock`·`release_lock` — 두 번째 획득 → `LockBusy` · mtime 을 과거로 돌린 잠금 → 치우고 획득 · **생성은 오래됐어도 `touch_lock` 으로 mtime 이 신선하면 `LockBusy`** · release 후 재획득 · 잠금 파일 JSON 에 pid·command·token, 반환값 == JSON token · 잠금 없는 `touch_lock` → `FileNotFoundError`.

## 13. 레드팀 지적 원장 (C3에서 채운다)

> `Workflow.md` §6-6-1. **기각엔 근거가 필요하다** — 제1원리 조항 / 코드 인용 / 실측치 중 하나.

| 심각도 | 지적 (요약 + 파일:줄) | 처리 | 근거 |
|---|---|---|---|
| P1 | | | |
| P2 | | | |
| P3 | | | |

- **레드팀에 무엇을 줬나**:
- **지적 0건이면**:

---

## 부록 A. `Docs/AuthoringSheetWriteback.md` 개정 구조 (전면 교체)

제목 `# 저작 시트 쓰기 — 설정과 사용 (정본 = 구글 시트)`. 머리 인용: 왜 있는가(2026-09-13 정본 복귀, 9/5~9/13 리포 CSV 정본 기간 종료) · 관련(`Localization.md` L-5 · `CombatWeaponCard.md` §2-3-10 · 명세 `Docs/Specs/SHEET1_SheetsApiWritePath.md`).

**§0 한눈에** — 흐름도(텍스트):
```
사람 ── 시트에서 직접 편집 ─────────────┐
자동화 ─ authoring_sheet.py apply ──────┤──▶ 구글 시트(정본) ──sync-authoring-csv.ps1──▶ Content/**/*.csv(스냅샷, git) ──▶ 임포터·StringTable
                                        │                          ▲ apply 가 끝에 스스로 부른다
리포 → 시트 통째 교체 = seed(이관·복구 전용, 시트 무편집일 때만) ┘
```
규칙 4줄: 리포 CSV 직접 편집 금지 / 상태는 `status` / 자동화 변경셋은 고치는 칸에 `expect` / **apply 가 도는 동안 시트에서 행 삽입·삭제·정렬 금지**.

**§1 1회 설정 — 사용자가 한다(약 10분, 비용 0)** — 아래 문구를 그대로 싣는다:
> 결제 수단은 필요 없다. **이 구글 클라우드 프로젝트에 결제 계정을 연결하지 말 것** — 청구는 결제 계정으로 가므로 연결하지 않으면 청구될 곳이 없다. "무료 체험 시작" 안내가 떠도 건너뛴다. 결제 계정 없이 진행이 안 되는 화면이 나오면 멈추고 알린다.
> 이 리포의 현재 설정(2026-09-13): 프로젝트 `fpsrproject` · 서비스 계정 `fpsr-sheets-writer@fpsrproject.iam.gserviceaccount.com`. 새 머신은 키 파일만 옮기면 되고(1-5), 1-1~1-4·1-6 은 다시 할 필요가 없다.

1. **프로젝트 만들기** — 시트 소유 구글 계정으로 `https://console.cloud.google.com/projectcreate` → 프로젝트 이름(예: `fpsrproject`) → **만들기**.
2. **Sheets API 켜기** — `https://console.cloud.google.com/apis/library/sheets.googleapis.com` → 화면 맨 위 프로젝트 선택 칸이 방금 만든 프로젝트인지 확인 → **사용(Enable)**.
3. **서비스 계정 만들기** — 왼쪽 메뉴 **IAM 및 관리자(IAM & Admin) > 서비스 계정(Service Accounts)** → 위쪽 **+ 서비스 계정 만들기** → 이름 `fpsr-sheets-writer` → **만들고 계속하기** → 역할(권한) 단계는 **비워 두고** 계속 → **완료**. (시트 권한은 여기서가 아니라 6번 공유로 준다.)
4. **키 파일 받기** — 서비스 계정 목록에서 방금 만든 계정의 **이메일** 클릭 → 위쪽 **키(Keys)** 탭 → **키 추가(Add key) > 새 키 만들기(Create new key)** → **JSON** → **만들기**. 파일(`<프로젝트ID>-<영숫자>.json`)이 내려받아진다 — **다시 받을 수 없는 유일한 사본**이다. 잃어버리거나 새면 같은 탭에서 그 키를 **삭제**하고 새 키를 만든다.
5. **키 파일 두기** — 권장 = `C:\Users\<사용자>\.fpsr\sheets-service-account.json`(도구 기본 경로). 다른 곳에 두면 환경변수 `FPSR_SHEETS_SA_KEY` 에 전체 경로를 넣는다. **리포 폴더 안에는 두지 않는다**(도구가 거부한다). 클라우드 동기화 폴더(OneDrive 등)도 피한다.
6. **시트 공유** — 구글 드라이브에서 저작 시트를 **하나씩** 오른쪽 클릭 > **공유** → 서비스 계정 이메일을 넣고 권한 **편집자** → **알림 보내기 체크 해제** → **공유**. 새 테이블 시트를 만들면 그 시트도 같은 방법으로 공유한다.
7. 🚨 **공개 링크는 뷰어로** — 같은 공유 창 아래쪽 **일반 액세스**가 "링크가 있는 모든 사용자"면 오른쪽 권한을 반드시 **뷰어**로 둔다(**편집자 금지**). 이 리포는 공개라 시트 주소가 공개돼 있다 — 편집자면 누구나 정본을 고칠 수 있다. 폴더와 시트 각각 확인한다. (pull 은 공개 보기 링크를 쓰므로 "제한됨"으로 바꾸지는 않는다.)
8. **파이썬 라이브러리 설치** — 리포 루트에서 `python -m pip install -r Scripts/requirements-sheets.txt`.
9. **점검** — `python Scripts/authoring_sheet.py doctor` → 4줄 모두 `ACCESS OK · TABS 1 · CONTROL OK` 면 끝.

**§2 일상 사용** — (키가 기본 경로에 없는 머신은 먼저 `$env:FPSR_SHEETS_SA_KEY = (Get-ChildItem "$env:USERPROFILE\Desktop\fpsrproject-*.json" | Select-Object -First 1).FullName` — 이 머신은 키가 바탕화면) / `status` / `apply --dry-run` → `apply` / 변경셋 형식(종전 예시 + `expect` 예시 `{"CardId": "DA_Card_Foo", "E1_Tiers": "C:0.04", "expect": {"E1_Tiers": "C:0.03"}}`) / 키 = 헤더 첫 컬럼 / upsert = 병합 / 한 변경셋에 같은 시트 항목은 하나 / 사람 편집 후엔 sync → 커밋 / apply 는 끝에 sync 까지 한다 / 카드 시트면 임포터 / `seed`(이관·복구 전용, `--confirm-replace`, 시트 무편집 전제) / **새 테이블**: 시트 생성 → 서비스 계정 공유 → 매핑 1항목 → 시트에 헤더 입력(또는 빈 시트에 seed) → sync 로 첫 스냅샷 → 이후 apply.

**§3 가드 표** — §7 동시성 표를 사람 말로.

**§4 갓차** — ① Claude 가 쓴 칸은 텍스트(숫자여도) — 계산에 쓰려면 `VALUE()` ② 시트 탭은 스프레드시트당 1개(L-5, gid) ③ 헤더 밖 칸에 값을 두면 apply 가 거부 ④ apply 중 행 삽입·삭제·정렬 금지(엉뚱한 행에 쓰일 수 있다 — 되읽기가 잡지만 사후다) ⑤ 한국어 Excel 로 CSV 직접 편집 금지(L-4) ⑥ 셀 줄바꿈은 `\n` 리터럴(L-4) ⑦ 키 파일 보안·폐기 ⑧ 공개 링크는 뷰어 ⑨ 쿼터(분당 60)·결제 계정 미연결 ⑩ 종료 코드 3 = 시트는 이미 바뀌었을 수 있다 → `status` 로 확인 후 sync ⑪ 잠금이 남아 있다는 오류 = 다른 세션이 쓰는 중(15분간 진행 신호가 없으면 자동 해제 · 그동안 sync 도 거부된다).

**§5 아직 안 되는 것** — 게임 시작 시 시트 런타임 읽기(결정 대기) · 열 추가 명령(시트 UI 에서 사람이) · 시트→리포 자동 감지.

## 부록 B. `Docs/SSOT/Localization.md` 변경

**L-1 10행** — 문장 끝에 덧붙인다: ` 자동화도 시트에 쓴다(`Scripts/authoring_sheet.py apply`, 서비스 계정 — L-5). (2026-09-05~09-13 리포 CSV 가 정본이던 기간을 거쳐 복귀.)`

**L-1 11행** — `시트에 행 추가 → sync → 키 참조.` → `시트에 행 추가(사람 또는 apply) → sync → 키 참조.`

**L-3 31행 위에 한 줄 추가**:
```
사람 편집 · authoring_sheet.py apply(Sheets API) ─→ 구글 시트(저작 정본)
```

**L-5 65~71행 교체**:
```markdown
- 시트 권한 = 공개 링크 **뷰어**(무인증 export URL — 2026-08-12 HTTP 200 검증. 🚨 2026-09-13 실측 시 "편집자"였다 — 리포가 공개라 시트 ID 가 공개이므로 **편집자 금지**) + **서비스 계정 편집자**(시트 파일 단위, 2026-09-13). 읽기(pull)는 계속 무인증 export, 쓰기만 서비스 계정.
- 🔁 **방향 재개정 (2026-09-13, 사용자 결정) — 정본 = 구글 시트로 복귀.** 2026-09-05 개정의 유일한 사유("자동화가 시트에 못 쓴다")를 Sheets API 서비스 계정 쓰기로 해소했다. 명세 = `Docs/Specs/SHEET1_SheetsApiWritePath.md`, 설정·사용 = `Docs/AuthoringSheetWriteback.md`.
  - **쓰는 길(사람)** = 시트에서 직접 편집 → `sync-authoring-csv.ps1` 로 스냅샷 갱신.
  - **쓰는 길(자동화)** = `Scripts/authoring_sheet.py apply <변경셋.json>` → **시트**에 행 단위로 쓴다(자동화 간 잠금 · 선택적 `expect` · 쓰기 직전 재조회 · 시트당 원자적 batchUpdate · 되읽기 검증) → 끝에 스스로 sync 를 불러 스냅샷까지 갱신한다.
  - **리포 CSV 직접 편집 금지**(스냅샷이다). sync 의 로컬-앞섬 가드는 그대로 두고, manifest 기록을 못 찾으면 **덮지 않는다**(fail-closed, 2026-09-13).
  - 상태 = `authoring_sheet.py status`(시트 export · manifest · 리포 CSV 3방향, 자격 불요).
  - 리포 → 시트 통째 교체는 `authoring_sheet.py seed` 뿐이고 **시트가 마지막 pull 이후 무편집일 때(또는 완전히 빈 새 시트일 때)만** 돈다.
- ~~🔁 방향 개정 (2026-09-05) — 마스터가 시트 → 리포 CSV(`apply` → CSV, Apps Script `push` → 시트 미러)~~ (2026-09-13 재개정으로 대체. `push`·Apps Script 경로 삭제)
- ~~초기 시딩(리포→시트)은 테이블당 1회만. 이후 시트=마스터, 동기화=단방향~~ (재시딩·복구 = `seed`)
```
(64행 매핑 줄과 72행 provenance 줄은 그대로.)

## 부록 C. `Docs/SSOT/CombatWeaponCard.md` §2-3-10 131행 교체

```markdown
> 진실 사슬 **(재개정 2026-09-13)**: **구글 시트(저작 정본) → `Scripts/sync-authoring-csv.ps1` → `Content/Authoring/*.csv`(git 스냅샷) → 에디터 임포터 → `DA_Card_*`(파생물)**. 카드 저작 = 사람은 시트에서 직접, 자동화는 **변경셋 JSON 1개**(`Scripts/authoring_sheet.py apply` — 시트에 행 단위로 쓰고 스냅샷까지 갱신). 리포 CSV 직접 편집 금지(sync 가 막는다). 설정·가드 = `Docs/AuthoringSheetWriteback.md` · 명세 = `Docs/Specs/SHEET1_SheetsApiWritePath.md`. ~~2026-09-05~09-13: 리포 CSV 가 저작 마스터, 시트는 미러~~. 공통 규약(단방향 동기화·provenance·인코딩 갓차) = `Docs/SSOT/Localization.md` L-4·L-5. 설계 명세 = `Docs/Specs/CARDCSV_ImporterPipeline.md`.
```

## 부록 D. `.gitignore` 53~56행 교체

```gitignore
# =============================================================================
# 구글 시트 서비스 계정 키 — 절대 커밋하지 않는다. 키는 리포 밖에 둔다(Docs/AuthoringSheetWriteback.md §1).
# 아래는 실수로 리포 안에 떨궜을 때의 방어선이다(authoring_sheet.py 도 리포 안 키 경로를 거부한다).
# 두 번째 줄 = 콘솔이 내려주는 원본 파일명(<프로젝트ID>-<영숫자>.json, 프로젝트 = fpsrproject).
*service-account*.json
fpsrproject-*.json
```

## 부록 E. `sync-authoring-csv.ps1` 문구

- `.DESCRIPTION` 첫 문단 교체:
  ```
  구글 시트 -> 리포 CSV 동기화 (LOC0 §5). 정본 = 구글 시트, 리포 CSV = 스냅샷 (2026-09-13 재개정, SHEET1).
  사람은 시트에서 직접 편집하고, 자동화는 Scripts/authoring_sheet.py apply 로 시트에 쓴다(apply 가 끝에 이 스크립트를 부른다).
  로컬 CSV가 매니페스트와 다르면(리포 CSV 를 직접 고쳤으면) 덮어쓰기를 거부한다(-Force 로만 강제).
  매니페스트 기록을 못 찾으면(파일 없음·파싱 실패·항목 없음) target 이 있는 시트는 덮지 않는다(fail-closed).
  ```
- `.PARAMETER MappingPath` 도움말 1줄 추가(§5-5 주석과 같은 뜻).
- 로컬-앞섬 경고(현 138~141행) 교체:
  ```
  "[$Name] 로컬 CSV가 마지막 pull보다 앞서 있습니다 — 덮어쓰기를 거부합니다(기존 파일 무접촉)."
  "         무엇이 다른지: git diff -- $TargetRelative"
  "         정본은 시트다: 그 변경을 변경셋으로 만들어 python Scripts/authoring_sheet.py apply 로 시트에 쓰고, 리포 파일은 git checkout 으로 되돌린다"
  "         로컬 변경을 버리고 시트로 되돌리려면: -Force"
  ```
- 131~134행 가드 주석의 "마스터 규칙 = 리포 CSV. 시트는 미러" → "정본 = 시트. 리포 CSV 직접 편집은 정본 위반이라 조용히 덮지 않고 막는다".
- manifest 파싱 실패 경고(현 68행 `"기존 .sync-manifest.json 파싱 실패 — 새로 씁니다"`) → `"manifest 파싱 실패 — 이번 실행은 target 이 있는 시트를 덮지 않습니다(fail-closed): $($_.Exception.Message)"`.

## 부록 F. C0 실측·대조군 (2026-09-13, G1 에 넘기는 근거)

| # | 실측 | 결과 | 의미 |
|---|---|---|---|
| 1 | Drive 커넥터 도구 목록 | 셀 쓰기 없음(`update_file` = 제목·부모 폴더만) | Claude 세션 도구로는 시트 칸을 못 쓴다 → A안 필요 |
| 2 | 구글 공식 Sheets MCP | 개발자 미리보기 · 신청서가 Workspace 계정 요구 · 범위 `drive.readonly`+`spreadsheets` · `update_values` 에 입력 방식 옵션 없음 | C안 기각 근거 |
| 3 | 시트 4종 export sha256 vs manifest | **4/4 일치**(Cards 는 14:28 modifiedTime 갱신 뒤 재측정에서도 일치 = 비내용 변경) | 마지막 pull(8/13·8/19) 이후 **내용 편집 0** → seed 가드 조건 성립 |
| 4 | 리포 CSV vs 시트(컬럼명 기준) | Cards: 리포 +10장·`BuildTags` 열·시트에만 `SniperScope` / CardCatalog +10·시트에만 `riflesniperscope` / ST_UI +11(`HUD.*`) / ST_CardEffect +1. **공통 행의 공통 컬럼 값 차이 0** | 이관 = 리포 → 시트 일방 |
| 5 | export 바이트 재직렬화 | Python `csv.writer(lineterminator="\r\n")` == export 바이트(끝 줄바꿈 제외). export 는 CRLF·끝 줄바꿈 없음. 현재 데이터에 인용 필드 0 | 인용 규칙 동일성은 미검증 → R4 |
| 6 | `authoring_sheet.py status` | 4/4 `LOCAL-AHEAD` | 9/5 이후 push 가 한 번도 안 돌았다 |
| 7 | 로컬 환경(설치 전) | Python 3.10.11, pip 외 패키지 없음 · gcloud 없음 · node 있음 | — |
| 8 | 공유 폴더 `1jdMK1VlVI2t71nMc89DWCPxuw-jQjnLv` 내용 | 저작 시트 4종뿐 | — |
| 9 | 기존 변경셋 6개 | CSV 형식 4개는 전부 시트당 항목 1개 · `expect` 사용 0 · 2개는 `edits[]`(asset_edit.py) 형식 | "시트당 1항목" 호환 · `changes` 없는 파일 거부 필요 |
| 10 | 병렬 활동 | 다른 세션이 14:00~14:39 stat1 에셋·스크립트 커밋(카드 CSV 무접촉) · 사용자가 시트를 열어 둠 | 이관 시점 조율(U3) |
| 11 | API 공식 문서 | batchUpdate 원자·순서 · `appendCells` = 마지막 데이터 행 뒤 · `UpdateCellsRequest.range` 미포함 칸 클리어 · `stringValue` · values.get 기본 `FORMATTED_VALUE` · 쿼터 분당 60/사용자 · 표준 사용 무료 | §5-4·§10 근거 |
| 12 | (G1 P1-1 재현) PS 5.1.22621 `[IO.Path]::ChangeExtension('…\AuthoringSheets.json', $null) + '.manifest.json'` | `…\AuthoringSheets..manifest.json` · 교정식(§5-5) = `…\AuthoringSheets.manifest.json` | 파생식 교체 + fail-closed |
| 13 | (G1 P2-1 재현) Python `os.path.commonpath([C:\…, E:\…])` | `ValueError: Paths don't have the same drive` | `is_inside` 드라이브 분기 |
| 14 | (G1 P2-5 재현) 리포 CSV 앞뒤 공백 셀 | Cards `BonusShot`.E1_Attr 끝 공백 1 · ST_UI `ReadyMark` 앞 공백 3(ko·en·ja) · `LevelLabel` 끝 공백(ko·en·ja) | 왕복 값 목록에 공백 추가 · U4 |
| 15 | (G1 P3-1 재현) `git config core.autocrlf` · `git ls-files --eol` | `true` · Cards.csv `i/lf w/lf` · ST_UI.csv `i/lf w/crlf` | §5-3 seed 안내·§12 #7 명령 |
| 16 | **서비스 계정 읽기 프로브**(사용자 설정 후, `google-auth 2.58.0`·`requests 2.34.2` 설치, 쓰기 0) | 인증 OK(Sheets API 활성) · 4시트 `ACCESS OK · TABS 1(title='Untitled', 1000×26)` · **CONTROL OK 4/4**(앞뒤 공백 셀 ST_UI 6·Cards 1 포함 API 서식값 == export) · Cards `HEADER MISMATCH`(이관 전 정상) | doctor 조건 사전 충족 · R4 읽기 측 확인 |
| 17 | 공유 권한(Drive 커넥터 `get_file_permissions`) | 폴더·시트 4종 모두 `anyone = writer` 🚨 · 서비스 계정 = 시트 4종 파일 단위 `writer`(폴더엔 없음) · GitHub 리포 `PUBLIC` | §8·R6·§12 #7-0 |
| 18 | 키 파일 | 바탕화면 `fpsrproject-<hex>.json` · `type=service_account` · `project_id=fpsrproject` · 바탕화면은 OneDrive 이동 폴더 아님(`GetFolderPath('Desktop')` = 로컬) | §8 |
| 19 | 공유 권한 재조회(사용자가 뷰어로 전환한 뒤) | 폴더·시트 4종 `anyone = reader` · 서비스 계정 시트 4종 `writer` 유지 · 읽기 프로브 재실행 4시트 ACCESS OK·CONTROL OK | §8·R6 해소, §12 #7-0 현재 충족 |
| 20 | (G1 2회차 보고 — Opus 미재현) | PS 5.1 교정 파생식 = 점 하나 · `GetUnresolvedProviderPathFromPSPath` 는 `Set-Location` 을 따르고 `GetFullPath` 는 안 따름 · `is_inside` 시제품(드라이브 다름·없는 드라이브 `Q:`·형제 접두 `FPSRoguelite2`) 판정 정상 · requests `ConnectionError`·`Timeout`·`JSONDecodeError` ⊂ `OSError`, google-auth `TransportError`·`RefreshError` ⊄ `OSError` · `AuthorizedSession.request(method, url, …, timeout, **kwargs)` | §5-1·§5-5 근거 |
| 21 | (C2 착수 전, 가설 검증) Python 이 `ensure_ascii=False` 로 쓴 BOM 없는 UTF-8 잠금 JSON 5종(한글 짝수 바이트 · 값 끝이 한글 홀수 바이트인 `host`·`command` · 한글 뒤 `.json`)을 PS 5.1.22621 `Get-Content -Raw \| ConvertFrom-Json` 과 `[IO.File]::ReadAllText(…, UTF8)` 로 읽음 | `Get-Content` 는 한글을 ANSI 로 읽어 **mojibake**(`카드추가` → `移대뱶異붽?`) · 그러나 **JSON 파싱·token 판독은 5/5 정상**(따옴표 삼킴 미발생 — H8 은 콘솔 stdin 경로) · `ReadAllText(UTF8)` 5/5 정상 | "잠금 파싱 실패 → 매 apply 종료 3" 가설 **기각**. 경고 문구 표시만 보강(부록 I-2) |
| 22 | (C2 착수 전) google-auth 2.58.0 `service_account.Credentials.from_service_account_file` 에 깨진 키 4종 + 없는 파일 | JSON 아님·빈 파일 = `JSONDecodeError` · 필드 누락 = `MalformedError`(MRO 에 `ValueError`) · 개인키 손상 = `ValueError` · 없는 파일 = `FileNotFoundError`(⊂ `OSError`) · `transport/requests.py` `Request.__call__` 이 `RequestException` 을 `TransportError`(⊂ `GoogleAuthError`)로 감싼다 | §5-1 `(ValueError, KeyError, OSError)` 계약 성립 · 갱신 중 네트워크 실패도 자격 오류 경로로 온다 → 부록 I-10 |
| 23 | (C2 진행 중) 파이프로 받는 Python 3.10.11 표준 출력 인코딩 · 리포 CSV 4종의 cp949 불가 글자 | `sys.stdout.encoding = cp949`(PYTHONUTF8·PYTHONIOENCODING 미설정) — 이 측정 스크립트 자신이 `—` 를 print 하다 `UnicodeEncodeError` 로 죽었다 · ST_UI 32자(`ー` 19 · `—` 6 · 한자 6, `ja`·`SourceString`·`en`) · ST_CardEffect 7자 · Cards 86자(`DisplayName_ja`·`Description_ja` — `ー`·`撃`·`発`·`弾` 등) · CardCatalog 1자(`Notes` 의 `—`) | 부록 I-14 |
| 24 | (C3 대조 중) PS 5.1.22621 에서 잠금 내용 4종(빈 문자열 · 공백+줄바꿈 · `{}` · token 없는 JSON)을 `ConvertFrom-Json` 후 C2 구현 조건 `(-not $LockParsedOk -or $LockInfo.token -ne $env:FPSR_AUTHORING_LOCK_TOKEN)`(환경변수 미설정) 으로 판정 | 4/4 **예외 없음**(빈 문자열·공백은 `$null` 객체) → 4/4 **거부 안 함(통과)** | 부록 I-15 로 교정 |
| 25 | (C3 대조 중, 가설 검증) Sheets API `GET spreadsheets/<id>?fields=sheets.properties(...)` 원본 JSON — 스크래치 시트(드라이브 커넥터로 CSV 변환 생성) · ST_CardEffect · Cards | 3/3 `index: 0` **생략 안 됨** · `sheetId` 3/3 0 아님(420577988 · 469644178 · 610789811) · 서비스 계정이 스크래치 시트 200(폴더엔 SA 권한 없는데 새 파일에 writer 가 붙어 있었음 — 경로 미확인) | "기본값 0 필드가 생략돼 `sheetId`·`index` 가 `None`" 가설 **기각** → `get_sheet_properties` 기본값 보정 불요 |
| 26 | (스크래치 라이브) 서비스 계정으로 스크래치 시트 한 칸을 `batchUpdate` 한 직후 export sha 가 바뀔 때까지 0.5초 간격 폴링 ×3 · 편집 직후 곧바로 `seed --confirm-replace` | 3/3 **첫 폴링에 반영**(0.9·1.0·0.9초, GET 왕복 포함) · 곧바로 돌린 seed 는 `시트가 마지막 pull 이후 편집됐다` 로 **종료 1**, 사람 편집 보존, `status` = `DIVERGED` | 현재 export 지연은 작다 — 그래도 전제는 부록 I-20 으로 검사화 |

## 부록 G. G1 1회차 지적 처리 (2026-09-13)

| 지적 | 처리 | 반영 위치 |
|---|---|---|
| P1-1 manifest 경로 파생식(PS 5.1 점 두 개) → 가드 조용히 꺼짐 | **수용**(부록 F #12 재현) | §5-5 파생식·`[manifest]` 출력·**fail-closed 가드** · §6 apply 6-c / seed 9 사후 대조 · §12 #4 실제 매핑 라이브 금지 |
| P2-1 `commonpath` 드라이브 다르면 ValueError | **수용**(부록 F #13 재현) | §5-1 `is_inside` · §12-1 #15 |
| P2-2 전송·자격 예외 계약 부재 | **수용** | §5-1 세션 계약·예외 계약 · §12-1 #17 |
| P2-3 인덱스 쓰기 vs 행 밀림, 세션 간 경합 | **수용** | §5-2 잠금 · §5-3 `acquire_lock`·`keyed_diff` · §6 apply 5-a0 / seed 4 재조회 · §7 · §11 R1 · 부록 A 규칙 |
| P2-4 manifest 항목 없음/CSV 없음 미정의 | **수용** | §5-3 `apply_precondition`·`seed_precondition` · §12 #5 절차 · 부록 A §2 새 테이블 |
| P2-5 앞뒤 공백 왕복 누락 | **수용**(부록 F #14 재현) | §12 #5 값 목록 · §12-1 #3·#4·#12 · U4 |
| P3-1 git 줄바꿈 주장 정정 | **수용**(부록 F #15 재현) | §6 seed 10 안내 · §12 #7 · §11 R3 |
| P3-2 status 범례 4/7 | **수용** | §5-6 범례 7줄 |
| P3-3 되읽기 실패·수렴 GET 실패 종료 코드 | **수용** | §6 apply 5-b·6-a |
| P3-4 subprocess 디코딩 | **수용** | §6 apply 6-b `text=True, errors="replace"` |
| P3-5 `.gitignore` 원본 파일명 | **수용**(실제 프로젝트 ID = `fpsrproject`) | 부록 D |
| P3-6 seed 계획 출력 | **수용** | §6 seed 3 `keyed_diff` |

## 부록 H. G1 2회차 지적 처리 (2026-09-13, 판정 = 통과)

| 지적 | 처리 | 반영 위치 |
|---|---|---|
| P2-A 잠금 만료가 생성 시각 기준 → 살아 있는 잠금을 치움 | **수용** | §5-2 진행 신호 · §5-3 `touch_lock`·만료 정의 · §7 · 부록 A ⑪ · §12-1 #18 |
| P2-B sync 가 파싱 실패·저장 0건에도 manifest 를 다시 씀 | **수용** | §5-5 manifest 쓰기 조건 · §12 #4 ⑦⑧ |
| P3-A 잠금이 전제조건 판정 뒤 | **수용** | §5-2 · §6 apply 1 · seed 1 |
| P3-B sync 가 잠금 밖 | **수용** | §5-5 잠금 존중(token) · §6 apply 6-b / seed 1(8단계 env) · §12 #4 ⑨ |
| P3-C `keyed_diff` 빈 결과·중복 키 | **수용** | §5-3 `keyed_diff`·호출부 규칙 · §12-1 #11 |
| P3-D 키 파일 문구 모순 | **수용** | §8 |
| P3-E §12 #2 참조 오류 | **수용** | §12 #2(#13 → #16) |
| P3-F doctor CONTROL `-` 종료 코드 | **수용** | §6 doctor |
| P3-G① 쓰기 전 예외 총칙 | **수용** | §5-2 |
| P3-G② try 범위·JSON 파싱 | **수용** | §5-1 예외 계약 4 · §12-1 #17 |
| P3-G③ 키 파일 `OSError` | **수용** | §5-1 `SheetsClient.__init__` |
| P3-G④ `planned_rows` 정의 | **수용** | §6 apply 3 |
| P3-G⑤ 오래된 잠금 삭제 경합 | **수용** | §5-3 `acquire_lock` |
| P3-G⑥ 바탕화면 키의 환경변수 | **수용** | §8 · 부록 A §2 |

> 구조 변경 없음(모두 계약·조건 보강) → Fable 판정대로 G1 재제출 없이 `확정`. 단 개정 2 는 Fable 이 본 문장이 아니므로 **G2 프롬프트에 "G1 이후 Opus 가 반영한 계약 14건 = 부록 H" 를 명시**해 함께 검증받는다(§6-5-2 "G1 과 G2 사이가 길다" 규칙).

## 부록 I. C2 착수 전~C3 대조 중 Opus 명확화·교정 (2026-09-13, 개정 3 — 구조 불변)

> Sonnet 이 구현 중 멈출 빈칸(명세 갭 후보)을 Opus 가 메웠다 — I-1~I-13 은 위임 전, **I-14 는 C2 진행 중 · I-15~I-20 은 C3 대조·스크래치 라이브 중 실측·코드 대조로 발견**. 전부 계약·출력 보강이고 구조 변경은 없다 → G1 재제출 불요.
> **G2 프롬프트에 부록 H(14건)와 함께 이 20건을 명시**한다(§6-5-2 (3) "G1 과 G2 사이" 규칙).

| # | 빈칸 | 결정 | 근거 |
|---|---|---|---|
| I-1 | `authoring_sheet.py` 의 export GET 수단 미지정 | stdlib `urllib.request.urlopen`(타임아웃 30초, 리다이렉트는 기본 동작). **모듈 최상단 third-party import 금지** — `status`·단위테스트가 google-auth·requests 없이 돈다. `OSError`(`URLError`·`HTTPError`·타임아웃 포함) · `http.client.HTTPException` · 본문 0바이트 → 실패(`None`). 재시도 없음(apply 6-a 는 자체 루프) | §2 목표 6 · §12 #2 |
| I-2 | 잠금 JSON 인코딩 미지정 | Python = `json.dumps(obj, ensure_ascii=True)`(기본값을 명시 — 파일이 순수 ASCII). PS = `[System.IO.File]::ReadAllText($LockPath, [System.Text.Encoding]::UTF8)`. **결함 교정이 아니라 표시 보강**: 부록 F #21 실측상 BOM 없는 UTF-8 한글 잠금도 PS 5.1 이 파싱·token 판독은 하나 경고의 `<command>` 가 mojibake 가 된다 | 부록 F #21 · [[ps51-hook-script-needs-bom]] |
| I-3 | 잠금 `command` 필드 값 | `" ".join(sys.argv[1:])` | — |
| I-4 | `keyed_diff` 경계 | 행이 0개인 표 = 헤더 `[]`·데이터 0행. `key_col` 이 한 표의 헤더에 없으면 그 표의 키는 0개. 키 칸이 비었거나 행이 짧아 키 칸이 없는 행은 비교하지 않는다. 행이 헤더보다 짧으면 모자란 칸 = `""`. 헤더 줄 `<cols>` = 각자 헤더 등장 순 `", "` 결합(없으면 빈 문자열) | seed 대상이 완전히 빈 새 시트일 때(§5-3 `seed_precondition` 통과 경로) |
| I-5 | `status` 상태 산출 순서 | 시트 이름 정렬 순. `repo_state` = 파일 없음 `MISSING` → manifest 항목 없음 `NO-PROV` → sha 같음 `SAME` / 다름 `CHANGED`. `sheet_state` = manifest 항목 없음 `NO-PROV`(GET 생략) → GET 실패 `FETCH-FAIL` → `SAME` / `CHANGED`. sha 비교는 양쪽 대문자 정규화 | §5-3 `classify_status` |
| I-6 | `doctor` 의 `-` 조건·순서 | 시트 이름 정렬 순. ACCESS FAIL → TABS·HEADER·CONTROL 전부 `-`. TABS ≠ 1 → HEADER·CONTROL `-`. export 실패 → CONTROL `-`. HEADER = API 값 첫 행(값이 없으면 `[]`) == `expectedHeader`. CONTROL = `rows_equal(API 전체 값, parse_csv_bytes(export))` | §6 doctor 종료 코드 |
| I-7 | `seed` 에서 리포 CSV 가 없을 때 | 1단계에서 `seed_precondition(sheet, None, None, [], False)` 의 문구로 종료 1 | §6 seed 1 |
| I-8 | `apply` 3단계 export 실패 | `WARN [<sheet>] export 실패 — 스냅샷 밖 시트 편집 여부 미확인` 출력 후 계속(경고 전용 검사) | §6 apply 3 |
| I-9 | NO-OP·종료 3 출력 | 전 시트 `touched()==0` → `NO-OP — 모든 시트가 이미 목표와 같다` 한 줄. 종료 3 직전 stderr 에 `사람 확인 필요 — 시트가 이미 바뀌었을 수 있다: python Scripts/authoring_sheet.py status 로 확인 후 sync` 한 줄 | §5-2 종료 코드 3 · 부록 A §4 ⑩ |
| I-10 | 자격 갱신 실패 문구가 네트워크 실패를 키 문제로 오진 | §5-1 예외 계약 1 문구 교체(`네트워크 연결 · 키 삭제/무효 · PC 시계를 확인`). 동작(재시도 없음 · 종료 2)은 불변 | 부록 F #22 — 토큰 갱신 중 전송 실패도 `TransportError` ⊂ `GoogleAuthError` |
| I-11 | 기존 코드 정리 범위 | 새 명세에서 호출되지 않는 기존 정의 삭제: `cmd_push` · `WRITEBACK_PATH` · `write_csv` · `read_csv` · `sha256_of` · 전역 `MAPPING_PATH`/`MANIFEST_PATH`(→ `--mapping` + `manifest_path_for`). 모듈 docstring 을 새 방향(정본 = 시트, 명령 4종)으로 교체. 명령 구현용 **`_` 접두 비공개 헬퍼**(매핑·manifest 로드, export GET, sync 호출, 명령 함수 등)는 허용 — §5 에 선언된 공개 이름·시그니처의 추가·변경은 여전히 금지 | 죽은 코드 금지 · §5-2 |
| I-12 | `resolve_target` 결합 형태 | 상대 → `os.path.normpath(os.path.join(repo_root, target))`(매핑의 `/` 를 OS 구분자로) · 절대 → 입력 그대로 | §5-3 |
| I-13 | `sync-authoring-csv.ps1` `.DESCRIPTION` 둘째 문단의 manifest 경로(`Content/StringTables/.sync-manifest.json` — 이미 틀림) | 부록 E 교체와 함께 "매핑 파일 옆 `<매핑 파일명>.manifest.json`(기본 `Config/AuthoringSheets.manifest.json`)" 으로 고친다 | §5-5 파생식과의 모순 제거 |
| I-14 | **표준 출력 인코딩 미지정 → 출력 한 줄이 종료 코드 계약을 깬다**(C2 진행 중 발견) | `main()` 첫 동작으로 `sys.stdout`·`sys.stderr` 를 `reconfigure(errors="backslashreplace")`(인코딩은 그대로 — 콘솔·파이프 어느 쪽이든 한국어 표시는 환경 인코딩을 따르고, 못 찍는 글자만 `ー` 로 나간다). `reconfigure` 가 없거나 거부하면(`AttributeError`·`ValueError`) 그대로 둔다 | 부록 F #23 — 파이프 출력 인코딩 = cp949, 리포 CSV 에 cp949 로 못 찍는 글자 실재. 쓰기 **뒤** 출력(되읽기 불일치 `keyed_diff`, sync 출력 끝 20줄의 `U+FFFD`)에서 `UnicodeEncodeError` 가 나면 종료 3 이어야 할 것이 1 로 나간다 |
| I-15 | **sync 잠금 존중의 "파싱 실패" 범위**(C3 대조 중 발견) — §5-5 문언("token 이 환경변수와 다르면 거부")을 그대로 옮기면, 빈 파일·공백·`{}`·token 없는 JSON 을 PS 5.1 이 **예외 없이** `$null` token 으로 받아 환경변수 미설정(`$null`)과 "같다"로 **통과**한다(부록 F #24) | 내용이 비었거나 `token` 이 비면 **파싱 실패와 같게 본다(거부)**. 빈 파일은 `acquire_lock` 이 `O_EXCL` 로 만든 뒤 JSON 을 쓰기 전의 실제 순간이다 | §5-5 "잠금 JSON 파싱 실패는 신선한 잠금으로 본다(거부)"의 취지 · §12 #4 ⑨ |
| I-16 | `keyed_diff` 중복 키 줄의 `(기대\|실제)` 표기(C3 대조 중 — 구현은 글자 그대로, 테스트는 표 이름으로 읽어 2건 불일치) | **표 이름으로 찍는다**: 기대 표 중복 = `"  ! 중복 키(기대): <k>"`, 실제 표 중복 = `"  ! 중복 키(실제): <k>"`(둘 다면 두 줄). §5-3 본문 문구도 이렇게 고쳤다 | 글자 그대로면 어느 표의 중복인지가 사라져 줄이 정보를 잃는다 |
| I-17 | **`delete` 키 중복 미처리 → 다음 행 삭제**(C3 코드 대조 중 발견) — §5-3 5단계가 중복을 말하지 않아, 같은 키가 두 번 오면 같은 `row_index` 의 `RowDelete` 가 둘 생기고 `deleteDimension` 두 번째가 밀려 올라온 **다음 행을 지운다**(되읽기가 3 으로 잡지만 사후) | 키마다 **첫 등장만** 처리한다(`RowDelete`·`missing_deletes` 모두 한 번) | 종전 `cmd_apply` 가 `set(change["delete"])` 로 중복을 없앴다 — "종전 병합 의미를 그대로 옮긴다"(§5-3) |
| I-18 | **첫 쓰기 시도 뒤 새어 나온 예기치 않은 예외의 종료 코드**(C3 코드 대조 중 발견) — 되읽기·재조회 중 자격 갱신 실패(`SheetsSetupError` → 2), 잠금 파일 소실(`touch_lock` 의 `FileNotFoundError`), `powershell` 실행 실패, 코드 버그가 `main()` 까지 올라가 **2·1 로 나간다** | apply·seed 본문을 비공개 실행기 `_run_guarded` 로 감싼다: 첫 `batch_update` **시도 직전** 표시를 세우고, 그 뒤 새어 나온 `Exception` 은 traceback 을 남기고 **종료 3**. 시도 전이면 그대로 올려 `main()` 분류(2·1)를 따른다 | §5-2 종료 코드 3 = "쓰기가 일어났거나 일어났을 수 있는데 그 뒤가 실패" |
| I-19 | `doctor` 에서 속성은 읽혔는데 값 읽기가 `SheetsApiError` 로 실패하면 예외가 `main()` 까지 올라가 **나머지 시트 보고 없이 중단** | 그 시트를 `ACCESS FAIL`(TABS·HEADER·CONTROL `-`)로 찍고 다음 시트로. 종료 코드 규칙(접근 실패 → 1)은 불변 | §6 doctor "시트마다 점검" |
| I-20 | **seed 가드가 "export 가 최신"이라는 전제에 기댄다**(스크래치 라이브 중 발견) — `seed_precondition` 의 `export_sha == manifest_sha` 는 export 가 시트 현재값을 반영할 때만 "무편집"을 뜻한다. export 가 늦으면 방금 한 사람 편집을 못 보고 통과해 seed 가 지운다 | seed 2단계에서 `manifest_sha` 가 있으면 **이미 받은 두 값** `rows_equal(parse_csv_bytes(export), current_rows)`(API 현재값)도 참이어야 진행, 아니면 종료 1(`export 가 시트 현재값과 다르다(export 반영 지연 또는 방금 편집) — 잠시 뒤 status 로 확인하고 다시`). 추가 네트워크 없음. 완전히 빈 새 시트 경로(manifest 없음)는 해당 없음 | 부록 F #26 — 지연 실측 ~1초(3/3)라 현재 위험은 작지만 구글이 보장하지 않는 전제를 검사로 바꾼다. 실제 4시트는 CONTROL OK(서식값 == export)라 이 검사로 인한 거짓 거부 없음 |
