# U18b final verify (read-only, fresh process): confirm GrantWeapon.WeaponToGrant persisted on each unlock card,
# and the re-routed DA_CardModifiers_* behavior cards carry Group=Weapon (so they target their source weapon).
import unreal
EAL = unreal.EditorAssetLibrary

for p in EAL.list_assets("/Game/Cards/Weapons/Unlock", recursive=True, include_folder=False):
    a = EAL.load_asset(p)
    if not isinstance(a, unreal.FPSRCardDataAsset):
        continue
    grant = "None"
    for e in a.get_editor_property("Effects"):
        if e and e.get_class().get_name() == "CardEffect_GrantWeapon":
            w = e.get_editor_property("WeaponToGrant")
            grant = w.get_name() if w else "NULL"
    unreal.log("UNLOCK %-26s WeaponToGrant=%s" % (a.get_name(), grant))

for c in ["DA_CardModifiers_BounsDamage", "DA_CardModifiers_ExplosiveRounds",
          "DA_CardModifiers_MultiShot", "DA_CardModifiers_NoSelfDamage"]:
    a = EAL.load_asset("/Game/Cards/Weapons/Modifiers/" + c)
    if a:
        unreal.log("MODCARD %-30s Group=%s" % (c, str(a.get_editor_property("Group"))))

unreal.log("U18B_VERIFY done")
