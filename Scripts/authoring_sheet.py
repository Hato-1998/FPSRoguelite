#!/usr/bin/env python3
"""저작 시트(Cards / CardCatalog / ST_UI / ST_CardEffect) 행 단위 쓰기 도구.

정본 = 구글 시트. 사람은 시트에서 직접 편집하고, 자동화는 이 도구로 시트에 쓴다.
리포의 Content/**/*.csv 는 스냅샷(정식 pull = sync-authoring-csv.ps1)이다 — 직접
편집하지 않는다(sync 가 정본-위반을 감지하면 덮어쓰기를 거부한다).

명령 4종:
  status : 시트 export · manifest · 리포 CSV 3방향 대조. 자격 불요.
  doctor : 서비스 계정 자격·접근·탭 수·헤더 + 대조군(API 서식값 == export) 점검.
  apply  : 변경셋(JSON, 기존 형식 그대로) → 시트에 행 단위로 쓴다 → 끝에 sync 로
           리포 스냅샷까지 갱신한다.
  seed   : 리포 CSV → 시트 통째 교체(1회 이관·복구 전용 — 시트가 마지막 pull 이후
           무편집일 때, 또는 완전히 빈 새 시트일 때만 돈다).

동시성 방어 — 자동화 세션 둘이 동시에 쓰거나 apply 중 사람이 sync 를 돌리는 경합을
막는다: ~/.fpsr/authoring-sheet.lock 잠금(apply·seed 진입 직후 ~ 마지막 검증, 네트워크
동작마다 진행 신호 갱신) · 변경셋의 선택적 expect · 쓰기 직전 재조회 · 시트당 1회
원자적 batchUpdate · 쓰기 후 되읽기 검증(키 기준 차이 보고).

쓴 값은 전부 문자열로 시트에 들어간다(자동 변환 없음 — encode_cell).

CSV 규약(기존 파이프라인과 동일): UTF-8 BOM 없음 · 헤더는 Config/AuthoringSheets.json
의 expectedHeader 와 정확히 일치.

설정·사용 = Docs/AuthoringSheetWriteback.md. 명세 = Docs/Specs/SHEET1_SheetsApiWritePath.md.
"""

import argparse
import csv
import hashlib
import http.client
import io
import json
import os
import socket
import subprocess
import sys
import time
import traceback
import urllib.request
import uuid
from dataclasses import dataclass

import sheets_api

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCK_PATH = os.path.join(os.path.expanduser("~"), ".fpsr", "authoring-sheet.lock")

# 구조 상수(§9) — API·도구 동작 연동값이지 콘텐츠 조정값이 아니다.
_DEFAULT_MAPPING_PATH = os.path.join(REPO_ROOT, "Config", "AuthoringSheets.json")
_EXPORT_TIMEOUT_SEC = 30
_CONVERGENCE_ATTEMPTS = 15
_CONVERGENCE_INTERVAL_SEC = 2


class PlanError(Exception):
    """쓰기 전에 잡는 변경셋·시트 데이터 오류. 메시지에 시트·키·컬럼을 담는다."""


class LockBusy(Exception):
    """다른 프로세스가 잠금을 쥐고 있다. 메시지에 pid·host·command·started_utc."""


@dataclass
class RowUpdate:
    row_index: int          # 0-based 데이터 행 인덱스(헤더 제외). 그리드 행 = row_index + 1
    key: str
    before: list            # header 폭으로 패딩
    after: list
    changed_cols: list      # 오름차순


@dataclass
class RowAppend:
    key: str
    after: list              # header 폭


@dataclass
class RowDelete:
    row_index: int
    key: str
    before: list


@dataclass
class Plan:
    sheet: str
    key_col: str
    updates: list
    appends: list
    deletes: list
    unchanged: int
    missing_deletes: list

    def touched(self) -> int:
        return len(self.updates) + len(self.appends) + len(self.deletes)


def manifest_path_for(mapping_path: str) -> str:
    root, _ext = os.path.splitext(mapping_path)
    return root + ".manifest.json"


def resolve_target(repo_root: str, target: str) -> str:
    if os.path.isabs(target):
        return target
    return os.path.normpath(os.path.join(repo_root, target))


def export_url(sheet_id: str, gid) -> str:
    url = "https://docs.google.com/spreadsheets/d/%s/export?format=csv" % sheet_id
    if gid:
        url += "&gid=%s" % gid
    return url


def parse_csv_bytes(raw: bytes) -> list:
    text = raw.decode("utf-8")
    return list(csv.reader(io.StringIO(text, newline="")))


def normalize_rows(rows: list) -> list:
    trimmed = []
    for row in rows:
        r = list(row)
        while r and r[-1] == "":
            r.pop()
        trimmed.append(r)
    while trimmed and trimmed[-1] == []:
        trimmed.pop()
    return trimmed


def rows_equal(a: list, b: list) -> bool:
    return normalize_rows(a) == normalize_rows(b)


