# Fix the parts the first apply couldn't set (EditDefaultsOnly struct fields): card magnitudes + mission windows.
# Uses KEYWORD construction (sets fields at init, bypassing set_editor_property's edit-flag check).
import unreal

def log(m): unreal.log_warning("[BALFIX] " + str(m))
def err(m): unreal.log_error("[BALFIX][ERR] " + str(m))

EAL = unreal.EditorAssetLibrary
R = unreal.CardRarity
C, RA, E, L = R.COMMON, R.RARE, R.EPIC, R.LEGENDARY

def save(p): EAL.save_asset(p, only_if_is_dirty=False)
def aslist(x):
    try: return [e for e in x]
    except Exception: return []
def tier(r, m): return unreal.FPSRCardRarityTier(rarity=r, magnitude=float(m))

def set_single(path, pairs):
    try:
        c = EAL.load_asset(path)
        effs = aslist(c.get_editor_property("Effects"))
        effs[0].set_editor_property("RarityTiers", [tier(r, m) for (r, m) in pairs])
        # OfferRarities is read-only (auto-recomputed in PostLoad/PostEditChange from the tiers) — do not set it.
        save(path)
        rb = aslist(effs[0].get_editor_property("RarityTiers"))
        log("CARD %s OK -> %s" % (path.split('/')[-1], [(str(t.get_editor_property('Rarity')), t.get_editor_property('Magnitude')) for t in rb]))
    except Exception as ex:
        err("CARD %s: %s" % (path, ex))

set_single("/Game/Cards/Character/Data/DA_Card_Damage", [(C,2.5),(RA,5.0),(E,6.5),(L,7.5)])
set_single("/Game/Cards/Character/Data/DA_Card_MaxHealth", [(C,5.0),(RA,8.0),(E,12.0),(L,15.0)])
set_single("/Game/Cards/Character/Data/DA_Card_Character_HealthRegen", [(C,0.5),(RA,1.0),(E,1.5),(L,2.0)])
set_single("/Game/Cards/Character/Data/DA_Card_Luck", [(C,1.0),(RA,2.0),(E,3.0),(L,4.0)])
set_single("/Game/Cards/Weapons/Card/DA_Card_FireRate_ThisWeapon", [(C,0.01),(RA,0.03),(E,0.05),(L,0.07)])

# MagSize ThisWeapon: % per rarity + second effect (ReloadTime additive -0.1, all rarities)
try:
    p = "/Game/Cards/Weapons/Card/DA_Card_MagSize_ThisWeapon"
    c = EAL.load_asset(p)
    effs = aslist(c.get_editor_property("Effects"))
    effs[0].set_editor_property("RarityTiers", [tier(C,0.05),tier(RA,0.07),tier(E,0.10),tier(L,0.10)])
    rl = unreal.new_object(unreal.CardEffect_WeaponStat, c)
    rl.set_editor_property("Stat", unreal.FPSRWeaponStat.RELOAD_TIME)
    rl.set_editor_property("Op", unreal.FPSRWeaponModOp.ADDITIVE)
    rl.set_editor_property("bThisWeaponOnly", True)
    rl.set_editor_property("RarityTiers", [tier(C,-0.1),tier(RA,-0.1),tier(E,-0.1),tier(L,-0.1)])
    c.set_editor_property("Effects", [effs[0], rl])
    save(p)
    log("CARD MagSize_ThisWeapon OK effects=%d" % len(aslist(c.get_editor_property("Effects"))))
except Exception as ex:
    err("MagSize multi-effect: %s" % ex)

# Mission windows: 2 hold-zone windows at exactly 120 and 240 (keyword construct)
try:
    p = "/Game/Game/Data/DA_RunSchedule"
    rs = EAL.load_asset(p)
    hz = EAL.load_asset("/Game/Mission/Data/DA_Mission_HoldZone")
    w0 = unreal.FPSRMissionWindow(min_time=120.0, max_time=120.0, mission_pool=[hz])
    w1 = unreal.FPSRMissionWindow(min_time=240.0, max_time=240.0, mission_pool=[hz])
    rs.set_editor_property("MissionWindows", [w0, w1])
    save(p)
    chk = aslist(rs.get_editor_property("MissionWindows"))
    log("RUNSCHED windows OK count=%d -> %s" % (len(chk), [(w.get_editor_property('MinTime'), [m.get_name() for m in aslist(w.get_editor_property('MissionPool'))]) for w in chk]))
except Exception as ex:
    err("RUNSCHED windows: %s" % ex)

log("===BALFIX_DONE===")
