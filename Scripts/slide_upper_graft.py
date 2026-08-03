# Slide upper-body graft (ADR 0002 4c / UE 3a-1).
#
# The 4 slide clips were authored in Blender lower-body only, so their arms are still in a T-pose.
# This pastes the rifle aiming upper body (frame 0 of Blu_W2_Crouch_Aim_Idle_IPC) onto them,
# leaving the lower body untouched. Wall clips are bare-handed by design and are NOT touched.
#
# Local-space copy is enough: the slide pelvis sits 11.5 deg off the aim pose's pelvis in every
# frame of every clip, which lands the gun 7.7 deg off - measured, not assumed. A component-space
# re-solve at `spine` would only be needed if that error were large.
#
# Run headless with the editor CLOSED:
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="Scripts/slide_upper_graft.py"
#     -unattended -nopause -nullrhi -nosplash -nosound -abslog=<log>
# Then verify from a fresh process with Scripts/slide_upper_graft_verify.py.
#
# Idempotent: re-running produces the same asset (upper comes from the source, lower is read back
# from the clip itself). Revert = git checkout -- Content/Characters/Blu/Anims/W2_Rifle/Blu_W2_Slide_*
import unreal
import json
import math
import os
import traceback

W2 = "/Game/Characters/Blu/Anims/W2_Rifle/"
AIM_NAME = "Blu_W2_Crouch_Aim_Idle_IPC"
AO_ASSET = "/Game/Rifle_01/AimOffset/AO_Crouch_Aim"
SLIDES = ["Blu_W2_Slide_Enter", "Blu_W2_Slide_Loop", "Blu_W2_Slide_Exit_Crouch", "Blu_W2_Slide_Exit_Stand"]
# The slide states were collapsed onto a single clip (user decision 2026-08-03), so a re-reflection
# only needs to graft that one. Narrow it with FPSR_GRAFT_SLIDES to keep the diff to what changed.
if os.environ.get("FPSR_GRAFT_SLIDES"):
    SLIDES = [s.strip() for s in os.environ["FPSR_GRAFT_SLIDES"].split(",") if s.strip()]

# Everything else in the 108-bone Blu skeleton is upper body (spine and below it in the hierarchy).
LOWER = ["root", "hips", "upper_leg_L", "upper_leg_R", "lower_leg_L", "lower_leg_R",
         "foot_L", "foot_R", "toes_L", "toes_R"]
# Cosmetic bones ride under the upper body; grafted like the rest, reported separately because
# they are the one group where an unexpected change could hide.
DECOR_PREFIX = ("hair", "eye", "Eyelid", "eyebrow", "mouth", "lip", "glasses", "Rope")
BASE_POSE_TOL_DEG = 1.0

gp = unreal.AnimationLibrary.get_bone_pose_for_frame
EAL = unreal.EditorAssetLibrary
SNAPSHOT = os.path.join(
    unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()),
    "NeonV", "anim", "pre_graft_snapshot.json")


def log(m):
    unreal.log_warning("[GRAFT] " + str(m))


def fail(m):
    unreal.log_error("[GRAFT][FAIL] " + str(m))


def model(a):
    return a.get_editor_property("data_model_interface")


def tolist(t):
    return [t.translation.x, t.translation.y, t.translation.z,
            t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w,
            t.scale3d.x, t.scale3d.y, t.scale3d.z]


def preflight_base_pose(aim, upper):
    """The graft is only correct if the aim offset's additive base is the pose we are pasting -
    otherwise the shoulders twist as soon as the player aims off-centre.

    Compares POSES, not asset names: AO_Crouch_Aim is extracted against
    Blu_W2_Crouch_Aim_Point_Center while PROGRESS names the idle (that is the editor *preview*
    base, a different field). Measured 2026-07-30, the two are the same pose to 0.02cm / 0.0deg,
    so a name check would abort on a difference that does not exist.
    Positive evidence of a mismatch aborts; an unreadable registry only warns."""
    try:
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
        deps = ar.get_dependencies(AO_ASSET, unreal.AssetRegistryDependencyOptions())
    except Exception as e:
        log("PREFLIGHT skipped - could not read AO dependencies (%s)" % str(e)[:100])
        return True
    seen = {}
    bases = {}
    for d in deps or []:
        name = str(d)
        if "Aim_Point" not in name and "Aim_Idle" not in name:
            continue
        try:
            s = EAL.load_asset(name)
            if not isinstance(s, unreal.AnimSequence):
                continue
            # Engine names: UAnimSequence::RefPoseSeq / RefPoseType / AdditiveAnimType
            # (Engine/Classes/Animation/AnimSequence.h:285-297).
            bp = s.get_editor_property("ref_pose_seq")
            bt = s.get_editor_property("ref_pose_type")
            at = s.get_editor_property("additive_anim_type")
            key = (str(bp.get_name()) if bp else "None", str(bt), str(at))
            seen[key] = seen.get(key, 0) + 1
            if bp is not None:
                bases[str(bp.get_name())] = bp
        except Exception as e:
            log("PREFLIGHT sample %s unreadable (%s)" % (name.split("/")[-1], str(e)[:60]))
    if not seen:
        log("PREFLIGHT skipped - no readable additive samples under %s" % AO_ASSET)
        return True
    for k, n in seen.items():
        log("PREFLIGHT base=%s type=%s additive=%s x%d" % (k[0], k[1], k[2], n))
    for bname, bseq in bases.items():
        worst = ("", 0.0)
        for bn in upper:
            try:
                qa = gp(aim, bn, 0, False).rotation
                qb = gp(bseq, bn, 0, False).rotation
                dot = abs(qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w)
                deg = math.degrees(2.0 * math.acos(max(-1.0, min(1.0, dot))))
                if deg > worst[1]:
                    worst = (bn, deg)
            except Exception:
                pass
        log("PREFLIGHT pose %s vs %s: worst upper bone %s %.2fdeg" % (bname, AIM_NAME, worst[0] or "-", worst[1]))
        if worst[1] > BASE_POSE_TOL_DEG:
            fail("additive base %s differs from the graft source %s by %.2fdeg at %s - aiming would "
                 "twist the shoulders. Nothing was modified." % (bname, AIM_NAME, worst[1], worst[0]))
            return False
    return True


