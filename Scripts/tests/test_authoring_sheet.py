#!/usr/bin/env python3
"""`Scripts/authoring_sheet.py` + `Scripts/sheets_api.py` 오프라인 단위테스트.

명세 = `Docs/Specs/SHEET1_SheetsApiWritePath.md` §12-1(18항목, 전부 필수). 이 파일은 그 표를
그대로 옮긴 것이다 — 클래스/메서드 이름에 항목 번호가 드러난다(예: T09PlanChanges).

네트워크 0. 파일 쓰기는 tempfile 안에서만. google-auth 유무와 무관하게 통과해야 한다(#16 이
`sys.modules` 차단으로 부재를 흉내 낸다 · #17 은 가짜 세션으로 HTTP 자체를 흉내 낸다).

기대값 표기 원칙(작업 지시 원문):
  - 명세가 문구를 축자로 정한 곳(예: apply_precondition·seed_precondition 반환 문구,
    keyed_diff 줄 형식, §5-4 요청 딕셔너리 형태) 만 정확히 비교한다.
  - 명세가 문구를 정하지 않은 곳(PlanError·LockBusy 의 메시지 전문 — 둘 다 클래스 docstring 이
    "메시지에 ~를 담는다" 라고만 적어 내용만 정하고 문구는 안 정했다) 은 예외 타입 + 핵심 토큰
    (시트명·키·컬럼 등) 포함 여부만 본다.
"""

import argparse
import contextlib
import csv
import io
import json
import os
import sys
import tempfile
import time
import types
import unittest
from unittest import mock

_SCRIPTS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _SCRIPTS_DIR not in sys.path:
    sys.path.insert(0, _SCRIPTS_DIR)

import authoring_sheet  # noqa: E402  (sys.path 조작 뒤에 와야 한다)
import sheets_api  # noqa: E402


def _google_auth_importable():
    """§12-1 #17 마지막 서브케이스의 skipUnless 조건 — 실제로 import 가능한지 읽기 전용으로 확인."""
    try:
        import google.oauth2.service_account  # noqa: F401
        import google.auth.transport.requests  # noqa: F401
        import google.auth.exceptions  # noqa: F401
    except ImportError:
        return False
    return True


_HAVE_GOOGLE_AUTH = _google_auth_importable()


def _abs_path(*parts):
    """OS 에 맞는 절대경로 하나를 만든다(실제 파일 존재 여부와 무관 — 문자열 조작 테스트용)."""
    if os.name == "nt":
        return os.path.join("E:" + os.sep, *parts)
    return os.path.join(os.sep, *parts)


# ══════════════════════════════════════════════════════════════════════════════════════════════
# §12-1 #1 — manifest_path_for
# ══════════════════════════════════════════════════════════════════════════════════════════════

class T01ManifestPathFor(unittest.TestCase):
    """os.path.splitext → root + '.manifest.json' (점 하나 — PS 5.1 파생식과 짝을 이루는 계약)."""

    def test_config_mapping_path(self):
        result = authoring_sheet.manifest_path_for(os.path.join("Config", "AuthoringSheets.json"))
        expected = os.path.join("Config", "AuthoringSheets.manifest.json")
        self.assertEqual(result, expected)
        self.assertNotIn("..manifest.json", result)

    def test_absolute_mapping_path(self):
        mapping_path = _abs_path("Repo", "Config", "AuthoringSheets.json")
        result = authoring_sheet.manifest_path_for(mapping_path)
        self.assertEqual(result, os.path.splitext(mapping_path)[0] + ".manifest.json")

    def test_no_extension_input_still_single_dot(self):
        mapping_path = os.path.join("Config", "AuthoringSheets")
        result = authoring_sheet.manifest_path_for(mapping_path)
        self.assertEqual(result, mapping_path + ".manifest.json")


# ══════════════════════════════════════════════════════════════════════════════════════════════
# §12-1 #2 — resolve_target (부록 I-12: 상대 = normpath(join), 절대 = 그대로)
# ══════════════════════════════════════════════════════════════════════════════════════════════

class T02ResolveTarget(unittest.TestCase):
    def test_relative_target_joins_and_normalizes(self):
        repo_root = _abs_path("Git_Project", "FPSRoguelite")
        result = authoring_sheet.resolve_target(repo_root, "Content/Authoring/Cards.csv")
        expected = os.path.normpath(os.path.join(repo_root, "Content/Authoring/Cards.csv"))
        self.assertEqual(result, expected)
        self.assertIn(os.sep, result)  # 매핑의 '/' 가 OS 구분자로 바뀐다

    def test_absolute_target_passthrough_unchanged(self):
        repo_root = _abs_path("Git_Project", "FPSRoguelite")
        absolute = _abs_path("Elsewhere", "x.csv")
        result = authoring_sheet.resolve_target(repo_root, absolute)
        self.assertEqual(result, absolute)


# ══════════════════════════════════════════════════════════════════════════════════════════════
# §12-1 #3 — parse_csv_bytes
# ══════════════════════════════════════════════════════════════════════════════════════════════

class T03ParseCsvBytes(unittest.TestCase):
    def test_crlf_and_lf_give_identical_rows(self):
        crlf = "Key,Value\r\nk1,1\r\nk2,2\r\n".encode("utf-8")
        lf = "Key,Value\nk1,1\nk2,2\n".encode("utf-8")
        self.assertEqual(authoring_sheet.parse_csv_bytes(crlf), authoring_sheet.parse_csv_bytes(lf))
        self.assertEqual(authoring_sheet.parse_csv_bytes(crlf), [["Key", "Value"], ["k1", "1"], ["k2", "2"]])

    def test_quoted_comma_cell(self):
        raw = 'Key,Value\r\nk1,"a,b"\r\n'.encode("utf-8")
        rows = authoring_sheet.parse_csv_bytes(raw)
        self.assertEqual(rows, [["Key", "Value"], ["k1", "a,b"]])

    def test_leading_trailing_whitespace_preserved(self):
        raw = 'Key,Value\r\nk1,"  padded  "\r\n'.encode("utf-8")
        rows = authoring_sheet.parse_csv_bytes(raw)
        self.assertEqual(rows[1][1], "  padded  ")


# ══════════════════════════════════════════════════════════════════════════════════════════════
# §12-1 #4 — rows_equal
# ══════════════════════════════════════════════════════════════════════════════════════════════

class T04RowsEqual(unittest.TestCase):
    def test_trailing_blank_cols_and_blank_rows_ignored(self):
        a = [["Key", "A"], ["k1", "1"], []]
        b = [["Key", "A", ""], ["k1", "1", ""]]
        self.assertTrue(authoring_sheet.rows_equal(a, b))

    def test_real_difference_detected(self):
        a = [["Key", "A"], ["k1", "1"]]
        b = [["Key", "A"], ["k1", "2"]]
        self.assertFalse(authoring_sheet.rows_equal(a, b))

    def test_leading_or_trailing_whitespace_counts_as_difference(self):
        a = [["Key", "A"], ["k1", "1"]]
        self.assertFalse(authoring_sheet.rows_equal(a, [["Key", "A"], ["k1", " 1"]]))
        self.assertFalse(authoring_sheet.rows_equal(a, [["Key", "A"], ["k1", "1 "]]))


# ══════════════════════════════════════════════════════════════════════════════════════════════
# §12-1 #5 — classify_status (§5-3 표 7행 전부 + 우선순위 확인)
# ══════════════════════════════════════════════════════════════════════════════════════════════

class T05ClassifyStatus(unittest.TestCase):
    def test_in_sync(self):
        self.assertEqual(authoring_sheet.classify_status("SAME", "SAME"), "IN-SYNC")

    def test_sheet_ahead(self):
        self.assertEqual(authoring_sheet.classify_status("CHANGED", "SAME"), "SHEET-AHEAD")

    def test_local_ahead(self):
        self.assertEqual(authoring_sheet.classify_status("SAME", "CHANGED"), "LOCAL-AHEAD")

    def test_diverged(self):
        self.assertEqual(authoring_sheet.classify_status("CHANGED", "CHANGED"), "DIVERGED")

    def test_repo_missing(self):
        self.assertEqual(authoring_sheet.classify_status("SAME", "MISSING"), "MISSING")

    def test_sheet_fetch_fail_becomes_unknown(self):
        self.assertEqual(authoring_sheet.classify_status("FETCH-FAIL", "SAME"), "UNKNOWN")

    def test_no_prov_either_side(self):
        self.assertEqual(authoring_sheet.classify_status("NO-PROV", "SAME"), "NO-PROV")
        self.assertEqual(authoring_sheet.classify_status("SAME", "NO-PROV"), "NO-PROV")

    def test_no_prov_takes_precedence_over_missing_and_changed(self):
        # 표의 첫 조건("어느 쪽이든 NO-PROV")이 그 아래 모든 조건보다 먼저 온다.
        self.assertEqual(authoring_sheet.classify_status("NO-PROV", "CHANGED"), "NO-PROV")
        self.assertEqual(authoring_sheet.classify_status("CHANGED", "NO-PROV"), "NO-PROV")

    def test_missing_takes_precedence_over_fetch_fail(self):
        # repo_state==MISSING 이 sheet_state==FETCH-FAIL 보다 먼저 검사된다(표 순서).
        self.assertEqual(authoring_sheet.classify_status("FETCH-FAIL", "MISSING"), "MISSING")


# ══════════════════════════════════════════════════════════════════════════════════════════════
# §12-1 #6 — apply_precondition (문구 축자 고정 — §5-3)
# ══════════════════════════════════════════════════════════════════════════════════════════════

