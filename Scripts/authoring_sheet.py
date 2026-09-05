#!/usr/bin/env python3
"""저작 시트(Cards / CardCatalog / ST_*) 를 **사람 손 없이** 고치기 위한 도구.

왜 있는가 — 종전 사슬은 `구글 시트(사람이 손으로 편집) → sync → Content/Authoring/*.csv → 임포터`
였고, 첫 칸이 사람이라 **자동화가 원천적으로 불가능**했다. 카드 물량이 30→70 으로 가는 로드맵에서
그 칸이 병목이다. 이 도구는 그 칸을 둘로 나눈다:

  apply  : 변경셋(JSON) → 리포 CSV        ← 자동화가 쓰는 길. 설정 0, 지금 바로 된다.
  push   : 리포 CSV → 구글 시트(미러)      ← 시트를 계속 보고 싶을 때. 1회 설정 필요(§Docs).
  status : 리포 CSV ↔ 매니페스트 대조      ← 어느 쪽이 앞서 있는지(=덮어쓰면 뭘 잃는지) 본다.

마스터 규칙(중요) — **리포 CSV 가 마스터고 시트는 미러다.** 둘 다 마스터로 두면 lost update 가 난다.
사람이 시트에서 편집했다면 `sync-authoring-csv.ps1` 로 당겨오되, 그 스크립트는 로컬 CSV 가 매니페스트와
다르면(=자동화가 먼저 고쳤으면) 덮어쓰기를 거부한다.

CSV 규약(기존 파이프라인과 동일하게 유지할 것):
  UTF-8 **BOM 없음** · 줄끝 `\\n` · 헤더는 Config/AuthoringSheets.json 의 expectedHeader 와 정확히 일치.
  (한국어 Excel 로 직접 열어 저장하면 CP949 로 파손된다 — Localization.md L-4.)

사용:
  python Scripts/authoring_sheet.py status
  python Scripts/authoring_sheet.py apply Content/Authoring/changesets/<파일>.json [--dry-run]
  python Scripts/authoring_sheet.py push [--sheet Cards]
"""

import argparse
import csv
import hashlib
import io
import json
import os
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAPPING_PATH = os.path.join(REPO_ROOT, "Config", "AuthoringSheets.json")
MANIFEST_PATH = os.path.join(REPO_ROOT, "Config", "AuthoringSheets.manifest.json")
WRITEBACK_PATH = os.path.join(REPO_ROOT, "Config", "AuthoringSheets.writeback.json")


# ── 공통 ────────────────────────────────────────────────────────────────────────────────────────

def load_mapping():
    with io.open(MAPPING_PATH, encoding="utf-8") as f:
        sheets = json.load(f)["sheets"]
    return {s["name"]: s for s in sheets}


def sheet_or_die(mapping, name):
    if name not in mapping:
        die("Config/AuthoringSheets.json 에 '%s' 시트가 없다. 있는 것: %s" % (name, ", ".join(sorted(mapping))))
    return mapping[name]


def die(msg):
    sys.stderr.write("ERROR: %s\n" % msg)
    sys.exit(1)


def read_csv(path, expected_header):
    """헤더를 검증하며 읽는다. 헤더가 어긋나면 즉시 죽는다 — 조용히 잘못된 열에 쓰는 것보다 낫다."""
    if not os.path.exists(path):
        die("대상 CSV 가 없다: %s" % path)
    with io.open(path, encoding="utf-8", newline="") as f:
        rows = list(csv.reader(f))
    if not rows:
        die("대상 CSV 가 비어 있다: %s" % path)
    header = rows[0]
    if header != expected_header:
        die("헤더 불일치: %s\n  파일  : %s\n  기대값: %s" % (path, header, expected_header))
    return header, rows[1:]


def write_csv(path, header, rows):
    """UTF-8(BOM 없음) + LF 로 쓴다. csv 모듈이 필요할 때만 따옴표를 붙인다(기존 파일과 같은 최소 인용)."""
    buf = io.StringIO()
    writer = csv.writer(buf, lineterminator="\n")
    writer.writerow(header)
    for row in rows:
        writer.writerow(row)
    data = buf.getvalue().encode("utf-8")
    with io.open(path, "wb") as f:
        f.write(data)
    return data


def sha256_of(path):
    with io.open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest().upper()


def load_manifest():
    if not os.path.exists(MANIFEST_PATH):
        return {}
    with io.open(MANIFEST_PATH, encoding="utf-8") as f:
        return {e["name"]: e for e in json.load(f)}


