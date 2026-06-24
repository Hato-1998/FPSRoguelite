# Balance pass 2: set bShowAsPercent on the clean fraction->percent CharacterGE cards, then sweep IsDataValid
# over all DA_Card_* (review residual c). Run headless with editor CLOSED via -run=pythonscript.
import unreal

EAL = unreal.EditorAssetLibrary
def log(m): unreal.log_warning("[BAL2] " + str(m))
def err(m): unreal.log_error("[BAL2][ERR] " + str(m))
def g(o, n):
    try: return o.get_editor_property(n)
    except Exception as e: return "<err:%s>" % e

# --- 1. bShowAsPercent on fraction->percent CharacterGE cards (Damage active; others future-proof). CritMult excluded
#        (additive to a x2.0 multiplier, not a clean percent); MaxHealth/Luck/Regen are flat. ---
PERCENT_CARDS = [
    "/Game/Cards/Character/Data/DA_Card_Damage",
    "/Game/Cards/Character/Data/DA_Card_CritChance",
    "/Game/Cards/Character/Data/DA_Card_PickupRadius",
    "/Game/Cards/Character/Data/DA_Card_XPGain",
]
log("=== SET bShowAsPercent ===")
for p in PERCENT_CARDS:
    a = EAL.load_asset(p)
    if not a:
        err("MISSING %s" % p); continue
    effs = [e for e in g(a, "Effects")]
    changed = False
    for e in effs:
        if e and e.get_class().get_name() == "CardEffect_CharacterGE":
            e.set_editor_property("bShowAsPercent", True)
            changed = True
    if changed:
        EAL.save_asset(p, only_if_is_dirty=False)
    rb = [(e.get_class().get_name(), g(e, "bShowAsPercent")) for e in effs if e]
    log("  %-32s saved=%s readback=%s" % (p.split("/")[-1], changed, rb))

# --- 2. IsDataValid sweep over all DA_Card_* (exercises the new 0-resolution guard) ---
log("=== IsDataValid SWEEP ===")
evs = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem)
paths = EAL.list_assets("/Game/Cards", recursive=True, include_folder=False)
cards = sorted(set(p.split(".")[0] for p in paths if "DA_Card" in p.split("/")[-1]))
n_valid = n_invalid = n_other = 0
for p in cards:
    a = EAL.load_asset(p)
    if not a:
        continue
    res = None
    for call in (lambda: evs.validate_loaded_asset(a, []), lambda: evs.validate_loaded_asset(a)):
        try:
            res = call(); break
        except Exception as e:
            res = "<err:%s>" % e
    rs = str(res).upper()
    if "INVALID" in rs: tag = "INVALID"; n_invalid += 1
    elif "VALID" in rs: tag = "VALID"; n_valid += 1
    else: tag = str(res); n_other += 1
    log("  %-40s %s" % (a.get_name(), tag))
log("SWEEP totals: VALID=%d INVALID=%d OTHER=%d (of %d)" % (n_valid, n_invalid, n_other, len(cards)))
log("=== BAL2 DONE ===")