class T06ApplyPrecondition(unittest.TestCase):
    def test_missing_when_repo_sha_none(self):
        result = authoring_sheet.apply_precondition("Cards", "ABC", None)
        self.assertEqual(
            result,
            "[Cards] 리포 스냅샷 CSV 가 없다(MISSING) — 시트에 헤더를 넣고 sync 로 첫 스냅샷을 만든 뒤 apply")

    def test_no_prov_when_manifest_sha_none(self):
        result = authoring_sheet.apply_precondition("Cards", None, "ABC")
        self.assertEqual(result, "[Cards] manifest 항목이 없다(NO-PROV) — sync 로 첫 스냅샷을 만든 뒤 apply")

    def test_local_ahead_when_shas_differ(self):
        result = authoring_sheet.apply_precondition("Cards", "AAA", "BBB")
        self.assertEqual(
            result,
            "[Cards] 리포 CSV 가 manifest 와 다르다(LOCAL-AHEAD) — 정본은 시트다. "
            "리포 CSV 를 직접 고쳤다면 git checkout 으로 되돌리고 그 변경을 변경셋으로 옮겨라")

    def test_passes_when_shas_match(self):
        self.assertIsNone(authoring_sheet.apply_precondition("Cards", "SAMESHA", "SAMESHA"))

    def test_missing_takes_precedence_over_no_prov(self):
        # repo_sha is None 검사가 먼저다 — manifest_sha 도 None 이어도 MISSING 문구가 나온다.
        result = authoring_sheet.apply_precondition("Cards", None, None)
        self.assertEqual(
            result,
            "[Cards] 리포 스냅샷 CSV 가 없다(MISSING) — 시트에 헤더를 넣고 sync 로 첫 스냅샷을 만든 뒤 apply")


# ══════════════════════════════════════════════════════════════════════════════════════════════
# §12-1 #7 — seed_precondition (문구 축자 고정 — §5-3)
# ══════════════════════════════════════════════════════════════════════════════════════════════

class T07SeedPrecondition(unittest.TestCase):
    def test_csv_missing(self):
        result = authoring_sheet.seed_precondition("Cards", None, None, [], False)
        self.assertEqual(result, "[Cards] seed 원본(리포 CSV)이 없다")

    def test_no_manifest_but_completely_empty_sheet_passes(self):
        # normalize_rows([]) == [] · normalize_rows([[]]) == [] (후행 빈 행 제거) 모두 "완전히 빈" 이다.
        self.assertIsNone(authoring_sheet.seed_precondition("Cards", None, None, [], True))
        self.assertIsNone(authoring_sheet.seed_precondition("Cards", None, None, [[]], True))

    def test_no_manifest_and_non_empty_sheet_blocks(self):
        result = authoring_sheet.seed_precondition("Cards", None, None, [["Key"], ["k1"]], True)
        self.assertEqual(
            result,
            "[Cards] manifest 항목이 없는데 시트가 비어 있지 않다 — seed 가 사람 편집을 지울 수 있다. "
            "먼저 sync 로 첫 스냅샷")

    def test_export_fetch_failed(self):
        result = authoring_sheet.seed_precondition("Cards", "ABC", None, [["Key"]], True)
        self.assertEqual(result, "[Cards] export 를 받지 못했다 — seed 는 시트 무편집을 확인할 수 있을 때만 돈다")

    def test_export_sha_differs_from_manifest(self):
        result = authoring_sheet.seed_precondition("Cards", "ABC", "DEF", [["Key"]], True)
        self.assertEqual(
            result,
            "[Cards] 시트가 마지막 pull 이후 편집됐다 — seed 는 그 편집을 지운다. "
            "먼저 sync 로 당겨 리포에 합친 뒤 다시")

    def test_passes_when_export_matches_manifest(self):
        self.assertIsNone(authoring_sheet.seed_precondition("Cards", "SHA1", "SHA1", [["Key"]], True))

    def test_missing_csv_takes_precedence_over_everything(self):
        # repo_csv_exists=False 면 나머지 인자가 '통과'처럼 보여도 첫 조건이 이긴다.
        result = authoring_sheet.seed_precondition("Cards", "SHA1", "SHA1", [], False)
        self.assertEqual(result, "[Cards] seed 원본(리포 CSV)이 없다")


# ══════════════════════════════════════════════════════════════════════════════════════════════
# §12-1 #8 — validate_changeset (PlanError = 타입 + 핵심 토큰만 확인 — 문구는 안 정해짐)
# ══════════════════════════════════════════════════════════════════════════════════════════════

_MAPPING = {
    "Cards": {"name": "Cards", "target": "Content/Authoring/Cards.csv",
              "expectedHeader": ["CardId", "Name", "Weight"]},
    "ST_UI": {"name": "ST_UI", "target": "Content/StringTables/ST_UI.csv",
              "expectedHeader": ["Key", "SourceString", "en", "ja"]},
}


class T08ValidateChangeset(unittest.TestCase):
    def test_missing_changes_key_raises_planerror(self):
        with self.assertRaises(authoring_sheet.PlanError) as cm:
            authoring_sheet.validate_changeset({}, _MAPPING)
        self.assertIn("changes[]", str(cm.exception))

    def test_empty_changes_list_raises_planerror(self):
        with self.assertRaises(authoring_sheet.PlanError) as cm:
            authoring_sheet.validate_changeset({"changes": []}, _MAPPING)
        self.assertIn("changes[]", str(cm.exception))

    def test_item_missing_sheet_key_raises(self):
        doc = {"changes": [{"upsert": [{"CardId": "x"}]}]}
        with self.assertRaises(authoring_sheet.PlanError):
            authoring_sheet.validate_changeset(doc, _MAPPING)

    def test_unknown_sheet_raises(self):
        doc = {"changes": [{"sheet": "NoSuchSheet", "upsert": []}]}
        with self.assertRaises(authoring_sheet.PlanError) as cm:
            authoring_sheet.validate_changeset(doc, _MAPPING)
        self.assertIn("NoSuchSheet", str(cm.exception))

    def test_duplicate_sheet_entries_raise(self):
        doc = {"changes": [{"sheet": "Cards", "upsert": []}, {"sheet": "Cards", "delete": ["x"]}]}
        with self.assertRaises(authoring_sheet.PlanError) as cm:
            authoring_sheet.validate_changeset(doc, _MAPPING)
        self.assertIn("Cards", str(cm.exception))

    def test_non_string_values_rejected_before_any_write(self):
        # 부록 I-21 (G2 P2) — JSON 숫자·null·true 는 쓰기 뒤 되읽기 비교가 어긋난다. 계획 전에 PlanError.
        bad_docs = {
            "int": {"sheet": "Cards", "upsert": [{"CardId": "c1", "Weight": 5}]},
            "null": {"sheet": "Cards", "upsert": [{"CardId": "c1", "Name": None}]},
            "bool": {"sheet": "Cards", "upsert": [{"CardId": "c1", "Name": True}]},
            "float_key": {"sheet": "Cards", "upsert": [{"CardId": 1.5}]},
            "expect_not_dict": {"sheet": "Cards", "upsert": [{"CardId": "c1", "expect": "Weight"}]},
            "expect_value_int": {"sheet": "Cards", "upsert": [{"CardId": "c1", "Weight": "9", "expect": {"Weight": 1}}]},
            "upsert_not_list": {"sheet": "Cards", "upsert": {"CardId": "c1"}},
            "record_not_dict": {"sheet": "Cards", "upsert": ["c1"]},
            "delete_string": {"sheet": "Cards", "delete": "c1"},
            "delete_int_key": {"sheet": "Cards", "delete": [3]},
            "key_not_str": {"sheet": "Cards", "key": 0, "upsert": []},
        }
        for label, change in bad_docs.items():
            with self.subTest(case=label):
                with self.assertRaises(authoring_sheet.PlanError) as cm:
                    authoring_sheet.validate_changeset({"changes": [change]}, _MAPPING)
                self.assertIn("Cards", str(cm.exception))

    def test_all_string_values_including_empty_pass(self):
        change = {"sheet": "Cards", "key": "CardId",
                  "upsert": [{"CardId": "c1", "Weight": "", "expect": {"Weight": "1"}}], "delete": ["c9"]}
        self.assertEqual(authoring_sheet.validate_changeset({"changes": [change]}, _MAPPING), [change])

    def test_valid_changeset_returns_changes_list_unchanged(self):
        changes = [{"sheet": "Cards", "upsert": [{"CardId": "k1"}]}]
        result = authoring_sheet.validate_changeset({"changes": changes}, _MAPPING)
        self.assertEqual(result, changes)


# ══════════════════════════════════════════════════════════════════════════════════════════════
# §12-1 #9 — plan_changes, part A: 병합·unchanged·추가·삭제 (정상 경로)
# ══════════════════════════════════════════════════════════════════════════════════════════════

def _header():
    return ["CardId", "Name", "Weight", "Tag"]


def _rows():
    return [
        ["c1", "Foo", "1", "melee"],
        ["c2", "Bar", "2", "ranged"],
        ["c3", "Baz", "3", "melee"],
    ]


