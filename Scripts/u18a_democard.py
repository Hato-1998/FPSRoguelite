# U18a demo: author a multi-effect "Glass Cannon" card (FireRate +25% & Damage -15% on THIS weapon) to exercise the
# new v2 polymorphic effect array + CardFamily mutual-exclusion + rarity coverage end-to-end, then wire it into the
# Rifle's WeaponCards so it appears in the level-up draw when the Rifle is owned.
import unreal

CARD_DIR = "/Game/Cards/Weapons/Card"
CARD_NAME = "DA_Card_Weapon_GlassCannon"
CARD_PATH = CARD_DIR + "/" + CARD_NAME
WEAPON_PATH = "/Game/Weapons/DataTable/DA_Weapon_Rifle"

tools = unreal.AssetToolsHelpers.get_asset_tools()

# --- create or load the card asset ---
if unreal.EditorAssetLibrary.does_asset_exist(CARD_PATH):
    card = unreal.EditorAssetLibrary.load_asset(CARD_PATH)
    unreal.log("DEMO load existing card")
else:
    card = tools.create_asset(CARD_NAME, CARD_DIR, unreal.FPSRCardDataAsset, None)
    unreal.log("DEMO create_asset -> %s" % ("ok" if card else "FAIL"))

if not card:
    unreal.log_error("DEMO_RESULT card creation failed")
else:
    def make_stat(stat, op, this_only, mag):
        e = unreal.new_object(unreal.CardEffect_WeaponStat, card)
        e.set_editor_property("Stat", stat)
        e.set_editor_property("Op", op)
        e.set_editor_property("bThisWeaponOnly", this_only)
        # Build the tier via the struct constructor (loose-struct EditDefaultsOnly fields can't be set post-hoc).
        tier = unreal.FPSRCardRarityTier(rarity=unreal.CardRarity.COMMON, magnitude=mag)
        e.set_editor_property("RarityTiers", [tier])
        return e

    e_fire = make_stat(unreal.FPSRWeaponStat.FIRE_RATE, unreal.FPSRWeaponModOp.PERCENT_MULTIPLY, True, 0.25)
    e_dmg = make_stat(unreal.FPSRWeaponStat.DAMAGE, unreal.FPSRWeaponModOp.PERCENT_MULTIPLY, True, -0.15)

    card.set_editor_property("DisplayName", unreal.Text("Glass Cannon"))
    card.set_editor_property("Description", unreal.Text("This weapon: +25% fire rate, -15% damage."))
    card.set_editor_property("Group", unreal.CardGroup.WEAPON)
    card.set_editor_property("Weight", 1.0)
    # Setting Effects fires PostEditChangeProperty -> RefreshOfferRarities (OfferRarities is auto-derived/read-only).
    card.set_editor_property("Effects", [e_fire, e_dmg])
    # Multi-effect cards require CardFamily for IsDataValid (no GE-class fallback in v2). Best-effort headless set
    # (does not affect runtime apply — only mutual-exclusion grouping + editor validation). Set in-editor if skipped.
    try:
        fam = unreal.GameplayTag(tag_name=unreal.Name("Card.Type.Weapon"))
        if fam.is_valid():
            card.set_editor_property("CardFamily", fam)
            unreal.log("DEMO CardFamily set: %s" % fam.to_string())
        else:
            unreal.log("DEMO CardFamily SKIP (tag invalid headless) — set 'Card.Type.Weapon' in editor to pass IsDataValid")
    except Exception as ex:
        unreal.log("DEMO CardFamily SKIP (%s) — set 'Card.Type.Weapon' in editor to pass IsDataValid" % ex)

    saved = unreal.EditorAssetLibrary.save_asset(CARD_PATH)
    eff = card.get_editor_property("Effects")
    ofr = card.get_editor_property("OfferRarities")
    unreal.log("DEMO card saved=%s effects=%d classes=%s offerRarities=%d" % (
        saved, len(eff), ",".join([x.get_class().get_name() if x else "None" for x in eff]), len(ofr)))

    # --- wire into the Rifle's WeaponCards ---
    weapon = unreal.EditorAssetLibrary.load_asset(WEAPON_PATH)
    if not weapon:
        unreal.log_error("DEMO weapon load failed: %s" % WEAPON_PATH)
    else:
        wc = list(weapon.get_editor_property("WeaponCards"))
        if card not in wc:
            wc.append(card)
            weapon.set_editor_property("WeaponCards", wc)
            unreal.EditorAssetLibrary.save_asset(WEAPON_PATH)
            unreal.log("DEMO wired into %s WeaponCards (now %d)" % (weapon.get_name(), len(wc)))
        else:
            unreal.log("DEMO already in %s WeaponCards" % weapon.get_name())

    unreal.log("DEMO_RESULT done")
