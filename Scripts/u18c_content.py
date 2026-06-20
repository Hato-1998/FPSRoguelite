# U18c content authoring (headless). Feature-unlock fragments B + C.
#   B) DA_Fragment_AmmoOnMiss (UFPSRFragment_AmmoOnMiss) + DA_CardModifiers_AmmoOnMiss -> LMG UnlockableFeatures.
#   C) DA_Fragment_ReloadOnKill (UFPSRFragment_ReloadOnKill) + DA_CardModifiers_ReloadOnKill -> Shotgun/Bazooka UnlockableFeatures.
# Cards mirror DA_CardModifiers_MultiShot (a working behavior-unlock card): Group=Weapon, single Common offer
# (behavior cards carry no RarityTiers). Idempotent: load-if-exists. Run headless with the editor CLOSED.
import unreal

MOD_DIR = "/Game/Cards/Weapons/Modifiers"
WEAPON_DIR = "/Game/Weapons/DataTable"
TEMPLATE = MOD_DIR + "/DA_CardModifiers_MultiShot"

tools = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary

def pkg(a):
    return a.get_path_name().split(".")[0]

# --- STEP 0: dump the template behavior card so we replicate its working field set ---
tmpl = EAL.load_asset(TEMPLATE)
tmpl_eff = None  # the template effect — copy its RarityTiers so OfferRarities derives identically (LEGENDARY label)
if not tmpl:
    unreal.log_error("STEP0 template missing: %s" % TEMPLATE)
else:
    te = list(tmpl.get_editor_property("Effects"))
    if te and te[0]:
        tmpl_eff = te[0]
    try:
        unreal.log("STEP0 template Group=%s Weight=%s OfferRarities=%s nEffects=%d" % (
            tmpl.get_editor_property("Group"),
            tmpl.get_editor_property("Weight"),
            list(tmpl.get_editor_property("OfferRarities")),
            len(list(tmpl.get_editor_property("Effects")))))
        for e in tmpl.get_editor_property("Effects"):
            if e:
                unreal.log("STEP0   effect=%s" % e.get_class().get_name())
    except Exception as ex:
        unreal.log_error("STEP0 dump failed: %s" % ex)

# (frag_asset, frag_class, frag_props, card_asset, display, target_weapons)
SPECS = [
    ("DA_Fragment_AmmoOnMiss", unreal.FPSRFragment_AmmoOnMiss, [("RefillAmount", 1)],
     "DA_CardModifiers_AmmoOnMiss", "Ammo on Miss", ["LMG"]),
    ("DA_Fragment_ReloadOnKill", unreal.FPSRFragment_ReloadOnKill, [("bInstantRefill", True)],
     "DA_CardModifiers_ReloadOnKill", "Reload on Kill", ["Shotgun", "Bazooka"]),
]

created = []
for fname, fclass, fprops, cname, disp, targets in SPECS:
    # --- fragment asset ---
    fpath = MOD_DIR + "/" + fname
    frag = EAL.load_asset(fpath) if EAL.does_asset_exist(fpath) else tools.create_asset(fname, MOD_DIR, fclass, None)
    if not frag:
        unreal.log_error("FRAG create failed: %s" % fname)
        continue
    for k, v in fprops:
        try:
            frag.set_editor_property(k, v)
        except Exception as ex:
            unreal.log_error("FRAG %s set %s=%s failed: %s" % (fname, k, v, ex))
    frag.set_editor_property("DisplayName", disp)
    EAL.save_asset(fpath)
    unreal.log("FRAG %-26s created/updated" % fname)

    # --- card asset (WeaponBehavior -> fragment) ---
    cpath = MOD_DIR + "/" + cname
    card = EAL.load_asset(cpath) if EAL.does_asset_exist(cpath) else tools.create_asset(cname, MOD_DIR, unreal.FPSRCardDataAsset, None)
    if not card:
        unreal.log_error("CARD create failed: %s" % cname)
        continue
    eff = unreal.new_object(unreal.CardEffect_WeaponBehavior, card)
    eff.set_editor_property("Fragment", frag)
    # Mirror the template's rarity label so OfferRarities derives identically on PostLoad (behavior magnitude unused).
    if tmpl_eff:
        eff.set_editor_property("RarityTiers", tmpl_eff.get_editor_property("RarityTiers"))
    card.set_editor_property("Group", unreal.CardGroup.WEAPON)
    card.set_editor_property("Weight", 1.0)
    card.set_editor_property("DisplayName", disp)
    card.set_editor_property("Effects", [eff])
    # OfferRarities is read-only (auto-maintained from the effects on PostLoad/PostEditChange) — do NOT set it.
    EAL.save_asset(cpath)
    unreal.log("CARD %-26s -> %s" % (cname, fname))
    created.append((card, cpath, targets))

# --- place cards in target weapons' UnlockableFeatures ---
for card, cpath, targets in created:
    for wname in targets:
        wpath = WEAPON_DIR + "/DA_Weapon_" + wname
        w = EAL.load_asset(wpath)
        if not w:
            unreal.log_error("WEAPON missing: %s" % wname)
            continue
        uf = list(w.get_editor_property("UnlockableFeatures"))
        if card not in uf:
            uf.append(card)
            w.set_editor_property("UnlockableFeatures", uf)
            EAL.save_asset(wpath)
        unreal.log("PLACE %-10s UnlockableFeatures=%d (+%s)" % (wname, len(uf), card.get_name()))

# --- validate: reload + dump ---
for card, cpath, targets in created:
    rc = EAL.load_asset(cpath)
    effs = list(rc.get_editor_property("Effects")) if rc else []
    fragref = effs[0].get_editor_property("Fragment") if effs else None
    unreal.log("VERIFY %-26s Group=%s nEffects=%d frag=%s OfferRarities=%s" % (
        rc.get_name(), rc.get_editor_property("Group"), len(effs),
        fragref.get_name() if fragref else "NONE",
        list(rc.get_editor_property("OfferRarities"))))

unreal.log("U18C_CONTENT done")
