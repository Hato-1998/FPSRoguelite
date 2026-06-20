# U18b content survey (read-only): dump each weapon DA's WeaponCards / AvailableModifiers / UnlockableFeatures
# and the MultiShot fragment's MaxStacks + the card pool's Cards/WeaponUnlockCards, so the migration script
# knows exactly what to move.
import unreal

WEAPON_DIR = "/Game/Weapons/DataTable"
POOL_PATH = "/Game/Cards/Character/DA_Character_CardPool"

def card_names(arr):
    return [c.get_name() if c else "None" for c in arr]

paths = unreal.EditorAssetLibrary.list_assets(WEAPON_DIR, recursive=True, include_folder=False)
for p in paths:
    a = unreal.EditorAssetLibrary.load_asset(p)
    if not isinstance(a, unreal.FPSRWeaponDataAsset):
        continue
    wc = card_names(a.get_editor_property("WeaponCards"))
    am = card_names(a.get_editor_property("AvailableModifiers"))
    uf = card_names(a.get_editor_property("UnlockableFeatures"))
    unreal.log("WPN %-22s | WeaponCards=%s | AvailableModifiers=%s | UnlockableFeatures=%s" % (
        a.get_name(), wc, am, uf))

# MultiShot card -> its fragment -> MaxStacks
ms_path = "/Game/Cards/Weapons/Modifiers/DA_CardModifiers_MultiShot"
ms = unreal.EditorAssetLibrary.load_asset(ms_path)
if ms:
    for e in ms.get_editor_property("Effects"):
        if e and e.get_class().get_name() == "CardEffect_WeaponBehavior":
            frag = e.get_editor_property("Fragment")
            if frag:
                unreal.log("MULTISHOT frag=%s class=%s MaxStacks=%s" % (
                    frag.get_name(), frag.get_class().get_name(), str(frag.get_editor_property("MaxStacks"))))

pool = unreal.EditorAssetLibrary.load_asset(POOL_PATH)
if pool:
    unreal.log("POOL Cards=%s" % card_names(pool.get_editor_property("Cards")))
    unreal.log("POOL WeaponUnlockCards=%s" % card_names(pool.get_editor_property("WeaponUnlockCards")))
else:
    unreal.log_error("POOL not found at %s" % POOL_PATH)

unreal.log("U18B_DUMP done")