def sha256_bytes(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest().upper()


def classify_status(sheet_state: str, repo_state: str) -> str:
    if sheet_state == "NO-PROV" or repo_state == "NO-PROV":
        return "NO-PROV"
    if repo_state == "MISSING":
        return "MISSING"
    if sheet_state == "FETCH-FAIL":
        return "UNKNOWN"
    if sheet_state == "SAME" and repo_state == "SAME":
        return "IN-SYNC"
    if sheet_state == "CHANGED" and repo_state == "SAME":
        return "SHEET-AHEAD"
    if sheet_state == "SAME" and repo_state == "CHANGED":
        return "LOCAL-AHEAD"
    if sheet_state == "CHANGED" and repo_state == "CHANGED":
        return "DIVERGED"
    raise ValueError("classify_status: 알 수 없는 조합 sheet_state=%r repo_state=%r" % (sheet_state, repo_state))


def apply_precondition(sheet: str, manifest_sha, repo_sha):
    """None = 통과, 아니면 사람이 읽을 오류 문구."""
    if repo_sha is None:
        return ("[%s] 리포 스냅샷 CSV 가 없다(MISSING) — 시트에 헤더를 넣고 sync 로 첫 스냅샷을 만든 뒤 apply" % sheet)
    if manifest_sha is None:
        return ("[%s] manifest 항목이 없다(NO-PROV) — sync 로 첫 스냅샷을 만든 뒤 apply" % sheet)
    if manifest_sha != repo_sha:
        return ("[%s] 리포 CSV 가 manifest 와 다르다(LOCAL-AHEAD) — 정본은 시트다. "
                 "리포 CSV 를 직접 고쳤다면 git checkout 으로 되돌리고 그 변경을 변경셋으로 옮겨라" % sheet)
    return None


def seed_precondition(sheet: str, manifest_sha, export_sha, current_rows: list, repo_csv_exists: bool):
    """None = 통과, 아니면 사람이 읽을 오류 문구."""
    if not repo_csv_exists:
        return "[%s] seed 원본(리포 CSV)이 없다" % sheet
    if manifest_sha is None:
        if normalize_rows(current_rows) == []:
            return None
        return ("[%s] manifest 항목이 없는데 시트가 비어 있지 않다 — seed 가 사람 편집을 지울 수 있다. "
                 "먼저 sync 로 첫 스냅샷" % sheet)
    if export_sha is None:
        return "[%s] export 를 받지 못했다 — seed 는 시트 무편집을 확인할 수 있을 때만 돈다" % sheet
    if export_sha != manifest_sha:
        return ("[%s] 시트가 마지막 pull 이후 편집됐다 — seed 는 그 편집을 지운다. "
                 "먼저 sync 로 당겨 리포에 합친 뒤 다시" % sheet)
    return None


def validate_changeset(doc: dict, mapping: dict) -> list:
    changes = doc.get("changes")
    if not changes:
        raise PlanError("changes[] 가 없다 — asset_edit.py 형식(edits[])의 변경셋인가?")
    seen = set()
    for change in changes:
        name = change.get("sheet")
        if not name:
            raise PlanError("변경셋 항목에 'sheet' 가 없다: %r" % change)
        if name not in mapping:
            raise PlanError("[%s] 매핑에 없는 시트다. 있는 것: %s" % (name, ", ".join(sorted(mapping))))
        if name in seen:
            raise PlanError("[%s] 한 변경셋에 같은 시트 항목이 둘 — 하나로 합칠 것" % name)
        seen.add(name)
    return changes


def plan_changes(sheet: str, header: list, data_rows: list, change: dict) -> Plan:
    key_col = change.get("key", header[0])
    if key_col not in header:
        raise PlanError("[%s] 키 컬럼 '%s' 이 헤더에 없다." % (sheet, key_col))
    key_idx = header.index(key_col)

    for i, row in enumerate(data_rows):
        if len(row) > len(header) and any(cell != "" for cell in row[len(header):]):
            raise PlanError("[%s] 헤더 밖 셀에 값이 있다: 데이터 행 %d" % (sheet, i + 1))

    counts = {}
    for row in data_rows:
        if len(row) > key_idx and row[key_idx]:
            counts[row[key_idx]] = counts.get(row[key_idx], 0) + 1
    dupes = sorted(k for k, c in counts.items() if c > 1)
    if dupes:
        raise PlanError("[%s] 중복 키: %s" % (sheet, ", ".join(dupes)))

    index = {}
    for i, row in enumerate(data_rows):
        if len(row) > key_idx and row[key_idx]:
            index.setdefault(row[key_idx], i)

    touched_existing = {}   # row_index -> {"key","before","current"} — current 는 이 변경 안의 누적 병합 상태
    appends_map = {}        # key -> RowAppend (dict 삽입 순 = 등장 순)
    upsert_seen_keys = set()

    for record in change.get("upsert", []):
        unknown = [c for c in record if c != "expect" and c not in header]
        if unknown:
            raise PlanError("[%s] 헤더에 없는 컬럼: %s" % (sheet, ", ".join(unknown)))
        key_value = record.get(key_col)
        if not key_value:
            raise PlanError("[%s] upsert 레코드에 키('%s') 가 없다: %r" % (sheet, key_col, record))
        upsert_seen_keys.add(key_value)
        expect = record.get("expect") or {}
        unknown_expect = [c for c in expect if c not in header]
        if unknown_expect:
            raise PlanError("[%s] expect 에 헤더에 없는 컬럼: %s" % (sheet, ", ".join(unknown_expect)))

        if key_value in index:
            row_i = index[key_value]
            if row_i not in touched_existing:
                before_row = list(data_rows[row_i]) + [""] * (len(header) - len(data_rows[row_i]))
                touched_existing[row_i] = {"key": key_value, "before": before_row, "current": list(before_row)}
            state = touched_existing[row_i]
            current = state["current"]
            for col, expected_value in expect.items():
                col_idx = header.index(col)
                if current[col_idx] != expected_value:
                    raise PlanError("[%s] expect 불일치 %s.%s: 현재='%s' 기대='%s'"
                                     % (sheet, key_value, col, current[col_idx], expected_value))
            for col, value in record.items():
                if col == "expect":
                    continue
                current[header.index(col)] = value
        else:
            if expect:
                raise PlanError("[%s] expect 가 있는데 행이 없다: %s" % (sheet, key_value))
            if key_value in appends_map:
                after = appends_map[key_value].after
                for col, value in record.items():
                    if col == "expect":
                        continue
                    after[header.index(col)] = value
            else:
                after = [record.get(c, "") for c in header]
                appends_map[key_value] = RowAppend(key=key_value, after=after)

    unchanged = 0
    updates = []
    for row_i in sorted(touched_existing):
        state = touched_existing[row_i]
        before_row = state["before"]
        after_row = state["current"]
        if after_row == before_row:
            unchanged += 1
            continue
        changed_cols = [i for i in range(len(header)) if after_row[i] != before_row[i]]
        updates.append(RowUpdate(row_index=row_i, key=state["key"], before=before_row,
                                   after=after_row, changed_cols=changed_cols))

    deletes = []
    missing_deletes = []
    seen_delete_keys = set()
    for key in change.get("delete", []):
        # 같은 키가 두 번 오면 한 번만 — 안 그러면 같은 row_index 삭제 요청이 둘 생겨 두 번째가
        # 밀려 올라온 다음 행을 지운다(종전 cmd_apply 도 set 으로 중복을 없앴다, 부록 I-17).
        if key in seen_delete_keys:
            continue
        seen_delete_keys.add(key)
        if key in upsert_seen_keys:
            raise PlanError("[%s] 같은 변경에서 upsert 와 delete 가 겹친다: %s" % (sheet, key))
        if key in index:
            row_i = index[key]
            before_row = list(data_rows[row_i]) + [""] * (len(header) - len(data_rows[row_i]))
            deletes.append(RowDelete(row_index=row_i, key=key, before=before_row))
        else:
            missing_deletes.append(key)
    deletes.sort(key=lambda d: d.row_index)

    return Plan(sheet=sheet, key_col=key_col, updates=updates, appends=list(appends_map.values()),
                deletes=deletes, unchanged=unchanged, missing_deletes=missing_deletes)


def expected_after(header: list, data_rows: list, plan: Plan) -> list:
    padded = [list(row) + [""] * (len(header) - len(row)) for row in data_rows]
    for u in plan.updates:
        padded[u.row_index] = list(u.after)
    delete_indexes = {d.row_index for d in plan.deletes}
    result = [row for i, row in enumerate(padded) if i not in delete_indexes]
    for a in plan.appends:
        result.append(list(a.after))
    return [list(header)] + result


def keyed_diff(key_col: str, expected: list, actual: list) -> list:
    exp_header = expected[0] if expected else []
    act_header = actual[0] if actual else []
    exp_rows = expected[1:] if expected else []
    act_rows = actual[1:] if actual else []

    def _index_by_key(header, rows):
        # 반환: {key: row}, [등장 순 키(첫 등장만)], [중복 키]. key_col 이 헤더에 없으면 전부 빈 값.
        by_key, order, dupes = {}, [], []
        if key_col not in header:
            return by_key, order, dupes
        key_idx = header.index(key_col)
        for row in rows:
            if key_idx >= len(row) or row[key_idx] == "":
                continue
            key = row[key_idx]
            if key in by_key:
                if key not in dupes:
                    dupes.append(key)
                continue
            by_key[key] = row
            order.append(key)
        return by_key, order, dupes

    lines = []
    if exp_header != act_header:
        only_exp = [c for c in exp_header if c not in act_header]
        only_act = [c for c in act_header if c not in exp_header]
        lines.append("  ! 헤더: 기대에만=%s 실제에만=%s" % (", ".join(only_exp), ", ".join(only_act)))

    exp_by_key, exp_order, exp_dupes = _index_by_key(exp_header, exp_rows)
    act_by_key, act_order, act_dupes = _index_by_key(act_header, act_rows)

    # 어느 표의 중복인지가 정보다 — 표마다 따로 찍는다(부록 I-16).
    for key in exp_dupes:
        lines.append("  ! 중복 키(기대): %s" % key)
    for key in act_dupes:
        lines.append("  ! 중복 키(실제): %s" % key)

    common_cols = [c for c in exp_header if c in act_header]

    for key in exp_order:
        if key not in act_by_key:
            lines.append("  ! 기대에만 있는 키: %s" % key)
            continue
        exp_row = exp_by_key[key]
        act_row = act_by_key[key]
        for col in common_cols:
            e_idx = exp_header.index(col)
            a_idx = act_header.index(col)
            e_val = exp_row[e_idx] if e_idx < len(exp_row) else ""
            a_val = act_row[a_idx] if a_idx < len(act_row) else ""
            if e_val != a_val:
                lines.append("  ! %s.%s: 기대='%s' 실제='%s'" % (key, col, e_val, a_val))

    for key in act_order:
        if key not in exp_by_key:
            lines.append("  ! 실제에만 있는 키: %s" % key)

    return lines


def encode_cell(value: str) -> dict:
    if value == "":
        return {}
    return {"userEnteredValue": {"stringValue": value}}


def build_apply_requests(sheet_gid: int, plan: Plan) -> list:
    requests = []
    # ① 수정 — 바뀐 칸만, 칸 하나당 1요청(사람이 입력한 나머지 칸의 형식을 건드리지 않는다)
    for u in plan.updates:
        for c in u.changed_cols:
            requests.append({
                "updateCells": {
                    "range": {
                        "sheetId": sheet_gid,
                        "startRowIndex": u.row_index + 1,
                        "endRowIndex": u.row_index + 2,
                        "startColumnIndex": c,
                        "endColumnIndex": c + 1,
                    },
                    "rows": [{"values": [encode_cell(u.after[c])]}],
                    "fields": "userEnteredValue",
                }
            })
    # ② 삭제 — row_index 내림차순(뒤에서부터 지워야 앞 인덱스가 안 밀린다)
    for d in sorted(plan.deletes, key=lambda item: item.row_index, reverse=True):
        requests.append({
            "deleteDimension": {
                "range": {
                    "sheetId": sheet_gid,
                    "dimension": "ROWS",
                    "startIndex": d.row_index + 1,
                    "endIndex": d.row_index + 2,
                }
            }
        })
    # ③ 추가 — 있으면 1요청
    if plan.appends:
        requests.append({
            "appendCells": {
                "sheetId": sheet_gid,
                "rows": [{"values": [encode_cell(v) for v in a.after]} for a in plan.appends],
                "fields": "userEnteredValue",
            }
        })
    return requests


def build_seed_requests(sheet_gid: int, grid_rows: int, grid_cols: int,
                          current_rows: list, new_rows: list) -> list:
    h = max(len(current_rows), len(new_rows))
    w = max(max((len(r) for r in current_rows), default=0),
            max((len(r) for r in new_rows), default=0))
    requests = []
    if h > grid_rows:
        requests.append({"appendDimension": {"sheetId": sheet_gid, "dimension": "ROWS", "length": h - grid_rows}})
    if w > grid_cols:
        requests.append({"appendDimension": {"sheetId": sheet_gid, "dimension": "COLUMNS", "length": w - grid_cols}})

    # new_rows 를 H×W 로 패딩("")해 전 칸을 명시 — 옛 값이 남는 칸이 없게 한다.
    padded = []
    for i in range(h):
        row = new_rows[i] if i < len(new_rows) else []
        padded.append(list(row) + [""] * (w - len(row)))

    requests.append({
        "updateCells": {
            "range": {"sheetId": sheet_gid, "startRowIndex": 0, "endRowIndex": h,
                       "startColumnIndex": 0, "endColumnIndex": w},
            "rows": [{"values": [encode_cell(v) for v in padded_row]} for padded_row in padded],
            "fields": "userEnteredValue",
        }
    })
    return requests


def acquire_lock(lock_path: str, command: str, *, stale_after_sec: int = 900, _now=time.time) -> str:
    dirname = os.path.dirname(lock_path)
    if dirname:
        os.makedirs(dirname, exist_ok=True)

    def _try_create():
        token = uuid.uuid4().hex
        payload = {
            "pid": os.getpid(),
            "host": socket.gethostname(),
            "command": command,
            "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "token": token,
        }
        fd = os.open(lock_path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
        try:
            os.write(fd, json.dumps(payload, ensure_ascii=True).encode("utf-8"))
        finally:
            os.close(fd)
        return token

    try:
        return _try_create()
    except FileExistsError:
        try:
            with io.open(lock_path, encoding="utf-8") as f:
                content = f.read()
        except OSError:
            content = ""
        try:
            mtime = os.path.getmtime(lock_path)
        except OSError:
            mtime = 0
        if mtime < _now() - stale_after_sec:
            print("WARN 15분간 진행 신호가 없는 잠금을 치웠다: %s" % content)
            try:
                os.remove(lock_path)
            except FileNotFoundError:
                pass   # 다른 프로세스가 먼저 치운 것
            try:
                return _try_create()
            except FileExistsError:
                raise LockBusy("다른 세션이 시트를 쓰는 중이다: %s — 끝난 뒤 다시" % content)
        raise LockBusy("다른 세션이 시트를 쓰는 중이다: %s — 끝난 뒤 다시" % content)


def touch_lock(lock_path: str) -> None:
    os.utime(lock_path, None)   # 파일이 없으면 FileNotFoundError 를 그대로 올린다 — 잠금을 잃은 것이다.


def release_lock(lock_path: str) -> None:
    try:
        os.remove(lock_path)
    except FileNotFoundError:
        pass


# ── 내부 헬퍼(명령 구현용 — 부록 I-11) ──────────────────────────────────────────────────────────────

def _load_mapping(mapping_path: str) -> dict:
    with io.open(mapping_path, encoding="utf-8") as f:
        sheets = json.load(f)["sheets"]
    return {s["name"]: s for s in sheets}


def _load_manifest(manifest_path: str) -> dict:
    if not os.path.exists(manifest_path):
        return {}
    with io.open(manifest_path, encoding="utf-8") as f:
        return {e["name"]: e for e in json.load(f)}


def _manifest_sha(manifest: dict, name: str):
    """없으면 None. sha 비교는 양쪽 대문자 정규화(I-5)."""
    entry = manifest.get(name)
    if not entry:
        return None
    sha = entry.get("sha256")
    return sha.upper() if sha else None


def _repo_csv_sha(path: str):
    """리포 스냅샷 CSV 가 없으면 None."""
    if not os.path.exists(path):
        return None
    with io.open(path, "rb") as f:
        return sha256_bytes(f.read())


def _fetch_export(sheet: dict):
    """export CSV 원시 바이트. 실패(전송 오류·타임아웃·빈 응답)하면 None.
    stdlib 만 쓴다 — status 등은 google-auth·requests 없이도 돌아야 한다(I-1). 재시도 없음."""
    url = export_url(sheet["sheetId"], sheet.get("gid"))
    try:
        with urllib.request.urlopen(url, timeout=_EXPORT_TIMEOUT_SEC) as response:
            data = response.read()
    except (OSError, http.client.HTTPException):
        return None
    return data if data else None


def _a1_sheet_range(title: str) -> str:
    """탭 전체를 가리키는 A1 범위 — 시트 이름의 홀따옴표만 이스케이프한다."""
    return "'%s'" % title.replace("'", "''")


def _get_sheet_table(client, spreadsheet_id: str, title: str) -> list:
    return client.get_values(spreadsheet_id, _a1_sheet_range(title))


def _run_sync(sheet_name: str, mapping_path: str, token: str):
    """§6 apply 6-b — sync-authoring-csv.ps1 을 서브프로세스로 부른다. 시작 직전 touch_lock 은
    호출부 책임(네트워크 동작 직전마다 갱신, §5-2)."""
    script = os.path.join(REPO_ROOT, "Scripts", "sync-authoring-csv.ps1")
    return subprocess.run(
        ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script,
         "-SheetName", sheet_name, "-MappingPath", mapping_path],
        cwd=REPO_ROOT, capture_output=True, text=True, errors="replace",
        env={**os.environ, "FPSR_AUTHORING_LOCK_TOKEN": token},
    )


def _exit3_notice() -> int:
    """종료 3 직전 stderr 한 줄(I-9) — 호출부는 `return _exit3_notice()` 로 쓴다."""
    sys.stderr.write("사람 확인 필요 — 시트가 이미 바뀌었을 수 있다: python Scripts/authoring_sheet.py status 로 확인 후 sync\n")
    return 3


def _run_guarded(run, *run_args) -> int:
    """apply·seed 본문 실행기 — 첫 batch_update 를 **시도한 뒤** 새어 나온 예기치 않은 예외(자격 갱신 실패 ·
    잠금 파일 소실 · powershell 실행 실패 · 코드 버그)는 시트가 이미 바뀌었을 수 있으므로 종료 3 이다(§5-2,
    부록 I-18). 시도 전이면 그대로 올려 main() 의 분류(설정 2 · 그 밖 1)를 따른다."""
    progress = {"write_attempted": False}
    try:
        return run(*run_args, progress)
    except Exception:
        if not progress["write_attempted"]:
            raise
        traceback.print_exc()
        return _exit3_notice()


# ── status ──────────────────────────────────────────────────────────────────────────────────────

def _cmd_status(args) -> int:
    """자격 불요. 시트 export · manifest · 리포 CSV 3방향 대조(§6 status, I-5)."""
    mapping = _load_mapping(args.mapping)
    manifest = _load_manifest(manifest_path_for(args.mapping))

    print("%-14s %-11s %-10s %-12s %s" % ("SHEET", "SHEET-STATE", "REPO-STATE", "OVERALL", "TARGET"))
    for name in sorted(mapping):
        sheet = mapping[name]
        path = resolve_target(REPO_ROOT, sheet["target"])
        repo_sha = _repo_csv_sha(path)
        manifest_sha = _manifest_sha(manifest, name)

        if repo_sha is None:
            repo_state = "MISSING"
        elif manifest_sha is None:
            repo_state = "NO-PROV"
        elif repo_sha == manifest_sha:
            repo_state = "SAME"
        else:
            repo_state = "CHANGED"

        if manifest_sha is None:
            sheet_state = "NO-PROV"          # GET 생략(I-5)
        else:
            raw = _fetch_export(sheet)
            if raw is None:
                sheet_state = "FETCH-FAIL"
            else:
                sheet_state = "SAME" if sha256_bytes(raw) == manifest_sha else "CHANGED"

        overall = classify_status(sheet_state, repo_state)
        print("%-14s %-11s %-10s %-12s %s" % (name, sheet_state, repo_state, overall, sheet["target"]))

    legend = [
        ("IN-SYNC", "시트 · 스냅샷(manifest) · 리포 CSV 일치."),
        ("SHEET-AHEAD", "시트에 스냅샷에 없는 편집이 있다 → powershell -File Scripts\\sync-authoring-csv.ps1 -SheetName <이름>"),
        ("LOCAL-AHEAD", "리포 CSV 를 직접 고쳤다(정본 위반) → 그 변경을 변경셋으로 옮겨 apply 하고 리포 파일은 git checkout 으로 되돌린다. (이관 전이면 seed)"),
        ("DIVERGED", "둘 다 바뀌었다 → 사람이 판단한다. 시트 편집을 먼저 살릴 것."),
        ("NO-PROV", "manifest 기록이 없다 → 새 테이블이면 시트에 헤더를 넣고 sync 로 첫 스냅샷."),
        ("MISSING", "리포 스냅샷 CSV 가 없다 → sync 로 받는다."),
        ("UNKNOWN", "시트 export 를 받지 못했다(네트워크·공유 설정) → 다시 시도하고, 계속되면 시트 공개 링크가 \"뷰어\"인지 확인."),
    ]
    print()
    for label, text in legend:
        print("%-12s= %s" % (label, text))
    return 0


# ── doctor ──────────────────────────────────────────────────────────────────────────────────────

def _cmd_doctor(args) -> int:
    """자격 필요. 서비스 계정 접근·탭 수·헤더 + 대조군(CONTROL) 점검(§6 doctor, I-6)."""
    mapping = _load_mapping(args.mapping)
    key_path = sheets_api.resolve_key_path(REPO_ROOT)   # SheetsSetupError 는 main() 이 2 로 처리
    client = sheets_api.SheetsClient(key_path)

    print("service account: %s" % client.service_account_email)

    has_bad = False
    for name in sorted(mapping):
        sheet = mapping[name]
        access_ok = True
        tabs = None
        header_state = "-"
        control_state = "-"

        try:
            props = client.get_sheet_properties(sheet["sheetId"])
        except sheets_api.SheetsApiError:
            access_ok = False
            props = None

        if access_ok:
            tabs = len(props)
            if tabs == 1:
                title = props[0]["title"]
                try:
                    api_values = _get_sheet_table(client, sheet["sheetId"], title)
                except sheets_api.SheetsApiError:
                    # 속성은 읽혔는데 값을 못 읽으면 접근 실패로 보고 다음 시트로 — 한 시트 때문에 보고 전체가 끊기지 않게.
                    access_ok, tabs = False, None
                if access_ok:
                    api_header = api_values[0] if api_values else []
                    header_state = "OK" if api_header == sheet["expectedHeader"] else "MISMATCH"
                    raw = _fetch_export(sheet)
                    if raw is None:
                        control_state = "-"
                    else:
                        control_state = "OK" if rows_equal(api_values, parse_csv_bytes(raw)) else "FAIL"

        print("%-14s ACCESS %s · TABS %s · HEADER %s · CONTROL %s" % (
            name, "OK" if access_ok else "FAIL", tabs if tabs is not None else "-",
            header_state, control_state))

        if (not access_ok) or (tabs is not None and tabs != 1) or control_state in ("FAIL", "-"):
            has_bad = True

    return 1 if has_bad else 0


# ── apply ───────────────────────────────────────────────────────────────────────────────────────

def _print_plan(name: str, header: list, plan: Plan) -> None:
    print("[%s] PLAN created=%d updated=%d unchanged=%d deleted=%d"
          % (name, len(plan.appends), len(plan.updates), plan.unchanged, len(plan.deletes)))
    for u in plan.updates:
        for c in u.changed_cols:
            print("  ~ %s.%s: '%s' -> '%s'" % (u.key, header[c], u.before[c], u.after[c]))
    for a in plan.appends:
        print("  + %s" % a.key)
    for d in plan.deletes:
        print("  - %s" % d.key)
    if plan.missing_deletes:
        print("  ? 지울 키가 이미 없음(무시): %s" % ", ".join(sorted(plan.missing_deletes)))


def _print_keyed_diff(key_col: str, expected: list, actual: list) -> None:
    """되읽기·재조회 불일치 출력 — 호출부 규칙(P3-C): 위치는 다른데 키 기준 차이가 없으면
    빈 줄 대신 그 사실을 알린다(행이 밀려도 사람이 읽을 수 있게, G1 P2-3·P3-6)."""
    diff = keyed_diff(key_col, expected, actual)
    if diff:
        for line in diff:
            print(line)
    else:
        print("  (키 기준 내용 동일 — 행 순서·빈 행만 다름)")


def _cmd_apply(args) -> int:
    command = " ".join(sys.argv[1:])
    token = acquire_lock(LOCK_PATH, command)   # 매핑·manifest·리포 CSV 를 읽기 전에(G1 2회차 P3-A)
    try:
        return _run_guarded(_run_apply, args, token)
    finally:
        release_lock(LOCK_PATH)


def _run_apply(args, token: str, progress: dict) -> int:
    mapping = _load_mapping(args.mapping)
    manifest = _load_manifest(manifest_path_for(args.mapping))
    with io.open(args.changeset, encoding="utf-8") as f:
        doc = json.load(f)
    try:
        changes = validate_changeset(doc, mapping)
    except PlanError as e:
        sys.stderr.write("%s\n" % e)
        return 1

    # 전제조건 — dry-run 은 WARN 후 계속, 실행은 1(§6 apply 표).
    precondition_failed = False
    for change in changes:
        name = change["sheet"]
        sheet = mapping[name]
        path = resolve_target(REPO_ROOT, sheet["target"])
        err = apply_precondition(name, _manifest_sha(manifest, name), _repo_csv_sha(path))
        if err:
            precondition_failed = True
            sys.stderr.write(("WARN %s\n" % err) if args.dry_run else ("%s\n" % err))
    if precondition_failed and not args.dry_run:
        return 1

    client = sheets_api.SheetsClient(sheets_api.resolve_key_path(REPO_ROOT))

    # 3. 모든 대상 시트를 먼저 읽고 계획한다(변경셋 순서) — 이 시점까지 아무것도 쓰지 않는다.
    sheet_states = []
    for change in changes:
        name = change["sheet"]
        sheet = mapping[name]

        touch_lock(LOCK_PATH)
        try:
            props = client.get_sheet_properties(sheet["sheetId"])
        except sheets_api.SheetsApiError as e:
            sys.stderr.write("[%s] %s\n" % (name, e))
            return 1
        if len(props) != 1:
            sys.stderr.write("[%s] 탭이 1개가 아니다: %d개\n" % (name, len(props)))
            return 1
        gid = props[0]["sheetId"]
        title = props[0]["title"]

        touch_lock(LOCK_PATH)
        try:
            table = _get_sheet_table(client, sheet["sheetId"], title)
        except sheets_api.SheetsApiError as e:
            sys.stderr.write("[%s] %s\n" % (name, e))
            return 1
        header = table[0] if table else []
        if header != sheet["expectedHeader"]:
            sys.stderr.write("[%s] 헤더 불일치: %s\n" % (name, header))
            return 1

        try:
            plan = plan_changes(name, header, table[1:], change)
        except PlanError as e:
            sys.stderr.write("%s\n" % e)
            return 1

        touch_lock(LOCK_PATH)
        raw = _fetch_export(sheet)
        if raw is None:
            sys.stderr.write("WARN [%s] export 실패 — 스냅샷 밖 시트 편집 여부 미확인\n" % name)   # I-8
        elif sha256_bytes(raw) != _manifest_sha(manifest, name):
            sys.stderr.write("WARN [%s] 스냅샷에 없는 시트 편집이 있다 — 이번 pull 에 함께 들어간다\n" % name)

        sheet_states.append({"name": name, "sheet": sheet, "header": header, "gid": gid,
                              "title": title, "plan": plan, "planned_rows": table})

    # 4. 계획 출력.
    for state in sheet_states:
        _print_plan(state["name"], state["header"], state["plan"])
        if args.dry_run and state["plan"].touched() > 0:
            print("[%s] DRY-RUN (쓰지 않음)" % state["name"])
    if args.dry_run:
        return 0
    if all(state["plan"].touched() == 0 for state in sheet_states):
        print("NO-OP — 모든 시트가 이미 목표와 같다")   # I-9
        return 0

    # 5. touched()>0 인 시트마다 순서대로 쓴다.
    wrote_any = False
    written = []
    for state in sheet_states:
        plan = state["plan"]
        if plan.touched() == 0:
            continue
        name, sheet, header = state["name"], state["sheet"], state["header"]
        gid, title = state["gid"], state["title"]

        # a0. 쓰기 직전 재조회.
        touch_lock(LOCK_PATH)
        try:
            current = _get_sheet_table(client, sheet["sheetId"], title)
        except sheets_api.SheetsApiError as e:
            sys.stderr.write("[%s] %s\n" % (name, e))
            return _exit3_notice() if wrote_any else 1
        if not rows_equal(current, state["planned_rows"]):
            _print_keyed_diff(plan.key_col, state["planned_rows"], current)
            sys.stderr.write("[%s] 계획 뒤 시트가 바뀌었다(사람·다른 도구) — 다시 실행\n" % name)
            return _exit3_notice() if wrote_any else 1

        # a. 쓰기.
        requests = build_apply_requests(gid, plan)
        touch_lock(LOCK_PATH)
        progress["write_attempted"] = True
        try:
            client.batch_update(sheet["sheetId"], requests)
        except sheets_api.SheetsWriteOutcomeUnknown:
            touch_lock(LOCK_PATH)
            try:
                readback = _get_sheet_table(client, sheet["sheetId"], title)
            except sheets_api.SheetsApiError as e2:
                print("되읽기 실패: %s" % e2)
            else:
                expected = expected_after(header, state["planned_rows"][1:], plan)
                _print_keyed_diff(plan.key_col, expected, readback)
            return _exit3_notice()
        except sheets_api.SheetsSetupError as e:
            sys.stderr.write("%s\n" % e)
            return _exit3_notice() if wrote_any else 2
        except sheets_api.SheetsApiError as e:
            sys.stderr.write("[%s] %s\n" % (name, e))
            if getattr(e, "status", None) == 403:
                sys.stderr.write("시트를 서비스 계정에 편집자로 공유했는지 확인\n")
            return _exit3_notice() if wrote_any else 1

        wrote_any = True
        print("[%s] WROTE requests=%d" % (name, len(requests)))

        # b. 되읽기.
        touch_lock(LOCK_PATH)
        try:
            readback = _get_sheet_table(client, sheet["sheetId"], title)
        except sheets_api.SheetsApiError as e:
            sys.stderr.write("[%s] 되읽기 실패: %s\n" % (name, e))
            return _exit3_notice()
        expected = expected_after(header, state["planned_rows"][1:], plan)
        if not rows_equal(readback, expected):
            _print_keyed_diff(plan.key_col, expected, readback)
            return _exit3_notice()
        print("[%s] VERIFY OK" % name)
        state["readback"] = readback
        written.append(state)

    # 6. 쓴 시트마다 pull.
    for state in written:
        name, sheet, readback = state["name"], state["sheet"], state["readback"]
        target_path = resolve_target(REPO_ROOT, sheet["target"])

        # a. export 수렴 대기 — 최대 15회 × 2초.
        converged = False
        for _attempt in range(_CONVERGENCE_ATTEMPTS):
            touch_lock(LOCK_PATH)
            raw = _fetch_export(sheet)
            if raw is not None and rows_equal(parse_csv_bytes(raw), readback):
                converged = True
                break
            time.sleep(_CONVERGENCE_INTERVAL_SEC)
        if not converged:
            sys.stderr.write(
                "[%s] 시트엔 반영됨 — 잠시 뒤 powershell -NoProfile -ExecutionPolicy Bypass -File "
                "Scripts\\sync-authoring-csv.ps1 -SheetName %s\n" % (name, name))
            return _exit3_notice()

        # b. sync 서브프로세스.
        touch_lock(LOCK_PATH)
        result = _run_sync(name, args.mapping, token)
        if result.returncode != 0:
            tail = "\n".join((result.stdout + result.stderr).splitlines()[-20:])
            sys.stderr.write("[%s] sync 실패(종료 코드 %d):\n%s\n" % (name, result.returncode, tail))
            return _exit3_notice()

        # c. 사후 대조.
        manifest_now = _load_manifest(manifest_path_for(args.mapping))
        entry_sha = _manifest_sha(manifest_now, name)
        repo_sha = _repo_csv_sha(target_path)
        csv_matches = False
        if repo_sha is not None and entry_sha is not None and repo_sha == entry_sha:
            with io.open(target_path, "rb") as f:
                csv_matches = rows_equal(parse_csv_bytes(f.read()), readback)
        if not csv_matches:
            sys.stderr.write("[%s] sync 가 다른 manifest 를 썼거나 스냅샷이 시트와 다르다\n" % name)
            return _exit3_notice()
        print("[%s] SNAPSHOT OK" % name)

    # 7. 끝 안내.
    print("다음: 카드 시트면 임포터(Tools > FPSR > 카드 CSV 임포트 / -run=FPSRImportCards) · "
          "스냅샷 커밋(CSV + Config/AuthoringSheets.manifest.json)")
    return 0


# ── seed ────────────────────────────────────────────────────────────────────────────────────────

def _cmd_seed(args) -> int:
    mapping = _load_mapping(args.mapping)
    if args.sheet not in mapping:
        sys.stderr.write("[%s] 매핑에 없는 시트다. 있는 것: %s\n" % (args.sheet, ", ".join(sorted(mapping))))
        return 1
    if not args.dry_run and not args.confirm_replace:
        sys.stderr.write(
            "seed 는 시트를 통째로 교체한다 — 확인 없이 실행하려면 --confirm-replace 를 추가해라"
            "(또는 --dry-run 으로 먼저 계획을 본다)\n")
        return 1

    command = " ".join(sys.argv[1:])
    token = acquire_lock(LOCK_PATH, command)   # 인자 확인 뒤(§6 seed 1)
    try:
        return _run_guarded(_run_seed, args, mapping, token)
    finally:
        release_lock(LOCK_PATH)


def _run_seed(args, mapping: dict, token: str, progress: dict) -> int:
    sheet = mapping[args.sheet]
    target_path = resolve_target(REPO_ROOT, sheet["target"])
    repo_csv_exists = os.path.exists(target_path)
    if not repo_csv_exists:
        sys.stderr.write("%s\n" % seed_precondition(args.sheet, None, None, [], False))   # I-7
        return 1

    with io.open(target_path, "rb") as f:
        new_rows = parse_csv_bytes(f.read())
    header = new_rows[0] if new_rows else []
    if header != sheet["expectedHeader"]:
        sys.stderr.write("[%s] 리포 CSV 헤더 불일치: %s\n" % (args.sheet, header))
        return 1

    # 2.
    touch_lock(LOCK_PATH)
    raw = _fetch_export(sheet)
    export_sha = sha256_bytes(raw) if raw is not None else None

    client = sheets_api.SheetsClient(sheets_api.resolve_key_path(REPO_ROOT))
    touch_lock(LOCK_PATH)
    try:
        props = client.get_sheet_properties(sheet["sheetId"])
    except sheets_api.SheetsApiError as e:
        sys.stderr.write("[%s] %s\n" % (args.sheet, e))
        return 1
    if len(props) != 1:
        sys.stderr.write("[%s] 탭이 1개가 아니다: %d개\n" % (args.sheet, len(props)))
        return 1
    gid, title = props[0]["sheetId"], props[0]["title"]
    grid_rows, grid_cols = props[0]["rowCount"], props[0]["columnCount"]

    touch_lock(LOCK_PATH)
    try:
        current_rows = _get_sheet_table(client, sheet["sheetId"], title)
    except sheets_api.SheetsApiError as e:
        sys.stderr.write("[%s] %s\n" % (args.sheet, e))
        return 1

    manifest = _load_manifest(manifest_path_for(args.mapping))
    manifest_sha = _manifest_sha(manifest, args.sheet)
    err = seed_precondition(args.sheet, manifest_sha, export_sha, current_rows, repo_csv_exists)
    if err:
        sys.stderr.write("%s\n" % err)
        return 1
    # export sha == manifest sha 는 "export 가 최신"일 때만 시트 무편집을 뜻한다. 방금 받은 API 현재값과
    # export 내용이 같은지까지 확인해 그 전제를 검사로 바꾼다(추가 네트워크 없음, 부록 I-20).
    if manifest_sha is not None and not rows_equal(parse_csv_bytes(raw), current_rows):
        sys.stderr.write("[%s] export 가 시트 현재값과 다르다(export 반영 지연 또는 방금 편집) — 잠시 뒤 status 로 확인하고 다시\n"
                         % args.sheet)
        return 1

    # 3. 계획 출력.
    key_col = sheet["expectedHeader"][0]
    cur_w = max((len(r) for r in current_rows), default=0)
    new_w = max((len(r) for r in new_rows), default=0)
    print("[%s] SEED 현재 %d×%d → 새 %d×%d" % (args.sheet, len(current_rows), cur_w, len(new_rows), new_w))
    for line in keyed_diff(key_col, new_rows, current_rows):
        print(line)
    if args.dry_run:
        print("[%s] DRY-RUN (쓰지 않음)" % args.sheet)
        return 0

    # 4. 쓰기 직전 재조회.
    touch_lock(LOCK_PATH)
    try:
        now_rows = _get_sheet_table(client, sheet["sheetId"], title)
    except sheets_api.SheetsApiError as e:
        sys.stderr.write("[%s] %s\n" % (args.sheet, e))
        return 1
    if not rows_equal(now_rows, current_rows):
        sys.stderr.write("[%s] 계획 뒤 시트가 바뀌었다(사람·다른 도구) — 다시 실행\n" % args.sheet)
        return 1

    # 5. 쓰기.
    requests = build_seed_requests(gid, grid_rows, grid_cols, current_rows, new_rows)
    touch_lock(LOCK_PATH)
    progress["write_attempted"] = True
    try:
        client.batch_update(sheet["sheetId"], requests)
    except sheets_api.SheetsWriteOutcomeUnknown:
        return _exit3_notice()
    except sheets_api.SheetsSetupError as e:
        sys.stderr.write("%s\n" % e)
        return 2
    except sheets_api.SheetsApiError as e:
        sys.stderr.write("[%s] %s\n" % (args.sheet, e))
        return 1
    print("[%s] WROTE requests=%d" % (args.sheet, len(requests)))

    # 6. 되읽기.
    touch_lock(LOCK_PATH)
    try:
        readback = _get_sheet_table(client, sheet["sheetId"], title)
    except sheets_api.SheetsApiError as e:
        sys.stderr.write("[%s] 되읽기 실패: %s\n" % (args.sheet, e))
        return _exit3_notice()
    if not rows_equal(readback, new_rows):
        _print_keyed_diff(key_col, new_rows, readback)
        return _exit3_notice()
    print("[%s] VERIFY OK" % args.sheet)

    # 7. export 수렴 대기(apply 6-a 와 동일, 비교 대상 = new_rows).
    converged = False
    for _attempt in range(_CONVERGENCE_ATTEMPTS):
        touch_lock(LOCK_PATH)
        raw = _fetch_export(sheet)
        if raw is not None and rows_equal(parse_csv_bytes(raw), new_rows):
            converged = True
            break
        time.sleep(_CONVERGENCE_INTERVAL_SEC)
    if not converged:
        sys.stderr.write(
            "[%s] 시트엔 반영됨 — 잠시 뒤 powershell -NoProfile -ExecutionPolicy Bypass -File "
            "Scripts\\sync-authoring-csv.ps1 -SheetName %s\n" % (args.sheet, args.sheet))
        return _exit3_notice()

    # 8. 여기서만 -Force 로 sync — 6·7 에서 시트==리포 CSV 를 증명했으므로 가드를 우회해도 잃는 게 없다.
    script = os.path.join(REPO_ROOT, "Scripts", "sync-authoring-csv.ps1")
    touch_lock(LOCK_PATH)
    result = subprocess.run(
        ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script,
         "-SheetName", args.sheet, "-MappingPath", args.mapping, "-Force"],
        cwd=REPO_ROOT, capture_output=True, text=True, errors="replace",
        env={**os.environ, "FPSR_AUTHORING_LOCK_TOKEN": token},
    )
    if result.returncode != 0:
        tail = "\n".join((result.stdout + result.stderr).splitlines()[-20:])
        sys.stderr.write("[%s] sync 실패(종료 코드 %d):\n%s\n" % (args.sheet, result.returncode, tail))
        return _exit3_notice()

    # 9. 사후 대조.
    manifest_now = _load_manifest(manifest_path_for(args.mapping))
    entry_sha = _manifest_sha(manifest_now, args.sheet)
    repo_sha = _repo_csv_sha(target_path)
    csv_matches = False
    if repo_sha is not None and entry_sha is not None and repo_sha == entry_sha:
        with io.open(target_path, "rb") as f:
            csv_matches = rows_equal(parse_csv_bytes(f.read()), new_rows)
    if not csv_matches:
        sys.stderr.write("[%s] sync 가 다른 manifest 를 썼거나 스냅샷이 시트와 다르다\n" % args.sheet)
        return _exit3_notice()
    print("[%s] SNAPSHOT OK" % args.sheet)

    # 10. 끝 안내.
    print("스냅샷이 export 원시 바이트(CRLF·끝 줄바꿈 없음)로 다시 쓰였다. git status 엔 M 으로 보일 수 있다. "
          "내용 대조 = git diff --ignore-cr-at-eol --stat -- %s 이 비어야 한다" % sheet["target"])
    return 0


