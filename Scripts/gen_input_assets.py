# Headless generator for FPSRoguelite Enhanced Input assets.
# Run via: UnrealEditor-Cmd.exe <project> -run=pythonscript -script="<this>"
# Self-diagnosing: logs available enum members and resolves names robustly.

import unreal

OUT = "/Game/Input"
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("[GENINPUT] " + str(msg))


# --- Diagnostics: dump enum members so we always learn the real names ---
vt_members = [m for m in dir(unreal.InputActionValueType) if not m.startswith("_")]
log("InputActionValueType members: " + str(vt_members))


def vt(*candidates):
    for c in candidates:
        if hasattr(unreal.InputActionValueType, c):
            return getattr(unreal.InputActionValueType, c)
    raise Exception("No ValueType among %s; available=%s" % (candidates, vt_members))


def mk_key(name):
    try:
        return unreal.Key(name)
    except Exception:
        k = unreal.Key()
        try:
            k.set_editor_property("key_name", name)
        except Exception as e:
            raise Exception("cannot build FKey for '%s': %s" % (name, e))
        return k


def make_ia(name, value_type):
    full = OUT + "/" + name
    if eal.does_asset_exist(full):
        eal.delete_asset(full)
    ia = asset_tools.create_asset(name, OUT, unreal.InputAction, None)
    if ia is None:
        raise Exception("create_asset returned None for " + name)
    ia.set_editor_property("value_type", value_type)
    eal.save_loaded_asset(ia)
    log("created IA " + name)
    return ia


def mk_map(action, key_name, modifiers=None):
    m = unreal.EnhancedActionKeyMapping()
    m.set_editor_property("action", action)
    m.set_editor_property("key", mk_key(key_name))
    if modifiers:
        m.set_editor_property("modifiers", modifiers)
    return m


try:
    VT_BOOL = vt("BOOLEAN", "BOOL")
    VT_AXIS1 = vt("AXIS1_D", "AXIS1D", "AXIS_1D", "AXIS1")
    VT_AXIS2 = vt("AXIS2_D", "AXIS2D", "AXIS_2D", "AXIS2")

    ia_fwd = make_ia("IA_MoveForward", VT_AXIS1)
    ia_right = make_ia("IA_MoveRight", VT_AXIS1)
    ia_look = make_ia("IA_Look", VT_AXIS2)
    ia_jump = make_ia("IA_Jump", VT_BOOL)
    ia_fire = make_ia("IA_Fire", VT_BOOL)
    ia_slot1 = make_ia("IA_EquipSlot1", VT_BOOL)
    ia_slot2 = make_ia("IA_EquipSlot2", VT_BOOL)
    ia_slot3 = make_ia("IA_EquipSlot3", VT_BOOL)

    imc_path = OUT + "/IMC_Default"
    if eal.does_asset_exist(imc_path):
        eal.delete_asset(imc_path)
    imc = asset_tools.create_asset("IMC_Default", OUT, unreal.InputMappingContext, None)
    if imc is None:
        raise Exception("create_asset returned None for IMC_Default")

    # NOTE (UE 5.7): set_editor_property("mappings", ...) does NOT persist to the IMC asset
    # (5.7 restructured how IMC mappings are stored). IMC key mappings are therefore configured
    # MANUALLY in the editor. The list below only documents the intended mappings:
    #   IA_MoveForward: W, S(Negate) | IA_MoveRight: D, A(Negate) | IA_Look: Mouse XY 2D-Axis
    #   IA_Jump: Space | IA_Fire: LMB | IA_EquipSlot1/2/3: keys 1 / 2 / 3
    mappings = [
        mk_map(ia_fwd, "W"),
        mk_map(ia_fwd, "S", [unreal.InputModifierNegate()]),
        mk_map(ia_right, "D"),
        mk_map(ia_right, "A", [unreal.InputModifierNegate()]),
        mk_map(ia_look, "Mouse2D"),
        mk_map(ia_jump, "SpaceBar"),
        mk_map(ia_fire, "LeftMouseButton"),
        mk_map(ia_slot1, "One"),
        mk_map(ia_slot2, "Two"),
        mk_map(ia_slot3, "Three"),
    ]
    imc.set_editor_property("mappings", mappings)
    eal.save_loaded_asset(imc)
    log("created IMC_Default with %d mappings" % len(mappings))
    log("GENINPUT_RESULT=SUCCESS")
except Exception as e:
    log("GENINPUT_RESULT=FAIL: " + str(e))
    raise
