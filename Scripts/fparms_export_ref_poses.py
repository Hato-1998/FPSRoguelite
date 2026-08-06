"""PWAS 견본 포즈를 FBX 로 뽑는다 — Blender 저작의 바닥에 깔 것. 커맨드렛 전용.

우리는 이 포즈를 **재생하지 않는다**(ADR 0004 — 자체 스켈레톤으로 떠났다). 견본으로만 쓴다:
회전만 옮겨 바닥을 깔고 그 위에서 손보는 게, 파라미터로 포즈를 유도하는 것보다 빠르다.
"총이 화면에 보인다"와 "양손이 총을 잡는다"를 동시에 만족시키는 문제를 PWAS 가 이미 풀어 놨다.

회전은 비율이 달라도 그대로 옮겨진다. 우리 스켈레톤은 **rest 회전이 S_Mannequin 과 같고**
(실측 최대 0.00089도) 위치만 다르므로, rest 기준 상대 포즈(`matrix_basis`)가 1:1 로 유효하다.

🚨 `AssetExportTask` 에 `options=FbxExportOption()` 을 안 주면 대화상자가 떠서 커맨드렛이 멈춘다.

실행:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<이 파일>" -unattended -nopause
"""
import os

import unreal

OUT_DIR = "E:/Git_Project/FPSRoguelite/Saved/NeonV/refpose"
POSES = {
    "A_FP_Rifle_Pose": "/Game/ProceduralWeaponAnimationSystem/Animations/Poses/Rifle/A_FP_Rifle_Pose",
    "A_FP_Rifle_AimPose": "/Game/ProceduralWeaponAnimationSystem/Animations/Poses/Rifle/A_FP_Rifle_AimPose",
    # 재장전은 포즈가 아니라 여러 프레임짜리 애니지만 내보내는 방법은 같다(AnimSequenceExporterFBX).
    # Reload 저작의 손 궤적 견본으로 쓴다 — 우리는 손 위치만 가져가고 팔 체인은 안 쓴다(ADR 0005).
    "A_FP_RifleReload": "/Game/ProceduralWeaponAnimationSystem/Animations/Reload/A_FP_RifleReload",
}

os.makedirs(OUT_DIR, exist_ok=True)
fails = []


def log(m):
    unreal.log("[REFPOSE] %s" % m)


opts = unreal.FbxExportOption()
opts.set_editor_property("ascii", False)
opts.set_editor_property("collision", False)
opts.set_editor_property("force_front_x_axis", False)
# 레벨 시퀀스가 아니라 애님 시퀀스라 아래 둘만 의미가 있다
try:
    opts.set_editor_property("map_skeletal_motion_to_root", False)
except Exception as e:  # noqa: BLE001
    log("(map_skeletal_motion_to_root 없음: %s)" % e)

for tag, path in POSES.items():
    anim = unreal.EditorAssetLibrary.load_asset(path)
    if anim is None:
        fails.append("%s 를 못 찾았다" % path)
        unreal.log_error("[REFPOSE] !! %s 없음" % path)
        continue
    out = os.path.join(OUT_DIR, tag + ".fbx")
    task = unreal.AssetExportTask()
    task.set_editor_property("object", anim)
    task.set_editor_property("filename", out)
    task.set_editor_property("automated", True)
    task.set_editor_property("prompt", False)
    task.set_editor_property("replace_identical", True)
    task.set_editor_property("options", opts)
    task.set_editor_property("exporter", unreal.AnimSequenceExporterFBX())
    ok = unreal.Exporter.run_asset_export_task(task)
    size = os.path.getsize(out) / 1024.0 if os.path.exists(out) else 0.0
    log("%-20s 스켈레톤 %s · 프레임 %s -> %s (%.0f KB) %s"
        % (tag, anim.get_editor_property("skeleton").get_name(),
           anim.get_editor_property("number_of_sampled_frames")
           if hasattr(anim, "get_editor_property") else "?",
           os.path.basename(out), size, "OK" if ok and size > 0 else "!! 실패"))
    if not ok or size <= 0:
        fails.append("%s 내보내기 실패" % tag)

log("=== 결과 ===")
if fails:
    for f in fails:
        unreal.log_error("[REFPOSE] !! %s" % f)
    raise SystemExit(1)
log("견본 포즈 %d개 -> %s" % (len(POSES), OUT_DIR))
log("REFPOSE_EXPORT_DONE")
