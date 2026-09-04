# Build a level's structure from an image file.
#
# Feed it a PNG (a maze bitmap, a heightmap, a noise field, a hand-painted layout) and it spawns the
# matching blockout: merged boxes with collision, arcade materials, lighting and a post-process volume.
#
# Run with the EDITOR OPEN: Tools > Execute Python Script, or paste into the Python console.
# (Not a commandlet — it creates a level and spawns actors; commandlets die on editor-UI APIs.)
#
#   import gen_map_from_image as G
#   G.build("D:/maps/my_maze.png", mode="threshold", tile=1000, wall_h=1200)
#
# WHY A HAND-ROLLED PNG DECODER: this engine's Python has no PIL and no numpy (checked 2026-09-03,
# Python 3.11.8). zlib and struct are there, which is all a PNG needs. Supported: 8-bit grayscale,
# grayscale+alpha, RGB, RGBA and 8-bit palette, non-interlaced. 16-bit and Adam7 interlaced are
# rejected with a clear message rather than decoded wrong.

import struct
import zlib

import unreal

# ---------------------------------------------------------------------------- project constraints
VOXEL_UNIT = 20.0        # cm. Environment voxel size (ArtDirection / ADR 0012 note).
STEP_LOW = 45.0          # cm. <= this, enemies walk over it.
STEP_HIGH = 60.0         # cm. >= this, it blocks. The band between is forbidden:
                         #     enemies neither climb it nor path around it, so they wedge.
ARENA_CAP = 16000.0      # cm. ADR 0012 caps an arena at 160 x 160 m with 100cm cells.
CELL = 100.0             # cm. Flow-field cell. Structures should land on this grid.

MAT_DIR = "/Game/Materials/Arcade/"
DEFAULT_LEVEL = "/Game/_ArcadeProto/L_ImageMap"


def log(m):
    unreal.log("[IMGMAP] " + str(m))


def warn(m):
    unreal.log_warning("[IMGMAP] " + str(m))


