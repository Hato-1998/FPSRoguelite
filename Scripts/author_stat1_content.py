"""STAT1 — 상태이상 콘텐츠 저작 (상태 DA 6 + 카탈로그 1 + 부여 프래그먼트 4).

명세 = Docs/Specs/STAT1_LightweightStatusEffects.md §5-1·§5-2·§5-5.

**구조 값과 감각 값을 나눠 넣는다.**
- 구조 값(슬롯 번호 · Kind · 강한 것의 재료쌍 · 카탈로그 등록 순서 · 프래그먼트↔상태 연결)은
  **여기서 확정한다** — 기계적이고, 틀리면 시스템이 아예 안 돈다.
- 감각 값(지속시간 · 배율 · 도트 dps · 쿨다운)은 **플레이스홀더**다. 사용자가 PIE 로 조정한다
  (memory leave-fine-tuning-to-user). 이름·설명도 컨셉 미확정이라 임시다.

**카탈로그 배열 순서 = 조합 우선순위**(§7-3 "카탈로그 배열 순서대로, 재료가 소모된 조합은 미성립").
실명(둔화+도트)을 속박(도트+방어력감소)보다 앞에 둔다 — 약한 3개가 동시에 걸리면 실명이 먼저 발동해
도트를 소모하므로 속박은 그 패스에서 성립하지 않는다. 순서를 바꾸면 우선순위가 바뀐다.

멱등: 이미 있는 에셋은 **완전히 건드리지 않는다**(재저장도 dirty 도 없음). 사용자가 값을 조정한 뒤
다시 돌려도 그 작업을 덮어쓰지 않는다 — CRIT1 의 author_crit1_fragments.py 와 같은 계약.

실행 = Scripts/run_author_stat1_content.bat (에디터를 먼저 닫을 것 — memory ue-editor-file-locks-block-git).
"""

import unreal

STATUS_PATH = "/Game/Status"
FRAGMENT_PATH = "/Game/Cards/Weapons/Modifiers"

# 슬롯 번호는 구조다. 0~3 = 약한 4종, 4~5 = 강한 2종. 카탈로그가 이 번호로 룩업하고
# 강한 것의 RequiredWeakSlots 가 이 번호를 가리킨다 — 바꾸면 조합이 통째로 어긋난다.
SLOT_SLOW = 0
SLOT_DOT = 1
SLOT_DEFENSE_DOWN = 2
SLOT_ATTACK_SLOW = 3
SLOT_BLIND = 4
SLOT_ROOT = 5

# (에셋명, 표시명[임시], 슬롯, Kind, 감각 값 dict, 재료쌍[강한 것만])
STATUSES = [
    ("DA_Status_Slow", "둔화(임시)", SLOT_SLOW, "WEAK",
     {"duration_seconds": 4.0, "move_speed_multiplier": 0.6}, None),

    ("DA_Status_Dot", "지속피해(임시)", SLOT_DOT, "WEAK",
     {"duration_seconds": 4.0, "damage_per_second": 8.0, "dot_tick_interval_seconds": 0.5}, None),

    ("DA_Status_DefenseDown", "방어력감소(임시)", SLOT_DEFENSE_DOWN, "WEAK",
     {"duration_seconds": 5.0, "incoming_damage_multiplier": 1.35}, None),

    ("DA_Status_AttackSlow", "공격속도저하(임시)", SLOT_ATTACK_SLOW, "WEAK",
     {"duration_seconds": 4.0, "attack_interval_multiplier": 1.5}, None),

    # 강한 것은 재료를 소모하므로 발동 순간 둔화·도트 효과가 사라진다(§5-1 재료쌍 주석).
    # "실명이면서 여전히 느림"을 원하면 아래 dict 에 move_speed_multiplier 를 직접 저작하면 된다.
    ("DA_Status_Blind", "실명(임시)", SLOT_BLIND, "STRONG",
     {"duration_seconds": 3.0, "disable_attack": True, "retrigger_cooldown_seconds": 0.0},
     [SLOT_SLOW, SLOT_DOT]),

    ("DA_Status_Root", "속박(임시)", SLOT_ROOT, "STRONG",
     {"duration_seconds": 2.5, "disable_movement": True, "retrigger_cooldown_seconds": 0.0},
     [SLOT_DOT, SLOT_DEFENSE_DOWN]),
]

# 부여 프래그먼트 = 약한 4종만. 강한 것은 카드로 주지 않는다(조합으로만 켜진다).
FRAGMENTS = [
    ("DA_Fragment_Rifle_StatusSlow", "둔화 탄(임시)", "DA_Status_Slow"),
    ("DA_Fragment_Rifle_StatusDot", "지속피해 탄(임시)", "DA_Status_Dot"),
    ("DA_Fragment_Rifle_StatusDefenseDown", "방어관통 탄(임시)", "DA_Status_DefenseDown"),
    ("DA_Fragment_Rifle_StatusAttackSlow", "제압 탄(임시)", "DA_Status_AttackSlow"),
]

