# Builds the floor material that draws Pac-Man style pellets along every walkable maze cell.
#
# Run (editor CLOSED) via commandlet:
#   "D:\UnrealEngine\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
#     "E:\Git_Project\FPSRoguelite\FPSRoguelite.uproject" ^
#     -run=pythonscript -script="E:\Git_Project\FPSRoguelite\Scripts\gen_arcade_floor_pellets.py"
#
# ⚠️ WHY NO MASK TEXTURE — asset import is unavailable from BOTH python contexts (2026-09-03):
#     live editor  : Assertion failed: ++Queue(QueueIndex).RecursionGuard == 1  (TaskGraph.cpp:689)
#     commandlet   : Assertion failed: CurrentApplication.IsValid()             (SlateApplication.h:321)
#   The importer wants Slate, which a commandlet does not have, and it wants a free game thread,
#   which the live editor does not give. So the maze is packed into the shader instead: 28 x 31
#   cells = 868 bits = 31 uints, one bit per cell, 1 = walkable. That removes the texture, the
#   sampler, the mip/filter/sRGB pitfalls and the import entirely, and the lookup is exact.
#
#   Trade-off: the material is now coupled to THIS maze. That is acceptable because the constant is
#   generated here from the same MAZE table the level builder uses — change the maze, re-run this.
#
# Pellets are drawn procedurally from world position (not baked), so they stay crisp at any
# distance. The maze spans 280 m; a baked dot texture would smear up close.

import unreal

A = "/Game/Materials/Arcade"
MAT = A + "/M_ArcadeFloorPellets"

# Must match the level build (Scripts/gen_pacmaze_proto.py / the L_Map_1 transplant).
MAZE = [
    "############################", "#............##............#", "#.####.#####.##.#####.####.#",
    "#.####.#####.##.#####.####.#", "#.####.#####.##.#####.####.#", "#..........................#",
    "#.####.##.########.##.####.#", "#.####.##.########.##.####.#", "#......##....##....##......#",
    "######.##### ## #####.######", "     #.##### ## #####.#     ", "     #.##          ##.#     ",
    "     #.## ######## ##.#     ", "######.## #      # ##.######", "      .   #      #   .      ",
    "######.## #      # ##.######", "     #.## ######## ##.#     ", "     #.##          ##.#     ",
    "     #.## ######## ##.#     ", "######.## ######## ##.######", "#............##............#",
    "#.####.#####.##.#####.####.#", "#.####.#####.##.#####.####.#", "#...##................##...#",
    "###.##.##.########.##.##.###", "###.##.##.########.##.##.###", "#......##....##....##......#",
    "#.##########.##.##########.#", "#.##########.##.##########.#", "#..........................#",
    "############################",
]
CELL = 1000.0                        # cm per maze tile — matches the level
PELLET_CELLS = [(3, 1), (3, 26), (23, 1), (23, 26)]   # Pac-Man's four power pellets

COLS, ROWS = len(MAZE[0]), len(MAZE)
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


def maze_hlsl():
    """31 uints, bit c set = cell (row, c) is walkable."""
    rows = []
    for r in MAZE:
        v = 0
        for c, ch in enumerate(r):
            if ch != "#":
                v |= (1 << c)
        rows.append(v)
    lits = ["0x{:08X}u".format(v) for v in rows]
    body = ",\n        ".join(", ".join(lits[i:i + 6]) for i in range(0, len(lits), 6))
    pel = " || ".join("(sx == {} && sy == {})".format(c, r) for r, c in PELLET_CELLS)
    # Cell index from world. Cell (c,r) centre is at ((c - COLS/2 + 0.5)*CELL, (ROWS/2 - r - 0.5)*CELL),
    # so the containing cell is floor(x/CELL) + COLS/2 and floor(ROWS/2 + 0.5 - y/CELL).
    return """
    const uint MazeRow[{rows}] = {{
        {body}
    }};

    int cx = (int)floor(WP.x / CellSize) + {halfc};
    int cy = (int)floor({halfr} - WP.y / CellSize);
    float inb = (cx >= 0 && cx < {cols} && cy >= 0 && cy < {rows}) ? 1.0 : 0.0;
    int sx = clamp(cx, 0, {cols_1});
    int sy = clamp(cy, 0, {rows_1});

    float walk   = (float)((MazeRow[sy] >> (uint)sx) & 1u) * inb;
    float pellet = ({pel}) ? 1.0 : 0.0;

    // Distance to this cell's centre, computed in world space so there is no frac() phase error.
    float2 c = float2(((float)sx - {halfc} + 0.5) * CellSize,
                      ({halfr} - 0.5 - (float)sy) * CellSize);
    float  d = length(WP.xy - c);
    float  r = lerp(DotR, PelletR, pellet);
    float aa = max(fwidth(d), 0.5);
    return (1.0 - smoothstep(r - aa, r + aa, d)) * walk;
""".format(rows=ROWS, rows_1=ROWS - 1, cols=COLS, cols_1=COLS - 1,
           halfc=COLS // 2, halfr=ROWS / 2.0 + 0.5, body=body, pel=pel)


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


def main():
    log("=== floor pellets (maze packed into the shader, no texture) ===")
    walkable = sum(row.count(".") + row.count(" ") for row in MAZE)
    log("maze {}x{}, walkable cells {}, power pellets {}".format(
        COLS, ROWS, walkable, len(PELLET_CELLS)))

    if EAL.does_asset_exist(MAT):
        EAL.delete_asset(MAT)
    mat = ATH.create_asset("M_ArcadeFloorPellets", A, unreal.Material, unreal.MaterialFactoryNew())

    p_cell = scalar(mat, "CellSize", CELL, -1500, -300)
    p_dot = scalar(mat, "DotRadius", 55.0, -1500, -210)        # cm
    p_pel = scalar(mat, "PelletRadius", 170.0, -1500, -120)    # cm
    c_face = vector(mat, "FaceColor", C("#05050E"), -1500, 20)
    c_dot = vector(mat, "DotColor", C("#FFB897"), -1500, 170)  # Pac-Man pellet peach
    e_dot = scalar(mat, "DotEmissive", 6.0, -1500, 320)
    p_rough = scalar(mat, "Roughness", 0.5, -1500, 400)
    wp = MEL.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1500, -400)

    ex = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, -1050, -300)
    ex.set_editor_property("description", "MazePellets")
    ex.set_editor_property("code", maze_hlsl())
    ex.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    ins = []
    for n in ["WP", "CellSize", "DotR", "PelletR"]:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    ex.set_editor_property("inputs", ins)      # replaces the default pin named "1"

    ok = link(wp, "", ex, "WP", "WP->maze")
    ok &= link(p_cell, "", ex, "CellSize", "CellSize->maze")
    ok &= link(p_dot, "", ex, "DotR", "DotR->maze")
    ok &= link(p_pel, "", ex, "PelletR", "PelletR->maze")
    if not ok:
        bad("Custom inputs not fully wired — an unwired named pin is a compile error")

    tint = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -700, 220)
    link(c_dot, "", tint, "A", "tint.A")
    link(e_dot, "", tint, "B", "tint.B")
    emis = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -420, 60)
    link(tint, "", emis, "A", "emissive.A")
    link(ex, "", emis, "B", "emissive.B")

    MEL.connect_material_property(c_face, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(p_rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    EAL.save_asset(MAT, only_if_is_dirty=False)

    st = MEL.get_statistics(mat)
    log("M_ArcadeFloorPellets VS={} PS={}".format(
        st.get_editor_property("num_vertex_shader_instructions"),
        st.get_editor_property("num_pixel_shader_instructions")))

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
