"""LPAMG 팔로 갈아탄 뒤의 배선 상태를 잰다 — 읽기 전용.

보는 것: 플레이어 BP 의 `FirstPersonArms`(메시·애님클래스·상대 트랜스폼) · 팔 AnimBP 들이
어느 스켈레톤을 대상으로 하는지 · 기존 IK Rig/리타게터가 어느 메시에 묶여 있는지.
리타게팅 경로를 새로 만들지, 있는 걸 재활용할지는 이 값으로 갈린다.

실행: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<이 파일>" -unattended -nopause
"""
import json
import os

import unreal

OUT = "E:/Git_Project/FPSRoguelite/Saved/NeonV/lpamg_wiring_probe.json"
PLAYER = "/Game/Character/Player/BP_FPSRPlayer"
ANIMBPS = ["/Game/Character/Player/ABP_FP_Base", "/Game/Character/Player/ABP_FPArms"]
IKS = ["/Game/Characters/Retarget/IK_UE4Mannequin", "/Game/Characters/Retarget/IK_Blu",
       "/Game/Characters/Blu/IK_MannySrc", "/Game/Characters/Blu/IK_Blu"]
RTGS = ["/Game/Characters/Retarget/RTG_UE4Man_to_Blu", "/Game/Characters/Blu/RTG_Manny_to_Blu"]

report = {}


def log(m):
    unreal.log("[WIRE] %s" % m)


def name_of(o):
    return o.get_name() if o else None


# --- 팔 AnimBP 들이 어느 스켈레톤인가 ---
for p in ANIMBPS:
    a = unreal.EditorAssetLibrary.load_asset(p)
    if a is None:
        log("%-46s 없음" % p)
        continue
    skel = a.get_editor_property("target_skeleton")
    # 🪤 `parent_class` 는 AnimBlueprint 에 노출돼 있지 않다(실측). 생성 클래스에서 올라간다.
    parent = None
    try:
        gc = a.generated_class()
        parent = unreal.get_default_object(gc).get_class().get_super_class() if gc else None
    except Exception:  # noqa: BLE001
        parent = None
    report.setdefault("animbp", {})[p] = {"skeleton": name_of(skel), "parent": name_of(parent)}
    log("%-46s 스켈레톤 %-28s 부모 %s" % (p, name_of(skel), name_of(parent)))

# --- IK Rig / 리타게터가 무엇에 묶여 있나 ---
for p in IKS:
    a = unreal.EditorAssetLibrary.load_asset(p)
    if a is None:
        log("%-46s 없음" % p)
        continue
    mesh = a.get_preview_mesh() if hasattr(a, "get_preview_mesh") else None
    if mesh is None:
        try:
            mesh = a.get_editor_property("preview_skeletal_mesh")
        except Exception:  # noqa: BLE001
            mesh = None
    sk = mesh.get_editor_property("skeleton") if mesh else None
    report.setdefault("ik_rigs", {})[p] = {"mesh": name_of(mesh), "skeleton": name_of(sk)}
    log("%-46s 프리뷰메시 %-26s 스켈레톤 %s" % (p, name_of(mesh), name_of(sk)))

for p in RTGS:
    a = unreal.EditorAssetLibrary.load_asset(p)
    if a is None:
        log("%-46s 없음" % p)
        continue
    src = a.get_editor_property("source_ik_rig_asset")
    tgt = a.get_editor_property("target_ik_rig_asset")
    report.setdefault("retargeters", {})[p] = {"source": name_of(src), "target": name_of(tgt)}
    log("%-46s 소스 %-22s 타깃 %s" % (p, name_of(src), name_of(tgt)))

# --- 플레이어 BP 의 팔 컴포넌트 ---
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
TAG = "TEMP_WireProbe"
bp = unreal.EditorAssetLibrary.load_asset(PLAYER)
if bp is not None:
    for a in eas.get_all_level_actors():
        if a.get_actor_label() == TAG:
            eas.destroy_actor(a)
    actor = eas.spawn_actor_from_class(bp.generated_class(), unreal.Vector(0, 0, 0),
                                       unreal.Rotator(0, 0, 0))
    actor.set_actor_label(TAG)
    try:
        for c in actor.get_components_by_class(unreal.SkeletalMeshComponent):
            if "FirstPersonArms" not in c.get_name():
                continue
            mesh = c.get_skeletal_mesh_asset()
            anim = c.get_editor_property("anim_class")
            rel = c.get_relative_transform()
            report["first_person_arms"] = {
                "mesh": name_of(mesh),
                "skeleton": name_of(mesh.get_editor_property("skeleton")) if mesh else None,
                "anim_class": name_of(anim),
                "location": [round(v, 3) for v in (rel.translation.x, rel.translation.y,
                                                   rel.translation.z)],
                "rotation_rpy": [round(rel.rotation.rotator().roll, 3),
                                 round(rel.rotation.rotator().pitch, 3),
                                 round(rel.rotation.rotator().yaw, 3)],
                "scale": [round(v, 3) for v in (rel.scale3d.x, rel.scale3d.y, rel.scale3d.z)],
                "has_SOCKET_Weapon": bool(c.does_socket_exist("SOCKET_Weapon")),
            }
            log("FirstPersonArms — 메시 %s / 스켈레톤 %s / 애님클래스 %s"
                % (report["first_person_arms"]["mesh"],
                   report["first_person_arms"]["skeleton"],
                   report["first_person_arms"]["anim_class"]))
            log("   loc %s rot %s scale %s · SOCKET_Weapon %s"
                % (report["first_person_arms"]["location"],
                   report["first_person_arms"]["rotation_rpy"],
                   report["first_person_arms"]["scale"],
                   report["first_person_arms"]["has_SOCKET_Weapon"]))
    finally:
        eas.destroy_actor(actor)

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as f:
    json.dump(report, f, ensure_ascii=False, indent=1)
log("기록 -> %s" % OUT)
log("WIRE_PROBE_DONE")