class T09PlanChangesBasics(unittest.TestCase):
    """정상 병합 경로 — §12-1 #9 의 앞쪽 절반."""

    def test_merge_only_given_columns(self):
        change = {"upsert": [{"CardId": "c1", "Weight": "9"}]}
        plan = authoring_sheet.plan_changes("Cards", _header(), _rows(), change)
        self.assertEqual(len(plan.updates), 1)
        u = plan.updates[0]
        self.assertEqual(u.row_index, 0)
        self.assertEqual(u.key, "c1")
        self.assertEqual(u.before, ["c1", "Foo", "1", "melee"])
        self.assertEqual(u.after, ["c1", "Foo", "9", "melee"])
        self.assertEqual(u.changed_cols, [2])
        self.assertEqual(plan.unchanged, 0)

    def test_unchanged_when_value_matches_current(self):
        change = {"upsert": [{"CardId": "c1", "Weight": "1"}]}  # 이미 "1" — 실질 변경 없음
        plan = authoring_sheet.plan_changes("Cards", _header(), _rows(), change)
        self.assertEqual(plan.updates, [])
        self.assertEqual(plan.unchanged, 1)

    def test_new_key_becomes_append_with_header_width_padding(self):
        change = {"upsert": [{"CardId": "c9", "Name": "New", "Weight": "5"}]}
        plan = authoring_sheet.plan_changes("Cards", _header(), _rows(), change)
        self.assertEqual(len(plan.appends), 1)
        a = plan.appends[0]
        self.assertEqual(a.key, "c9")
        self.assertEqual(a.after, ["c9", "New", "5", ""])  # Tag 미기재 -> ""

    def test_same_new_key_twice_merges_into_one_append(self):
        change = {"upsert": [{"CardId": "c9", "Name": "New"}, {"CardId": "c9", "Weight": "7"}]}
        plan = authoring_sheet.plan_changes("Cards", _header(), _rows(), change)
        self.assertEqual(len(plan.appends), 1)
        self.assertEqual(plan.appends[0].after, ["c9", "New", "7", ""])

    def test_same_existing_key_twice_accumulates_into_one_update(self):
        change = {"upsert": [{"CardId": "c2", "Name": "Bar2"}, {"CardId": "c2", "Weight": "22"}]}
        plan = authoring_sheet.plan_changes("Cards", _header(), _rows(), change)
        self.assertEqual(len(plan.updates), 1)
        u = plan.updates[0]
        self.assertEqual(u.before, ["c2", "Bar", "2", "ranged"])
        self.assertEqual(u.after, ["c2", "Bar2", "22", "ranged"])
        self.assertEqual(u.changed_cols, [1, 2])  # 오름차순

    def test_delete_existing_key(self):
        change = {"delete": ["c3"]}
        plan = authoring_sheet.plan_changes("Cards", _header(), _rows(), change)
        self.assertEqual(len(plan.deletes), 1)
        d = plan.deletes[0]
        self.assertEqual(d.row_index, 2)
        self.assertEqual(d.key, "c3")
        self.assertEqual(d.before, ["c3", "Baz", "3", "melee"])
        self.assertEqual(plan.missing_deletes, [])

    def test_delete_missing_key_reported_separately_from_existing_delete(self):
        change = {"delete": ["c3", "nope"]}
        plan = authoring_sheet.plan_changes("Cards", _header(), _rows(), change)
        self.assertEqual([d.key for d in plan.deletes], ["c3"])
        self.assertEqual(plan.missing_deletes, ["nope"])

    def test_duplicate_delete_key_yields_single_row_delete(self):
        # 부록 I-17 — 같은 키가 두 번 오면 삭제 요청도 하나여야 한다(둘이면 두 번째가 밀려 올라온 다음 행을 지운다).
        change = {"delete": ["c2", "c2", "nope", "nope"]}
        plan = authoring_sheet.plan_changes("Cards", _header(), _rows(), change)
        self.assertEqual([(d.row_index, d.key) for d in plan.deletes], [(1, "c2")])
        self.assertEqual(plan.missing_deletes, ["nope"])
        delete_requests = [r for r in authoring_sheet.build_apply_requests(1, plan) if "deleteDimension" in r]
        self.assertEqual(len(delete_requests), 1)

    def test_touched_counts_updates_appends_deletes(self):
        change = {"upsert": [{"CardId": "c1", "Weight": "9"}, {"CardId": "c9", "Name": "New"}],
                  "delete": ["c3"]}
        plan = authoring_sheet.plan_changes("Cards", _header(), _rows(), change)
        self.assertEqual(plan.touched(), 3)  # 1 update + 1 append + 1 delete


# ══════════════════════════════════════════════════════════════════════════════════════════════
# §12-1 #9 — plan_changes, part B: 검증 오류(PlanError) · expect · 커스텀 key · 정렬
# PlanError 는 문구가 안 정해져 있다(클래스 docstring "메시지에 시트·키·컬럼을 담는다") — 타입 +
# 핵심 토큰만 본다.
# ══════════════════════════════════════════════════════════════════════════════════════════════

class T09PlanChangesValidation(unittest.TestCase):
    def test_unknown_column_in_upsert_raises(self):
        change = {"upsert": [{"CardId": "c1", "Bogus": "x"}]}
        with self.assertRaises(authoring_sheet.PlanError) as cm:
            authoring_sheet.plan_changes("Cards", _header(), _rows(), change)
        self.assertIn("Bogus", str(cm.exception))

    def test_upsert_missing_key_field_raises(self):
        change = {"upsert": [{"Name": "NoKeyGiven"}]}
        with self.assertRaises(authoring_sheet.PlanError):
            authoring_sheet.plan_changes("Cards", _header(), _rows(), change)

    def test_upsert_empty_key_value_raises(self):
        change = {"upsert": [{"CardId": "", "Name": "Blank"}]}
        with self.assertRaises(authoring_sheet.PlanError):
            authoring_sheet.plan_changes("Cards", _header(), _rows(), change)

    def test_duplicate_key_in_existing_data_rows_raises(self):
        rows = [
            ["c1", "Foo", "1", "melee"],
            ["c1", "Dup", "9", "melee"],
            ["c2", "Bar", "2", "ranged"],
        ]
        with self.assertRaises(authoring_sheet.PlanError) as cm:
            authoring_sheet.plan_changes("Cards", _header(), rows, {})
        self.assertIn("c1", str(cm.exception))

    def test_upsert_and_delete_same_key_conflict_raises(self):
        change = {"upsert": [{"CardId": "c1", "Name": "X"}], "delete": ["c1"]}
        with self.assertRaises(authoring_sheet.PlanError) as cm:
            authoring_sheet.plan_changes("Cards", _header(), _rows(), change)
        self.assertIn("c1", str(cm.exception))

    def test_header_overflow_with_non_blank_cell_raises(self):
        rows = [["c1", "Foo", "1", "melee", "EXTRA"]]  # 헤더는 4칸인데 5번째 칸에 값
        with self.assertRaises(authoring_sheet.PlanError) as cm:
            authoring_sheet.plan_changes("Cards", _header(), rows, {})
        self.assertIn("Cards", str(cm.exception))

    def test_header_overflow_with_only_blank_cells_is_allowed(self):
        rows = [["c1", "Foo", "1", "melee", ""]]  # 초과분이 전부 빈칸이면 허용
        plan = authoring_sheet.plan_changes("Cards", _header(), rows, {})
        self.assertEqual(plan.touched(), 0)

    def test_expect_matching_current_value_passes_and_merges(self):
        change = {"upsert": [{"CardId": "c1", "Weight": "9", "expect": {"Weight": "1"}}]}
        plan = authoring_sheet.plan_changes("Cards", _header(), _rows(), change)
        self.assertEqual(plan.updates[0].after, ["c1", "Foo", "9", "melee"])

    def test_expect_mismatch_raises(self):
        change = {"upsert": [{"CardId": "c1", "Weight": "9", "expect": {"Weight": "999"}}]}
        with self.assertRaises(authoring_sheet.PlanError) as cm:
            authoring_sheet.plan_changes("Cards", _header(), _rows(), change)
        msg = str(cm.exception)
        self.assertIn("c1", msg)
        self.assertIn("Weight", msg)

    def test_expect_checks_against_value_already_merged_earlier_in_same_change(self):
        # 앞 레코드가 Weight 를 "9" 로 먼저 바꿔놓으면, 뒤 레코드의 expect="9" 는 원본("1")이 아니라
        # 그 병합된 값과 비교돼야 통과한다.
        change = {"upsert": [
            {"CardId": "c1", "Weight": "9"},
            {"CardId": "c1", "Weight": "10", "expect": {"Weight": "9"}},
        ]}
        plan = authoring_sheet.plan_changes("Cards", _header(), _rows(), change)
        self.assertEqual(len(plan.updates), 1)
        self.assertEqual(plan.updates[0].after, ["c1", "Foo", "10", "melee"])

    def test_expect_unknown_column_raises(self):
        change = {"upsert": [{"CardId": "c1", "expect": {"Bogus": "x"}}]}
        with self.assertRaises(authoring_sheet.PlanError) as cm:
            authoring_sheet.plan_changes("Cards", _header(), _rows(), change)
        self.assertIn("Bogus", str(cm.exception))

    def test_expect_on_brand_new_row_raises(self):
        change = {"upsert": [{"CardId": "c9", "Name": "New", "expect": {"Name": "whatever"}}]}
        with self.assertRaises(authoring_sheet.PlanError) as cm:
            authoring_sheet.plan_changes("Cards", _header(), _rows(), change)
        self.assertIn("c9", str(cm.exception))

    def test_custom_key_column_overrides_header_first_column(self):
        change = {"key": "Name", "upsert": [{"Name": "Bar", "Weight": "99"}]}
        plan = authoring_sheet.plan_changes("Cards", _header(), _rows(), change)
        self.assertEqual(plan.key_col, "Name")
        self.assertEqual(len(plan.updates), 1)
        u = plan.updates[0]
        self.assertEqual(u.key, "Bar")
        self.assertEqual(u.row_index, 1)  # c2 행
        self.assertEqual(u.after, ["c2", "Bar", "99", "ranged"])

    def test_custom_key_not_in_header_raises(self):
        change = {"key": "Bogus", "upsert": []}
        with self.assertRaises(authoring_sheet.PlanError):
            authoring_sheet.plan_changes("Cards", _header(), _rows(), change)

    def test_updates_and_deletes_sorted_ascending_appends_stay_in_encounter_order(self):
        rows = [
            ["c1", "Foo", "1", "melee"],
            ["c2", "Bar", "2", "ranged"],
            ["c3", "Baz", "3", "melee"],
            ["c4", "Qux", "4", "ranged"],
        ]
        change = {
            "upsert": [
                {"CardId": "c3", "Weight": "33"},
                {"CardId": "c1", "Weight": "11"},
                {"CardId": "zz", "Name": "Z"},
                {"CardId": "aa", "Name": "A"},
            ],
            "delete": ["c4", "c2"],
        }
        plan = authoring_sheet.plan_changes("Cards", _header(), rows, change)
        self.assertEqual([u.row_index for u in plan.updates], [0, 2])
        self.assertEqual([d.row_index for d in plan.deletes], [1, 3])
        self.assertEqual([a.key for a in plan.appends], ["zz", "aa"])