# ── apply ───────────────────────────────────────────────────────────────────────────────────────

def cmd_apply(args):
    """변경셋 JSON 을 리포 CSV 에 적용한다.

    변경셋 형식:
      {
        "description": "왜 이 변경인가 (감사 흔적)",
        "changes": [
          { "sheet": "Cards",
            "upsert": [ {"CardId": "...", "AssetName": "...", ...} ],   // 키가 있으면 병합, 없으면 새 행
            "delete": ["DA_CardModifiers_SniperScope"] }                // 키 값 목록
        ]
      }
    키 컬럼 = 헤더의 첫 컬럼(Cards=CardId · CardCatalog=AttrId · ST_*=Key). 필요하면 "key" 로 덮어쓴다.
    upsert 는 **병합**이다 — 준 컬럼만 덮어쓰고 나머지는 보존한다(새 행이면 빈 문자열).
    """
    mapping = load_mapping()
    with io.open(args.changeset, encoding="utf-8") as f:
        doc = json.load(f)

    total_touched = 0
    for change in doc.get("changes", []):
        name = change.get("sheet")
        if not name:
            die("변경셋 항목에 'sheet' 가 없다.")
        sheet = sheet_or_die(mapping, name)
        path = os.path.join(REPO_ROOT, sheet["target"].replace("/", os.sep))
        expected_header = list(sheet["expectedHeader"])
        header, rows = read_csv(path, expected_header)

        key_col = change.get("key", header[0])
        if key_col not in header:
            die("[%s] 키 컬럼 '%s' 이 헤더에 없다." % (name, key_col))
        key_idx = header.index(key_col)

        index = {}
        for i, row in enumerate(rows):
            if len(row) > key_idx and row[key_idx]:
                index.setdefault(row[key_idx], i)

        created = updated = unchanged = deleted = 0

        for record in change.get("upsert", []):
            unknown = [c for c in record if c not in header]
            if unknown:
                die("[%s] 헤더에 없는 컬럼: %s" % (name, ", ".join(unknown)))
            key_value = record.get(key_col)
            if not key_value:
                die("[%s] upsert 레코드에 키('%s') 가 없다: %r" % (name, key_col, record))

            if key_value in index:
                i = index[key_value]
                before = list(rows[i]) + [""] * (len(header) - len(rows[i]))
                after = list(before)
                for col, value in record.items():
                    after[header.index(col)] = value
                if after == before:
                    unchanged += 1
                else:
                    rows[i] = after
                    updated += 1
            else:
                new_row = [record.get(col, "") for col in header]
                rows.append(new_row)
                index[key_value] = len(rows) - 1
                created += 1

        delete_keys = set(change.get("delete", []))
        if delete_keys:
            kept = []
            for row in rows:
                value = row[key_idx] if len(row) > key_idx else ""
                if value in delete_keys:
                    deleted += 1
                else:
                    kept.append(row)
            missing = delete_keys - {row[key_idx] for row in rows if len(row) > key_idx}
            if missing:
                print("[%s] 지울 키가 이미 없음(무시): %s" % (name, ", ".join(sorted(missing))))
            rows = kept

        touched = created + updated + deleted
        total_touched += touched
        verb = "DRY-RUN" if args.dry_run else "WROTE" if touched else "NO-OP"
        print("[%s] %s  created=%d updated=%d unchanged=%d deleted=%d  -> %s"
              % (name, verb, created, updated, unchanged, deleted, sheet["target"]))

        # 멱등: 바뀐 게 없으면 파일을 건드리지 않는다(mtime 도 안 움직인다).
        if touched and not args.dry_run:
            write_csv(path, header, rows)

    if total_touched and not args.dry_run:
        print("\n다음: 임포터를 돌려 DA 를 갱신하고(Tools>FPSR 카드 CSV 임포트 또는 -run=FPSRImportCards),")
        print("      시트 미러를 맞추려면  python Scripts/authoring_sheet.py push")
    return 0


# ── status ──────────────────────────────────────────────────────────────────────────────────────

