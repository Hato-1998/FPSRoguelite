# Headless generator for the FPSRoguelite ARCADE-CYBERSPACE look prototype (environment half).
#
# Run (editor CLOSED) via commandlet:
#   "D:\UnrealEngine\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
#     "E:\Git_Project\FPSRoguelite\FPSRoguelite.uproject" ^
#     -run=pythonscript -script="E:\Git_Project\FPSRoguelite\Scripts\gen_arcade_proto_materials.py"
#
# Output: /Game/_ArcadeProto/Materials/   (throwaway prototype content — follows the _SyntyPilot precedent)
#
# WHY a new master instead of LevelPrototyping/MF_ProcGrid: that function is CHECKER-TEXTURE based
# (T_GridChecker_A; params Scale/SizeX/SizeY/TileSize) and has no parametric LINE WIDTH. Measuring the
# A-1 area rule (line width + screen coverage cap) is one of this prototype's two questions, so line
# width must be a scalar parameter. Everything else follows the repo's proven pattern.
#
# API notes learned from Scripts/author_proto_state_material.py (do not "simplify" these):
#   * unreal.CustomInput() must be default-constructed then set_editor_property("input_name", ...).
#     The keyword-constructor form does not stick.
#   * A named Custom input that is NOT connected is a COMPILE ERROR ("Custom material missing input").
#     A fresh Custom node ships with a default pin named "1", so the inputs list must be REPLACED.
#   * connect_material_expressions / connect_material_property RETURN A BOOL. Ignoring it is how a
#     material ends up silently half-wired and constant-black (see memory: material-if-node-scalar-silent-fail).
#
# The prototype deliberately ships TWO floor variants so a human can settle the one open question:
#   A  MI_ArcadeGrid_Floor_Dim     - ADR 0010 D8 compliant: the decorative floor grid stays inside the
#                                    substrate band, so "bright line = walkable" is not diluted.
#   B  MI_ArcadeGrid_Floor_Bright  - faithful to the reference image: the line is the brightest thing.
# D8 and the reference cannot both be satisfied. This exists to be looked at, not argued about.

import unreal

OUT = "/Game/_ArcadeProto/Materials"
MEL = unreal.MaterialEditingLibrary
ATH = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary

PROBLEMS = []


def log(msg):
    unreal.log("[ARCADEPROTO] " + str(msg))


def bad(msg):
    unreal.log_warning("[ARCADEPROTO] !! " + str(msg))
    PROBLEMS.append(str(msg))


def link(src, src_pin, dst, dst_pin, label):
    """connect + verify. Every wire in this file goes through here."""
    ok = MEL.connect_material_expressions(src, src_pin, dst, dst_pin)
    if not ok:
        bad("wire failed: {} (pin '{}')".format(label, dst_pin))
    return ok


def link_prop(src, prop, label):
    ok = MEL.connect_material_property(src, "", prop)
    if not ok:
        bad("property wire failed: " + label)
    return ok


def C(hex_str, a=1.0):
    """sRGB hex -> linear LinearColor (same convention the HUD widgets used)."""
    h = hex_str.lstrip("#")
    srgb = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    lin = [(c / 12.92) if c <= 0.04045 else (((c + 0.055) / 1.055) ** 2.4) for c in srgb]
    return unreal.LinearColor(lin[0], lin[1], lin[2], a)


# ArtDirection A-3 draft values. PIE is the authority - these are starting points, not the contract.
PAL = {
    "void":        C("#07060E"),   # background - very dark blue-violet, never pure black (A-1 rule 1)
    "face":        C("#0E0C1C"),   # grid face
    "face_near":   C("#161331"),
    "blocker":     C("#241F45"),
    "line_dim":    C("#1E5A66"),   # inactive / distant line          (A-3-2)
    "line":        C("#2A8A96"),   # base grid line
    "trace":       C("#39B8B0"),   # wide corridor = walkable         (D8: bright line = where you stand)
    "junction":    C("#5FE0D2"),   # junction / node
    "contrast":    C("#9B3F86"),   # contrast axis - OUTSIDE the boundary only (A-3-6)
    "contrast_dk": C("#5A2E63"),
}

