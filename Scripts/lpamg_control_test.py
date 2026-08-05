"""측정 도구 대조군 — 원본 재장전을 여러 시점에서 재서 값이 변하는지 본다.

결과가 시간에 따라 안 변하면 **리타게팅이 아니라 측정 도구가 고장난 것**이다.
"""
import unreal
def log(m): unreal.log("[CTL] %s" % m)
CH = {"pwas": ["pelvis","spine_01","spine_02","spine_03","spine_04","spine_05",
               "clavicle_r","upperarm_r","lowerarm_r","hand_r"],
      "lpamg": ["pelvis","spine_01","spine_02","spine_03",
                "clavicle_r","upperarm_r","lowerarm_r","hand_r"]}
def hand_at(a, kind, t):
    m = unreal.Transform()
    for b in CH[kind]:
        lt = unreal.AnimationLibrary.get_bone_pose_for_time(a, b, t, False)
        if lt: m = lt.multiply(m)
    return m.translation
for label, path, kind in [("원본 Reload", "/Game/ProceduralWeaponAnimationSystem/Animations/Reload/A_FP_RifleReload", "pwas"),
                          ("리타 Reload", "/Game/Character/FPArms/Anims_LPAMG/FP_Rifle_Reload", "lpamg")]:
    a = unreal.EditorAssetLibrary.load_asset(path)
    if not a: unreal.log_error("[CTL] !! %s 없음" % path); continue
    dur = a.get_editor_property("sequence_length")
    log("%s (길이 %.2fs)" % (label, dur))
    for t in (0.0, 0.5, 1.0, 1.5, 2.0):
        p = hand_at(a, kind, min(t, dur))
        log("    t=%.1f  hand_r = [%7.2f, %7.2f, %7.2f]" % (t, p.x, p.y, p.z))
# 원본 Idle vs AimPose 가 서로 다른가 (같으면 소스 읽기 자체가 의심)
for label, path in [("원본 Idle","/Game/ProceduralWeaponAnimationSystem/Animations/Poses/Rifle/A_FP_Rifle_Pose"),
                    ("원본 AimPose","/Game/ProceduralWeaponAnimationSystem/Animations/Poses/Rifle/A_FP_Rifle_AimPose")]:
    a = unreal.EditorAssetLibrary.load_asset(path)
    p = hand_at(a, "pwas", 0.0)
    log("%-14s hand_r = [%7.2f, %7.2f, %7.2f]" % (label, p.x, p.y, p.z))
log("CTL_DONE")
