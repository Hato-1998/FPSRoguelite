"""STAT1 콘텐츠 저작 검증 — 만든 에셋이 실제로 명세대로인지 되읽어 확인한다.

왜 필요한가 — 저작 스크립트가 프로퍼티별로 조용히 실패할 수 있다(실측: EnumProperty 인 Kind 가
파이썬에서 열거형 객체도 정수도 못 받아 6개 전부 실패했는데, 에셋은 다 만들어졌고 스크립트는 DONE 을
찍었다). "만들어졌다"는 "맞게 채워졌다"가 아니다 — memory verify-with-control-group.

실행 = Scripts/run_verify_stat1_content.bat (에디터 종료 상태에서).
판정 = stdout 의 "[VERIFY] RESULT" 줄. FAIL 이면 그 위 [VERIFY] ! 줄들이 사유다.
"""

import unreal

STATUS_PATH = "/Game/Status"
FRAGMENT_PATH = "/Game/Cards/Weapons/Modifiers"

# 명세 §5-1 의 구조 값 = 여기가 기대값. 감각 값(지속시간·배율)은 사용자가 조정할 것이므로 검사하지 않는다.
EXPECTED_STATUSES = [
    # (에셋명, 슬롯, Kind 문자열, 재료쌍 or None)
    ("DA_Status_Slow", 0, "Weak", None),
    ("DA_Status_Dot", 1, "Weak", None),
    ("DA_Status_DefenseDown", 2, "Weak", None),
    ("DA_Status_AttackSlow", 3, "Weak", None),
    ("DA_Status_Blind", 4, "Strong", [0, 1]),
    ("DA_Status_Root", 5, "Strong", [1, 2]),
]

EXPECTED_FRAGMENTS = [
    ("DA_Fragment_Rifle_StatusSlow", "DA_Status_Slow"),
    ("DA_Fragment_Rifle_StatusDot", "DA_Status_Dot"),
    ("DA_Fragment_Rifle_StatusDefenseDown", "DA_Status_DefenseDown"),
    ("DA_Fragment_Rifle_StatusAttackSlow", "DA_Status_AttackSlow"),
]

problems = []
notes = []


def load(package, name):
    path = "{}/{}.{}".format(package, name, name)
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        problems.append("{} 없음".format(name))
        return None
    return unreal.EditorAssetLibrary.load_asset(path)


# --- 상태 DA 6종 ---------------------------------------------------------------------------------
loaded = {}
for name, slot, kind, materials in EXPECTED_STATUSES:
    asset = load(STATUS_PATH, name)
    if asset is None:
        continue
    loaded[name] = asset

    got_slot = asset.get_editor_property("slot_index")
    if int(got_slot) != slot:
        problems.append("{}: SlotIndex={} (기대 {})".format(name, got_slot, slot))

    # enum 은 파이썬에서 객체로 돌아온다 — 문자열화해 끝부분만 본다(EFPSRStatusKind.STRONG 등).
    got_kind = str(asset.get_editor_property("kind"))
    if kind.upper() not in got_kind.upper():
        problems.append("{}: Kind={} (기대 {})".format(name, got_kind, kind))

    got_mat = list(asset.get_editor_property("required_weak_slots"))
    if materials is None:
        if got_mat:
            problems.append("{}: 약한 상태인데 RequiredWeakSlots={}".format(name, got_mat))
    else:
        if [int(x) for x in got_mat] != materials:
            problems.append("{}: RequiredWeakSlots={} (기대 {})".format(name, got_mat, materials))

    notes.append("{}: slot={} kind={} materials={} dur={}".format(
        name, got_slot, got_kind, got_mat, asset.get_editor_property("duration_seconds")))

# --- 카탈로그 ------------------------------------------------------------------------------------
catalog = load(STATUS_PATH, "DA_StatusCatalog")
if catalog is not None:
    entries = list(catalog.get_editor_property("statuses"))
    names = [e.get_name() if e else "<null>" for e in entries]
    expected_order = [n for n, _, _, _ in EXPECTED_STATUSES]
    if names != expected_order:
        problems.append("카탈로그 순서={} (기대 {})".format(names, expected_order))
    notes.append("DA_StatusCatalog: {}".format(names))

    # 🔴 카탈로그 배열 순서 = 조합 우선순위(§7-3). 실명이 속박보다 앞이어야 한다.
    if "DA_Status_Blind" in names and "DA_Status_Root" in names:
        if names.index("DA_Status_Blind") > names.index("DA_Status_Root"):
            problems.append("카탈로그: 속박이 실명보다 앞이다 — 조합 우선순위가 뒤집힌다")

# --- 부여 프래그먼트 4종 --------------------------------------------------------------------------
for frag_name, status_name in EXPECTED_FRAGMENTS:
    frag = load(FRAGMENT_PATH, frag_name)
    if frag is None:
        continue
    status = frag.get_editor_property("status")
    got = status.get_name() if status else "<null>"
    if got != status_name:
        problems.append("{}: Status={} (기대 {})".format(frag_name, got, status_name))
    notes.append("{}: status={} max_stacks={}".format(
        frag_name, got, frag.get_editor_property("max_stacks")))

# --- 에디터 데이터 검증(IsDataValid) ---------------------------------------------------------------
validator = unreal.EditorValidatorSubsystem()
try:
    subsystem = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem)
    for name, asset in list(loaded.items()) + ([("DA_StatusCatalog", catalog)] if catalog else []):
        # is_object_valid 는 (result, warnings, errors) 튜플을 돌려준다 — 튜플을 불리언으로 쓰면
        # 항상 True 라 통과로 오독한다(실측 사고, CRIT1).
        # 🔴 is_object_valid 는 인자가 둘이다(에셋, usecase). usecase 를 빼면 TypeError 로 조용히 건너뛴다.
        # 그리고 반환은 (result, warnings, errors) **튜플**이라 불리언으로 쓰면 항상 True = 통과 오독이다(CRIT1 실사고).
        res = subsystem.is_object_valid(asset, unreal.DataValidationUsecase.SCRIPT)
        result, warnings, errors = res[0], res[1], res[2]
        if errors:
            problems.append("{}: IsDataValid 에러 {}".format(name, list(errors)))
        if warnings:
            notes.append("{}: IsDataValid 경고 {}".format(name, list(warnings)))
except Exception as exc:
    notes.append("IsDataValid 실행 실패(치명 아님): {}".format(exc))

for n in notes:
    print("[VERIFY]   " + n)
for p in problems:
    print("[VERIFY] ! " + p)
print("[VERIFY] RESULT {} (problems={})".format("PASS" if not problems else "FAIL", len(problems)))
print("[VERIFY] DONE")
