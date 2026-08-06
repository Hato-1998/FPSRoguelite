"""인게임 배선에서 "카메라가 팔을 어떻게 보는가"를 실측한다 — 저작 씬 카메라 검증용.

Blender 저작 씬은 카메라를 **팔 기준 위치**(CAM_IN_ARMS_UE)에 놓고 리그 정면(-Y)을 보게 한다.
그런데 배선에는 yaw -95° 가 있다. 위치는 그 yaw 를 반영해 뽑았는데 **회전은 안 넣었다** —
둘 중 하나는 틀렸다. 눈대중으로 못 가르는 값이라 여기서 잰다.

내는 값: 팔 컴포넌트의 로컬 축(정면/오른쪽/위)이 **카메라 공간**에서 어디를 향하는가,
그리고 `hand_r`·`SOCKET_Weapon` 이 카메라 공간 어디에 있는가(레퍼런스 포즈 기준).

⚠️ 플레이어 BP 를 잠깐 스폰한다. 반드시 지우고 나온다(끝에 검사).

실행: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<이 파일>" -unattended -nopause
"""
import json
import os

import unreal

PLAYER_BP = "/Game/Character/Player/BP_FPSRPlayer"
OUT = "E:/Git_Project/FPSRoguelite/Saved/NeonV/refpose/ingame_frame_probe.json"
TAG = "TEMP_IngameFrameProbe"

report, fails = {}, []


def log(m):
    unreal.log("[FRAME] %s" % m)


def fail(m):
    fails.append(m)
    unreal.log_error("[FRAME] !! %s" % m)


def v3(v):
    return [round(v.x, 4), round(v.y, 4), round(v.z, 4)]


os.makedirs(os.path.dirname(OUT), exist_ok=True)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

bp = unreal.EditorAssetLibrary.load_asset(PLAYER_BP)
if bp is None:
    fail("%s 를 못 연다" % PLAYER_BP)
    raise SystemExit(1)

for a in eas.get_all_level_actors():
    if a.get_actor_label() == TAG:
        eas.destroy_actor(a)

actor = eas.spawn_actor_from_class(bp.generated_class(), unreal.Vector(0, 0, 0),
                                   unreal.Rotator(0, 0, 0))
actor.set_actor_label(TAG)
try:
    cams = actor.get_components_by_class(unreal.CameraComponent)
    meshes = actor.get_components_by_class(unreal.SkeletalMeshComponent)
    log("카메라 %d개 / 스켈레탈메시 %d개 — %s"
        % (len(cams), len(meshes), [m.get_name() for m in meshes]))
    arms = next((m for m in meshes if "FirstPersonArms" in m.get_name()), None)
    if arms is None:
        arms = next((m for m in meshes
                     if m.get_skeletal_mesh_asset() is not None
                     and "FPArms" in m.get_skeletal_mesh_asset().get_name()), None)
    cam = cams[0] if cams else None
    if arms is None or cam is None:
        fail("팔 컴포넌트 또는 카메라를 못 찾았다")
    else:
        mesh_asset = arms.get_skeletal_mesh_asset()
        log("팔 컴포넌트 %s · 메시 %s"
            % (arms.get_name(), mesh_asset.get_name() if mesh_asset else "없음"))
        cam_w = cam.get_world_transform()
        cam_inv = cam_w.inverse()
        arms_w = arms.get_world_transform()
        rel = arms_w.multiply(cam_inv)
        report["arms_in_camera"] = {
            "location": v3(rel.translation),
            "rotation_rpy": [round(rel.rotation.rotator().roll, 3),
                             round(rel.rotation.rotator().pitch, 3),
                             round(rel.rotation.rotator().yaw, 3)],
            "scale": v3(rel.scale3d),
        }
        log("팔 컴포넌트 (카메라 기준) loc %s rot %s"
            % (report["arms_in_camera"]["location"], report["arms_in_camera"]["rotation_rpy"]))

        # 팔 컴포넌트의 로컬 축이 카메라 공간에서 어디를 보나
        for axis, name in ((unreal.Vector(1, 0, 0), "forward_X"),
                           (unreal.Vector(0, 1, 0), "right_Y"),
                           (unreal.Vector(0, 0, 1), "up_Z")):
            d = rel.rotation.rotate_vector(axis)
            report.setdefault("arms_axes_in_camera", {})[name] = v3(d)
        log("팔 축(카메라 공간) %s" % report["arms_axes_in_camera"])

        # 레퍼런스 포즈 기준 hand_r / SOCKET_Weapon 이 카메라 공간 어디인가
        for sock in ("hand_r", "hand_l", "SOCKET_Weapon"):
            if not arms.does_socket_exist(sock):
                log("  (%s 없음)" % sock)
                continue
            w = arms.get_socket_transform(sock, unreal.RelativeTransformSpace.RTS_WORLD)
            r = w.multiply(cam_inv)
            report.setdefault("sockets_in_camera_cm", {})[sock] = {
                "location": v3(r.translation),
                "rotation_rpy": [round(r.rotation.rotator().roll, 3),
                                 round(r.rotation.rotator().pitch, 3),
                                 round(r.rotation.rotator().yaw, 3)]}
        log("소켓(카메라 기준) %s" % json.dumps(report.get("sockets_in_camera_cm", {}),
                                            ensure_ascii=False))
        report["camera_fov"] = round(cam.get_editor_property("field_of_view"), 3)
        report["arms_mesh"] = mesh_asset.get_name() if mesh_asset else None
except Exception as e:  # noqa: BLE001
    fail("측정 실패: %s" % e)
finally:
    eas.destroy_actor(actor)

left = [a for a in eas.get_all_level_actors() if a.get_actor_label() == TAG]
log("임시 액터 정리 — 남은 것 %d개" % len(left))
if left:
    fail("임시 액터가 안 지워졌다")

with open(OUT, "w", encoding="utf-8") as f:
    json.dump(report, f, ensure_ascii=False, indent=1)
log("기록 -> %s" % OUT)
if fails:
    raise SystemExit(1)
log("INGAME_FRAME_PROBE_DONE")