def cmd_status(args):
    """리포 CSV 와 매니페스트(마지막으로 시트에서 당겨온 내용)의 해시를 대조한다."""
    mapping = load_mapping()
    manifest = load_manifest()
    print("%-14s %-10s %s" % ("SHEET", "STATE", "TARGET"))
    for name in sorted(mapping):
        sheet = mapping[name]
        path = os.path.join(REPO_ROOT, sheet["target"].replace("/", os.sep))
        if not os.path.exists(path):
            state = "MISSING"
        elif name not in manifest:
            state = "NO-PROV"          # 한 번도 당겨온 적 없음
        elif sha256_of(path) == manifest[name].get("sha256", "").upper():
            state = "IN-SYNC"          # 마지막 pull 이후 로컬 변경 없음
        else:
            state = "LOCAL-AHEAD"      # 자동화(또는 사람)가 리포에서 고쳤다 = pull 하면 잃는다
        print("%-14s %-10s %s" % (name, state, sheet["target"]))
    print("\nLOCAL-AHEAD = 리포 CSV 가 마지막 시트 pull 보다 앞서 있다.")
    print("  → 시트에 반영하려면 push. sync-authoring-csv.ps1 로 당기면 이 변경을 잃는다(스크립트가 막는다).")
    return 0


# ── push ────────────────────────────────────────────────────────────────────────────────────────

def cmd_push(args):
    """리포 CSV 를 Apps Script 웹앱으로 POST 해 시트를 미러링한다.

    설정 파일이 없으면 **무엇을 해야 하는지 알려주고 비-0 으로 죽는다** — 조용히 아무것도 안 하는 것이
    가장 나쁘다(사람은 성공한 줄 안다).
    """
    if not os.path.exists(WRITEBACK_PATH):
        sys.stderr.write(
            "시트 쓰기 설정이 없다: %s\n\n"
            "1회 설정(약 3분) 절차 = Docs/AuthoringSheetWriteback.md\n"
            "  ① 각 시트에서 확장 프로그램 > Apps Script > Scripts/AppsScript/AuthoringSheetWriter.gs 붙여넣기\n"
            "  ② 배포 > 새 배포 > 웹 앱(실행: 나 / 액세스: 링크가 있는 모든 사용자) > URL 복사\n"
            "  ③ Config/AuthoringSheets.writeback.example.json 을 .writeback.json 으로 복사하고 URL·토큰 기입\n"
            "     (실제 파일은 .gitignore 에 있다 — 토큰은 커밋하지 않는다)\n" % WRITEBACK_PATH)
        return 2

    try:
        import urllib.request
    except ImportError:  # pragma: no cover
        die("urllib 을 쓸 수 없다.")

    with io.open(WRITEBACK_PATH, encoding="utf-8") as f:
        config = json.load(f)
    token = config.get("token", "")
    endpoints = config.get("endpoints", {})
    if not token:
        die("%s 에 token 이 비어 있다." % WRITEBACK_PATH)

    mapping = load_mapping()
    names = [args.sheet] if args.sheet else sorted(mapping)
    failures = 0
    for name in names:
        sheet = sheet_or_die(mapping, name)
        url = endpoints.get(name)
        if not url:
            print("[%s] SKIP — writeback 설정에 endpoint 가 없다." % name)
            continue
        path = os.path.join(REPO_ROOT, sheet["target"].replace("/", os.sep))
        with io.open(path, encoding="utf-8", newline="") as f:
            body = f.read()

        payload = json.dumps({"token": token, "sheet": name, "csv": body}).encode("utf-8")
        request = urllib.request.Request(url, data=payload,
                                         headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                result = json.loads(response.read().decode("utf-8"))
        except Exception as error:  # noqa: BLE001 — 어떤 실패든 사람이 읽을 한 줄로 보고한다
            print("[%s] FAIL — %s" % (name, error))
            failures += 1
            continue
        if result.get("ok"):
            print("[%s] OK — %d행 반영" % (name, result.get("rows", -1)))
        else:
            print("[%s] FAIL — %s" % (name, result.get("error", result)))
            failures += 1

    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p_apply = sub.add_parser("apply", help="변경셋 JSON 을 리포 CSV 에 적용")
    p_apply.add_argument("changeset")
    p_apply.add_argument("--dry-run", action="store_true", help="쓰지 않고 결과만 출력")
    p_apply.set_defaults(func=cmd_apply)

    p_status = sub.add_parser("status", help="리포 CSV ↔ 매니페스트 대조")
    p_status.set_defaults(func=cmd_status)

    p_push = sub.add_parser("push", help="리포 CSV → 구글 시트 미러링")
    p_push.add_argument("--sheet", help="시트 이름 하나만 (기본: 전부)")
    p_push.set_defaults(func=cmd_push)

    args = parser.parse_args()
    sys.exit(args.func(args))


if __name__ == "__main__":
    main()
