# Floor material: a guide line down the centre of every corridor, with light running along it.
#
# Run (editor CLOSED) via commandlet:
#   "D:\UnrealEngine\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
#     "E:\Git_Project\FPSRoguelite\FPSRoguelite.uproject" ^
#     -run=pythonscript -script="E:\Git_Project\FPSRoguelite\Scripts\gen_arcade_floor_guideline.py"
#
# Replaces the pellet floor (2026-09-03 user call): dots and a full grid both spread attention
# evenly across the screen. A centre line with a travelling pulse instead gives the eye a direction
# to follow, which is what a corridor wants to communicate.
#
# The maze is packed into the shader as 31 uints (one bit per cell, 1 = walkable) — see
# gen_arcade_floor_pellets.py for why there is no mask texture (asset import crashes the editor in
# the live path and asserts on Slate in the commandlet path).
#
# Shape comes from the four neighbours of each cell, so corners, T-junctions and crossings build
# themselves: a horizontal half-band is drawn toward each open left/right neighbour, a vertical one
# toward each open up/down neighbour. Nothing pokes into a wall because a half-band is only drawn
# on the side whose neighbour is actually open.
#
# The pulse travels along +X on horizontal runs and +Y on vertical runs, so at a crossing the two
# streams pass through each other rather than fighting.
#
# ⚠️ `line` is a RESERVED WORD in HLSL (geometry-shader primitive modifier, alongside point /
#    triangle / lineadj / triangleadj). `float line = ...` fails with "modifiers must appear before
#    type" and the whole material silently falls back to the Default Material. Verified 2026-09-03.

import unreal

A = "/Game/Materials/Arcade"
MAT = A + "/M_ArcadeFloorGuide"

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
CELL = 1000.0
COLS, ROWS = len(MAZE[0]), len(MAZE)

MEL = unreal.MaterialEditingLibrary
ATH = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
PROBLEMS = []


def log(m):
    unreal.log("[GUIDELINE] " + str(m))


def bad(m):
    unreal.log_warning("[GUIDELINE] !! " + str(m))
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