def main():
    aim = EAL.load_asset(W2 + AIM_NAME)
    if aim is None:
        fail("source pose %s not found" % AIM_NAME)
        return False
    # get_bone_track_names() hands back unreal.Name. FName compares case-insensitively but str()
    # does not, and the tracks really do carry different case than the skeleton (lower_leg_r vs
    # lower_leg_R) - so normalise case on both sides or a leg bone silently lands in `upper`.
    aim_tracks = [str(b) for b in model(aim).get_bone_track_names()]
    lower_lc = set(b.lower() for b in LOWER)
    upper = [b for b in aim_tracks if b.lower() not in lower_lc]
    track_lc = set(b.lower() for b in aim_tracks)
    missing_lower = [b for b in LOWER if b.lower() not in track_lc]
    if missing_lower:
        fail("source is missing lower bones %s - bone naming changed" % missing_lower)
        return False
    if len(upper) != len(aim_tracks) - len(LOWER):
        fail("bone split is wrong: %d tracks, %d upper, expected %d lower to be excluded" %
             (len(aim_tracks), len(upper), len(LOWER)))
        return False
    log("SOURCE %s tracks=%d upper=%d lower=%d" % (AIM_NAME, len(aim_tracks), len(upper), len(LOWER)))

    if not preflight_base_pose(aim, upper):
        return False

    # Frame 0 of the source, one transform per upper bone (the idle drifts 0.30cm over 60 frames,
    # so a single frame loses nothing and keeps all four clips identical up top).
    pose = {}
    for bn in upper:
        pose[bn] = gp(aim, bn, 0, False)

    snapshot = {"source": AIM_NAME, "lower": LOWER, "clips": {}}
    ok = True
    for name in SLIDES:
        path = W2 + name
        a = EAL.load_asset(path)
        if a is None:
            fail("%s not found" % name)
            ok = False
            continue
        m = model(a)
        nf = int(m.get_number_of_frames())
        rate = m.get_frame_rate()
        tracks_lc = set(str(b).lower() for b in m.get_bone_track_names())
        gone = [b for b in upper if b.lower() not in tracks_lc]

        # Snapshot the lower body from disk state BEFORE touching anything, so a fresh process can
        # prove it survived.
        pre_lower = {}
        for bn in LOWER:
            pre_lower[bn] = [tolist(gp(a, bn, f, False)) for f in range(nf + 1)]
        pre_upper = {}
        for bn in upper:
            pre_upper[bn] = tolist(gp(a, bn, 0, False))

        ctrl = a.get_editor_property("controller")
        ctrl.open_bracket("Graft rifle aim upper body", False)
        wrote = 0
        for bn in upper:
            t = pose[bn]
            keys = nf + 1
            pos = [t.translation] * keys
            rot = [t.rotation] * keys
            scl = [t.scale3d] * keys
            if bn.lower() not in tracks_lc:
                ctrl.add_bone_curve(bn, False)
            ctrl.set_bone_track_keys(bn, pos, rot, scl, False)
            wrote += 1
        ctrl.close_bracket(False)
        EAL.save_asset(a.get_path_name())

        m2 = model(a)
        nf2 = int(m2.get_number_of_frames())
        rate2 = m2.get_frame_rate()
        if nf2 != nf or rate2.numerator != rate.numerator or rate2.denominator != rate.denominator:
            fail("%s frame count/rate changed %d@%s/%s -> %d@%s/%s" %
                 (name, nf, rate.numerator, rate.denominator, nf2, rate2.numerator, rate2.denominator))
            ok = False

        # How far each group actually moved, so nothing changes silently.
        worst_decor = ("", 0.0)
        worst_body = ("", 0.0)
        for bn in upper:
            now = tolist(gp(a, bn, 0, False))
            d = max(abs(now[i] - pre_upper[bn][i]) for i in range(10))
            is_decor = bn.lower().startswith(tuple(p.lower() for p in DECOR_PREFIX))
            grp = worst_decor if is_decor else worst_body
            if d > grp[1]:
                if is_decor:
                    worst_decor = (bn, d)
                else:
                    worst_body = (bn, d)
        snapshot["clips"][name] = {"frames": nf, "lower": pre_lower}
        log("CLIP %s frames=%d wrote=%d addedTracks=%d | biggest change: body %s=%.3f  decor %s=%.3f" %
            (name, nf, wrote, len(gone), worst_body[0], worst_body[1], worst_decor[0], worst_decor[1]))

    d = os.path.dirname(SNAPSHOT)
    if not os.path.isdir(d):
        os.makedirs(d)
    with open(SNAPSHOT, "w") as f:
        json.dump(snapshot, f)
    log("SNAPSHOT %s" % SNAPSHOT)
    return ok


try:
    if main():
        log("ALLDONE")
    else:
        fail("ABORTED")
except Exception:
    fail(traceback.format_exc()[:2500])