# Triplanar. The first pass projected WP.xy only, which is correct on floors but degenerates to
# VERTICAL STRIPES on walls (one axis is constant on a vertical face, so only one line family shows).
# Verified in the editor 2026-09-03: all four boundary walls now carry a real grid.
GRID_HLSL = """
float gs = max(GridSize, 1.0);
float hw = max(LineWidth, 0.1) * 0.5;

float3 n = abs(N);
n /= max(n.x + n.y + n.z, 1e-4);          // axis weights = which plane the surface faces

float2 uvs[3];
uvs[0] = WP.yz;                            // X-facing (wall)
uvs[1] = WP.xz;                            // Y-facing (wall)
uvs[2] = WP.xy;                            // Z-facing (floor / ceiling)
float w[3] = { n.x, n.y, n.z };

float acc = 0.0;
for (int i = 0; i < 3; ++i)
{
    float2 g = abs(frac(uvs[i] / gs) - 0.5) * gs;   // cm to the nearest line
    float d  = min(g.x, g.y);
    float aa = max(fwidth(d), 0.001);
    acc += (1.0 - smoothstep(hw - aa, hw + aa, d)) * w[i];
}
return acc;
"""


def new_material(name):
    path = "{}/{}".format(OUT, name)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    mat = ATH.create_asset(name, OUT, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        bad("create_asset failed: " + name)
    return mat


def scalar(mat, pname, value, x, y):
    n = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    n.set_editor_property("parameter_name", pname)
    n.set_editor_property("default_value", value)
    return n


def vector(mat, pname, color, x, y):
    n = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    n.set_editor_property("parameter_name", pname)
    n.set_editor_property("default_value", color)
    return n


def custom_node(mat, desc, code, input_names, out_type, x, y):
    """Create a Custom node whose input list is EXACTLY input_names (replaces the default '1' pin)."""
    ex = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    ex.set_editor_property("description", desc)
    ex.set_editor_property("code", code)
    ex.set_editor_property("output_type", out_type)
    ins = []
    for n in input_names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    ex.set_editor_property("inputs", ins)
    have = [str(ci.get_editor_property("input_name")) for ci in ex.get_editor_property("inputs")]
    if have != list(input_names):
        bad("Custom '{}' input list mismatch: wanted {} got {}".format(desc, input_names, have))
    return ex


def build_grid_master():
    mat = new_material("M_ArcadeGrid")
    if mat is None:
        return None

    p_size = scalar(mat, "GridSize", 100.0, -1500, -320)    # cm - arena cell is 100 (0011 E1)
    p_width = scalar(mat, "LineWidth", 4.0, -1500, -220)    # cm - the A-1 area-rule knob
    c_face = vector(mat, "FaceColor", PAL["face"], -1500, 0)
    c_line = vector(mat, "LineColor", PAL["line"], -1500, 160)
    e_face = scalar(mat, "FaceEmissive", 0.0, -1500, 320)
    e_line = scalar(mat, "LineEmissive", 2.0, -1500, 420)   # the other half of the area rule
    p_rough = scalar(mat, "Roughness", 0.35, -1500, 520)    # low = wet floor, neon reflects twice

    mask = None
    try:
        cus = custom_node(mat, "ArcadeGrid", GRID_HLSL, ["WP", "GridSize", "LineWidth", "N"],
                          unreal.CustomMaterialOutputType.CMOT_FLOAT1, -1080, -270)
        wp = MEL.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1320, -380)
        # VertexNormalWS (not PixelNormalWS) - geometric normal, no normal-map feedback loop.
        nrm = MEL.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -1320, -180)
        ok = link(wp, "", cus, "WP", "WorldPosition->grid")
        ok &= link(p_size, "", cus, "GridSize", "GridSize->grid")
        ok &= link(p_width, "", cus, "LineWidth", "LineWidth->grid")
        ok &= link(nrm, "", cus, "N", "VertexNormalWS->grid")
        mask = cus if ok else None
        if mask is None:
            bad("grid inputs not fully wired - an unwired Custom pin would be a compile error, dropping node")
    except Exception as exc:                                # noqa: BLE001 - degrade, never hard-fail
        bad("grid Custom node raised ({}) - degrading to flat face color".format(exc))
        mask = None

    face_e = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -760, 300)
    link(c_face, "", face_e, "A", "face*faceEmissive.A")
    link(e_face, "", face_e, "B", "face*faceEmissive.B")
    line_e = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -760, 440)
    link(c_line, "", line_e, "A", "line*lineEmissive.A")
    link(e_line, "", line_e, "B", "line*lineEmissive.B")

    if mask is not None:
        emis = MEL.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -430, 360)
        link(face_e, "", emis, "A", "emissive.A")
        link(line_e, "", emis, "B", "emissive.B")
        link(mask, "", emis, "Alpha", "emissive.Alpha")

        k = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -900, 130)
        k.set_editor_property("r", 0.2)
        dim = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -760, 100)
        link(c_line, "", dim, "A", "lineDim.A")
        link(k, "", dim, "B", "lineDim.B")
        base = MEL.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -430, 40)
        link(c_face, "", base, "A", "base.A")
        link(dim, "", base, "B", "base.B")
        link(mask, "", base, "Alpha", "base.Alpha")
    else:
        emis, base = face_e, c_face

    link_prop(base, unreal.MaterialProperty.MP_BASE_COLOR, "BaseColor")
    link_prop(emis, unreal.MaterialProperty.MP_EMISSIVE_COLOR, "Emissive")
    link_prop(p_rough, unreal.MaterialProperty.MP_ROUGHNESS, "Roughness")
    MEL.recompile_material(mat)
    EAL.save_asset(mat.get_path_name())
    log("M_ArcadeGrid built (grid={})".format("procedural" if mask else "DEGRADED-flat"))
    return mat


