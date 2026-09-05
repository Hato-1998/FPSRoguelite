"""DataAsset 프로퍼티를 EditDefaultsOnly 디테일 패널 가드 없이 스크립트로 고치는 도구.

왜 있는가 -- UE 파이썬으로 EditDefaultsOnly 필드에 직접 값을 쓰면 거부된다:
  Exception: ...: Property '...' ... cannot be edited on instances
그 가드는 **디테일 패널 편집 가드**이지 FProperty 쓰기 자체를 막는 게 아니다. 이 도구는 그 아래 레이어
(C++ UFPSRAssetScripting, PropertyPathHelpers 기반)를 통해 읽고 쓴다 -- 경로 해석/타입 변환은 전부
엔진 코드가 하고, 이 스크립트는 그 위에 얇게 얹은 CLI일 뿐 자체 파싱은 없다.

이 스크립트는 **에디터 안에서만** 돈다(unreal 모듈이 필요) -- 반드시 Scripts/run_asset_edit.bat 로
실행할 것(헤드리스 정식 에디터, Troubleshooting D11). authoring_sheet.py 와 달리 일반 python
인터프리터로는 못 돌린다(그쪽은 CSV, 이쪽은 실제 .uasset을 건드린다).

변경셋(JSON) 형식:
  { "description": "왜 이 변경인가 (감사 흔적)",
    "edits": [ {"asset": "/Game/Weapons/DataTable/DA_Weapon_Rifle",
                "property": "WeaponParts[0].Stages[0].StatValue", "value": "36.0"} ] }
값은 항상 문자열이다(대상 FProperty의 ImportText가 파싱한다 -- 불리언은 "true"/"false", enum은 리터럴명).
멱등: 이미 목표값이면 WROTE가 아니라 NO-OP으로 보고하고 아무것도 건드리지 않는다(UFPSRAssetScripting 계약).

사용(반드시 .bat 경유 -- 에디터는 닫혀 있어야 한다):
  run_asset_edit.bat get /Game/Weapons/DataTable/DA_Weapon_Rifle "WeaponParts[0].Stages[0].StatValue"
  run_asset_edit.bat apply Content/Authoring/changesets/<파일>.json --dry-run
  run_asset_edit.bat apply Content/Authoring/changesets/<파일>.json
"""

import argparse
import json
import sys

import unreal

FPSR = unreal.FPSRAssetScripting


def cmd_get(args):
    """읽기 전용 -- 값을 쓰기 전에 프로퍼티 경로가 맞는지 확인하는 용도(작업 지시의 1단계)."""
    result = FPSR.get_asset_property(args.asset, args.property)
    print("[asset_edit] GET %s :: %s" % (args.asset, args.property))
    print("[asset_edit]   ok=%s value=%r" % (result.ok, result.old_value))
    print("[asset_edit]   %s" % result.message)
    return 0 if result.ok else 1


def cmd_apply(args):
    """변경셋의 각 edit을 순서대로 적용. 하나가 실패해도 나머지는 계속 시도하고(서로 다른 자산이면 서로
    영향이 없으므로), 실패가 하나라도 있으면 종료코드를 비-0으로 돌려 CI/배치가 놓치지 않게 한다."""
    with open(args.changeset, "r", encoding="utf-8") as f:
        doc = json.load(f)

    edits = doc.get("edits", [])
    if not edits:
        print("[asset_edit] FAIL 변경셋에 'edits'가 없거나 비어 있다: %s" % args.changeset)
        return 1

    description = doc.get("description", "")
    if description:
        print("[asset_edit] %s" % description)

    changed = unchanged = failed = 0
    for edit in edits:
        asset = edit.get("asset")
        prop = edit.get("property")
        value = edit.get("value")
        if not asset or not prop or value is None:
            print("[asset_edit] FAIL edit 항목에 asset/property/value가 없다: %r" % edit)
            failed += 1
            continue

        result = FPSR.set_asset_property(asset, prop, str(value), dry_run=args.dry_run, save=True)
        verb = "DRY-RUN" if args.dry_run else ("WROTE" if result.changed else "NO-OP")
        status = "OK" if result.ok else "FAIL"
        print("[asset_edit] %-4s %-7s %s :: %s -> %s" % (status, verb, asset, prop, value))
        print("[asset_edit]      %s" % result.message)

        if not result.ok:
            failed += 1
        elif result.changed:
            changed += 1
        else:
            unchanged += 1

    print("[asset_edit] ===== changed=%d unchanged=%d failed=%d =====" % (changed, unchanged, failed))
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p_get = sub.add_parser("get", help="프로퍼티 값 읽기(쓰기 없음)")
    p_get.add_argument("asset")
    p_get.add_argument("property")
    p_get.set_defaults(func=cmd_get)

    p_apply = sub.add_parser("apply", help="변경셋 JSON을 에셋에 적용")
    p_apply.add_argument("changeset")
    p_apply.add_argument("--dry-run", action="store_true", help="쓰지 않고 결과만 출력")
    p_apply.set_defaults(func=cmd_apply)

    args = parser.parse_args()
    sys.exit(args.func(args))


if __name__ == "__main__":
    main()
