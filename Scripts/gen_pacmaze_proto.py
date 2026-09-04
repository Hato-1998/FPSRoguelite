# Builds the classic Pac-Man (1980, level 1) maze as a throwaway look/structure prototype.
#
# Run with the EDITOR OPEN, via Tools > Execute Python Script, or paste into the Python console.
# (Not a commandlet: it creates a level and spawns actors, and commandlets die on editor-UI APIs
#  — see memory ue-commandlet-editor-ui-apis-crash.)
#
# Output level: /Game/_ArcadeProto/L_PacMaze   (gitignored throwaway — this script is the source)
# Materials:    /Game/Materials/Arcade/MI_Pac_Blocker | MI_Pac_Floor  (tracked)
#
# TUNING — the two numbers the user actually adjusts:
#   TILE    corridor width in cm. Pac-Man corridors are 1 tile wide, so this IS the corridor width.
#   WALL_H  wall height in cm. Keep it a multiple of 20 (the environment voxel unit) and >= 60
#           (ADR 0012 invariant 7: 45~60cm is the band where enemies neither climb nor path around).
#
# ⚠️ ARENA SIZE: the maze is 28 x 31 tiles, so world size = 28*TILE by 31*TILE.
#    ADR 0012 caps an arena at 160 x 160 m with 100cm cells (25,600 cells). That means:
#      TILE = 500  -> 140 x 155 m   fits
#      TILE = 1000 -> 280 x 310 m   EXCEEDS the cap ~2x (~86,800 cells at 100cm)
#    A prototype level can exceed it; a shipped arena cannot without revisiting ADR 0012.
#
# ⚠️ This builds COLLISION, not just a shell. Porting it into a real arena sublevel would make the
#    baked flowfield mask stale (ADR 0012 invariant 1) and replace ADR 0010 D1's multi-core crossing
#    topology with a corridor maze. That is a deliberate decision, not a restyle.

import unreal

# Classic Pac-Man level 1. 28 wide x 31 tall. '#' = wall, anything else = walkable.
MAZE = [
    "############################",
    "#............##............#",
    "#.####.#####.##.#####.####.#",
    "#.####.#####.##.#####.####.#",
    "#.####.#####.##.#####.####.#",
    "#..........................#",
    "#.####.##.########.##.####.#",
    "#.####.##.########.##.####.#",
    "#......##....##....##......#",
    "######.##### ## #####.######",
    "     #.##### ## #####.#     ",
    "     #.##          ##.#     ",
    "     #.## ######## ##.#     ",
    "######.## #      # ##.######",
    "      .   #      #   .      ",
    "######.## #      # ##.######",
    "     #.## ######## ##.#     ",
    "     #.##          ##.#     ",
    "     #.## ######## ##.#     ",
    "######.## ######## ##.######",
    "#............##............#",
    "#.####.#####.##.#####.####.#",
    "#.####.#####.##.#####.####.#",
    "#...##................##...#",
    "###.##.##.########.##.##.###",
    "###.##.##.########.##.##.###",
    "#......##....##....##......#",
    "#.##########.##.##########.#",
    "#.##########.##.##########.#",
    "#..........................#",
    "############################",
]

TILE = 1000.0     # corridor width (cm). 2026-09-03 user: doubled from the first pass (500).
WALL_H = 1200.0   # wall height (cm).   2026-09-03 user: tripled from the first pass (400).

LEVEL = "/Game/_ArcadeProto/L_PacMaze"
MAT = "/Game/Materials/Arcade/"

COLS, ROWS = len(MAZE[0]), len(MAZE)


def log(m):
    unreal.log("[PACMAZE] " + str(m))


