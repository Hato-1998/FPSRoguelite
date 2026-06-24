# Phase B balance content edits (run headless with editor CLOSED via -run=pythonscript).
# Idempotent: re-running sets the same values. Per-asset try/except so one failure doesn't block the rest.
import unreal

def log(m): unreal.log_warning("[BALAPPLY] " + str(m))
def err(m): unreal.log_error("[BALAPPLY][ERR] " + str(m))

EAL = unreal.EditorAssetLibrary
R = unreal.CardRarity  # COMMON/RARE/EPIC/LEGENDARY

def save(path):
    EAL.save_asset(path, only_if_is_dirty=False)

def aslist(x):
    try: return [e for e in x]
    except Exception: return []

def make_tier(rarity, mag):
    t = unreal.FPSRCardRarityTier()
    t.set_editor_property("Rarity", rarity)
    t.set_editor_property("Magnitude", float(mag))
    return t

def set_effect_tiers(effect, pairs):
    effect.set_editor_property("RarityTiers", [make_tier(r, m) for (r, m) in pairs])

# ---------- 1. WEAPON DAMAGES (BaseStats nested struct: get -> set fields -> set back) ----------
def set_weapon(name, damage, pellet=None, charge_tick=None):
    path = "/Game/Weapons/DataTable/DA_Weapon_%s" % name
    try:
        w = EAL.load_asset(path)
        bs = w.get_editor_property("BaseStats")
        bs.set_editor_property("Damage", float(damage))
        if pellet is not None: bs.set_editor_property("PelletCount", int(pellet))
        if charge_tick is not None: bs.set_editor_property("ChargeTickDamage", float(charge_tick))
        w.set_editor_property("BaseStats", bs)
        save(path)
        log("WEAPON %s Damage=%s pellet=%s tick=%s" % (name, damage, pellet, charge_tick))
    except Exception as e:
        err("WEAPON %s: %s" % (name, e))

# HP 180 -> per-shot damages. Shotgun per-pellet = 180/8 = 22.5. ChargeLaser payoff 90 + tick 4.
set_weapon("Rifle", 12)
set_weapon("BurstRifle", 20)
set_weapon("Sniper", 60)
set_weapon("Shotgun", 22.5, pellet=8)
set_weapon("LMG", 9)
set_weapon("Bazooka", 90)
set_weapon("Grenade", 60)
set_weapon("ChargeLaser", 90, charge_tick=4)
set_weapon("Knife", 90)

# ---------- 2. BOSS HP ----------
try:
    p = "/Game/Character/Boss/DA_BossDefinition"
    bd = EAL.load_asset(p)
    bd.set_editor_property("MaxHealth", 24000.0)
    save(p); log("BOSS MaxHealth=24000")
except Exception as e:
    err("BOSS: %s" % e)

# ---------- 3. ENEMY HP (BP_EnemyBase inherited health component CDO) ----------
try:
    p = "/Game/Character/Enemy/BP_EnemyBase"
    bp = EAL.load_asset(p)
    cdo = unreal.get_default_object(bp.generated_class())
    hc = cdo.get_editor_property("HealthComponent")
    hc.set_editor_property("MaxHealth", 180.0)
    try: hc.set_editor_property("Health", 180.0)
    except Exception: pass
    save(p); log("ENEMY HealthComponent.MaxHealth=180 (read-back=%s)" % hc.get_editor_property("MaxHealth"))
except Exception as e:
    err("ENEMY HP: %s" % e)

# ---------- 4. RUN SCHEDULE: rates + 2 hold-zone windows at 120 & 240 ----------
try:
    p = "/Game/Game/Data/DA_RunSchedule"
    rs = EAL.load_asset(p)
    rs.set_editor_property("BossTime", 300.0)
    rs.set_editor_property("BaseAliveCount", 40)
    rs.set_editor_property("AliveCountPerMinute", 30.0)
    rs.set_editor_property("AliveCountPerMinuteAfterBoss", 50.0)
    rs.set_editor_property("MaxAliveCount", 300)
    holdzone = EAL.load_asset("/Game/Mission/Data/DA_Mission_HoldZone")
    def window(t):
        w = unreal.FPSRMissionWindow()
        w.set_editor_property("MinTime", float(t))
        w.set_editor_property("MaxTime", float(t))
        w.set_editor_property("MissionPool", [holdzone])
        return w
    rs.set_editor_property("MissionWindows", [window(120), window(240)])
    save(p)
    log("RUNSCHED windows=120/240 pool=HoldZone rates 30/50 max300")
except Exception as e:
    err("RUNSCHED: %s" % e)

# ---------- 5. MISSION HoldZone TimeLimit = 60 ----------
try:
    p = "/Game/Mission/Data/DA_Mission_HoldZone"
    mh = EAL.load_asset(p)
    mh.set_editor_property("TimeLimit", 60.0)
    save(p); log("MISSION_HoldZone TimeLimit=60")
