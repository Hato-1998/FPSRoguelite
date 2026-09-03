# Imports the corridor mask and builds the floor material that draws Pac-Man style pellets on it.
#
# Run (editor CLOSED) via commandlet:
#   "D:\UnrealEngine\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
#     "E:\Git_Project\FPSRoguelite\FPSRoguelite.uproject" ^
#     -run=pythonscript -script="E:\Git_Project\FPSRoguelite\Scripts\gen_arcade_floor_pellets.py"
#
# ⚠️ MUST be a commandlet. Importing an asset from Python inside a LIVE editor took the editor down
#    with `Assertion failed: ++Queue(QueueIndex).RecursionGuard == 1` (TaskGraph.cpp:689) on
#    2026-09-03. Same class as the known FBX-import deadlock — asset import is not safe from the
#    live-editor Python path, regardless of file type.
#
# WHY A MASK TEXTURE AND NOT A BAKED DOT PATTERN: the maze spans 280 m. A texture holding the dots
# themselves would smear up close. Instead the texture is ONE TEXEL PER MAZE CELL (28x31, 237 bytes)
# and only answers "is this cell walkable / a power pellet / a junction". The dot itself is drawn
# procedurally from world position, so it stays crisp at any distance.
#
# Mask channels (Docs/MapSources/pacman_corridor_mask.png):
#   R = walkable    G = power pellet (the 4 classic corners)    B = junction (3+ exits)

import unreal

SRC_PNG = "E:/Git_Project/FPSRoguelite/Docs/MapSources/pacman_corridor_mask.png"
A = "/Game/Materials/Arcade"
TEX = A + "/T_CorridorMask"
MAT = A + "/M_ArcadeFloorPellets"

MAZE_W, MAZE_H = 28000.0, 31000.0   # cm, must match the level build (28 x 31 tiles @ 1000)
CELL = 1000.0                        # cm per maze tile

MEL = unreal.MaterialEditingLibrary
ATH = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
PROBLEMS = []


def log(m):
    unreal.log("[FLOORPELLET] " + str(m))


def bad(m):
    unreal.log_warning("[FLOORPELLET] !! " + str(m))
    PROBLEMS.append(str(m))


def link(src, sp, dst, dp, label):
    ok = MEL.connect_material_expressions(src, sp, dst, dp)
    if not ok:
        bad("wire failed: {} (pin '{}')".format(label, dp))
    return ok


def C(hex_str):
    h = hex_str.lstrip("#")
    s = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    l = [(c / 12.92) if c <= 0.04045 else (((c + 0.055) / 1.055) ** 2.4) for c in s]
    return unreal.LinearColor(l[0], l[1], l[2], 1.0)


def custom(mat, desc, code, inputs, out_type, x, y):
    ex = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    ex.set_editor_property("description", desc)
    ex.set_editor_property("code", code)
    ex.set_editor_property("output_type", out_type)
    ins = []
    for n in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    ex.set_editor_property("inputs", ins)          # replaces the default pin named "1"
    return ex