# ── main ────────────────────────────────────────────────────────────────────────────────────────

def main():
    # I-14 — 파이프 출력 인코딩(cp949 등)이 쓰기 뒤 진단 출력(keyed_diff·sync 출력)에서
    # UnicodeEncodeError 로 죽어 종료 3 이 1 로 나가는 것을 막는다. 없거나 거부하면 그대로 둔다.
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors="backslashreplace")
        except (AttributeError, ValueError):
            pass

    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mapping", help="매핑 파일 경로 (기본: Config/AuthoringSheets.json)")
    sub = parser.add_subparsers(dest="command", required=True)

    p_status = sub.add_parser("status", help="시트 export · manifest · 리포 CSV 3방향 대조(자격 불요)")
    p_status.set_defaults(func=_cmd_status)

    p_doctor = sub.add_parser("doctor", help="서비스 계정 자격·접근·헤더·대조군(CONTROL) 점검")
    p_doctor.set_defaults(func=_cmd_doctor)

    p_apply = sub.add_parser("apply", help="변경셋 JSON 을 시트에 행 단위로 적용")
    p_apply.add_argument("changeset")
    p_apply.add_argument("--dry-run", action="store_true", help="쓰지 않고 계획만 출력")
    p_apply.set_defaults(func=_cmd_apply)

    p_seed = sub.add_parser("seed", help="리포 CSV 로 시트를 통째 교체(이관·복구 전용)")
    p_seed.add_argument("--sheet", required=True, help="대상 시트 이름")
    p_seed.add_argument("--confirm-replace", action="store_true", help="시트 전체 교체를 확인")
    p_seed.add_argument("--dry-run", action="store_true", help="쓰지 않고 계획만 출력")
    p_seed.set_defaults(func=_cmd_seed)

    args = parser.parse_args()
    args.mapping = os.path.abspath(args.mapping) if args.mapping else _DEFAULT_MAPPING_PATH

    try:
        exit_code = args.func(args)
    except sheets_api.SheetsSetupError as e:
        sys.stderr.write("%s\n" % e)
        exit_code = 2
    except LockBusy as e:
        sys.stderr.write("%s\n" % e)
        exit_code = 1
    except PlanError as e:
        sys.stderr.write("%s\n" % e)
        exit_code = 1
    except sheets_api.SheetsApiError as e:
        sys.stderr.write("%s\n" % e)
        exit_code = 1
    sys.exit(exit_code)


if __name__ == "__main__":
    main()

