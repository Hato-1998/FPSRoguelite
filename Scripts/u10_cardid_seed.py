# U10 CardId backfill (Codex merge-review P2). Every UFPSRCardDataAsset under /Game/Cards that has no CardId gets a
# stable key derived from its asset name and FROZEN into the asset (a later asset rename won't change the saved
# unlock key — exactly the rename-resilience the meta save needs). Cards that already carry a CardId are left
# untouched. Duplicate proposed keys are flagged (the pool's IsDataValid also guards this at author time).
#
# Run headless (editor closed):
#   UnrealEditor-Cmd <uproject> -run=pythonscript -script="E:/Git_Project/FPSRoguelite/Scripts/u10_cardid_seed.py"
import unreal

paths = unreal.EditorAssetLibrary.list_assets("/Game/Cards", recursive=True, include_folder=False)
total = 0
seeded = 0
already = 0
fails = 0
dups = 0
seen = {}

for p in paths:
    asset = unreal.EditorAssetLibrary.load_asset(p)
    if not isinstance(asset, unreal.FPSRCardDataAsset):
        continue
    total += 1
    name = asset.get_name()
    cur = asset.get_editor_property("CardId")

    if not cur.is_none():
        already += 1
        key = str(cur)
    else:
        asset.set_editor_property("CardId", unreal.Name(name))
        ok = unreal.EditorAssetLibrary.save_asset(p, only_if_is_dirty=False)
        if not ok:
            fails += 1
        seeded += 1
        key = name
        unreal.log("CARDID_SEED %-40s -> CardId='%s' %s" % (name, key, "OK" if ok else "FAIL"))

    if key in seen:
        dups += 1
        unreal.log_warning("CARDID_DUP '%s' shared by %s and %s" % (key, seen[key], name))
    else:
        seen[key] = name

unreal.log("CARDID_SEED_SUMMARY total=%d seeded=%d already=%d fails=%d dups=%d" % (total, seeded, already, fails, dups))
