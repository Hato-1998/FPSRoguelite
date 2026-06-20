# U18a migration persist + Instanced-serialization proof. PostLoad already migrated each card in-memory; force-save
# to persist the Instanced Effects subobjects + cleared legacy fields to disk. A separate reload pass (u18a_migcheck)
# then confirms the Instanced subobjects survive a save/load roundtrip (the #1 Instanced risk gate).
import unreal

paths = unreal.EditorAssetLibrary.list_assets("/Game/Cards", recursive=True, include_folder=False)
n = 0
fails = 0
for p in paths:
    asset = unreal.EditorAssetLibrary.load_asset(p)
    if not isinstance(asset, unreal.FPSRCardDataAsset):
        continue
    n += 1
    ok = unreal.EditorAssetLibrary.save_asset(p, only_if_is_dirty=False)
    if not ok:
        fails += 1
    unreal.log("RESAVE %-32s -> %s" % (asset.get_name(), "OK" if ok else "FAIL"))

unreal.log("RESAVE_SUMMARY total=%d fails=%d" % (n, fails))