def main():
    bad = [(i, len(r)) for i, r in enumerate(MAZE) if len(r) != COLS]
    if bad:
        unreal.log_error("[PACMAZE] row length mismatch: {}".format(bad))
        return
    if WALL_H % 20 != 0 or WALL_H < 60:
        unreal.log_warning("[PACMAZE] WALL_H {} violates the 20cm voxel unit / >=60cm rule".format(WALL_H))

    log("maze {}x{} tiles -> {:.0f} x {:.0f} m".format(
        COLS, ROWS, COLS * TILE / 100, ROWS * TILE / 100))
    if COLS * TILE > 16000 or ROWS * TILE > 16000:
        log("NOTE: exceeds the ADR 0012 arena cap (160m). Fine for a prototype, not for a shipped arena.")

    LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    eal = unreal.EditorAssetLibrary

    dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    if dirty:
        unreal.log_error("[PACMAZE] unsaved level(s) open: {} — aborting so nothing is lost".format(
            [p.get_name() for p in dirty]))
        return

    if not LES.new_level(LEVEL):
        unreal.log_error("[PACMAZE] new_level failed")
        return

    cube = eal.load_asset("/Engine/BasicShapes/Cube")
    wall_mat = eal.load_asset(MAT + "MI_Pac_Blocker")
    floor_mat = eal.load_asset(MAT + "MI_Pac_Floor")

    def spawn(label, loc, scale, mat):
        a = EAS.spawn_actor_from_object(cube, unreal.Vector(*loc))
        a.set_actor_label(label)
        a.set_actor_scale3d(unreal.Vector(*scale))
        c = a.static_mesh_component
        c.set_material(0, mat)
        c.set_collision_profile_name("BlockAll")
        return a

    def wx(col):
        return (col - COLS / 2.0 + 0.5) * TILE

    def wy(row):
        return (ROWS / 2.0 - row - 0.5) * TILE

    spawn("Floor", (0, 0, -25), (COLS * TILE / 100.0, ROWS * TILE / 100.0, 0.5), floor_mat)

    # Greedy horizontal merge: 490 wall tiles collapse to ~151 boxes. Fewer actors, same shape.
    n = 0
    for r, row in enumerate(MAZE):
        c = 0
        while c < len(row):
            if row[c] != "#":
                c += 1
                continue
            s = c
            while c < len(row) and row[c] == "#":
                c += 1
            run = c - s
            spawn("Wall_r{}_c{}".format(r, s),
                  ((wx(s) + wx(c - 1)) / 2.0, wy(r), WALL_H / 2.0),
                  (run * TILE / 100.0, TILE / 100.0, WALL_H / 100.0), wall_mat)
            n += 1
    log("wall boxes: {} (from {} tiles)".format(n, sum(row.count("#") for row in MAZE)))

    d = EAS.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 2000))
    d.set_actor_label("Light_Dim")
    d.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-50.0, yaw=-35.0), False)
    lc = d.get_component_by_class(unreal.DirectionalLightComponent)
    lc.set_intensity(0.35)
    lc.set_light_color(unreal.LinearColor(0.47, 0.51, 0.75, 1.0))

    pp = EAS.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 500))
    pp.set_actor_label("PP_Arcade")
    pp.set_editor_property("unbound", True)
    s = pp.get_editor_property("settings")
    for flag, key, val in [("override_bloom_intensity", "bloom_intensity", 2.5),
                           ("override_bloom_threshold", "bloom_threshold", -1.0),
                           ("override_auto_exposure_bias", "auto_exposure_bias", 11.0)]:
        s.set_editor_property(flag, True)
        s.set_editor_property(key, val)
    s.set_editor_property("override_auto_exposure_method", True)
    s.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    pp.set_editor_property("settings", s)

    # Bottom corridor (row 29 is fully open), facing +Y into the maze.
    ps = EAS.spawn_actor_from_class(
        unreal.PlayerStart,
        unreal.Vector(0, -ROWS * TILE / 2.0 + TILE * 1.5, 150),
        unreal.Rotator(roll=0.0, pitch=0.0, yaw=90.0))   # Rotator(a,b,c) = (roll,pitch,yaw)
    ps.set_actor_label("PlayerStart_Maze")

    unreal.EditorLoadingAndSavingUtils.save_current_level()
    log("saved {} — {} actors".format(LEVEL, len(EAS.get_all_level_actors())))


main()
