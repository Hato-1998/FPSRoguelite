# U18a migration verification (read-only). Loads every UFPSRCardDataAsset under /Game/Cards and reports the
# migrated polymorphic Effects / Group / OfferRarities so we can confirm PostLoad migration is faithful.
import unreal

paths = unreal.EditorAssetLibrary.list_assets("/Game/Cards", recursive=True, include_folder=False)
total = 0
bad = 0
for p in paths:
    asset = unreal.EditorAssetLibrary.load_asset(p)
    if not isinstance(asset, unreal.FPSRCardDataAsset):
        continue
    total += 1
    effects = asset.get_editor_property("Effects")
    group = asset.get_editor_property("Group")
    rarities = asset.get_editor_property("OfferRarities")
    family = asset.get_editor_property("CardFamily")
    parts = []
    for e in effects:
        if e is None:
            parts.append("None")
            continue
        cls = e.get_class().get_name()
        tiers = e.get_editor_property("RarityTiers")
        detail = cls
        if cls == "CardEffect_WeaponStat":
            detail = "%s(stat=%s,op=%s,thisOnly=%s)" % (
                cls,
                str(e.get_editor_property("Stat")),
                str(e.get_editor_property("Op")),
                str(e.get_editor_property("bThisWeaponOnly")))
        elif cls == "CardEffect_WeaponBehavior":
            frag = e.get_editor_property("Fragment")
            detail = "%s(frag=%s)" % (cls, frag.get_name() if frag else "None")
        elif cls == "CardEffect_CharacterGE":
            ge = e.get_editor_property("Effect")
            detail = "%s(ge=%s)" % (cls, ge.get_name() if ge else "None")
        detail += "[tiers=%d]" % len(tiers)
        parts.append(detail)
    flag = "" if len(effects) > 0 else "  <<< EMPTY EFFECTS"
    if len(effects) == 0:
        bad += 1
    unreal.log("MIGCHK %-32s | Group=%-14s | OfferRar=%d | Effects=%d : %s%s" % (
        asset.get_name(), str(group), len(rarities), len(effects), " + ".join(parts), flag))

unreal.log("MIGCHK_SUMMARY total=%d emptyEffects=%d" % (total, bad))
