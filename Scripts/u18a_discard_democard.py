# U18a: discard the throwaway demo card. The Rifle is reverted to its committed state on disk BEFORE this runs
# (git checkout), so it no longer references the demo card; here we delete the demo asset cleanly and verify.
import unreal

CARD = "/Game/Cards/Weapons/Card/DA_Card_Weapon_GlassCannon"
RIFLE = "/Game/Weapons/DataTable/DA_Weapon_Rifle"

if unreal.EditorAssetLibrary.does_asset_exist(CARD):
    ok = unreal.EditorAssetLibrary.delete_asset(CARD)
    unreal.log("DISCARD delete_asset(%s) -> %s" % (CARD, ok))
else:
    unreal.log("DISCARD card already absent")

rifle = unreal.EditorAssetLibrary.load_asset(RIFLE)
names = [c.get_name() for c in rifle.get_editor_property("WeaponCards") if c] if rifle else []
unreal.log("DISCARD Rifle WeaponCards=%s" % names)

paths = unreal.EditorAssetLibrary.list_assets("/Game/Cards", recursive=True, include_folder=False)
total = sum(1 for p in paths if isinstance(unreal.EditorAssetLibrary.load_asset(p), unreal.FPSRCardDataAsset))
unreal.log("DISCARD total_cards=%d (expect 17)" % total)
unreal.log("DISCARD_DONE")