# ══════════════════════════════════════════════════════════════════════════════════════════════
# §12-1 #10 — expected_after (수정+삭제+추가 동시, 삭제는 "전" 인덱스 의미)
# ══════════════════════════════════════════════════════════════════════════════════════════════

class T10ExpectedAfter(unittest.TestCase):
    def test_update_delete_append_combined(self):
        header = _header()
        data_rows = [
            ["c1", "Foo", "1", "melee"],
            ["c2", "Bar"],                    # 헤더보다 짧다 -> 패딩되어야 한다
            ["c3", "Baz", "3", "melee"],
        ]
        plan = authoring_sheet.Plan(
            sheet="Cards", key_col="CardId",
            updates=[authoring_sheet.RowUpdate(
                row_index=0, key="c1",
                before=["c1", "Foo", "1", "melee"], after=["c1", "Foo", "9", "melee"],
                changed_cols=[2])],
            appends=[authoring_sheet.RowAppend(key="c9", after=["c9", "New", "5", ""])],
            deletes=[authoring_sheet.RowDelete(row_index=2, key="c3", before=["c3", "Baz", "3", "melee"])],
            unchanged=0, missing_deletes=[])
        result = authoring_sheet.expected_after(header, data_rows, plan)
        self.assertEqual(result, [
            ["CardId", "Name", "Weight", "Tag"],
            ["c1", "Foo", "9", "melee"],
            ["c2", "Bar", "", ""],
            ["c9", "New", "5", ""],
        ])


# ══════════════════════════════════════════════════════════════════════════════════════════════
# §12-1 #11 — keyed_diff (부록 I-4 경계 포함)
# ══════════════════════════════════════════════════════════════════════════════════════════════

class T11KeyedDiff(unittest.TestCase):
    HEADER = ["Key", "A", "B"]

    def test_identical_tables_yield_empty(self):
        expected = [self.HEADER, ["k1", "1", "2"], ["k2", "3", "4"]]
        actual = [self.HEADER, ["k1", "1", "2"], ["k2", "3", "4"]]
        self.assertEqual(authoring_sheet.keyed_diff("Key", expected, actual), [])

    def test_key_only_in_expected(self):
        expected = [self.HEADER, ["k1", "1", "2"], ["k2", "3", "4"]]
        actual = [self.HEADER, ["k1", "1", "2"]]
        self.assertEqual(authoring_sheet.keyed_diff("Key", expected, actual),
                          ["  ! 기대에만 있는 키: k2"])

    def test_keys_only_in_actual_preserve_actual_table_order(self):
        expected = [self.HEADER, ["k1", "1", "2"]]
        actual = [self.HEADER, ["k1", "1", "2"], ["zeta", "9", "9"], ["alpha", "8", "8"]]
        self.assertEqual(authoring_sheet.keyed_diff("Key", expected, actual),
                          ["  ! 실제에만 있는 키: zeta", "  ! 실제에만 있는 키: alpha"])

    def test_common_column_value_difference(self):
        expected = [self.HEADER, ["k1", "1", "2"]]
        actual = [self.HEADER, ["k1", "9", "2"]]
        self.assertEqual(authoring_sheet.keyed_diff("Key", expected, actual),
                          ["  ! k1.A: 기대='1' 실제='9'"])

    def test_row_order_only_difference_is_empty_but_rows_equal_is_false(self):
        expected = [self.HEADER, ["k1", "1", "2"], ["k2", "3", "4"]]
        actual = [self.HEADER, ["k2", "3", "4"], ["k1", "1", "2"]]
        self.assertEqual(authoring_sheet.keyed_diff("Key", expected, actual), [])
        self.assertFalse(authoring_sheet.rows_equal(expected, actual))

    def test_header_diff_line_precedes_common_column_diff(self):
        expected = [["Key", "A", "B"], ["k1", "1", "2"]]
        actual = [["Key", "A", "C"], ["k1", "9", "3"]]
        result = authoring_sheet.keyed_diff("Key", expected, actual)
        self.assertEqual(result, [
            "  ! 헤더: 기대에만=B 실제에만=C",
            "  ! k1.A: 기대='1' 실제='9'",
        ])

    def test_duplicate_key_in_expected_table_compares_first_occurrence(self):
        expected = [self.HEADER, ["k1", "1", "2"], ["k1", "5", "6"]]
        actual = [self.HEADER, ["k1", "1", "2"]]
        # 명세 갭(§12-1 #11 표기 "중복 키(기대|실제)") — 문구를 "기대" 라고 못박는 것은 Sonnet 의
        # 해석이다(최종 보고 4절 참조). 구조는 §5-3 그대로: 첫 등장 행으로 비교, 그 외 차이 없음.
        self.assertEqual(authoring_sheet.keyed_diff("Key", expected, actual),
                          ["  ! 중복 키(기대): k1"])

    def test_duplicate_key_in_actual_table_compares_first_occurrence(self):
        expected = [self.HEADER, ["k1", "1", "2"]]
        actual = [self.HEADER, ["k1", "1", "2"], ["k1", "9", "9"]]
        self.assertEqual(authoring_sheet.keyed_diff("Key", expected, actual),
                          ["  ! 중복 키(실제): k1"])

    def test_key_column_absent_from_one_header_yields_zero_keys_for_that_table(self):
        expected = [["Key", "A"], ["k1", "1"]]
        actual = [["Foo", "A"], ["x", "9"]]  # "Key" 컬럼 자체가 없다 -> 실제쪽 키는 0개
        result = authoring_sheet.keyed_diff("Key", expected, actual)
        self.assertEqual(result, [
            "  ! 헤더: 기대에만=Key 실제에만=Foo",
            "  ! 기대에만 있는 키: k1",
        ])

    def test_blank_or_missing_key_cells_are_ignored(self):
        header = ["Key", "A", "B"]
        expected = [header, ["", "1", "2"], ["k2", "3", "4"]]  # 첫 행 키 칸이 빈칸 -> 비교 제외
        actual = [header, ["k2", "3", "4"]]
        self.assertEqual(authoring_sheet.keyed_diff("Key", expected, actual), [])

        # key_col = "B"(인덱스 2) 인데 행이 짧아 그 칸이 아예 없으면(len(row) <= 2) 비교 제외.
        expected2 = [header, ["k1", "x"], ["k2", "y", "zzz"]]
        actual2 = [header, ["k2", "y", "zzz"]]
        self.assertEqual(authoring_sheet.keyed_diff("B", expected2, actual2), [])


# ══════════════════════════════════════════════════════════════════════════════════════════════
# §12-1 #12 — encode_cell (§5-4 요청 딕셔너리 형태 — 축자 고정)
# ══════════════════════════════════════════════════════════════════════════════════════════════

class T12EncodeCell(unittest.TestCase):
    def test_empty_string_clears_cell(self):
        self.assertEqual(authoring_sheet.encode_cell(""), {})

    def test_values_round_trip_as_exact_string_value(self):
        for value in ("1.0", "0.03", "=SUM(A1)", "'x", "   [a]"):
            with self.subTest(value=value):
                self.assertEqual(authoring_sheet.encode_cell(value),
                                  {"userEnteredValue": {"stringValue": value}})


# ══════════════════════════════════════════════════════════════════════════════════════════════
# §12-1 #13 — build_apply_requests (§5-4 — 순서 ①수정②삭제③추가, 삭제는 행 내림차순)
# ══════════════════════════════════════════════════════════════════════════════════════════════