def scalar(mat, n, v, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", n)
    e.set_editor_property("default_value", v)
    return e


def vector(mat, n, c, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property("parameter_name", n)
    e.set_editor_property("default_value", c)
    return e


# World XY -> mask UV. Maze is centred on the origin; image +Y runs south, world +Y runs north.
UV_CODE = """
return float2(WP.x / MazeW + 0.5, 0.5 - WP.y / MazeH);
"""

# Procedural dot at each cell centre. Radius grows for a power-pellet cell (mask.G).
DOT_CODE = """
float2 p = (frac(WP.xy / CellSize) - 0.5) * CellSize;   // cm from this cell's centre
float  d = length(p);
float  r = lerp(DotR, PelletR, saturate(MaskG));
float aa = max(fwidth(d), 0.5);
return (1.0 - smoothstep(r - aa, r + aa, d)) * saturate(MaskR);
"""


def import_mask():
    if EAL.does_asset_exist(TEX):
        EAL.delete_asset(TEX)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", SRC_PNG)
    task.set_editor_property("destination_path", A)
    task.set_editor_property("destination_name", "T_CorridorMask")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    ATH.import_asset_tasks([task])
    tex = EAL.load_asset(TEX)
    if tex is None:
        bad("mask import failed")
        return None
    # It is DATA, not colour. sRGB, block compression and mips would all corrupt per-cell values.
    tex.set_editor_property("srgb", False)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    tex.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    tex.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    EAL.save_asset(TEX, only_if_is_dirty=False)
    log("mask imported {}x{} nearest/no-mips/no-sRGB".format(
        tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))
    return tex


def build_material(tex):
    if EAL.does_asset_exist(MAT):
        EAL.delete_asset(MAT)
    mat = ATH.create_asset("M_ArcadeFloorPellets", A, unreal.Material, unreal.MaterialFactoryNew())

    p_mw = scalar(mat, "MazeW", MAZE_W, -1700, -420)
    p_mh = scalar(mat, "MazeH", MAZE_H, -1700, -340)
    p_cell = scalar(mat, "CellSize", CELL, -1700, -260)
    p_dot = scalar(mat, "DotRadius", 55.0, -1700, -180)      # cm
    p_pel = scalar(mat, "PelletRadius", 170.0, -1700, -100)  # cm
    c_face = vector(mat, "FaceColor", C("#05050E"), -1700, 40)
    c_dot = vector(mat, "DotColor", C("#FFB897"), -1700, 180)   # Pac-Man pellet peach
    e_dot = scalar(mat, "DotEmissive", 6.0, -1700, 320)
    p_rough = scalar(mat, "Roughness", 0.5, -1700, 400)

    wp = MEL.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1700, -520)

    uv = custom(mat, "MaskUV", UV_CODE, ["WP", "MazeW", "MazeH"],
                unreal.CustomMaterialOutputType.CMOT_FLOAT2, -1350, -430)
    ok = link(wp, "", uv, "WP", "WP->uv")
    ok &= link(p_mw, "", uv, "MazeW", "MazeW->uv")
    ok &= link(p_mh, "", uv, "MazeH", "MazeH->uv")

    samp = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D,
                                          -1050, -300)
    samp.set_editor_property("parameter_name", "CorridorMask")
    samp.set_editor_property("texture", tex)
    samp.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    ok &= link(uv, "", samp, "UVs", "uv->sampler")

    dot = custom(mat, "Pellet", DOT_CODE, ["WP", "CellSize", "DotR", "PelletR", "MaskR", "MaskG"],
                 unreal.CustomMaterialOutputType.CMOT_FLOAT1, -700, -300)
    ok &= link(wp, "", dot, "WP", "WP->dot")
    ok &= link(p_cell, "", dot, "CellSize", "CellSize->dot")
    ok &= link(p_dot, "", dot, "DotR", "DotR->dot")
    ok &= link(p_pel, "", dot, "PelletR", "PelletR->dot")
    ok &= link(samp, "R", dot, "MaskR", "mask.R->dot")
    ok &= link(samp, "G", dot, "MaskG", "mask.G->dot")

    if not ok:
        bad("pellet chain not fully wired — an unwired Custom pin is a compile error")

    tint = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -430, 200)
    link(c_dot, "", tint, "A", "dotColor*emissive.A")
    link(e_dot, "", tint, "B", "dotColor*emissive.B")
    emis = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -220, 60)
    link(tint, "", emis, "A", "emissive.A")
    link(dot, "", emis, "B", "emissive.B")

    MEL.connect_material_property(c_face, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(p_rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    EAL.save_asset(MAT, only_if_is_dirty=False)

    st = MEL.get_statistics(mat)
    log("M_ArcadeFloorPellets VS={} PS={}".format(
        st.get_editor_property("num_vertex_shader_instructions"),
        st.get_editor_property("num_pixel_shader_instructions")))
    return mat


def main():
    log("=== corridor mask + floor pellet material ===")
    tex = import_mask()
    if tex is None:
        return
    mat = build_material(tex)
    if mat is None:
        return
    name = "MI_Pac_FloorPellets"
    p = A + "/" + name
    if EAL.does_asset_exist(p):
        EAL.delete_asset(p)
    mi = ATH.create_asset(name, A, unreal.MaterialInstanceConstant,
                          unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, mat)
    EAL.save_asset(p, only_if_is_dirty=False)
    log("MI " + name)

    if PROBLEMS:
        bad("finished with {} problem(s)".format(len(PROBLEMS)))
    else:
        log("=== DONE - every wire verified ===")


main()
