"""리타게팅된 포즈에 **실제 값**이 들어갔는지 본다 — 읽기 전용.

트랙 존재는 앞서 확인했다. 그런데 트랙이 있어도 값이 전부 rest 면 팔이 A자로 선다 —
그건 트랙 개수로는 안 잡힌다. 그래서 본 로컬 포즈를 직접 읽고, 부모 체인을 곱해
`hand_r` 의 **컴포넌트 공간 위치**를 원본과 나란히 낸다. 컴포넌트를 틱시킬 필요가 없다.
"""
import unreal
def log(m): unreal.log("[POSE] %s" % m)
CH = {"pwas": ["pelvis","spine_01","spine_02","spine_03","spine_04","spine_05",
               "clavicle_r","upperarm_r","lowerarm_r","hand_r"],
      "lpamg": ["pelvis","spine_01","spine_02","spine_03",
                "clavicle_r","upperarm_r","lowerarm_r","hand_r"]}
CASES = [("PWAS 원본 Idle", "/Game/ProceduralWeaponAnimationSystem/Animations/Poses/Rifle/A_FP_Rifle_Pose", "pwas"),
         ("리타게팅 Idle",  "/Game/Character/FPArms/Anims_LPAMG/FP_Rifle_Idle", "lpamg"),
         ("리타게팅 ADS",   "/Game/Character/FPArms/Anims_LPAMG/FP_Rifle_ADS", "lpamg"),
         ("리타게팅 Reload@1.0s", "/Game/Character/FPArms/Anims_LPAMG/FP_Rifle_Reload", "lpamg")]
for label, path, kind in CASES:
    a = unreal.EditorAssetLibrary.load_asset(path)
    if not a:
        unreal.log_error("[POSE] !! %s 없음" % path); continue
    t = 1.0 if "Reload@" in label else 0.0
    m = unreal.Transform()
    moved = []
    for b in CH[kind]:
        try:
            lt = unreal.AnimationLibrary.get_bone_pose_for_time(a, b, t, False)
        except Exception as e:
            unreal.log_error("[POSE] !! %s %s: %s" % (label, b, e)); lt = None
        if lt is None: continue
        m = lt.multiply(m)
        r = lt.rotation.rotator()
        if abs(r.roll) + abs(r.pitch) + abs(r.yaw) > 1.0:
            moved.append("%s(%.0f,%.0f,%.0f)" % (b, r.roll, r.pitch, r.yaw))
    p = m.translation
    log("%-22s hand_r 컴포넌트공간 = [%7.2f, %7.2f, %7.2f]" % (label, p.x, p.y, p.z))
    log("     회전이 실린 본 %d개: %s" % (len(moved), ", ".join(moved[:6]) if moved else "**없음 = rest 그대로**"))
log("POSE_DONE")
