#!/usr/bin/env python3
"""구글 서비스 계정 자격 해석 + Sheets API v4 얇은 REST 클라이언트.

왜 이 모듈이 따로 있는가 — `authoring_sheet.py` 의 `status` 서브커맨드와 단위테스트는
google-auth·requests 없이도 돌아야 한다(§2 목표 6). 그래서 이 모듈은 **최상단에서
google.*·requests 를 import 하지 않는다** — 둘 다 `SheetsClient.__init__` 안에서
`_session` 이 주어지지 않았을 때만 지연 import 한다.

책임 범위:
  - 서비스 계정 키 파일 경로 해석(리포 밖 강제 — 커밋 사고 방지)
  - Sheets API 세 엔드포인트(속성 조회·값 조회·batchUpdate)의 예외·재시도 계약

읽기와 쓰기의 재시도 정책이 다른 이유(§5-1) — 쓰기 응답이 5xx·전송 실패로 끝나면
"요청이 서버에 적용됐는지 알 수 없다"(SheetsWriteOutcomeUnknown). 이걸 재시도하면
`appendCells` 가 행을 중복 추가하거나 `deleteDimension` 이 이미 밀린 인덱스로 엉뚱한
행을 지울 수 있다 — 그래서 쓰기는 429(쿼터 거부 = 미적용이 확정된 경우)만 재시도한다.
"""

import os
import time
import urllib.parse

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
    """HTTP 비-2xx 또는 전송 실패. 필드: status(int — 전송 실패는 0), reason(str — 응답 JSON
    error.message, 없으면 본문 앞 300자, 전송 실패는 예외 문자열), method(str), url(str)."""

    def __init__(self, status, reason, method, url):
        self.status = status
        self.reason = reason
        self.method = method
        self.url = url
        super().__init__("%s %s -> %s: %s" % (method, url, status, reason))


class SheetsWriteOutcomeUnknown(SheetsApiError):
    """batchUpdate 가 5xx·전송 실패로 끝났다 — 적용됐는지 알 수 없다."""


class _TransportFailure(Exception):
    """단일 요청 시도의 전송 실패(OSError)를 재시도 루프까지 옮기는 내부 신호. 공개 API 아님."""


def is_inside(path, root):
    """realpath·normcase 후 판정. os.path.splitdrive 의 드라이브가 다르면 False(= 밖).
    같으면 os.path.commonpath([p, r]) == r. (commonpath 는 드라이브가 다르면 ValueError — G1 P2-1, 부록 F #13)"""
    p = os.path.normcase(os.path.realpath(path))
    r = os.path.normcase(os.path.realpath(root))
    p_drive, _ = os.path.splitdrive(p)
    r_drive, _ = os.path.splitdrive(r)
    if p_drive != r_drive:
        return False
    return os.path.commonpath([p, r]) == r


def resolve_key_path(repo_root):
    """env KEY_ENV 가 비어 있지 않으면 그 경로, 아니면 DEFAULT_KEY_PATH.
    리포 안이면 거부, 없으면 거부. 파일 내용은 읽지 않는다."""
    env_value = os.environ.get(KEY_ENV, "")
    path = env_value if env_value else DEFAULT_KEY_PATH
    if is_inside(path, repo_root):
        raise SheetsSetupError("키 파일이 리포 안에 있다 — 리포 밖으로 옮겨라(커밋 사고 방지): %s" % path)
    if not os.path.exists(path):
        raise SheetsSetupError(
            "서비스 계정 키가 없다: %s. 키를 이 경로에 두거나 환경변수 FPSR_SHEETS_SA_KEY 에 전체 경로를 넣어라. "
            "1회 설정 = %s §1" % (path, SETUP_DOC))
    return path


def _error_reason(response):
    """SheetsApiError.reason 구성 — 응답 JSON 의 error.message, 없으면 본문 앞 300자."""
    try:
        data = response.json()
    except ValueError:
        return response.text[:300]
    if isinstance(data, dict):
        error = data.get("error")
        if isinstance(error, dict):
            message = error.get("message")
            if message:
                return message
    return response.text[:300]


