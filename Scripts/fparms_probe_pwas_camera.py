"""PWAS 데모 폰의 "카메라가 팔 root 기준 어디에 있나"를 잰다 — 읽기 전용 참고치.

**기준선이 아니다.** 우리 구도의 진실원천은 `BP_FPSRPlayer` 의 `FirstPersonArms` 컴포넌트
트랜스폼이고, Blender 저작 씬의 `REF_FPCamera` 가 그 역을 그대로 들고 있다. 여기서 재는 값은
"PWAS 견본 애니를 1:1 로 옮기면 총이 카메라에서 얼마나 떨어지는가"를 **예측**하기 위한 것이다.

견본 애니의 손 위치를 root 공간에서 그대로 복사할 계획이므로(ADR 0005 — 손이 root 직계),
PWAS 가 전제한 카메라↔root 관계를 알면 우리 rest 트랜스폼을 얼마나 옮겨야 같은 구도가
나오는지 계산으로 나온다. 눈대중 반복을 한 바퀴 줄인다.

⚠️ 데모 폰을 잠깐 스폰한다. 반드시 지우고 나온다(끝에 검사).

실행: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<이 파일>" -unattended -nopause
"""
import json
import os

import unreal

DEMO_PAWN = "/Game/ProceduralWeaponAnimationSystem/Blueprints/BP_FPCharacter"
RELOAD = "/Game/ProceduralWeaponAnimationSystem/Animations/Reload/A_FP_RifleReload"
IDLE = "/Game/ProceduralWeaponAnimationSystem/Animations/Poses/Rifle/A_FP_Rifle_Pose"
OUT = "E:/Git_Project/FPSRoguelite/Saved/NeonV/refpose/pwas_camera_probe.json"

TAG = "TEMP_PWASCameraProbe"
report = {}
fails = []


def log(m):
    unreal.log("[PWASCAM] %s" % m)


def fail(m):
    fails.append(m)
    unreal.log_error("[PWASCAM] !! %s" % m)


def vec(v):
    return [round(v.x, 4), round(v.y, 4), round(v.z, 4)]


def rot(r):
    return [round(r.roll, 4), round(r.pitch, 4), round(r.yaw, 4)]


os.makedirs(os.path.dirname(OUT), exist_ok=True)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# --- 애니 메타(프레임 수·길이) — Blender 전이 스크립트가 프레임 범위를 알아야 한다 ---
for tag, path in (("idle", IDLE), ("reload", RELOAD)):
    a = unreal.EditorAssetLibrary.load_asset(path)
    if a is None:
        fail("%s 를 못 연다" % path)
        continue
    info = {"asset": path,
            "skeleton": a.get_editor_property("skeleton").get_name(),
            "frames": a.get_editor_property("number_of_sampled_frames"),
            "length_sec": round(a.get_editor_property("sequence_length"), 4)}
    try:
        fr = a.get_editor_property("target_frame_rate")
        info["fps"] = "%s/%s" % (fr.numerator, fr.denominator)
    except Exception as e:  # noqa: BLE001
        info["fps"] = "?(%s)" % e
    report[tag] = info
    log("%-7s %s" % (tag, info))

# --- 데모 폰을 잠깐 스폰해 카메라↔root 를 잰다 ---
bp = unreal.EditorAssetLibrary.load_asset(DEMO_PAWN)
if bp is None:
    fail("데모 폰 %s 를 못 연다 — 카메라 실측은 건너뛴다" % DEMO_PAWN)
else:
    for a in eas.get_all_level_actors():
        if a.get_actor_label() == TAG:
            eas.destroy_actor(a)
    actor = None
    try:
        actor = eas.spawn_actor_from_class(bp.generated_class(),
                                           unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
        actor.set_actor_label(TAG)
        cams = actor.get_components_by_class(unreal.CameraComponent)
        meshes = actor.get_components_by_class(unreal.SkeletalMeshComponent)
        log("컴포넌트 — 카메라 %d개 / 스켈레탈메시 %d개" % (len(cams), len(meshes)))
        if not cams or not meshes:
            fail("데모 폰에서 카메라 또는 스켈레탈 메시를 못 찾았다")
        else:
            # 팔 메시 = root 본을 가진 것 중 첫 번째(데모는 FP 메시 하나다)
            mesh = next((m for m in meshes if m.get_skeletal_mesh_asset() is not None), meshes[0])
            cam = cams[0]
            mesh_w = mesh.get_socket_transform("root", unreal.RelativeTransformSpace.RTS_WORLD)
            cam_w = cam.get_world_transform()
            rel = cam_w.multiply(mesh_w.inverse())
            report["camera_in_root_ue_cm"] = {
                "mesh_component": mesh.get_name(),
                "mesh_asset": mesh.get_skeletal_mesh_asset().get_name(),
                "camera_component": cam.get_name(),
                "location": vec(rel.translation),
                "rotation_rpy": rot(rel.rotation.rotator()),
                "camera_fov": round(cam.get_editor_property("field_of_view"), 3),
                "mesh_root_world": vec(mesh_w.translation),
                "camera_world": vec(cam_w.translation),
            }
            log("카메라 in root = %s · FOV %s"
                % (report["camera_in_root_ue_cm"]["location"],
                   report["camera_in_root_ue_cm"]["camera_fov"]))
            # 견본 idle 에서 hand_r 이 root 기준 어디인지 — 총이 어디 붙는지의 대리 지표
            for bone in ("hand_r", "hand_l"):
                if mesh.does_socket_exist(bone):
                    bw = mesh.get_socket_transform(bone, unreal.RelativeTransformSpace.RTS_WORLD)
                    br = bw.multiply(mesh_w.inverse())
                    report.setdefault("bones_in_root_refpose_cm", {})[bone] = vec(br.translation)
            log("레퍼런스 포즈 손 위치(root 기준) = %s"
                % report.get("bones_in_root_refpose_cm"))
    except Exception as e:  # noqa: BLE001
        fail("데모 폰 스폰/측정 실패 (참고치라 치명적이지 않음): %s" % e)
    finally:
        if actor is not None:
            eas.destroy_actor(actor)

left = [a for a in eas.get_all_level_actors() if a.get_actor_label() == TAG]
log("임시 액터 정리 — 남은 것 %d개" % len(left))
if left:
    fail("임시 액터가 안 지워졌다 — 레벨 저장 전에 손으로 지울 것")

with open(OUT, "w", encoding="utf-8") as f:
    json.dump(report, f, ensure_ascii=False, indent=1)
log("기록 -> %s" % OUT)

log("=== 결과 ===")
if fails:
    for f_ in fails:
        unreal.log_error("[PWASCAM] !! %s" % f_)
    raise SystemExit(1)
log("PWASCAM_PROBE_DONE")
