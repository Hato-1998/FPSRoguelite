# U18c c2 content authoring (headless). Character GAS-native recovery cards: Lifesteal + Health Regen.
#   1) GE_Card_LifestealHeal  (Instant,  Health Add, SetByCaller.CardMagnitude)  -- duplicate of BP_Card_MaxHealth, attribute rebound MaxHealth->Health
#   2) GE_Card_HealthRegen    (Infinite, Period 1.0, Health Add, SetByCaller)     -- same + duration/period
#   3) GA_Lifesteal           (BP : UFPSRPassiveAbility_Lifesteal, HealEffect=#1, HealRatio=0.05)
#   4) DA_Card_Character_Lifesteal   (Group=Character, Effects=[CharacterPassive{PassiveAbility=#3}], 1 dummy COMMON tier so it is offerable)
#   5) DA_Card_Character_HealthRegen (Group=Character, Effects=[CharacterGE{Effect=#2}], RarityTiers = HP/sec per rarity)
#   6) DA_Character_CardPool.Cards += #4, #5
#
# ENGINE SOURCE FIRST: the FGameplayAttribute(Health) construction is the one fragile GAS-Python step. This script
#   DUMPS BP_Card_MaxHealth first, then builds the Health attribute via discovered idioms and VERIFIES name=="Health"
#   BEFORE creating any card -- so a wrong idiom aborts cleanly with diagnostics, never leaving a half-authored card.
# Idempotent: duplicate-if-not-exists / load-if-exists; the attribute/CDO rebind is RE-APPLIED every run (so a fixed
#   re-run repairs a prior bad attribute). Run headless with the editor CLOSED:
#   UnrealEditor-Cmd.exe <project> -run=pythonscript -script="<this>" -unattended -nopause -nullrhi -nosplash -nosound -stdout
import re
import unreal

EAL   = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

GE_DIR   = "/Game/Cards/Character/GameplayEffect"
GA_DIR   = "/Game/Cards/Character"
DATA_DIR = "/Game/Cards/Character/Data"
POOL     = "/Game/Cards/Character/DA_Character_CardPool"
GE_TMPL  = GE_DIR + "/BP_Card_MaxHealth"          # SetByCaller.CardMagnitude reference (modifies MaxHealth)

GE_LIFESTEAL = GE_DIR + "/GE_Card_LifestealHeal"
GE_REGEN     = GE_DIR + "/GE_Card_HealthRegen"
GA_LIFESTEAL = GA_DIR + "/GA_Lifesteal"
DA_LIFESTEAL = DATA_DIR + "/DA_Card_Character_Lifesteal"
DA_REGEN     = DATA_DIR + "/DA_Card_Character_HealthRegen"

errors = []
def log(s):  unreal.log("C2| " + s)
def err(s):  errors.append(s); unreal.log_error("C2|ERR " + s)

def gen_class(asset):
    """UClass for a Blueprint/GE asset (TSubclassOf targets + CDO access)."""
    try:
        return asset.generated_class()
    except Exception:
        return unreal.BlueprintEditorLibrary.generated_class(asset)

def attr_name(attr):
    """Readable name of a GameplayAttribute STRUCT (it has no get_name())."""
    try:
        return attr.get_editor_property("attribute_name")
    except Exception:
        try:
            return attr.to_tuple()[0]
        except Exception:
            return None

# ----------------------------------------------------------------------------- STEP 0: dump template
log("==== STEP 0: dump template %s ====" % GE_TMPL)
tmpl = EAL.load_asset(GE_TMPL)
if not tmpl:
    err("template missing: %s -- cannot proceed" % GE_TMPL)
