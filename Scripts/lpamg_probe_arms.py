"""LPAMG 팔이 PWAS(S_Mannequin) 애니를 받을 수 있는지 잰다 — 읽기 전용.

사용자 말: "팔은 메카님 기준이니 PWAS 에 적용해봐라". 그런데 팔은 **자체 스켈레톤**
`SKEL_LPAMG_Character` 를 쓴다. PWAS 애니는 전부 `S_Mannequin` 이다. 둘이 붙으려면
**본 이름과 계층이 같아야** 한다 — 아니면 리타게팅이 필요하고 그건 전혀 다른 작업량이다.
눈으로 못 가르는 것이니 여기서 잰다.

같이 뽑는 것:
  · 두 스켈레톤의 본 이름/계층 · 공통·차집합
  · 팔 메시들의 정점 수·머티리얼·실제로 웨이트가 걸린 본
  · 팔이 쓰는 텍스처/머티리얼 (삭제 범위를 정할 때 남길 목록)

실행: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<이 파일>" -unattended -nopause
"""
import json
import os

import unreal

ARMS = [
    "/Game/LowPolyAnimatedModernGuns/Art/Characters/Meshes/SK_LPAMG_Arms_Base",
    "/Game/LowPolyAnimatedModernGuns/Art/Characters/Meshes/SK_LPAMG_Arms_Base_Smooth",
    "/Game/LowPolyAnimatedModernGuns/Art/Characters/Meshes/SK_LPAMG_Arms_Gloves",
    "/Game/LowPolyAnimatedModernGuns/Art/Characters/Meshes/SK_LPAMG_Arms_Gloves_Smooth",
]
PWAS_MESH = "/Game/ProceduralWeaponAnimationSystem/Demo/FPManny/SK_FP_Manny_Simple"
OUT = "E:/Git_Project/FPSRoguelite/Saved/NeonV/lpamg_arms_probe.json"

report = {}
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
TAG = "TEMP_LPAMGProbe"


def log(m):
    unreal.log("[LPAMG] %s" % m)


def bone_tree(mesh):
    """스폰해서 본 목록·부모를 읽는다(스켈레톤 에셋에서 직접 읽는 API 가 파이썬에 없다)."""
    for a in eas.get_all_level_actors():
        if a.get_actor_label() == TAG:
            eas.destroy_actor(a)
    actor = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0),
                                       unreal.Rotator(0, 0, 0))
    actor.set_actor_label(TAG)
    comp = actor.skeletal_mesh_component
    comp.set_skeletal_mesh_asset(mesh)
    try:
        names = [str(comp.get_bone_name(i)) for i in range(comp.get_num_bones())]
        parents = {}
        for n in names:
            p = comp.get_parent_bone(n)
            parents[n] = str(p) if p and str(p) != "None" else None
        sockets = [str(s) for s in comp.get_all_socket_names()]
        return names, parents, sockets
    finally:
        eas.destroy_actor(actor)


def mesh_info(path):
    m = unreal.EditorAssetLibrary.load_asset(path)
    if m is None:
        return None
    sk = m.get_editor_property("skeleton")
    mats = []
    for mi in m.get_editor_property("materials"):
        mat = mi.get_editor_property("material_interface")
        mats.append(mat.get_path_name() if mat else None)
    info = {"path": path, "skeleton": sk.get_path_name() if sk else None,
            "materials": mats,
            "physics_asset": (m.get_editor_property("physics_asset").get_path_name()
                              if m.get_editor_property("physics_asset") else None)}
    names, parents, sockets = bone_tree(m)
    info["bone_count"] = len(names)
    info["bones"] = names
    info["parents"] = parents
    info["sockets"] = sockets
    return info


report["arms"] = {}
for p in ARMS:
    i = mesh_info(p)
    if i is None:
        log("!! %s 없음" % p)
        continue
    report["arms"][p.rsplit("/", 1)[-1]] = i
    log("%-28s 본 %d · 스켈레톤 %s · 머티 %d · 소켓 %d"
        % (p.rsplit("/", 1)[-1], i["bone_count"],
           (i["skeleton"] or "?").rsplit(".", 1)[-1], len(i["materials"]), len(i["sockets"])))

pw = mesh_info(PWAS_MESH)
if pw:
    report["pwas"] = pw
    log("PWAS %-23s 본 %d · 스켈레톤 %s"
        % ("SK_FP_Manny_Simple", pw["bone_count"], (pw["skeleton"] or "?").rsplit(".", 1)[-1]))

# --- 핵심: 본 이름이 겹치는가 ---
first = next(iter(report["arms"].values()), None)
if first and pw:
    a, b = set(first["bones"]), set(pw["bones"])
    common = sorted(a & b)
    report["compare"] = {
        "lpamg_only": sorted(a - b), "mannequin_only": sorted(b - a),
        "common": common, "common_count": len(common),
        "lpamg_count": len(a), "mannequin_count": len(b),
    }
    log("본 비교 — 공통 %d / LPAMG 전용 %d / 마네킹 전용 %d"
        % (len(common), len(a - b), len(b - a)))
    log("  LPAMG 전용(앞 20): %s" % sorted(a - b)[:20])
    log("  마네킹 전용(앞 20): %s" % sorted(b - a)[:20])
    # 공통 본의 부모가 같은가 — 이름만 같고 계층이 다르면 애니가 안 맞는다
    mismatched = [n for n in common if first["parents"].get(n) != pw["parents"].get(n)]
    report["compare"]["parent_mismatch"] = mismatched
    log("  공통 본 중 부모가 다른 것 %d개: %s" % (len(mismatched), mismatched[:10]))

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as f:
    json.dump(report, f, ensure_ascii=False, indent=1)
log("기록 -> %s" % OUT)
log("LPAMG_PROBE_DONE")
