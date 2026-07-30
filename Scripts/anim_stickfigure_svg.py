# Draws animation poses as an SVG stick figure so a pose can be eyeballed WITHOUT opening the
# editor. Headless, editor CLOSED, -run=pythonscript. Output: Saved/NeonV/anim/pose_preview.svg
#
# Edit SHOTS below to pick what to draw. Two projections per shot: Y-Z (side, the character faces
# roughly -Y in this rig) and X-Z (front).
import unreal
import os
import traceback

W2 = "/Game/Characters/Blu/Anims/W2_Rifle/"
# (asset path, label, frame; frame -1 means last)
SHOTS = [
    (W2 + "Blu_W2_Crouch_Aim_Idle_IPC", "source: Crouch_Aim_Idle", 0),
    (W2 + "Blu_W2_Slide_Enter", "Slide_Enter f0", 0),
    (W2 + "Blu_W2_Slide_Loop", "Slide_Loop f0", 0),
    (W2 + "Blu_W2_Slide_Exit_Crouch", "Slide_Exit_Crouch last", -1),
    (W2 + "Blu_W2_Slide_Exit_Stand", "Slide_Exit_Stand last", -1),
]
OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()),
                   "NeonV", "anim", "pose_preview.svg")

# bone -> full parent path from root, so component space needs no skeleton query
PATHS = {}


def chain(*bones):
    acc = []
    for b in bones:
        acc.append(b)
        PATHS[b] = list(acc)
    return list(bones)


SPINE = chain("root", "hips", "spine", "chest", "neck", "head")
ARM_R = ["chest", "shoulder_R", "upper_arm_R", "lower_arm_R", "hand_R"]
ARM_L = ["chest", "shoulder_L", "upper_arm_L", "lower_arm_L", "hand_L"]
LEG_R = ["hips", "upper_leg_R", "lower_leg_R", "foot_R", "toes_R"]
LEG_L = ["hips", "upper_leg_L", "lower_leg_L", "foot_L", "toes_L"]
for limb, base in ((ARM_R, "chest"), (ARM_L, "chest"), (LEG_R, "hips"), (LEG_L, "hips")):
    acc = list(PATHS[base])
    for b in limb[1:]:
        acc.append(b)
        PATHS[b] = list(acc)
LIMBS = [(SPINE[1:], "#e6e6e6"), (ARM_R, "#4fc3f7"), (ARM_L, "#81c784"),
         (LEG_R, "#ffb74d"), (LEG_L, "#f06292")]

gp = unreal.AnimationLibrary.get_bone_pose_for_frame
MK = unreal.MathLibrary


def log(m):
    unreal.log_warning("[SVG] " + str(m))


def pos(anim, bone, f):
    w = unreal.Transform()
    for b in PATHS[bone]:
        w = MK.compose_transforms(gp(anim, b, f, False), w)
    return w.translation


try:
    PW, PH, PAD = 190, 260, 12
    Z0, Z1 = -10.0, 185.0          # world cm mapped to panel height
    SPAN = 95.0                    # +/- cm horizontally
    sc = (PH - 2 * PAD) / (Z1 - Z0)
    panels = []
    for si, (path, label, frame) in enumerate(SHOTS):
        a = unreal.EditorAssetLibrary.load_asset(path)
        if a is None:
            log("missing %s" % path)
            continue
        nf = int(a.get_editor_property("data_model_interface").get_number_of_frames())
        f = nf if frame < 0 else frame
        for pi, axis in enumerate(("y", "x")):
            ox = (si * 2 + pi) * PW
            parts = ['<rect x="%d" y="0" width="%d" height="%d" fill="#181c22" stroke="#2c333d"/>'
                     % (ox, PW, PH),
                     '<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke="#3a4350" stroke-dasharray="3,3"/>'
                     % (ox, PAD + (Z1 - 0.0) * sc, ox + PW, PAD + (Z1 - 0.0) * sc)]

            def sx(p):
                return ox + PW / 2.0 + getattr(p, axis) * ((PW - 2 * PAD) / (2 * SPAN))

            def sy(p):
                return PAD + (Z1 - p.z) * sc

            for bones, col in LIMBS:
                pts = []
                for b in bones:
                    try:
                        pts.append(pos(a, b, f))
                    except Exception:
                        pass
                d = " ".join(("%s%.1f,%.1f" % ("M" if i == 0 else "L", sx(p), sy(p)))
                             for i, p in enumerate(pts))
                parts.append('<path d="%s" fill="none" stroke="%s" stroke-width="2.6" '
                             'stroke-linecap="round" stroke-linejoin="round"/>' % (d, col))
                for p in pts:
                    parts.append('<circle cx="%.1f" cy="%.1f" r="2.2" fill="%s"/>' % (sx(p), sy(p), col))
            parts.append('<text x="%d" y="%d" fill="#9aa7b8" font-family="sans-serif" '
                         'font-size="10">%s</text>' % (ox + 8, PH - 16, "side (Y-Z)" if pi == 0 else "front (X-Z)"))
            if pi == 0:
                parts.append('<text x="%d" y="18" fill="#dfe6f0" font-family="sans-serif" '
                             'font-size="11">%s (f%d)</text>' % (ox + 8, label, f))
            panels.append("".join(parts))
        log("drew %s frame %d/%d" % (label, f, nf))
    w = len(panels) * PW
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">'
           '<rect width="%d" height="%d" fill="#12151a"/>%s</svg>' % (w, PH, w, PH, w, PH, "".join(panels)))
    d = os.path.dirname(OUT)
    if not os.path.isdir(d):
        os.makedirs(d)
    with open(OUT, "w") as fh:
        fh.write(svg)
    log("WROTE %s" % OUT)
except Exception:
    unreal.log_error("[SVG] " + traceback.format_exc()[:2000])