tmpl_cdo = None
tmpl_mod0_attr = None
if tmpl:
    tgen = gen_class(tmpl)
    log("template type=%s generated_class=%s" % (type(tmpl).__name__, tgen.get_name() if tgen else "None"))
    tmpl_cdo = unreal.get_default_object(tgen) if tgen else None
    if tmpl_cdo:
        try:
            log("template duration_policy=%s" % tmpl_cdo.get_editor_property("duration_policy"))
        except Exception as ex:
            err("read template duration_policy: %s" % ex)
        try:
            tmods = tmpl_cdo.get_editor_property("modifiers")
            log("template modifiers=%d" % len(tmods))
            if len(tmods):
                m0 = tmods[0]
                tmpl_mod0_attr = m0.get_editor_property("attribute")
                an = None
                try: an = tmpl_mod0_attr.get_name()
                except Exception: pass
                log("template mod0 attribute repr=%r name=%s type=%s" % (tmpl_mod0_attr, an, type(tmpl_mod0_attr).__name__))
                log("template mod0 attribute dir=%s" % [d for d in dir(tmpl_mod0_attr) if not d.startswith("_")])
                log("template mod0 modifier_op=%s" % m0.get_editor_property("modifier_op"))
        except Exception as ex:
            err("dump template modifiers: %s" % ex)

# ----------------------------------------------------------------------------- STEP 1: build Health attribute (fail-fast)
log("==== STEP 1: construct Health GameplayAttribute (import_text round-trip) ====")
health_attr = None
if tmpl_cdo:
    try:
        src = tmpl_cdo.get_editor_property("modifiers")[0].get_editor_property("attribute").export_text()
        # Reuse the template's exact serialization, swapping only the "MaxHealth" token. "FPSRHealthSet" has no
        # "MaxHealth" substring so the owner class is preserved; import_text rebinds the Attribute FProperty path.
        cand = unreal.GameplayAttribute()
        cand.import_text(src.replace("MaxHealth", "Health"))
        log("constructed Health attr: export_text=%s name=%s" % (cand.export_text(), attr_name(cand)))
        if attr_name(cand) == "Health":
            health_attr = cand
    except Exception as ex:
        err("Health attribute construction raised: %s" % ex)

if not health_attr:
    err("could not construct a GameplayAttribute resolving to 'Health' -- ABORTING before creating any card "
        "(clean state; fix and re-run).")
    log("==== ABORT: %d error(s) ====" % len(errors))
    raise SystemExit(1)
log("Health attribute OK: name=%s" % attr_name(health_attr))

# ----------------------------------------------------------------------------- STEP 2: GE assets (duplicate + rebind)
def author_ge(dst_path, infinite, period):
    name = dst_path.rsplit("/", 1)[-1]
    if not EAL.does_asset_exist(dst_path):
        dup = EAL.duplicate_asset(GE_TMPL, dst_path)
        if not dup:
            err("duplicate failed: %s" % name); return None
        log("GE %-22s duplicated from template" % name)
    ge = EAL.load_asset(dst_path)
    cdo = unreal.get_default_object(gen_class(ge))
    # FGameplayModifierInfo.Attribute is EditDefaultsOnly -> set_editor_property on an extracted struct copy is
    # blocked ("cannot be edited on instances"). Rebuild each modifier from its OWN serialized text with the
    # MaxHealth->Health token swap (import_text bypasses the per-property edit-flag check), then reassign the whole
    # array on the CDO (an archetype, so EditDefaultsOnly is allowed). SetByCaller magnitude is preserved verbatim.
    mods = cdo.get_editor_property("modifiers")
    if not mods:
        err("%s has no modifiers after duplicate" % name); return ge
    new_mods = []
    for m in mods:
        nm = unreal.GameplayModifierInfo()
        nm.import_text(m.export_text().replace("MaxHealth", "Health"))
        new_mods.append(nm)
    cdo.set_editor_property("modifiers", new_mods)
    if infinite:
        cdo.set_editor_property("duration_policy", unreal.GameplayEffectDurationType.INFINITE)
        sf = unreal.ScalableFloat(); sf.set_editor_property("value", float(period))
        cdo.set_editor_property("period", sf)
    else:
        cdo.set_editor_property("duration_policy", unreal.GameplayEffectDurationType.INSTANT)
    EAL.save_asset(dst_path)
    log("GE %-22s authored (duration=%s period=%s)" % (name, "INFINITE" if infinite else "INSTANT", period if infinite else "-"))
    return ge

log("==== STEP 2: GE assets ====")
ge_life  = author_ge(GE_LIFESTEAL, infinite=False, period=0.0)
ge_regen = author_ge(GE_REGEN,     infinite=True,  period=1.0)

