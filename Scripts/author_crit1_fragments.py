"""CRIT1 — create the 5 rifle crit-build fragment DataAsset shells (Docs/Specs/CRIT1_...md §4).

Scaffolding only: each asset is created with the class's C++ defaults, which ARE the spec's §9 authored
values. Tuning them afterwards is the user's job (memory da-edits-are-user-work) — this script exists so the
CardCatalog rows have something to point at, not to author balance.

Idempotent: an asset that already exists is left completely untouched (no re-save, no dirty), so re-running
this after the user has tuned values cannot clobber their work.

Run headless via Scripts/run_author_crit1_fragments.bat (editor must be closed — memory ue-editor-file-locks-block-git).
"""

import unreal

PACKAGE_PATH = "/Game/Cards/Weapons/Modifiers"

# (asset name, C++ class name, display name shown on the card/fragment)
FRAGMENTS = [
    ("DA_Fragment_Rifle_CritOverkill",     "FPSRFragment_CritBonusInstance",   "치명타 추가타"),
    ("DA_Fragment_Rifle_CritLifesteal",    "FPSRFragment_CritLifesteal",       "치명타 흡혈"),
    ("DA_Fragment_Rifle_CritOnReload",     "FPSRFragment_CritOnReload",        "재장전 각성"),
    ("DA_Fragment_Rifle_WeakpointCrit",    "FPSRFragment_WeakpointAlwaysCrit", "약점 관통"),
    ("DA_Fragment_Rifle_CritOnSlide",      "FPSRFragment_CritOnSlide",         "활강 집중"),
]

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
created, skipped, failed = [], [], []

for asset_name, class_name, display_name in FRAGMENTS:
    object_path = "{}/{}.{}".format(PACKAGE_PATH, asset_name, asset_name)
    if unreal.EditorAssetLibrary.does_asset_exist(object_path):
        skipped.append(asset_name)
        continue

    fragment_class = unreal.load_class(None, "/Script/FPSRoguelite." + class_name)
    if fragment_class is None:
        failed.append("{}: class /Script/FPSRoguelite.{} not found".format(asset_name, class_name))
        continue

    factory = unreal.DataAssetFactory()
    # UDataAssetFactory picks the concrete UDataAsset subclass from this property; without it the factory
    # opens a class-picker dialog, which is fatal in a commandlet (memory ue-commandlet-editor-ui-apis-crash).
    factory.set_editor_property("data_asset_class", fragment_class)

    new_asset = asset_tools.create_asset(asset_name, PACKAGE_PATH, fragment_class, factory)
    if new_asset is None:
        failed.append("{}: create_asset returned None".format(asset_name))
        continue

    new_asset.set_editor_property("display_name", unreal.Text(display_name))
    unreal.EditorAssetLibrary.save_loaded_asset(new_asset, False)
    created.append(asset_name)

print("[CRIT1] created={} skipped(existing)={} failed={}".format(len(created), len(skipped), len(failed)))
for name in created:
    print("[CRIT1]   + " + name)
for name in skipped:
    print("[CRIT1]   = " + name + " (already existed, untouched)")
for msg in failed:
    print("[CRIT1]   ! " + msg)
print("[CRIT1] DONE")
