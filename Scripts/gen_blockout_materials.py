# Headless generator for FPSRoguelite S1 developer-blockout ("orange box") materials.
#
# Creates ONE parametric master material (world-aligned grid + tint) and a set of colored
# Material Instances used to differentiate a greybox CyberCity map: buildings (obstacles) vs
# roads/ground (walkable) vs gameplay zones (spawn / mission / boss / portal doors).
#
# Run (editor closed) via commandlet:
#   "D:\UnrealEngine\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" \
#     "E:\Git_Project\FPSRoguelite\FPSRoguelite.uproject" \
#     -run=pythonscript -script="E:\Git_Project\FPSRoguelite\Scripts\gen_blockout_materials.py"
# OR (editor open) paste the path into: Tools > Execute Python Script.
#
# Output: /Game/_SyntyPilot/DevBlockout/  (throwaway pilot content — not committed until art gate)
#
# Robustness: the master's grid graph is wrapped in try/except. If any grid node/pin mismatches
# on this engine build, the master degrades to a FLAT tinted color (still fully usable for
# differentiation) and every instance keeps working. Self-diagnosing logs mirror gen_input_assets.py.

import unreal

OUT = "/Game/_SyntyPilot/DevBlockout"
MEL = unreal.MaterialEditingLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("[GENBLOCKOUT] " + str(msg))


def warn(msg):
    unreal.log_warning("[GENBLOCKOUT] " + str(msg))


# --------------------------------------------------------------------------------------------------
# Node helpers
# --------------------------------------------------------------------------------------------------
def mk(mat, cls, x, y):
    """Create a material expression node."""
    return MEL.create_material_expression(mat, cls, x, y)


def conn(a, aout, b, bin_name):
    """Connect a.aout -> b.bin_name. Retries with an empty input pin name (single-input math nodes
    like Frac/Abs/Saturate expose their input as "") so a pin-name guess never hard-fails."""
    if MEL.connect_material_expressions(a, aout, b, bin_name):
        return True
    if bin_name != "" and MEL.connect_material_expressions(a, aout, b, ""):
        return True
    raise Exception("connect failed: %s.%s -> %s.%s" % (a.get_name(), aout, b.get_name(), bin_name))


def conn_prop(a, aout, prop):
    if not MEL.connect_material_property(a, aout, prop):
        raise Exception("connect_property failed: %s.%s -> %s" % (a.get_name(), aout, str(prop)))


def lc(r, g, b, a=1.0):
    return unreal.LinearColor(r, g, b, a)