def build_solid_master():
    """Dark faces + fresnel edge glow. A stand-in for the voxel 'dark base + glowing edge' vocabulary
    until real voxel meshes exist - this edge is view-derived, not authored geometry."""
    mat = new_material("M_ArcadeSolid")
    if mat is None:
        return None
    c_base = vector(mat, "BaseColor", PAL["blocker"], -1250, 0)
    c_edge = vector(mat, "EdgeColor", PAL["line"], -1250, 200)
    e_edge = scalar(mat, "EdgeEmissive", 1.5, -1250, 360)
    p_exp = scalar(mat, "EdgePower", 4.0, -1250, 460)
    p_rough = scalar(mat, "Roughness", 0.4, -1250, 560)

    emis = c_edge
    fres = MEL.create_material_expression(mat, unreal.MaterialExpressionFresnel, -950, 260)
    if link(p_exp, "", fres, "ExponentIn", "EdgePower->Fresnel"):
        m1 = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -700, 260)
        ok = link(c_edge, "", m1, "A", "edge*fresnel.A")
        ok &= link(fres, "", m1, "B", "edge*fresnel.B")
        if ok:
            m2 = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -450, 260)
            ok = link(m1, "", m2, "A", "edgeGlow.A")
            ok &= link(e_edge, "", m2, "B", "edgeGlow.B")
            if ok:
                emis = m2
    if emis is c_edge:
        bad("fresnel edge not wired - M_ArcadeSolid falls back to flat emissive")

    link_prop(c_base, unreal.MaterialProperty.MP_BASE_COLOR, "BaseColor")
    link_prop(emis, unreal.MaterialProperty.MP_EMISSIVE_COLOR, "Emissive")
    link_prop(p_rough, unreal.MaterialProperty.MP_ROUGHNESS, "Roughness")
    MEL.recompile_material(mat)
    EAL.save_asset(mat.get_path_name())
    log("M_ArcadeSolid built")
    return mat


