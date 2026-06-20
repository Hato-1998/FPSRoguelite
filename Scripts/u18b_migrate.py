# U18b content migration (headless). Routing rework + feature-unlock + new-weapon unlock cards.
#  1) MultiShot fragment MaxStacks 2 -> 3 (탄도 최대 3).
#  2) Each weapon: AvailableModifiers -> WeaponCards (non-MultiShot behavior frags, now level-up) /
#     UnlockableFeatures (MultiShot 탄도 feature-unlock). Ensure ALL ranged weapons carry the 탄도 feature.
#     Clear AvailableModifiers (deprecated). Set DA_CardModifiers_* Group=Weapon (target their source weapon).
#  3) Create DA_CardUnlock_<Weapon> (UCardEffect_GrantWeapon) for the 8 ranged weapons + populate the card pool's
#     WeaponUnlockCards[]. Knife (melee) is excluded from both the 탄도 feature and the unlock pool.
import unreal

WEAPON_DIR = "/Game/Weapons/DataTable"
MOD_DIR = "/Game/Cards/Weapons/Modifiers"
UNLOCK_DIR = "/Game/Cards/Weapons/Unlock"
POOL_PATH = "/Game/Cards/Character/DA_Character_CardPool"
MULTISHOT_CARD = MOD_DIR + "/DA_CardModifiers_MultiShot"

tools = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary

RANGED = ["Bazooka", "BurstRifle", "ChargeLaser", "Grenade", "LMG", "Rifle", "Shotgun", "Sniper"]
ALL_WEAPONS = RANGED + ["Knife"]
MOD_CARDS = ["DA_CardModifiers_BounsDamage", "DA_CardModifiers_ExplosiveRounds",
             "DA_CardModifiers_MultiShot", "DA_CardModifiers_NoSelfDamage"]

def wpath(name):
    return WEAPON_DIR + "/DA_Weapon_" + name

def pkg(asset):
    return asset.get_path_name().split(".")[0]

# --- STEP 1: MultiShot fragment MaxStacks = 3 ---
ms_card = EAL.load_asset(MULTISHOT_CARD)
ms_frag = None
if ms_card:
    for e in ms_card.get_editor_property("Effects"):
        if e and e.get_class().get_name() == "CardEffect_WeaponBehavior":
            ms_frag = e.get_editor_property("Fragment")
            break
if ms_frag:
    ms_frag.set_editor_property("MaxStacks", 3)
    EAL.save_asset(pkg(ms_frag))
    unreal.log("STEP1 MultiShot MaxStacks=3 (%s)" % ms_frag.get_name())
else:
    unreal.log_error("STEP1 MultiShot fragment NOT found")

# --- STEP 2: reassign weapon card arrays ---
for name in ALL_WEAPONS:
    w = EAL.load_asset(wpath(name))
    if not w:
        unreal.log_error("STEP2 weapon missing: %s" % name)
        continue
    am = list(w.get_editor_property("AvailableModifiers"))
    wc = list(w.get_editor_property("WeaponCards"))
    uf = list(w.get_editor_property("UnlockableFeatures"))
    for card in am:
        if not card:
            continue
        card.set_editor_property("Group", unreal.CardGroup.WEAPON)
        if card.get_name() == "DA_CardModifiers_MultiShot":
            if card not in uf:
                uf.append(card)
        else:
            if card not in wc:
                wc.append(card)
    # Ensure every RANGED weapon carries the 탄도(MultiShot) feature-unlock, even if absent before.
    if name in RANGED and ms_card and ms_card not in uf:
        uf.append(ms_card)
    w.set_editor_property("WeaponCards", wc)
    w.set_editor_property("UnlockableFeatures", uf)
    w.set_editor_property("AvailableModifiers", [])
    EAL.save_asset(wpath(name))
    unreal.log("STEP2 %-12s WeaponCards=%d UnlockableFeatures=%d (AvailableModifiers cleared)" % (
        name, len(wc), len(uf)))

for c in MOD_CARDS:
    EAL.save_asset(MOD_DIR + "/" + c)

# --- STEP 3: create DA_CardUnlock_<Weapon> + populate pool WeaponUnlockCards ---
unlock_cards = []
for name in RANGED:
    weapon = EAL.load_asset(wpath(name))
    cname = "DA_CardUnlock_" + name
    cpath = UNLOCK_DIR + "/" + cname
    card = EAL.load_asset(cpath) if EAL.does_asset_exist(cpath) else tools.create_asset(
        cname, UNLOCK_DIR, unreal.FPSRCardDataAsset, None)
    if not card:
        unreal.log_error("STEP3 create failed: %s" % cname)
        continue
    eff = unreal.new_object(unreal.CardEffect_GrantWeapon, card)
    eff.set_editor_property("WeaponToGrant", weapon)
    card.set_editor_property("DisplayName", weapon.get_editor_property("DisplayName"))
    card.set_editor_property("Group", unreal.CardGroup.WEAPON_UNLOCK)
    card.set_editor_property("Weight", 1.0)
    card.set_editor_property("Effects", [eff])
    EAL.save_asset(cpath)
    unlock_cards.append(card)
    unreal.log("STEP3 %-22s grants=%s" % (cname, name))

pool = EAL.load_asset(POOL_PATH)
if pool:
    existing = list(pool.get_editor_property("WeaponUnlockCards"))
    for c in unlock_cards:
        if c not in existing:
            existing.append(c)
    pool.set_editor_property("WeaponUnlockCards", existing)
    EAL.save_asset(POOL_PATH)
    unreal.log("STEP3 pool WeaponUnlockCards=%d" % len(existing))
else:
    unreal.log_error("STEP3 pool NOT found: %s" % POOL_PATH)

unreal.log("U18B_MIGRATE done")