# ----------------------------------------------------------------------------- STEP 3: GA_Lifesteal BP
log("==== STEP 3: GA_Lifesteal ====")
if not EAL.does_asset_exist(GA_LIFESTEAL):
    bpf = unreal.BlueprintFactory()
    bpf.set_editor_property("parent_class", unreal.FPSRPassiveAbility_Lifesteal)
    ga = tools.create_asset("GA_Lifesteal", GA_DIR, None, bpf)
    if not ga:
        err("GA_Lifesteal create failed")
    else:
        log("GA_Lifesteal created (parent=UFPSRPassiveAbility_Lifesteal)")
ga = EAL.load_asset(GA_LIFESTEAL)
if ga and ge_life:
    ga_cdo = unreal.get_default_object(gen_class(ga))
    ga_cdo.set_editor_property("HealRatio", 0.05)
    ga_cdo.set_editor_property("HealEffect", gen_class(ge_life))
    try:
        unreal.BlueprintEditorLibrary.compile_blueprint(ga)
    except Exception as ex:
        log("compile_blueprint(GA) note: %s" % ex)
    EAL.save_asset(GA_LIFESTEAL)
    log("GA_Lifesteal authored: HealRatio=0.05 HealEffect=%s" % gen_class(ge_life).get_name())

# ----------------------------------------------------------------------------- STEP 4-5: card DataAssets
# FFPSRCardRarityTier.Rarity/Magnitude are EditDefaultsOnly -> set_editor_property on a detached struct is blocked.
# Build each tier from a REAL template tier's serialized text (DA_Card_MaxHealth), substituting the rarity
# enumerator + magnitude. import_text bypasses the per-property edit-flag check.
TMPL_CARD = DATA_DIR + "/DA_Card_MaxHealth"
_ref_tier_text = None
_tc = EAL.load_asset(TMPL_CARD)
if _tc:
    _te = list(_tc.get_editor_property("Effects"))
    if _te and _te[0]:
        _tt = list(_te[0].get_editor_property("RarityTiers"))
        if _tt:
            _ref_tier_text = _tt[0].export_text()
            log("ref tier text = %s" % _ref_tier_text)
if not _ref_tier_text:
    err("could not read a reference RarityTier from %s -- cannot build tiers" % TMPL_CARD)
    raise SystemExit(1)

def tier(rar_name, mag):                                       # rar_name = ECardRarity enumerator: Common/Rare/Epic/Legendary
    txt = re.sub(r'Rarity=\w+', 'Rarity=' + rar_name, _ref_tier_text)
    txt = re.sub(r'Magnitude=[-\d.eE]+', 'Magnitude=%f' % float(mag), txt)
    t = unreal.FPSRCardRarityTier()
    if not t.import_text(txt):
        err("tier import_text failed: %s" % txt)
    return t

def author_card(dst_path, display, effect_class, set_props, rarity_tiers):
    name = dst_path.rsplit("/", 1)[-1]
    card = EAL.load_asset(dst_path) if EAL.does_asset_exist(dst_path) else tools.create_asset(name, DATA_DIR, unreal.FPSRCardDataAsset, None)
    if not card:
        err("card create failed: %s" % name); return None
    eff = unreal.new_object(effect_class, card)
    for k, v in set_props:
        eff.set_editor_property(k, v)
    eff.set_editor_property("RarityTiers", rarity_tiers)   # drives auto OfferRarities (PostLoad); magnitude unused for passive
    card.set_editor_property("Group", unreal.CardGroup.CHARACTER)
    card.set_editor_property("Weight", 1.0)
    card.set_editor_property("DisplayName", display)
    card.set_editor_property("Effects", [eff])
    EAL.save_asset(dst_path)
    log("CARD %-32s effect=%s" % (name, effect_class.__name__))
    return card

log("==== STEP 4-5: card DataAssets ====")
card_life = None
if ga:
    card_life = author_card(
        DA_LIFESTEAL, "Lifesteal", unreal.CardEffect_CharacterPassive,
        [("PassiveAbility", gen_class(ga))],
        [tier("Common", 0.0)])                          # passive: magnitude unused; single tier => offerable at Common
card_regen = None
if ge_regen:
    card_regen = author_card(
        DA_REGEN, "Health Regen", unreal.CardEffect_CharacterGE,
        [("Effect", gen_class(ge_regen))],
        [tier("Common", 1.0), tier("Rare", 2.0), tier("Epic", 3.0), tier("Legendary", 5.0)])   # HP/sec per rarity