# --------------------------------------------------------------------------------------------------
# Master material
# --------------------------------------------------------------------------------------------------
def build_master():
    path = OUT + "/M_Dev_Blockout"
    if eal.does_asset_exist(path):
        log("Master already exists, deleting to rebuild: " + path)
        eal.delete_asset(path)

    mat = asset_tools.create_asset("M_Dev_Blockout", OUT, unreal.Material, unreal.MaterialFactoryNew())

    # --- Parameters (overridden per-instance) -----------------------------------------------------
    tint = mk(mat, unreal.MaterialExpressionVectorParameter, -900, 0)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", lc(1.0, 0.40, 0.10))  # orange box default

    line_color = mk(mat, unreal.MaterialExpressionVectorParameter, -900, 200)
    line_color.set_editor_property("parameter_name", "LineColor")
    line_color.set_editor_property("default_value", lc(0.015, 0.015, 0.02))  # near-black grid lines

    grid_size = mk(mat, unreal.MaterialExpressionScalarParameter, -900, 400)
    grid_size.set_editor_property("parameter_name", "GridSize")
    grid_size.set_editor_property("default_value", 100.0)  # cm per grid cell (matches placement grid)

    line_width = mk(mat, unreal.MaterialExpressionScalarParameter, -900, 500)
    line_width.set_editor_property("parameter_name", "LineWidth")
    line_width.set_editor_property("default_value", 0.03)  # line thickness as fraction of a cell

    emissive_strength = mk(mat, unreal.MaterialExpressionScalarParameter, -900, 600)
    emissive_strength.set_editor_property("parameter_name", "EmissiveStrength")
    emissive_strength.set_editor_property("default_value", 0.0)  # zones raise this so they self-light

    # --- Fixed surface look -----------------------------------------------------------------------
    rough = mk(mat, unreal.MaterialExpressionConstant, -300, 700)
    rough.set_editor_property("r", 0.9)
    conn_prop(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    # Metallic left at default 0.

    # Emissive = Tint * EmissiveStrength  (so zone instances glow in their own hue)
    emul = mk(mat, unreal.MaterialExpressionMultiply, -300, 600)
    conn(tint, "", emul, "A")
    conn(emissive_strength, "", emul, "B")
    conn_prop(emul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # --- Base color: try grid overlay, else flat tint ---------------------------------------------
    base_source = tint          # fallback
    base_out = ""
    try:
        # World-aligned grid so the lines are TRUE scale regardless of each Synty mesh's own UVs.
        wp = mk(mat, unreal.MaterialExpressionWorldPosition, -700, 900)
        wpxy = mk(mat, unreal.MaterialExpressionComponentMask, -560, 900)
        wpxy.set_editor_property("r", True)
        wpxy.set_editor_property("g", True)
        wpxy.set_editor_property("b", False)
        wpxy.set_editor_property("a", False)
        conn(wp, "", wpxy, "")

        div = mk(mat, unreal.MaterialExpressionDivide, -420, 900)
        conn(wpxy, "", div, "A")
        conn(grid_size, "", div, "B")

        frac = mk(mat, unreal.MaterialExpressionFrac, -300, 900)
        conn(div, "", frac, "")

        half = mk(mat, unreal.MaterialExpressionConstant, -420, 1050)
        half.set_editor_property("r", 0.5)

        subh = mk(mat, unreal.MaterialExpressionSubtract, -180, 900)
        conn(frac, "", subh, "A")
        conn(half, "", subh, "B")

        absn = mk(mat, unreal.MaterialExpressionAbs, -60, 900)  # 0 at cell center, 0.5 at boundary
        conn(subh, "", absn, "")

        rmask = mk(mat, unreal.MaterialExpressionComponentMask, 80, 820)
        rmask.set_editor_property("r", True)
        rmask.set_editor_property("g", False)
        rmask.set_editor_property("b", False)
        rmask.set_editor_property("a", False)
        conn(absn, "", rmask, "")

        gmask = mk(mat, unreal.MaterialExpressionComponentMask, 80, 980)
        gmask.set_editor_property("r", False)
        gmask.set_editor_property("g", True)
        gmask.set_editor_property("b", False)
        gmask.set_editor_property("a", False)
        conn(absn, "", gmask, "")

        maxn = mk(mat, unreal.MaterialExpressionMax, 220, 900)  # high (~0.5) near ANY boundary
        conn(rmask, "", maxn, "A")
        conn(gmask, "", maxn, "B")

        edge = mk(mat, unreal.MaterialExpressionSubtract, 220, 1080)  # 0.5 - LineWidth
        conn(half, "", edge, "A")
        conn(line_width, "", edge, "B")

        diff = mk(mat, unreal.MaterialExpressionSubtract, 360, 950)
        conn(maxn, "", diff, "A")
        conn(edge, "", diff, "B")

        scaled = mk(mat, unreal.MaterialExpressionDivide, 480, 950)
        conn(diff, "", scaled, "A")
        conn(line_width, "", scaled, "B")

        sat = mk(mat, unreal.MaterialExpressionSaturate, 600, 950)  # line mask 0..1
        conn(scaled, "", sat, "")

        lerp = mk(mat, unreal.MaterialExpressionLinearInterpolate, 760, 200)
        conn(tint, "", lerp, "A")
        conn(line_color, "", lerp, "B")
        conn(sat, "", lerp, "Alpha")

        base_source = lerp
        log("Grid overlay built OK.")
    except Exception as e:
        warn("Grid overlay failed (%s) -> falling back to FLAT tint base color." % e)
        base_source = tint

    conn_prop(base_source, base_out, unreal.MaterialProperty.MP_BASE_COLOR)

    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    eal.save_asset(path)
    log("Master saved: " + path)
    return mat


# --------------------------------------------------------------------------------------------------
# Instances
# --------------------------------------------------------------------------------------------------
# (name, tint RGB, emissive strength)  -- emissive raises zone visibility in any lighting.
INSTANCES = [
    ("MI_Dev_Building",      (1.00, 0.40, 0.10), 0.0),   # buildings = solid obstacles (classic orange)
    ("MI_Dev_Wall",          (0.85, 0.28, 0.06), 0.0),   # interior walls / low cover (darker amber)
    ("MI_Dev_Road",          (0.05, 0.05, 0.06), 0.0),   # roads / lanes (walkable, near-black)
    ("MI_Dev_Ground",        (0.20, 0.20, 0.23), 0.0),   # plaza / ground fill (mid grey)
    ("MI_Dev_Zone_Spawn",    (0.75, 0.04, 0.04), 0.35),  # enemy spawn zones (red)
    ("MI_Dev_Zone_Mission",  (0.05, 0.30, 0.85), 0.35),  # mission / hold zones (blue)
    ("MI_Dev_Zone_Boss",     (0.45, 0.05, 0.65), 0.35),  # boss arena (purple)
    ("MI_Dev_Door",          (0.00, 0.70, 0.70), 0.5),   # the 4 cardinal portal doors (teal, Concept 1-C-9)
]


def build_instances(master):
    made = 0
    for name, rgb, emis in INSTANCES:
        path = OUT + "/" + name
        if eal.does_asset_exist(path):
            eal.delete_asset(path)
        mi = asset_tools.create_asset(name, OUT, unreal.MaterialInstanceConstant,
                                      unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, master)
        MEL.set_material_instance_vector_parameter_value(mi, "Tint", lc(rgb[0], rgb[1], rgb[2]))
        if emis > 0.0:
            MEL.set_material_instance_scalar_parameter_value(mi, "EmissiveStrength", emis)
        eal.save_asset(path)
        made += 1
        log("  instance: %s  tint=%s emis=%s" % (name, rgb, emis))
    return made


# --------------------------------------------------------------------------------------------------
def main():
    if not eal.does_directory_exist(OUT):
        eal.make_directory(OUT)
    log("Building developer-blockout materials into " + OUT)
    master = build_master()
    n = build_instances(master)
    log("DONE. Master + %d instances created. Apply MI_Dev_* to your greybox actors "
        "(Details > Materials, or drag onto the mesh)." % n)


main()