# ------------------------------------------------------------------------------- PNG decoding
def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def decode_png(path):
    """-> (width, height, get(x, y) -> (r, g, b)).  Pure stdlib."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG: " + path)

    pos, idat, plte, hdr = 8, bytearray(), None, None
    while pos < len(data):
        ln = struct.unpack(">I", data[pos:pos + 4])[0]
        typ = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            hdr = struct.unpack(">IIBBBBB", body)
        elif typ == b"PLTE":
            plte = body
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            break
        pos += 12 + ln

    w, h, depth, ctype, comp, filt, interlace = hdr
    if depth != 8:
        raise ValueError("only 8-bit PNG supported (got {}-bit). Re-export as 8-bit.".format(depth))
    if interlace:
        raise ValueError("interlaced (Adam7) PNG not supported. Re-export without interlacing.")

    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if channels is None:
        raise ValueError("unsupported PNG color type {}".format(ctype))

    raw = zlib.decompress(bytes(idat))
    stride = w * channels
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        ft = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if ft == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                c = prev[i - channels] if i >= channels else 0
                line[i] = (line[i] + _paeth(a, prev[i], c)) & 0xFF
        elif ft != 0:
            raise ValueError("bad PNG filter type {} on row {}".format(ft, y))
        out[y * stride:(y + 1) * stride] = line
        prev = line

    if ctype == 3 and plte is None:
        raise ValueError("palette PNG without a PLTE chunk")

    def get(x, y):
        i = y * stride + x * channels
        if ctype == 3:
            j = out[i] * 3
            return (plte[j], plte[j + 1], plte[j + 2])
        if ctype in (0, 4):
            v = out[i]
            return (v, v, v)
        return (out[i], out[i + 1], out[i + 2])

    return w, h, get


# ------------------------------------------------------------------------------- height rules
def snap_height(cm):
    """Quantize to the voxel unit and push out of the forbidden 45~60 band."""
    q = round(cm / VOXEL_UNIT) * VOXEL_UNIT
    if STEP_LOW < q < STEP_HIGH:
        # Snap to whichever edge is closer; ties go UP so it stays a real blocker.
        q = STEP_LOW if (q - STEP_LOW) < (STEP_HIGH - q) else STEP_HIGH
    return q


# ------------------------------------------------------------------------------- build
def build(image_path,
          mode="threshold",
          tile=1000.0,
          wall_h=1200.0,
          threshold=128,
          invert=False,
          height_range=(0.0, 1200.0),
          palette=None,
          level=DEFAULT_LEVEL,
          fit_to=None):
    """
    mode="threshold"  bright(or dark, with invert) pixels become walls of a single height `wall_h`.
    mode="height"     pixel brightness maps into `height_range`; equal-height runs merge.
    mode="palette"    `palette` maps (r,g,b) -> height in cm. Nearest colour wins. 0 = open.

    tile     cm per pixel. This IS the corridor width for a maze bitmap.
    fit_to   if set (cm), `tile` is recomputed so the longer image side spans exactly this.
    """
    w, h, get = decode_png(image_path)
    log("image {} -> {} x {} px".format(image_path, w, h))

    if fit_to:
        tile = float(fit_to) / max(w, h)
        log("fit_to {:.0f}cm -> tile {:.1f}cm/px".format(fit_to, tile))

    world_w, world_h = w * tile, h * tile
    log("world {:.0f} x {:.0f} cm  ({:.0f} x {:.0f} m)".format(
        world_w, world_h, world_w / 100, world_h / 100))
    if world_w > ARENA_CAP or world_h > ARENA_CAP:
        # Plain log, not a warning: exceeding the cap is expected for a look prototype, and some
        # tool wrappers surface UE warnings as hard failures.
        log("NOTE: exceeds the ADR 0012 arena cap ({:.0f}m). Fine for a prototype; a shipped arena "
            "needs a smaller image, a smaller tile, or a revisit of that ADR.".format(ARENA_CAP / 100))
    if abs(tile / CELL - round(tile / CELL)) > 1e-6:
        warn("tile {:.1f}cm is not a multiple of the {:.0f}cm flow-field cell — structures will not "
             "land on the grid, and the occupancy probe can miss thin edges.".format(tile, CELL))

    # --- pixel -> height (cm). 0 means "open". -----------------------------------------------
    def height_at(x, y):
        r, g, b = get(x, y)
        if mode == "palette":
            best, bestd = 0.0, None
            for (pr, pg, pb), ph in palette.items():
                d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
                if bestd is None or d < bestd:
                    best, bestd = ph, d
            return snap_height(best) if best > 0 else 0.0
        lum = 0.299 * r + 0.587 * g + 0.114 * b
        if mode == "threshold":
            solid = (lum < threshold) if invert else (lum >= threshold)
            return snap_height(wall_h) if solid else 0.0
        if mode == "height":
            t = lum / 255.0
            if invert:
                t = 1.0 - t
            v = height_range[0] + t * (height_range[1] - height_range[0])
            return 0.0 if v < VOXEL_UNIT else snap_height(v)
        raise ValueError("unknown mode " + mode)

    grid = [[height_at(x, y) for x in range(w)] for y in range(h)]
    solid = sum(1 for row in grid for v in row if v > 0)
    heights = sorted({v for row in grid for v in row if v > 0})
    log("solid {} / {} px, distinct heights: {}".format(
        solid, w * h, ["{:.0f}".format(v) for v in heights[:12]] + (["..."] if len(heights) > 12 else [])))
    if not solid:
        warn("nothing solid — check `threshold` / `invert`, or the image is blank")
        return

    # --- greedy horizontal merge of equal-height runs ------------------------------------------
    runs = []
    for y in range(h):
        x = 0
        while x < w:
            v = grid[y][x]
            if v <= 0:
                x += 1
                continue
            s = x
            while x < w and grid[y][x] == v:
                x += 1
            runs.append((y, s, x - s, v))
    log("merged {} solid px -> {} boxes".format(solid, len(runs)))

    # --- spawn ----------------------------------------------------------------------------------
    LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    eal = unreal.EditorAssetLibrary

    dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    if dirty:
        unreal.log_error("[IMGMAP] unsaved level(s) open: {} — aborting so nothing is lost".format(
            [p.get_name() for p in dirty]))
        return
    if not LES.new_level(level):
        unreal.log_error("[IMGMAP] new_level failed: " + level)
        return

    cube = eal.load_asset("/Engine/BasicShapes/Cube")
    wall_mat = eal.load_asset(MAT_DIR + "MI_Pac_Blocker")
    floor_mat = eal.load_asset(MAT_DIR + "MI_Pac_Floor")

    def spawn(label, loc, scale, mat):
        a = EAS.spawn_actor_from_object(cube, unreal.Vector(*loc))
        a.set_actor_label(label)
        a.set_actor_scale3d(unreal.Vector(*scale))
        c = a.static_mesh_component
        c.set_material(0, mat)
        c.set_collision_profile_name("BlockAll")
        return a

    def wx(px):
        return (px - w / 2.0 + 0.5) * tile

    def wy(py):
        return (h / 2.0 - py - 0.5) * tile     # image +y is down; world +y is north

    spawn("Floor", (0, 0, -25), (world_w / 100.0, world_h / 100.0, 0.5), floor_mat)

    for y, s, run, ht in runs:
        spawn("Blk_{}_{}".format(y, s),
              ((wx(s) + wx(s + run - 1)) / 2.0, wy(y), ht / 2.0),
              (run * tile / 100.0, tile / 100.0, ht / 100.0), wall_mat)

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

    # Drop the spawn on the first open pixel found scanning up from the bottom edge.
    sx, sy = w // 2, h - 1
    for y in range(h - 1, -1, -1):
        row = [x for x in range(w) if grid[y][x] <= 0]
        if row:
            sy = y
            sx = min(row, key=lambda x: abs(x - w // 2))
            break
    ps = EAS.spawn_actor_from_class(
        unreal.PlayerStart, unreal.Vector(wx(sx), wy(sy), 150),
        unreal.Rotator(roll=0.0, pitch=0.0, yaw=90.0))   # Rotator(a,b,c) = (roll, pitch, yaw)
    ps.set_actor_label("PlayerStart_ImageMap")

    unreal.EditorLoadingAndSavingUtils.save_current_level()
    log("saved {} — {} actors".format(level, len(EAS.get_all_level_actors())))