class T13BuildApplyRequests(unittest.TestCase):
    GID = 555

    def test_order_and_shape_of_updates_deletes_appends(self):
        plan = authoring_sheet.Plan(
            sheet="Cards", key_col="CardId",
            updates=[
                authoring_sheet.RowUpdate(row_index=0, key="c1",
                                           before=["c1", "Foo", "1", "melee"],
                                           after=["c1", "Foo", "9", "melee"],
                                           changed_cols=[2]),
                authoring_sheet.RowUpdate(row_index=2, key="c3",
                                           before=["c3", "Baz", "3", "melee"],
                                           after=["c3", "Qux", "3", "ranged"],
                                           changed_cols=[1, 3]),
            ],
            deletes=[
                authoring_sheet.RowDelete(row_index=1, key="c2", before=["c2", "Bar", "2", "ranged"]),
                authoring_sheet.RowDelete(row_index=4, key="c5", before=["c5", "X", "5", "melee"]),
            ],
            appends=[
                authoring_sheet.RowAppend(key="c9", after=["c9", "New", "5", ""]),
                authoring_sheet.RowAppend(key="c10", after=["c10", "New2", "6", "melee"]),
            ],
            unchanged=0, missing_deletes=[])

        requests = authoring_sheet.build_apply_requests(self.GID, plan)

        expected = [
            # ① 수정 — RowUpdate 등장 순, 각 changed_cols 오름차순, 칸 하나당 1요청
            {"updateCells": {"range": {"sheetId": self.GID, "startRowIndex": 1, "endRowIndex": 2,
                                        "startColumnIndex": 2, "endColumnIndex": 3},
                              "rows": [{"values": [authoring_sheet.encode_cell("9")]}],
                              "fields": "userEnteredValue"}},
            {"updateCells": {"range": {"sheetId": self.GID, "startRowIndex": 3, "endRowIndex": 4,
                                        "startColumnIndex": 1, "endColumnIndex": 2},
                              "rows": [{"values": [authoring_sheet.encode_cell("Qux")]}],
                              "fields": "userEnteredValue"}},
            {"updateCells": {"range": {"sheetId": self.GID, "startRowIndex": 3, "endRowIndex": 4,
                                        "startColumnIndex": 3, "endColumnIndex": 4},
                              "rows": [{"values": [authoring_sheet.encode_cell("ranged")]}],
                              "fields": "userEnteredValue"}},
            # ② 삭제 — row_index 내림차순(4 먼저, 1 나중) — 뒤에서부터 지워야 인덱스가 안 밀린다
            {"deleteDimension": {"range": {"sheetId": self.GID, "dimension": "ROWS",
                                            "startIndex": 5, "endIndex": 6}}},
            {"deleteDimension": {"range": {"sheetId": self.GID, "dimension": "ROWS",
                                            "startIndex": 2, "endIndex": 3}}},
            # ③ 추가 — appends 등장 순으로 한 appendCells 요청에 전부
            {"appendCells": {"sheetId": self.GID,
                              "rows": [
                                  {"values": [authoring_sheet.encode_cell(v)
                                              for v in ["c9", "New", "5", ""]]},
                                  {"values": [authoring_sheet.encode_cell(v)
                                              for v in ["c10", "New2", "6", "melee"]]},
                              ],
                              "fields": "userEnteredValue"}},
        ]
        self.assertEqual(requests, expected)

    def test_no_touched_changes_yields_empty_request_list(self):
        plan = authoring_sheet.Plan(sheet="Cards", key_col="CardId", updates=[], appends=[], deletes=[],
                                     unchanged=3, missing_deletes=[])
        self.assertEqual(authoring_sheet.build_apply_requests(self.GID, plan), [])

    def test_updates_only_no_delete_or_append_requests_emitted(self):
        plan = authoring_sheet.Plan(
            sheet="Cards", key_col="CardId",
            updates=[authoring_sheet.RowUpdate(row_index=0, key="c1",
                                                before=["c1", "1"], after=["c1", "2"], changed_cols=[1])],
            appends=[], deletes=[], unchanged=0, missing_deletes=[])
        requests = authoring_sheet.build_apply_requests(self.GID, plan)
        self.assertEqual(len(requests), 1)
        self.assertIn("updateCells", requests[0])


# ══════════════════════════════════════════════════════════════════════════════════════════════
# §12-1 #14 — build_seed_requests (§5-4 — H/W = max(현재,새) 독립 계산, 패딩은 new_rows 기준)
# ══════════════════════════════════════════════════════════════════════════════════════════════

class T14BuildSeedRequests(unittest.TestCase):
    GID = 777

    def test_append_dimension_when_grid_smaller_in_both_dimensions(self):
        current_rows = [["Key", "A"], ["k1", "1"]]  # 2×2
        new_rows = [["Key", "A", "B"], ["k1", "1", "2"], ["k2", "3", "4"], ["k3", "5", "6"]]  # 4×3
        requests = authoring_sheet.build_seed_requests(self.GID, grid_rows=2, grid_cols=2,
                                                         current_rows=current_rows, new_rows=new_rows)
        self.assertEqual(requests[0], {"appendDimension": {"sheetId": self.GID, "dimension": "ROWS", "length": 2}})
        self.assertEqual(requests[1],
                          {"appendDimension": {"sheetId": self.GID, "dimension": "COLUMNS", "length": 1}})
        self.assertEqual(len(requests), 3)
        update = requests[2]["updateCells"]
        self.assertEqual(update["range"], {"sheetId": self.GID, "startRowIndex": 0, "endRowIndex": 4,
                                            "startColumnIndex": 0, "endColumnIndex": 3})
        self.assertEqual(update["fields"], "userEnteredValue")
        expected_rows = [{"values": [authoring_sheet.encode_cell(v) for v in row]} for row in new_rows]
        self.assertEqual(update["rows"], expected_rows)

    def test_no_append_dimension_when_grid_exactly_sufficient(self):
        current_rows = [["Key", "A", "B", "C"], ["k1", "1", "2", "3"]]  # 2×4 -> W 는 이걸로 정해진다
        new_rows = [["Key", "A"], ["k1", "1"]]  # 2×2 -> H 는 둘 다 2
        requests = authoring_sheet.build_seed_requests(self.GID, grid_rows=2, grid_cols=4,
                                                         current_rows=current_rows, new_rows=new_rows)
        self.assertEqual(len(requests), 1)  # 그리드가 이미 충분 -> appendDimension 없음
        update = requests[0]["updateCells"]
        self.assertEqual(update["range"], {"sheetId": self.GID, "startRowIndex": 0, "endRowIndex": 2,
                                            "startColumnIndex": 0, "endColumnIndex": 4})
        expected_rows = [
            {"values": [authoring_sheet.encode_cell(v) for v in ["Key", "A", "", ""]]},
            {"values": [authoring_sheet.encode_cell(v) for v in ["k1", "1", "", ""]]},
        ]
        self.assertEqual(update["rows"], expected_rows)

    def test_row_and_column_extents_computed_independently(self):
        # H(행수) 는 new_rows 가 더 크고 W(열수) 는 current_rows 가 더 크다 — 서로 다른 쪽에서 온다.
        current_rows = [["Key", "A", "B", "C", "D"], ["k1", "1", "2", "3", "4"]]  # 2행×5열
        new_rows = [["Key", "A"], ["k1", "1"], ["k2", "2"], ["k3", "3"], ["k4", "4"], ["k5", "5"]]  # 6행×2열
        requests = authoring_sheet.build_seed_requests(self.GID, grid_rows=6, grid_cols=5,
                                                         current_rows=current_rows, new_rows=new_rows)
        self.assertEqual(len(requests), 1)
        update = requests[0]["updateCells"]
        self.assertEqual(update["range"], {"sheetId": self.GID, "startRowIndex": 0, "endRowIndex": 6,
                                            "startColumnIndex": 0, "endColumnIndex": 5})
        self.assertEqual(len(update["rows"]), 6)
        self.assertEqual(update["rows"][0]["values"],
                          [authoring_sheet.encode_cell(v) for v in ["Key", "A", "", "", ""]])
        self.assertEqual(update["rows"][5]["values"],
                          [authoring_sheet.encode_cell(v) for v in ["k5", "5", "", "", ""]])

    def test_empty_current_rows_still_produces_full_new_rows_block(self):
        requests = authoring_sheet.build_seed_requests(self.GID, grid_rows=0, grid_cols=0,
                                                         current_rows=[],
                                                         new_rows=[["Key", "A"], ["k1", "1"]])
        self.assertEqual(requests[0], {"appendDimension": {"sheetId": self.GID, "dimension": "ROWS", "length": 2}})
        self.assertEqual(requests[1],
                          {"appendDimension": {"sheetId": self.GID, "dimension": "COLUMNS", "length": 2}})
        update = requests[2]["updateCells"]
        self.assertEqual(update["range"], {"sheetId": self.GID, "startRowIndex": 0, "endRowIndex": 2,
                                            "startColumnIndex": 0, "endColumnIndex": 2})


# ══════════════════════════════════════════════════════════════════════════════════════════════
# §12-1 #15 — sheets_api.is_inside · resolve_key_path
# ══════════════════════════════════════════════════════════════════════════════════════════════

def _pick_other_drive(path):
    """path 와 다른 드라이브 문자 하나(Windows 전용 — tempfile 드라이브와 다른 문자를 고른다)."""
    drive = os.path.splitdrive(path)[0].upper()
    for letter in "QZYXWV":
        candidate = letter + ":"
        if candidate != drive:
            return candidate
    raise AssertionError("대체 드라이브 문자를 못 찾음")  # pragma: no cover