def hlsl():
    rows = []
    for r in MAZE:
        v = 0
        for c, ch in enumerate(r):
            if ch != "#":
                v |= (1 << c)
        rows.append(v)
    lits = ["0x{:08X}u".format(v) for v in rows]
    body = ",\n        ".join(", ".join(lits[i:i + 6]) for i in range(0, len(lits), 6))

    # No functions allowed inside a Custom node (the body is inlined), so the cell lookup is a macro.
    return """
    const uint MazeRow[{rows}] = {{
        {body}
    }};
    #define WALK(r, c) ( ((r) >= 0 && (r) < {rows} && (c) >= 0 && (c) < {cols}) \\
        ? (float)((MazeRow[clamp((r), 0, {rows_1})] >> (uint)clamp((c), 0, {cols_1})) & 1u) : 0.0 )

    int cx = (int)floor(WP.x / CellSize) + {halfc};
    int cy = (int)floor({halfri} - WP.y / CellSize);

    float here = WALK(cy, cx);
    float nL   = WALK(cy, cx - 1);
    float nR   = WALK(cy, cx + 1);
    float nN   = WALK(cy - 1, cx);      // image row-1 is world +Y (north)
    float nS   = WALK(cy + 1, cx);

    // Room test: a cell inside ANY fully-open 2x2 block is open space, not a corridor.
    // A one-cell-wide corridor can never form a 2x2, so rooms (the ghost house, the side
    // pockets) drop out on their own and only real corridors keep a guide line.
    float room = 0.0;
    room = max(room, here * nL * nN * WALK(cy - 1, cx - 1));
    room = max(room, here * nR * nN * WALK(cy - 1, cx + 1));
    room = max(room, here * nL * nS * WALK(cy + 1, cx - 1));
    room = max(room, here * nR * nS * WALK(cy + 1, cx + 1));
    float corridor = here * (1.0 - room);

    // Offset from this cell's centre, in world cm (no frac() so there is no half-cell phase error).
    float2 c = float2(((float)cx - {halfc} + 0.5) * CellSize,
                      ({halfrc} - (float)cy) * CellSize);
    float2 q = WP.xy - c;

    float hw = max(LineWidth, 1.0) * 0.5;
    float aaX = max(fwidth(q.x), 0.5);
    float aaY = max(fwidth(q.y), 0.5);

    // Thin bands through the cell centre.
    float bandH = 1.0 - smoothstep(hw - aaY, hw + aaY, abs(q.y));   // runs along X
    float bandV = 1.0 - smoothstep(hw - aaX, hw + aaX, abs(q.x));   // runs along Y

    // Half-extent toward each OPEN neighbour only, so nothing bleeds into a wall.
    float extW = nL * (1.0 - step(0.0, q.x));
    float extE = nR * step(0.0, q.x);
    float extN = nN * step(0.0, q.y);
    float extS = nS * (1.0 - step(0.0, q.y));

    float lineH = bandH * saturate(extW + extE);
    float lineV = bandV * saturate(extN + extS);
    float lineMask = saturate(lineH + lineV) * corridor;   // NOT 'line' — reserved word in HLSL

    // Travelling light. Horizontal runs flow along +X, vertical along +Y, so the two streams cross
    // a junction independently instead of cancelling.
    float tH = frac(WP.x / max(Wavelength, 1.0) - Time * Speed);
    float tV = frac(WP.y / max(Wavelength, 1.0) - Time * Speed);
    float dH = min(tH, 1.0 - tH);
    float dV = min(tV, 1.0 - tV);
    float w  = max(PulseWidth, 0.001);
    float pH = exp(-(dH * dH) / (w * w));
    float pV = exp(-(dV * dV) / (w * w));

    float pulse = saturate(pH * lineH + pV * lineV) * corridor;
    return float2(lineMask, pulse);
""".format(rows=ROWS, rows_1=ROWS - 1, cols=COLS, cols_1=COLS - 1,
           halfc=COLS // 2,
           # TWO different Y constants — conflating them put every horizontal line half a cell
           # (500 cm) off, i.e. exactly on the wall edge (found 2026-09-03, verified against the
           # level's actual wall positions). X was right, which is why only horizontals looked wrong.
           halfri=ROWS / 2.0,          # cell INDEX  : cy = floor(ROWS/2 - y/CELL)
           halfrc=ROWS / 2.0 - 0.5,    # cell CENTRE : y  = (ROWS/2 - 0.5 - cy) * CELL
           body=body)


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
    log("=== corridor guide line + travelling light ===")
    if EAL.does_asset_exist(MAT):
        EAL.delete_asset(MAT)
    mat = ATH.create_asset("M_ArcadeFloorGuide", A, unreal.Material, unreal.MaterialFactoryNew())

    p_cell = scalar(mat, "CellSize", CELL, -1600, -430)
    p_lw = scalar(mat, "LineWidth", 26.0, -1600, -350)        # cm — thin. This is the A-1 area knob.
    p_wave = scalar(mat, "Wavelength", 6000.0, -1600, -270)   # cm between pulses
    p_speed = scalar(mat, "Speed", 0.14, -1600, -190)         # wavelengths per second
    p_pw = scalar(mat, "PulseWidth", 0.055, -1600, -110)      # fraction of a wavelength
    c_face = vector(mat, "FaceColor", C("#05050E"), -1600, 10)
    c_line = vector(mat, "LineColor", C("#2A8A96"), -1600, 150)
    e_base = scalar(mat, "BaseGlow", 0.18, -1600, 290)        # the line when no pulse is on it
    e_pulse = scalar(mat, "PulseGain", 9.0, -1600, 370)       # the travelling light
    p_rough = scalar(mat, "Roughness", 0.5, -1600, 450)

    wp = MEL.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1600, -520)
    tm = MEL.create_material_expression(mat, unreal.MaterialExpressionTime, -1600, -600)

    ex = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, -1150, -350)
    ex.set_editor_property("description", "CorridorGuide")
    ex.set_editor_property("code", hlsl())
    ex.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT2)
    ins = []
    for n in ["WP", "Time", "CellSize", "LineWidth", "Wavelength", "Speed", "PulseWidth"]:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    ex.set_editor_property("inputs", ins)      # replaces the default pin named "1"

    ok = link(wp, "", ex, "WP", "WP")
    ok &= link(tm, "", ex, "Time", "Time")
    ok &= link(p_cell, "", ex, "CellSize", "CellSize")
    ok &= link(p_lw, "", ex, "LineWidth", "LineWidth")
    ok &= link(p_wave, "", ex, "Wavelength", "Wavelength")
    ok &= link(p_speed, "", ex, "Speed", "Speed")
    ok &= link(p_pw, "", ex, "PulseWidth", "PulseWidth")
    if not ok:
        bad("Custom inputs not fully wired — an unwired named pin is a compile error")

    # A Custom node has ONE unnamed output — you cannot pull "R"/"G" off it by pin name (the first
    # pass tried and link() caught both failures). Split the float2 with ComponentMask instead.
    def mask(comp, x, y):
        m = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, x, y)
        m.set_editor_property("r", comp == "r")
        m.set_editor_property("g", comp == "g")
        m.set_editor_property("b", False)
        m.set_editor_property("a", False)
        link(ex, "", m, "", "custom->mask_" + comp)
        return m

    mask_line = mask("r", -980, -280)
    mask_pulse = mask("g", -980, -140)

    # emissive = LineColor * (line*BaseGlow + pulse*PulseGain)
    mL = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -820, -260)
    link(mask_line, "", mL, "A", "line*base.A")
    link(e_base, "", mL, "B", "line*base.B")
    mP = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -820, -120)
    link(mask_pulse, "", mP, "A", "pulse*gain.A")
    link(e_pulse, "", mP, "B", "pulse*gain.B")
    add = MEL.create_material_expression(mat, unreal.MaterialExpressionAdd, -600, -190)
    link(mL, "", add, "A", "sum.A")
    link(mP, "", add, "B", "sum.B")
    emis = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -380, -60)
    link(c_line, "", emis, "A", "emissive.A")
    link(add, "", emis, "B", "emissive.B")

    MEL.connect_material_property(c_face, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(p_rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    EAL.save_asset(MAT, only_if_is_dirty=False)
    log("M_ArcadeFloorGuide saved")

    name = "MI_Pac_FloorGuide"
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