class SheetsClient:
    def __init__(self, key_path, *, _session=None, _sleep=time.sleep, _auth_error_types=None):
        if _session is None:
            try:
                from google.oauth2 import service_account
                from google.auth.transport.requests import AuthorizedSession
                from google.auth.exceptions import GoogleAuthError
            except ImportError:
                raise SheetsSetupError("python -m pip install -r Scripts/requirements-sheets.txt")
            try:
                credentials = service_account.Credentials.from_service_account_file(
                    key_path, scopes=list(SCOPES))
            except (ValueError, KeyError, OSError) as e:
                raise SheetsSetupError(
                    "키 파일을 읽을 수 없다 — 서비스 계정 JSON 키인지·읽기 권한이 있는지 확인: %s" % e)
            self._session = AuthorizedSession(credentials)
            self._email = credentials.service_account_email
            self._auth_error_types = (GoogleAuthError,)
        else:
            self._session = _session
            self._email = "test@example.invalid"
            self._auth_error_types = _auth_error_types if _auth_error_types is not None else ()
        self._sleep = _sleep

    @property
    def service_account_email(self):
        return self._email

    # ── 내부 ────────────────────────────────────────────────────────────────────────────────────

    def _request_once(self, method, url, **kwargs):
        """단일 시도. try 범위 = 세션 호출 한 줄뿐(§5-1 예외 계약 4) — 응답 본문 파싱은
        호출부가 이 밖에서 한다(JSONDecodeError 가 OSError 파생이라 안에 두면 쓰기 성공이
        '결과 불명'으로 위장된다)."""
        try:
            return self._session.request(method, url, timeout=REQUEST_TIMEOUT_SEC, **kwargs)
        except self._auth_error_types as e:
            raise SheetsSetupError("자격 갱신 실패 — 네트워크 연결 · 키 삭제/무효 · PC 시계를 확인: %s" % e)
        except OSError as e:
            raise _TransportFailure(str(e))

    def _get_with_retry(self, url, params):
        """읽기 재시도 루프 — RETRYABLE_READ_STATUS·전송 실패는 RETRY_DELAYS_SEC 순서로
        대기 후 최대 len(RETRY_DELAYS_SEC)+1 회 시도, 소진 시 마지막 실패로 SheetsApiError."""
        attempts = len(RETRY_DELAYS_SEC) + 1
        last_status = None
        last_reason = None
        for attempt in range(attempts):
            try:
                response = self._request_once("GET", url, params=params)
            except _TransportFailure as failure:
                last_status, last_reason = 0, str(failure)
            else:
                if 200 <= response.status_code < 300:
                    return response
                if response.status_code in RETRYABLE_READ_STATUS:
                    last_status = response.status_code
                    last_reason = _error_reason(response)
                else:
                    raise SheetsApiError(response.status_code, _error_reason(response), "GET", url)
            if attempt < attempts - 1:
                self._sleep(RETRY_DELAYS_SEC[attempt])
        raise SheetsApiError(last_status, last_reason, "GET", url)

    # ── 공개 ────────────────────────────────────────────────────────────────────────────────────

    def get_sheet_properties(self, spreadsheet_id):
        """GET {API_ROOT}/{spreadsheet_id} — 탭 목록. index 오름차순으로 반환."""
        url = "%s/%s" % (API_ROOT, spreadsheet_id)
        params = {"fields": "sheets.properties(sheetId,title,index,gridProperties(rowCount,columnCount))"}
        response = self._get_with_retry(url, params)
        try:
            data = response.json()
        except ValueError:
            raise SheetsApiError(response.status_code,
                                  "응답 JSON 파싱 실패: %s" % response.text[:300], "GET", url)
        result = []
        for entry in data.get("sheets", []):
            props = entry.get("properties", {})
            grid = props.get("gridProperties", {})
            result.append({
                "sheetId": props.get("sheetId"),
                "title": props.get("title"),
                "index": props.get("index"),
                "rowCount": grid.get("rowCount"),
                "columnCount": grid.get("columnCount"),
            })
        result.sort(key=lambda entry: entry["index"])
        return result

    def get_values(self, spreadsheet_id, a1_range):
        """GET {API_ROOT}/{spreadsheet_id}/values/{a1_range}. 모든 셀을 str(v) 로 정규화."""
        url = "%s/%s/values/%s" % (API_ROOT, spreadsheet_id, urllib.parse.quote(a1_range, safe=""))
        params = {"valueRenderOption": "FORMATTED_VALUE", "majorDimension": "ROWS"}
        response = self._get_with_retry(url, params)
        try:
            data = response.json()
        except ValueError:
            raise SheetsApiError(response.status_code,
                                  "응답 JSON 파싱 실패: %s" % response.text[:300], "GET", url)
        values = data.get("values", [])
        return [[str(cell) for cell in row] for row in values]

    def batch_update(self, spreadsheet_id, requests):
        """POST {API_ROOT}/{spreadsheet_id}:batchUpdate. 429 만 재시도(쿼터 거부 = 미적용
        확정) — 5xx·전송 실패는 즉시 SheetsWriteOutcomeUnknown, 그 밖 4xx 는 SheetsApiError."""
        url = "%s/%s:batchUpdate" % (API_ROOT, spreadsheet_id)
        attempts = len(RETRY_DELAYS_SEC) + 1
        for attempt in range(attempts):
            try:
                response = self._request_once("POST", url, json={"requests": requests})
            except _TransportFailure as failure:
                raise SheetsWriteOutcomeUnknown(0, str(failure), "POST", url)

            if 200 <= response.status_code < 300:
                try:
                    return response.json()
                except ValueError:
                    return {}
            if response.status_code == 429:
                if attempt < attempts - 1:
                    self._sleep(RETRY_DELAYS_SEC[attempt])
                    continue
                raise SheetsApiError(429, _error_reason(response), "POST", url)
            if 500 <= response.status_code < 600:
                raise SheetsWriteOutcomeUnknown(response.status_code, _error_reason(response), "POST", url)
            raise SheetsApiError(response.status_code, _error_reason(response), "POST", url)