class T15IsInsideAndResolveKeyPath(unittest.TestCase):
    def setUp(self):
        self._repo_tmp = tempfile.TemporaryDirectory()
        self._outside_tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._repo_tmp.cleanup)
        self.addCleanup(self._outside_tmp.cleanup)
        self.repo_root = self._repo_tmp.name
        self.outside_dir = self._outside_tmp.name

    def test_is_inside_true_for_nested_path(self):
        nested = os.path.join(self.repo_root, "sub", "key.json")
        self.assertTrue(sheets_api.is_inside(nested, self.repo_root))

    def test_is_inside_false_for_sibling_path_same_drive(self):
        sibling = os.path.join(self.outside_dir, "key.json")
        self.assertFalse(sheets_api.is_inside(sibling, self.repo_root))

    @unittest.skipUnless(os.name == "nt", "드라이브 문자는 Windows 전용 개념")
    def test_is_inside_false_for_different_drive(self):
        other_drive = _pick_other_drive(self.repo_root)
        outside = other_drive + os.sep + "somewhere" + os.sep + "key.json"
        self.assertFalse(sheets_api.is_inside(outside, self.repo_root))

    def test_resolve_key_path_prefers_env_var_over_default(self):
        real_key = os.path.join(self.outside_dir, "outside-key.json")
        with io.open(real_key, "w", encoding="utf-8") as f:
            f.write("{}")
        bogus_default = os.path.join(self.outside_dir, "unused-default.json")  # 존재하지 않음 — 안 쓰여야 정상
        with mock.patch.dict(os.environ, {sheets_api.KEY_ENV: real_key}), \
                mock.patch.object(sheets_api, "DEFAULT_KEY_PATH", bogus_default):
            result = sheets_api.resolve_key_path(self.repo_root)
        self.assertEqual(result, real_key)

    def test_resolve_key_path_empty_env_falls_back_to_default(self):
        default_key = os.path.join(self.outside_dir, "default-key.json")
        with io.open(default_key, "w", encoding="utf-8") as f:
            f.write("{}")
        with mock.patch.dict(os.environ, {sheets_api.KEY_ENV: ""}), \
                mock.patch.object(sheets_api, "DEFAULT_KEY_PATH", default_key):
            result = sheets_api.resolve_key_path(self.repo_root)
        self.assertEqual(result, default_key)

    def test_resolve_key_path_rejects_path_inside_repo(self):
        inside = os.path.join(self.repo_root, "key.json")
        with mock.patch.dict(os.environ, {sheets_api.KEY_ENV: inside}):
            with self.assertRaises(sheets_api.SheetsSetupError) as cm:
                sheets_api.resolve_key_path(self.repo_root)
        self.assertEqual(
            str(cm.exception),
            "키 파일이 리포 안에 있다 — 리포 밖으로 옮겨라(커밋 사고 방지): %s" % inside)

    def test_resolve_key_path_missing_file(self):
        missing = os.path.join(self.outside_dir, "does-not-exist.json")
        self.assertFalse(os.path.exists(missing))
        with mock.patch.dict(os.environ, {sheets_api.KEY_ENV: missing}):
            with self.assertRaises(sheets_api.SheetsSetupError) as cm:
                sheets_api.resolve_key_path(self.repo_root)
        self.assertEqual(
            str(cm.exception),
            "서비스 계정 키가 없다: %s. 키를 이 경로에 두거나 환경변수 FPSR_SHEETS_SA_KEY 에 전체 경로를 넣어라. "
            "1회 설정 = %s §1" % (missing, sheets_api.SETUP_DOC))


# ══════════════════════════════════════════════════════════════════════════════════════════════
# §12-1 #16 — SheetsClient 라이브러리 부재 (sys.modules 차단으로 흉내)
# ══════════════════════════════════════════════════════════════════════════════════════════════

class T16SheetsClientLibraryAbsent(unittest.TestCase):
    def test_missing_google_auth_modules_raise_setup_error(self):
        blocked = {
            "google": None,
            "google.oauth2": None,
            "google.auth": None,
            "google.auth.transport": None,
            "google.auth.transport.requests": None,
            "google.auth.exceptions": None,
        }
        with mock.patch.dict(sys.modules, blocked):
            with self.assertRaises(sheets_api.SheetsSetupError) as cm:
                sheets_api.SheetsClient("unused-key-path.json")
        self.assertIn("requirements-sheets.txt", str(cm.exception))


# ══════════════════════════════════════════════════════════════════════════════════════════════
# §12-1 #17 — 예외·재시도 계약 (가짜 세션 + _sleep 주입, 실제 대기 없음)
# ══════════════════════════════════════════════════════════════════════════════════════════════

class _FakeResponse:
    """AuthorizedSession 이 돌려주는 requests.Response 대역 — status_code·text·json() 만 필요."""

    def __init__(self, status_code, json_data=None, text="", json_error=False):
        self.status_code = status_code
        self._json_data = json_data
        self.text = text
        self._json_error = json_error

    def json(self):
        if self._json_error:
            raise ValueError("가짜 응답 — JSON 파싱 실패 흉내")
        return self._json_data


class _FakeSession:
    """sheets_api 가 요구하는 세션 계약(request(method, url, **kwargs)) 만 구현한다."""

    def __init__(self, script):
        self._script = list(script)
        self.calls = []

    def request(self, method, url, **kwargs):
        self.calls.append((method, url, kwargs))
        item = self._script.pop(0)
        if isinstance(item, BaseException):
            raise item
        return item


class _FakeAuthError(Exception):
    """진짜 google.auth.exceptions.GoogleAuthError 대역 — _auth_error_types 주입 전용."""


class T17ExceptionAndRetryContract(unittest.TestCase):
    def _client(self, script, **kwargs):
        session = _FakeSession(script)
        client = sheets_api.SheetsClient("unused.json", _session=session, _sleep=lambda s: None, **kwargs)
        return client, session

    def test_get_values_retries_503_then_succeeds_two_calls(self):
        client, session = self._client([
            _FakeResponse(503, text="busy"),
            _FakeResponse(200, json_data={"values": [["a", "b"]]}),
        ])
        result = client.get_values("SSID", "A1:B2")
        self.assertEqual(result, [["a", "b"]])
        self.assertEqual(len(session.calls), 2)

    def test_get_values_retries_connection_error_then_succeeds(self):
        client, session = self._client([
            ConnectionError("network blip"),
            _FakeResponse(200, json_data={"values": [["x"]]}),
        ])
        result = client.get_values("SSID", "A1")
        self.assertEqual(result, [["x"]])
        self.assertEqual(len(session.calls), 2)

    def test_get_values_exhausts_retries_and_raises_api_error(self):
        attempts = len(sheets_api.RETRY_DELAYS_SEC) + 1
        client, session = self._client([_FakeResponse(503, text="still busy")] * attempts)
        with self.assertRaises(sheets_api.SheetsApiError) as cm:
            client.get_values("SSID", "A1")
        self.assertEqual(cm.exception.status, 503)
        self.assertEqual(len(session.calls), attempts)

    def test_batch_update_retries_429_then_succeeds(self):
        client, session = self._client([
            _FakeResponse(429, text="quota"),
            _FakeResponse(200, json_data={"ok": True}),
        ])
        result = client.batch_update("SSID", [{"noop": {}}])
        self.assertEqual(result, {"ok": True})
        self.assertEqual(len(session.calls), 2)

    def test_batch_update_5xx_raises_write_outcome_unknown_without_retry(self):
        client, session = self._client([_FakeResponse(503, text="server error")])
        with self.assertRaises(sheets_api.SheetsWriteOutcomeUnknown):
            client.batch_update("SSID", [{"noop": {}}])
        self.assertEqual(len(session.calls), 1)  # 5xx 는 재시도하지 않는다(§5-1)

    def test_batch_update_timeout_raises_write_outcome_unknown_status_zero(self):
        client, session = self._client([TimeoutError("no response")])
        with self.assertRaises(sheets_api.SheetsWriteOutcomeUnknown) as cm:
            client.batch_update("SSID", [{"noop": {}}])
        self.assertEqual(cm.exception.status, 0)
        self.assertEqual(len(session.calls), 1)

    def test_batch_update_400_raises_plain_api_error_not_outcome_unknown(self):
        client, session = self._client([_FakeResponse(400, json_data={"error": {"message": "bad request"}})])
        with self.assertRaises(sheets_api.SheetsApiError) as cm:
            client.batch_update("SSID", [{"noop": {}}])
        self.assertIs(type(cm.exception), sheets_api.SheetsApiError)  # WriteOutcomeUnknown 서브클래스 아님
        self.assertEqual(len(session.calls), 1)


    def test_auth_error_type_raises_setup_error_without_retry(self):
        client, session = self._client([_FakeAuthError("token refresh failed")],
                                        _auth_error_types=(_FakeAuthError,))
        with self.assertRaises(sheets_api.SheetsSetupError):
            client.get_values("SSID", "A1")
        self.assertEqual(len(session.calls), 1)  # 재시도 없음(§5-1 예외 계약 1)

    def test_unrecognized_exception_from_session_propagates_unchanged(self):
        client, session = self._client([ValueError("코드 버그 흉내 — 잡히면 안 된다")])
        with self.assertRaises(ValueError):
            client.get_values("SSID", "A1")

    def test_get_values_200_with_broken_json_raises_api_error_without_retry(self):
        client, session = self._client([_FakeResponse(200, text="not json", json_error=True)])
        with self.assertRaises(sheets_api.SheetsApiError) as cm:
            client.get_values("SSID", "A1")
        self.assertEqual(cm.exception.status, 200)
        self.assertEqual(len(session.calls), 1)

    def test_batch_update_200_with_broken_json_returns_empty_dict_not_exception(self):
        client, session = self._client([_FakeResponse(200, text="not json", json_error=True)])
        result = client.batch_update("SSID", [{"noop": {}}])
        self.assertEqual(result, {})

    @unittest.skipUnless(_HAVE_GOOGLE_AUTH, "google-auth 미설치 — 실제 자격 파싱 경로를 검증할 수 없다")
    def test_real_google_auth_rejects_malformed_key_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            broken_key = os.path.join(tmp, "broken-key.json")
            with io.open(broken_key, "w", encoding="utf-8") as f:
                f.write("{ not valid json")
            with self.assertRaises(sheets_api.SheetsSetupError):
                sheets_api.SheetsClient(broken_key)


# ══════════════════════════════════════════════════════════════════════════════════════════════
# §12-1 #18 — acquire_lock · touch_lock · release_lock (authoring_sheet.py, 파일시스템만)
# 실제 ~/.fpsr/authoring-sheet.lock 는 절대 쓰지 않는다 — 매번 tempfile 안의 경로만 넘긴다.
# ══════════════════════════════════════════════════════════════════════════════════════════════