def make_mi(parent, name, scalars=None, vectors=None):
    path = "{}/{}".format(OUT, name)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    mi = ATH.create_asset(name, OUT, unreal.MaterialInstanceConstant,
                          unreal.MaterialInstanceConstantFactoryNew())
    if mi is None:
        bad("MI create failed: " + name)
        return None
    MEL.set_material_instance_parent(mi, parent)
    for k, v in (scalars or {}).items():
        MEL.set_material_instance_scalar_parameter_value(mi, k, v)
    for k, v in (vectors or {}).items():
        MEL.set_material_instance_vector_parameter_value(mi, k, v)
    EAL.save_asset(mi.get_path_name())
    log("  MI " + name)
    return mi


def main():
    log("=== arcade look prototype - environment materials ===")
    grid = build_grid_master()
    solid = build_solid_master()

    if grid:
        # --- the A/B question this prototype exists to answer -------------------------------------
        make_mi(grid, "MI_ArcadeGrid_Floor_Dim",        # A: D8 compliant (decorative grid stays quiet)
                {"GridSize": 100.0, "LineWidth": 3.0, "LineEmissive": 0.35, "FaceEmissive": 0.0},
                {"LineColor": PAL["line_dim"], "FaceColor": PAL["face"]})
        make_mi(grid, "MI_ArcadeGrid_Floor_Bright",     # B: faithful to the reference (line is the star)
                {"GridSize": 100.0, "LineWidth": 4.0, "LineEmissive": 4.0, "FaceEmissive": 0.0},
                {"LineColor": PAL["line"], "FaceColor": PAL["face"]})
        # --- D8: the bright line IS the corridor ---------------------------------------------------
        make_mi(grid, "MI_ArcadeGrid_Trace",
                {"GridSize": 200.0, "LineWidth": 12.0, "LineEmissive": 6.0, "FaceEmissive": 0.0},
                {"LineColor": PAL["trace"], "FaceColor": PAL["face"]})
        make_mi(grid, "MI_ArcadeGrid_Junction",
                {"GridSize": 200.0, "LineWidth": 18.0, "LineEmissive": 9.0, "FaceEmissive": 0.0},
                {"LineColor": PAL["junction"], "FaceColor": PAL["face"]})
        # --- outside the boundary the contrast axis may be strong (A-3-6) --------------------------
        make_mi(grid, "MI_ArcadeGrid_OuterWall",
                {"GridSize": 400.0, "LineWidth": 10.0, "LineEmissive": 3.0, "FaceEmissive": 0.0},
                {"LineColor": PAL["contrast"], "FaceColor": PAL["contrast_dk"]})

    if solid:
        make_mi(solid, "MI_ArcadeSolid_Blocker",        # >=60cm tall, Box collision, cell-snapped
                {"EdgeEmissive": 1.2, "EdgePower": 5.0},
                {"BaseColor": PAL["blocker"], "EdgeColor": PAL["line"]})
        make_mi(solid, "MI_ArcadeSolid_Float",          # decoration only -> NoCollision (0012:167)
                {"EdgeEmissive": 0.5, "EdgePower": 3.0},  # A-2 secondary element: stays quiet
                {"BaseColor": PAL["face_near"], "EdgeColor": PAL["line_dim"]})

    if PROBLEMS:
        bad("=== finished with {} problem(s) - DO NOT trust the look until these are read ===".format(len(PROBLEMS)))
        for p in PROBLEMS:
            unreal.log_warning("[ARCADEPROTO]    - " + p)
    else:
        log("=== DONE - every wire verified ===")


main()