CATALOG_NAME = "DA_StatusCatalog"

KIND_VALUES = {"WEAK": 0, "STRONG": 1}   # EFPSRStatusKind 선언 순서

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
created, skipped, failed = [], [], []


def object_path(package, name):
    return "{}/{}.{}".format(package, name, name)


def create_data_asset(package, name, class_name):
    """없으면 만들고 (asset, True), 있으면 (asset, False). 실패하면 (None, False)."""
    path = object_path(package, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.EditorAssetLibrary.load_asset(path), False

    cls = unreal.load_class(None, "/Script/FPSRoguelite." + class_name)
    if cls is None:
        failed.append("{}: class /Script/FPSRoguelite.{} not found".format(name, class_name))
        return None, False

    factory = unreal.DataAssetFactory()
    # data_asset_class 를 안 주면 팩토리가 클래스 선택 다이얼로그를 띄우는데, 커맨드렛에서 UI 는
    # 프로세스 즉사다(memory ue-commandlet-editor-ui-apis-crash).
    factory.set_editor_property("data_asset_class", cls)
    asset = asset_tools.create_asset(name, package, cls, factory)
    if asset is None:
        failed.append("{}: create_asset returned None".format(name))
        return None, False
    return asset, True


def set_props(asset, name, props):
    """set_editor_property 를 개별 try 로 — 이름이 하나 틀려도 나머지가 들어가고, 무엇이 틀렸는지 남는다."""
    for key, value in props.items():
        try:
            asset.set_editor_property(key, value)
        except Exception as exc:
            failed.append("{}: set '{}' -> {}".format(name, key, exc))


# --- 상태 DA 6종 -------------------------------------------------------------------------------------
status_assets = {}
for asset_name, display, slot, kind, feel, materials in STATUSES:
    asset, is_new = create_data_asset(STATUS_PATH, asset_name, "FPSRStatusEffectDataAsset")
    if asset is None:
        continue
    status_assets[asset_name] = asset
    if not is_new:
        skipped.append(asset_name)
        continue

    # Kind 는 정수로 넘긴다. `unreal.EFPSRStatusKind` 는 UENUM(BlueprintType) 인데도 이 커맨드렛에서
    # Python 바인딩이 안 잡혔다(AttributeError, 2026-09-10 실측). set_editor_property 는 enum 프로퍼티에
    # 정수를 그대로 받으므로 바인딩 유무와 무관하게 동작한다. 값 = 선언 순서(Weak=0, Strong=1).
    props = {"slot_index": slot, "kind": KIND_VALUES[kind]}
    props.update(feel)
    if materials is not None:
        props["required_weak_slots"] = materials
    set_props(asset, asset_name, props)
    try:
        asset.set_editor_property("display_name", unreal.Text(display))
    except Exception:
        pass  # 표시명이 없는 클래스면 무시 — 구조 값이 본체다
    unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
    created.append(asset_name)

# --- 카탈로그 ----------------------------------------------------------------------------------------
catalog, is_new = create_data_asset(STATUS_PATH, CATALOG_NAME, "FPSRStatusCatalogDataAsset")
if catalog is not None:
    if is_new:
        ordered = [status_assets[n] for n, _, _, _, _, _ in STATUSES if n in status_assets]
        set_props(catalog, CATALOG_NAME, {"statuses": ordered})
        unreal.EditorAssetLibrary.save_loaded_asset(catalog, False)
        created.append(CATALOG_NAME)
    else:
        skipped.append(CATALOG_NAME)

# --- 부여 프래그먼트 4종 ------------------------------------------------------------------------------
for asset_name, display, status_name in FRAGMENTS:
    asset, is_new = create_data_asset(FRAGMENT_PATH, asset_name, "FPSRStatusApplyFragment")
    if asset is None:
        continue
    if not is_new:
        skipped.append(asset_name)
        continue

    status = status_assets.get(status_name)
    if status is None:
        failed.append("{}: status asset '{}' missing".format(asset_name, status_name))
        continue
    set_props(asset, asset_name, {"status": status})
    try:
        asset.set_editor_property("display_name", unreal.Text(display))
    except Exception:
        pass
    unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
    created.append(asset_name)

print("[STAT1] created={} skipped(existing, untouched)={} failed={}".format(
    len(created), len(skipped), len(failed)))
for n in created:
    print("[STAT1]   + " + n)
for n in skipped:
    print("[STAT1]   = " + n)
for m in failed:
    print("[STAT1]   ! " + m)
print("[STAT1] DONE")