except Exception as e:
    err("MISSION: %s" % e)

# ---------- 6. CENTRAL CARD POOL: keep only Damage/MaxHealth/Luck/HealthRegen ----------
try:
    p = "/Game/Cards/Character/DA_Character_CardPool"
    cp = EAL.load_asset(p)
    keep_names = {"DA_Card_Damage", "DA_Card_MaxHealth", "DA_Card_Luck", "DA_Card_Character_HealthRegen"}
    cur = aslist(cp.get_editor_property("Cards"))
    new = [c for c in cur if c and c.get_name() in keep_names]
    cp.set_editor_property("Cards", new)
    save(p); log("CARDPOOL.Cards -> %s" % [c.get_name() for c in new])
except Exception as e:
    err("CARDPOOL: %s" % e)

# ---------- 7. WEAPON CARDS: drop RecoilVertical_ThisWeapon (keep FireRate/MagSize + fragments) ----------
for name in ["Rifle","BurstRifle","Sniper","Shotgun","LMG","Bazooka","Grenade","ChargeLaser","Knife"]:
    try:
        p = "/Game/Weapons/DataTable/DA_Weapon_%s" % name
        w = EAL.load_asset(p)
        cur = aslist(w.get_editor_property("WeaponCards"))
        new = [c for c in cur if c and c.get_name() != "DA_Card_RecoilVertical_ThisWeapon"]
        if len(new) != len(cur):
            w.set_editor_property("WeaponCards", new)
            save(p); log("WEAPONCARDS %s -> %s" % (name, [c.get_name() for c in new]))
    except Exception as e:
        err("WEAPONCARDS %s: %s" % (name, e))

# ---------- 8. CARD MAGNITUDES ----------
# CharacterGE Damage card: magnitude is a percent (GE applies x0.01 to GlobalDamageMultiplier).
def set_single_effect_card(path, pairs):
    try:
        c = EAL.load_asset(path)
        effs = aslist(c.get_editor_property("Effects"))
        set_effect_tiers(effs[0], pairs)
        c.set_editor_property("OfferRarities", [r for (r, _) in pairs])
        save(path); log("CARD %s tiers=%s" % (path.split('/')[-1], pairs))
    except Exception as e:
        err("CARD %s: %s" % (path, e))

C, RA, E, L = R.COMMON, R.RARE, R.EPIC, R.LEGENDARY
set_single_effect_card("/Game/Cards/Character/Data/DA_Card_Damage", [(C,2.5),(RA,5.0),(E,6.5),(L,7.5)])
set_single_effect_card("/Game/Cards/Character/Data/DA_Card_MaxHealth", [(C,5.0),(RA,8.0),(E,12.0),(L,15.0)])
set_single_effect_card("/Game/Cards/Character/Data/DA_Card_Character_HealthRegen", [(C,0.5),(RA,1.0),(E,1.5),(L,2.0)])
set_single_effect_card("/Game/Cards/Character/Data/DA_Card_Luck", [(C,1.0),(RA,2.0),(E,3.0),(L,4.0)])  # was 2 tiers -> 4
set_single_effect_card("/Game/Cards/Weapons/Card/DA_Card_FireRate_ThisWeapon", [(C,0.01),(RA,0.03),(E,0.05),(L,0.07)])

# MagSize ThisWeapon: % per rarity (5/7/10/10) + ADD a second effect (ReloadTime -0.1s flat, all rarities)
try:
    p = "/Game/Cards/Weapons/Card/DA_Card_MagSize_ThisWeapon"
    c = EAL.load_asset(p)
    effs = aslist(c.get_editor_property("Effects"))
    set_effect_tiers(effs[0], [(C,0.05),(RA,0.07),(E,0.10),(L,0.10)])  # MagSize %
    # Second effect: ReloadTime additive -0.1 (flat across rarities), this-weapon
    rl = unreal.new_object(unreal.CardEffect_WeaponStat, c)
    rl.set_editor_property("Stat", unreal.FPSRWeaponStat.RELOAD_TIME)
    rl.set_editor_property("Op", unreal.FPSRWeaponModOp.ADDITIVE)
    rl.set_editor_property("bThisWeaponOnly", True)
    set_effect_tiers(rl, [(C,-0.1),(RA,-0.1),(E,-0.1),(L,-0.1)])
    c.set_editor_property("Effects", [effs[0], rl])
    c.set_editor_property("OfferRarities", [C, RA, E, L])
    save(p); log("CARD MagSize_ThisWeapon: MagSize% + ReloadTime -0.1 (effects=%d)" % len(aslist(c.get_editor_property("Effects"))))
except Exception as e:
    err("CARD MagSize multi-effect: %s" % e)

log("===BALANCE_APPLY_DONE===")