# ----------------------------------------------------------------------------- STEP 6: register in character pool
log("==== STEP 6: register in %s ====" % POOL)
pool = EAL.load_asset(POOL)
if not pool:
    err("pool missing: %s" % POOL)
else:
    cards = list(pool.get_editor_property("Cards"))
    for c in [card_life, card_regen]:
        if c and c not in cards:
            cards.append(c)
    pool.set_editor_property("Cards", cards)
    EAL.save_asset(POOL)
    log("pool Cards=%d" % len(cards))

# ----------------------------------------------------------------------------- VERIFY (fresh reload + invariants)
log("==== VERIFY ====")
def verify_ge(path, want_dur):
    ge = EAL.load_asset(path)
    if not ge: err("verify: missing %s" % path); return
    cdo = unreal.get_default_object(gen_class(ge))
    mods = cdo.get_editor_property("modifiers")
    an = attr_name(mods[0].get_editor_property("attribute")) if mods else "NONE"
    dur = cdo.get_editor_property("duration_policy")
    period_val = None
    if want_dur == "INFINITE":
        try: period_val = cdo.get_editor_property("period").get_editor_property("value")
        except Exception as ex: period_val = "ERR:%s" % ex
    ok = (an == "Health") and (want_dur in str(dur)) and (want_dur != "INFINITE" or period_val == 1.0)
    log("VERIFY GE %-22s attr=%s duration=%s period=%s %s" % (path.rsplit('/',1)[-1], an, dur, period_val, "OK" if ok else "<<FAIL"))
    if not ok: err("GE verify failed: %s (attr=%s dur=%s period=%s)" % (path, an, dur, period_val))

verify_ge(GE_LIFESTEAL, "INSTANT")
verify_ge(GE_REGEN, "INFINITE")

ga_r = EAL.load_asset(GA_LIFESTEAL)
if ga_r:
    cdo = unreal.get_default_object(gen_class(ga_r))
    he = cdo.get_editor_property("HealEffect")
    hr = cdo.get_editor_property("HealRatio")
    ok = (he is not None) and abs(hr - 0.05) < 1e-6
    log("VERIFY GA GA_Lifesteal HealEffect=%s HealRatio=%s %s" % (he.get_name() if he else "NONE", hr, "OK" if ok else "<<FAIL"))
    if not ok: err("GA verify failed (HealEffect=%s HealRatio=%s)" % (he, hr))

for path in [DA_LIFESTEAL, DA_REGEN]:
    c = EAL.load_asset(path)
    if not c: err("verify: missing %s" % path); continue
    effs = list(c.get_editor_property("Effects"))
    offer = list(c.get_editor_property("OfferRarities"))
    grp = c.get_editor_property("Group")
    ref = None
    if effs:
        e0 = effs[0]
        ref = e0.get_editor_property("PassiveAbility") if e0.get_class().get_name().endswith("CharacterPassive") else e0.get_editor_property("Effect")
    ok = (len(effs) == 1) and (len(offer) >= 1) and (ref is not None) and ("CHARACTER" in str(grp))
    log("VERIFY CARD %-32s Group=%s nEff=%d ref=%s OfferRarities=%s %s" % (
        path.rsplit('/',1)[-1], grp, len(effs), ref.get_name() if ref else "NONE", offer, "OK" if ok else "<<FAIL"))
    if not ok: err("card verify failed: %s" % path)

prr = EAL.load_asset(POOL)
if prr:
    names = [x.get_name() for x in prr.get_editor_property("Cards") if x]
    has_both = ("DA_Card_Character_Lifesteal" in names) and ("DA_Card_Character_HealthRegen" in names)
    log("VERIFY POOL Cards=%d bothPresent=%s" % (len(names), has_both))
    if not has_both: err("pool missing the new cards: %s" % names)

if errors:
    log("==== RESULT: FAIL (%d error[s]) ====" % len(errors))
    for e in errors: unreal.log_error("C2|SUMMARY " + e)
    raise SystemExit(1)
log("==== RESULT: SUCCESS -- U18C_C2_CONTENT done ====")