class T18LockFunctions(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.lock_path = os.path.join(self._tmp.name, "authoring-sheet.lock")

    def _write_raw_lock(self, command="old-cmd", token="OLDTOKEN"):
        payload = {"pid": 111, "host": "otherhost", "command": command,
                   "started_utc": "2020-01-01T00:00:00Z", "token": token}
        with io.open(self.lock_path, "w", encoding="utf-8") as f:
            f.write(json.dumps(payload))
        return payload

    def test_second_acquire_while_fresh_raises_lock_busy(self):
        authoring_sheet.acquire_lock(self.lock_path, "cmd1")
        with self.assertRaises(authoring_sheet.LockBusy) as cm:
            authoring_sheet.acquire_lock(self.lock_path, "cmd2")
        self.assertIn("cmd1", str(cm.exception))  # 먼저 잡은 세션의 command 가 메시지에 담긴다(docstring)

    def test_stale_lock_by_mtime_is_cleared_and_reacquired(self):
        self._write_raw_lock(command="old-cmd", token="OLDTOKEN")
        past = time.time() - 100000
        os.utime(self.lock_path, (past, past))
        fixed_now = past + 901  # stale_after_sec 기본값(900)보다 1초 더 지난 시점
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            new_token = authoring_sheet.acquire_lock(self.lock_path, "cmd2", _now=lambda: fixed_now)
        self.assertIn("WARN", buf.getvalue())
        with io.open(self.lock_path, encoding="utf-8") as f:
            content = json.load(f)
        self.assertEqual(content["command"], "cmd2")
        self.assertEqual(content["token"], new_token)
        self.assertNotEqual(new_token, "OLDTOKEN")

    def test_stale_lock_race_loser_backs_off(self):
        # 부록 I-22 (G2 P3) — 오래된 잠금을 치우고 새로 만든 직후, 다른 세션이 그것을 지우고 제 잠금을 만들었다면
        # (되읽기 대기 중에 파일 token 이 바뀌면) 물러나야 한다. 파일은 이긴 쪽 것이므로 건드리지 않는다.
        self._write_raw_lock(command="old-cmd", token="OLDTOKEN")
        past = time.time() - 100000
        os.utime(self.lock_path, (past, past))

        def other_session_steals(_seconds):
            with io.open(self.lock_path, "w", encoding="utf-8") as f:
                f.write(json.dumps({"pid": 2, "host": "h", "command": "winner", "started_utc": "x", "token": "WINNER"}))

        with mock.patch.object(authoring_sheet.time, "sleep", side_effect=other_session_steals), \
                contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(authoring_sheet.LockBusy):
                authoring_sheet.acquire_lock(self.lock_path, "loser", _now=lambda: past + 901)
        with io.open(self.lock_path, encoding="utf-8") as f:
            self.assertEqual(json.load(f)["token"], "WINNER")

    def test_touch_lock_refreshes_mtime_so_old_creation_still_blocks(self):
        # "생성"은 오래됐어도 touch_lock 으로 진행 신호(mtime)가 신선해지면 여전히 LockBusy 여야 한다
        # (§5-3 — 만료 판정은 생성 시각이 아니라 mtime 이다).
        self._write_raw_lock(command="old-cmd", token="OLDTOKEN")
        past = time.time() - 100000
        os.utime(self.lock_path, (past, past))
        authoring_sheet.touch_lock(self.lock_path)  # mtime -> 지금(실제 시각)
        with self.assertRaises(authoring_sheet.LockBusy):
            authoring_sheet.acquire_lock(self.lock_path, "cmd2")  # 기본 _now=time.time — 방금 touch 해 신선하다

    def test_release_then_reacquire_succeeds(self):
        authoring_sheet.acquire_lock(self.lock_path, "cmd1")
        authoring_sheet.release_lock(self.lock_path)
        self.assertFalse(os.path.exists(self.lock_path))
        token2 = authoring_sheet.acquire_lock(self.lock_path, "cmd2")
        self.assertTrue(os.path.exists(self.lock_path))
        self.assertIsInstance(token2, str)
        self.assertTrue(token2)

    def test_release_missing_lock_is_a_no_op(self):
        self.assertFalse(os.path.exists(self.lock_path))
        authoring_sheet.release_lock(self.lock_path)  # 예외 없이 조용히 넘어가야 한다

    def test_lock_file_json_fields_and_return_value_match(self):
        token = authoring_sheet.acquire_lock(self.lock_path, "mycommand")
        with io.open(self.lock_path, encoding="utf-8") as f:
            content = json.load(f)
        for field in ("pid", "host", "command", "started_utc", "token"):
            self.assertIn(field, content)
        self.assertEqual(content["command"], "mycommand")
        self.assertEqual(content["token"], token)

    def test_touch_lock_without_existing_file_raises_file_not_found(self):
        self.assertFalse(os.path.exists(self.lock_path))
        with self.assertRaises(FileNotFoundError):
            authoring_sheet.touch_lock(self.lock_path)


# ══════════════════════════════════════════════════════════════════════════════════════════════
# 부록 I-18 — 쓰기 시도 뒤 새어 나온 예외 = 종료 3 (시도 전이면 그대로 올려 main() 분류를 따른다)
# ══════════════════════════════════════════════════════════════════════════════════════════════

class T19RunGuardedExitCode(unittest.TestCase):
    def test_exception_before_write_attempt_propagates(self):
        def run(progress):
            raise sheets_api.SheetsSetupError("자격 없음")
        with self.assertRaises(sheets_api.SheetsSetupError):
            authoring_sheet._run_guarded(run)

    def test_exception_after_write_attempt_returns_3(self):
        def run(progress):
            progress["write_attempted"] = True
            raise FileNotFoundError("잠금 파일을 잃었다")
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            self.assertEqual(authoring_sheet._run_guarded(run), 3)
        self.assertIn("FileNotFoundError", err.getvalue())   # traceback 이 남는다

    def test_normal_return_passes_through(self):
        self.assertEqual(authoring_sheet._run_guarded(lambda a, b, progress: a + b, 1, 2), 3)
        self.assertEqual(authoring_sheet._run_guarded(lambda progress: 0), 0)


# ══════════════════════════════════════════════════════════════════════════════════════════════
# §13 P3 "오케스트레이션" 후속(G2 레드팀) — apply 의 종료 코드 분기 · 다중 시트 부분실패 · sync 사슬은
# 이 파일의 다른 테스트가 하나도 태우지 않는다(순수함수 단위 테스트뿐). 여기서는 §6 apply 알고리즘
# 3~6단계를 가짜 SheetsClient·가짜 _run_sync 로 오프라인 재현한다.
#
# _FakeSession/_FakeResponse(§12-1 #17) 는 SheetsClient *밑*(HTTP 세션)을 흉내내는 것이라 여기엔 안
# 맞는다 — authoring_sheet.py 는 SheetsClient 의 세 메서드(get_sheet_properties·get_values·
# batch_update)만 직접 부르므로, 그 세 메서드를 시나리오대로 응답하는 가짜 클라이언트가 필요하다.
# ══════════════════════════════════════════════════════════════════════════════════════════════

class _FakeApplyClient:
    """T20 전용 가짜 SheetsClient. get_sheet_properties·get_values·batch_update 호출을 기록하고,
    시트(spreadsheet_id)별로 준비된 스크립트대로 응답한다. get_values 는 불릴 때마다 그 시트의 큐에서
    하나씩 꺼낸다 — 큐 순서 = §6 apply 알고리즘이 실제로 재조회하는 시점(3.계획 → 5-a0.재조회 →
    5-b.되읽기, 또는 5-a 실패 시 되읽기)과 1:1 대응이라 시나리오를 그대로 코드로 옮길 수 있다."""

    def __init__(self, properties_by_id, values_queue_by_id, batch_update_by_id=None):
        self._properties_by_id = properties_by_id
        self._values_queue = {k: list(v) for k, v in values_queue_by_id.items()}
        self._batch_update_by_id = batch_update_by_id or {}
        self.get_values_calls = []
        self.batch_update_calls = []

    def get_sheet_properties(self, spreadsheet_id):
        return self._properties_by_id[spreadsheet_id]

    def get_values(self, spreadsheet_id, a1_range):
        self.get_values_calls.append(spreadsheet_id)
        queue = self._values_queue[spreadsheet_id]
        if not queue:
            raise AssertionError("get_values(%s) 가 이 테스트가 준비한 스크립트보다 많이 불렸다" % spreadsheet_id)
        return queue.pop(0)

    def batch_update(self, spreadsheet_id, requests):
        self.batch_update_calls.append(spreadsheet_id)
        outcome = self._batch_update_by_id.get(spreadsheet_id, {})
        if isinstance(outcome, BaseException):
            raise outcome
        return outcome


class T20ApplyOrchestration(unittest.TestCase):
    """apply 의 오프라인 오케스트레이션 회귀(§13 P3) — 실제 자격·네트워크·시트 없이 `_cmd_apply` 를
    끝까지 돌려 종료 코드·시트별 batch_update 호출·sync 호출을 검증한다. 매핑 2시트(SA·SB)를
    tempfile 안에 두고 manifest sha == CSV sha 로 맞춰 apply_precondition 을 항상 통과시킨다."""

    HEADER = ["Key", "V"]

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tmp = self._tmp.name
        self.lock_path = os.path.join(self.tmp, "authoring-sheet.lock")
        self.sa_id = "FAKE_SHEET_A"
        self.sb_id = "FAKE_SHEET_B"

        self.sa_csv = os.path.join(self.tmp, "SA.csv")
        self.sb_csv = os.path.join(self.tmp, "SB.csv")
        sa_bytes = b"Key,V\r\nK1,oldA\r\n"
        sb_bytes = b"Key,V\r\nK1,oldB\r\n"
        with io.open(self.sa_csv, "wb") as f:
            f.write(sa_bytes)
        with io.open(self.sb_csv, "wb") as f:
            f.write(sb_bytes)

        self.mapping_path = os.path.join(self.tmp, "mapping.json")
        mapping_doc = {"sheets": [
            {"name": "SA", "sheetId": self.sa_id, "target": self.sa_csv, "expectedHeader": list(self.HEADER)},
            {"name": "SB", "sheetId": self.sb_id, "target": self.sb_csv, "expectedHeader": list(self.HEADER)},
        ]}
        with io.open(self.mapping_path, "w", encoding="utf-8") as f:
            json.dump(mapping_doc, f)

        # manifest sha == CSV 바이트 sha → apply_precondition 이 두 시트 모두 통과한다.
        manifest_path = authoring_sheet.manifest_path_for(self.mapping_path)
        manifest_doc = [
            {"name": "SA", "sha256": authoring_sheet.sha256_bytes(sa_bytes)},
            {"name": "SB", "sha256": authoring_sheet.sha256_bytes(sb_bytes)},
        ]
        with io.open(manifest_path, "w", encoding="utf-8") as f:
            json.dump(manifest_doc, f)

    # ── 헬퍼 ────────────────────────────────────────────────────────────────────────────────────

    def _make_args(self, changeset_doc):
        changeset_path = os.path.join(self.tmp, "changeset.json")
        with io.open(changeset_path, "w", encoding="utf-8") as f:
            json.dump(changeset_doc, f)
        return argparse.Namespace(mapping=self.mapping_path, changeset=changeset_path, dry_run=False)

    def _invoke(self, fake_client, args):
        """authoring_sheet._cmd_apply(args) 를 모든 네트워크 지점을 목으로 막고 실행한다.
        반환 = (종료코드, stdout, stderr, SheetsClient 생성자 목, _run_sync 목) — 호출부가 생성자
        호출 횟수·sync 호출 횟수까지 단언할 수 있게 목 객체 자체를 돌려준다."""
        run_sync_mock = mock.Mock(return_value=types.SimpleNamespace(returncode=0, stdout="", stderr=""))
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.object(authoring_sheet, "LOCK_PATH", self.lock_path), \
                mock.patch.object(authoring_sheet.sheets_api, "resolve_key_path", return_value="unused-key.json"), \
                mock.patch.object(authoring_sheet.sheets_api, "SheetsClient", return_value=fake_client) as ctor_mock, \
                mock.patch.object(authoring_sheet, "_fetch_export", return_value=None), \
                mock.patch.object(authoring_sheet, "_run_sync", run_sync_mock), \
                mock.patch.object(authoring_sheet.time, "sleep", lambda s: None), \
                contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = authoring_sheet._cmd_apply(args)
        return rc, out.getvalue(), err.getvalue(), ctor_mock, run_sync_mock

    # ── 테스트 ──────────────────────────────────────────────────────────────────────────────────

    def test_second_sheet_recheck_mismatch_returns_3_without_sync(self):
        # SA: 계획 읽기(3.) · 쓰기 직전 재조회(5-a0.) · 쓰기 뒤 되읽기(5-b.) 가 전부 일관 → 정상 완료.
        # SB: 계획 읽기 값과 쓰기 직전 재조회 값이 다르다(사람이 그 사이에 시트를 고쳤다) → 5-a0 에서
        # 중단하고 SB 는 아예 쓰지 않는다. pull(6.) 은 "쓴 시트"에 대해서만 도는 단계라 SB 에서 멈추면
        # 전혀 불리지 않는다 — SA 가 이미 쓰였어도 마찬가지(모든 시트의 쓰기·되읽기가 끝난 뒤에야 6. 로
        # 넘어가므로).
        sa_old = [list(self.HEADER), ["K1", "oldA"]]
        sa_new = [list(self.HEADER), ["K1", "newA"]]
        sb_old = [list(self.HEADER), ["K1", "oldB"]]
        sb_changed_by_someone_else = [list(self.HEADER), ["K1", "changedExternally"]]

        properties = {
            self.sa_id: [{"sheetId": 1, "title": "SA", "index": 0, "rowCount": 10, "columnCount": 5}],
            self.sb_id: [{"sheetId": 2, "title": "SB", "index": 0, "rowCount": 10, "columnCount": 5}],
        }
        values_queue = {
            self.sa_id: [sa_old, sa_old, sa_new],              # 3.계획 · 5-a0.재조회(일치) · 5-b.되읽기
            self.sb_id: [sb_old, sb_changed_by_someone_else],  # 3.계획 · 5-a0.재조회(불일치 — 여기서 중단)
        }
        fake_client = _FakeApplyClient(properties, values_queue, batch_update_by_id={self.sa_id: {}})

        args = self._make_args({"changes": [
            {"sheet": "SA", "upsert": [{"Key": "K1", "V": "newA"}]},
            {"sheet": "SB", "upsert": [{"Key": "K1", "V": "newB"}]},
        ]})

        rc, out, err, ctor_mock, run_sync_mock = self._invoke(fake_client, args)

        self.assertEqual(rc, 3)
        self.assertEqual(fake_client.batch_update_calls, [self.sa_id])   # SB 는 쓰기까지 못 갔다
        self.assertEqual(run_sync_mock.call_count, 0)                    # pull 은 안 불렸다
        self.assertIn("사람 확인 필요", err)
        self.assertFalse(os.path.exists(self.lock_path))

    def test_first_sheet_write_5xx_returns_3_with_diff(self):
        # SA 의 batch_update 가 5xx(SheetsWriteOutcomeUnknown) 로 죽는다 — "적용됐는지 알 수 없다"
        # 이므로 실패 직후 되읽기를 1회 시도해 keyed_diff 를 찍는다. 되읽기가 옛 값 그대로(=쓰기가
        # 실제로는 안 먹었다)라서 기대(newA) vs 실제(oldA) 차이가 stdout 에 남는다. 이 예외 분기는
        # wrote_any 값과 무관하게 무조건 3 이다(§6 5-a — "적용됐는지 알 수 없다"는 이미 쓴 시트가
        # 있든 없든 사람 확인이 필요하다는 뜻이라서). SB 는 changeset 에 있지만 SA 에서 멈추므로
        # batch_update 가 전혀 안 불린다.
        sa_old = [list(self.HEADER), ["K1", "oldA"]]
        sb_old = [list(self.HEADER), ["K1", "oldB"]]

        properties = {
            self.sa_id: [{"sheetId": 1, "title": "SA", "index": 0, "rowCount": 10, "columnCount": 5}],
            self.sb_id: [{"sheetId": 2, "title": "SB", "index": 0, "rowCount": 10, "columnCount": 5}],
        }
        values_queue = {
            self.sa_id: [sa_old, sa_old, sa_old],   # 3.계획 · 5-a0.재조회(일치) · 실패 뒤 되읽기(옛 값)
            self.sb_id: [sb_old],                    # 3.계획만 — 5-a0 까지 못 간다
        }
        write_failure = sheets_api.SheetsWriteOutcomeUnknown(503, "x", "POST", "u")
        fake_client = _FakeApplyClient(properties, values_queue, batch_update_by_id={self.sa_id: write_failure})

        args = self._make_args({"changes": [
            {"sheet": "SA", "upsert": [{"Key": "K1", "V": "newA"}]},
            {"sheet": "SB", "upsert": [{"Key": "K1", "V": "newB"}]},
        ]})

        rc, out, err, ctor_mock, run_sync_mock = self._invoke(fake_client, args)

        self.assertEqual(rc, 3)
        self.assertTrue(any(line.startswith("  ! ") for line in out.splitlines()),
                         "되읽기 불일치 keyed_diff 줄이 stdout 에 없다:\n%s" % out)
        self.assertIn("기대='newA' 실제='oldA'", out)
        self.assertEqual(fake_client.batch_update_calls, [self.sa_id])   # SB 의 batch_update 는 0회
        self.assertEqual(run_sync_mock.call_count, 0)
        self.assertIn("사람 확인 필요", err)
        self.assertFalse(os.path.exists(self.lock_path))

    def test_non_string_value_rejected_before_client_or_write(self):
        # 변경셋 값이 JSON 숫자(5) — validate_changeset(→_validate_change_value_types) 가 자격 해석·
        # SheetsClient 생성보다 먼저 거부한다(부록 I-21, §13 P2: "쓰기 전 데이터 오류 전부 차단" 계약).
        # _run_apply 에서 `client = sheets_api.SheetsClient(...)` 줄은 validate_changeset 뒤에만
        # 있으므로, 여기서 막히면 그 줄 자체가 실행되지 않는다 — 네트워크는커녕 자격 조회조차 없다.
        fake_client = _FakeApplyClient({}, {})   # 어떤 메서드도 불리면 안 된다(생성 자체가 0회여야 한다)
        args = self._make_args({"changes": [
            {"sheet": "SA", "upsert": [{"Key": "K1", "V": 5}]},
        ]})

        rc, out, err, ctor_mock, run_sync_mock = self._invoke(fake_client, args)

        self.assertEqual(rc, 1)
        self.assertEqual(ctor_mock.call_count, 0)          # SheetsClient(...) 자체가 안 불렸다
        self.assertEqual(fake_client.batch_update_calls, [])
        self.assertEqual(fake_client.get_values_calls, [])
        self.assertEqual(run_sync_mock.call_count, 0)
        self.assertIn("문자열이 아닌 값", err)
        self.assertFalse(os.path.exists(self.lock_path))


if __name__ == "__main__":
    unittest.main()
