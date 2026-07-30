# Verifies Scripts/slide_upper_graft.py from a FRESH process (assets loaded from disk, compressed
# data - the same stream the game plays). Run headless with the editor CLOSED, after the graft.
#
# Checks: lower body survived unchanged / upper body equals the source pose / the rifle two-hand
# grip spacing is intact / foot height per frame is unchanged / the T-pose is actually gone.
import unreal
import json
import math
import os
import traceback

W2 = "/Game/Characters/Blu/Anims/W2_Rifle/"
AIM_NAME = "Blu_W2_Crouch_Aim_Idle_IPC"
SLIDES = ["Blu_W2_Slide_Enter", "Blu_W2_Slide_Loop", "Blu_W2_Slide_Exit_Crouch", "Blu_W2_Slide_Exit_Stand"]
SNAPSHOT = os.path.join(
    unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()),
    "NeonV", "anim", "pre_graft_snapshot.json")

# Anim compression runs again after the edit, so identical raw tracks can decompress a hair
# differently. These are "clearly wrong" thresholds; the measured worst is always printed.
TOL_POS = 0.1        # cm
TOL_QUAT = 0.002     # ~0.23 deg
CHAIN_FOOT = ["root", "hips", "upper_leg_L", "lower_leg_L", "foot_L"]
CHAIN_HANDR = ["root", "hips", "spine", "chest", "shoulder_R", "upper_arm_R", "lower_arm_R", "hand_R"]
CHAIN_HANDL = ["root", "hips", "spine", "chest", "shoulder_L", "upper_arm_L", "lower_arm_L", "hand_L"]

gp = unreal.AnimationLibrary.get_bone_pose_for_frame
MK = unreal.MathLibrary
EAL = unreal.EditorAssetLibrary
failures = []


def log(m):
    unreal.log_warning("[VERIFY] " + str(m))


def bad(m):
    failures.append(str(m))
    unreal.log_error("[VERIFY][FAIL] " + str(m))


def model(a):
    return a.get_editor_property("data_model_interface")


def tolist(t):
    return [t.translation.x, t.translation.y, t.translation.z,
            t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w,
            t.scale3d.x, t.scale3d.y, t.scale3d.z]


def make_quat(x, y, z, w):
    try:
        return unreal.Quat(x, y, z, w)
    except Exception:
        q = unreal.Quat()
        q.x, q.y, q.z, q.w = x, y, z, w
        return q


def fromlist(v):
    t = unreal.Transform()
    t.translation = unreal.Vector(v[0], v[1], v[2])
    t.rotation = make_quat(v[3], v[4], v[5], v[6])
    t.scale3d = unreal.Vector(v[7], v[8], v[9])
    return t


def delta(a, b):
    """(worst translation delta, worst quaternion component delta). Quaternions are sign-normalised
    first - q and -q are the same rotation."""
    dp = max(abs(a[i] - b[i]) for i in range(3))
    dot = sum(a[3 + i] * b[3 + i] for i in range(4))
    sgn = -1.0 if dot < 0 else 1.0
    dq = max(abs(a[3 + i] * sgn - b[3 + i]) for i in range(4))
    return dp, dq


def comp(anim, chain, f):
    w = unreal.Transform()
    for b in chain:
        w = MK.compose_transforms(gp(anim, b, f, False), w)
    return w


def comp_from(locals_by_bone, chain, f):
    w = unreal.Transform()
    for b in chain:
        w = MK.compose_transforms(fromlist(locals_by_bone[b][f]), w)
    return w


def dist(a, b):
    return math.sqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (a.z - b.z) ** 2)


try:
    with open(SNAPSHOT, "r") as f:
        snap = json.load(f)
    log("SNAPSHOT loaded (source=%s)" % snap.get("source"))

    aim = EAL.load_asset(W2 + AIM_NAME)
    # Case-insensitive on purpose: track names and skeleton names differ in case (lower_leg_r vs
    # lower_leg_R) and FName does not care, but Python str does.
    lower_lc = set(b.lower() for b in snap["lower"])
    aim_tracks = [str(b) for b in model(aim).get_bone_track_names()]
    upper = [b for b in aim_tracks if b.lower() not in lower_lc]
    if len(upper) != len(aim_tracks) - len(snap["lower"]):
        bad("bone split is wrong: %d tracks, %d upper" % (len(aim_tracks), len(upper)))
    aim_pose = {}
    for bn in upper:
        aim_pose[bn] = tolist(gp(aim, bn, 0, False))
    ar = comp(aim, CHAIN_HANDR, 0).translation
    al = comp(aim, CHAIN_HANDL, 0).translation
    aim_grip = dist(ar, al)
    log("SOURCE grip spacing = %.2fcm" % aim_grip)

    for name in SLIDES:
        a = EAL.load_asset(W2 + name)
        if a is None:
            bad("%s missing" % name)
            continue
        nf = int(model(a).get_number_of_frames())
        s = snap["clips"].get(name)
        if s is None:
            bad("%s has no snapshot entry" % name)
            continue
        if nf != s["frames"]:
            bad("%s frame count %d -> %d" % (name, s["frames"], nf))
            continue

        # 1. lower body survived
        wl = (0.0, 0.0, "")
        for bn in snap["lower"]:
            for f in range(nf + 1):
                dp, dq = delta(tolist(gp(a, bn, f, False)), s["lower"][bn][f])
                if dp > wl[0] or dq > wl[1]:
                    wl = (max(dp, wl[0]), max(dq, wl[1]), bn)
        if wl[0] > TOL_POS or wl[1] > TOL_QUAT:
            bad("%s lower body moved: pos %.4fcm quat %.5f (%s)" % (name, wl[0], wl[1], wl[2]))

        # 2. upper body equals the source pose, on every frame
        wu = (0.0, 0.0, "")
        for bn in upper:
            for f in (0, nf // 2, nf):
                dp, dq = delta(tolist(gp(a, bn, f, False)), aim_pose[bn])
                if dp > wu[0] or dq > wu[1]:
                    wu = (max(dp, wu[0]), max(dq, wu[1]), bn)
        if wu[0] > TOL_POS or wu[1] > TOL_QUAT:
            bad("%s upper body != source: pos %.4fcm quat %.5f (%s)" % (name, wu[0], wu[1], wu[2]))

        # 3. grip spacing kept
        grip = dist(comp(a, CHAIN_HANDR, 0).translation, comp(a, CHAIN_HANDL, 0).translation)
        if abs(grip - aim_grip) > 0.5:
            bad("%s grip spacing %.2fcm vs source %.2fcm" % (name, grip, aim_grip))

        # 4. foot height per frame unchanged
        wf = 0.0
        for f in range(nf + 1):
            wf = max(wf, abs(comp(a, CHAIN_FOOT, f).translation.z -
                             comp_from(s["lower"], CHAIN_FOOT, f).translation.z))
        if wf > TOL_POS:
            bad("%s foot_L height moved %.3fcm" % (name, wf))

        # 5. the T-pose is gone
        q = gp(a, "upper_arm_R", 0, False).rotation
        armdeg = math.degrees(2.0 * math.acos(max(-1.0, min(1.0, abs(q.w)))))
        if armdeg < 15.0:
            bad("%s upper_arm_R still near rest (%.1f deg) - arms did not take" % (name, armdeg))

        log("CLIP %s frames=%d | lower drift pos=%.4fcm quat=%.5f (%s) | upper vs source pos=%.4fcm "
            "quat=%.5f | grip=%.2fcm | foot drift=%.4fcm | upper_arm_R=%.1fdeg" %
            (name, nf, wl[0], wl[1], wl[2] or "-", wu[0], wu[1], grip, wf, armdeg))

    if failures:
        unreal.log_error("[VERIFY][FAIL] %d check(s) failed" % len(failures))
    else:
        log("ALLPASS")
except Exception:
    unreal.log_error("[VERIFY][FAIL] " + traceback.format_exc()[:2500])
