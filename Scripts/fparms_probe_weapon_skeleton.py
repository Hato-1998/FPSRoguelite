"""무기 리시버 스켈레톤이 **총 전체를 움직일 루트 본**과 부품 본을 갖고 있는지 잰다 — 읽기 전용.

1인칭을 "총만" 으로 가면 애니의 주체가 팔이 아니라 총이 된다. 그러면 두 가지가 성립해야 한다:
  ① 총 전체를 움직일 본(루트)이 있어야 하고,
  ② 재장전 연출에 쓸 부품 본(장전손잡이·노리쇠·탄창)이 있어야 한다.
Blender 로 들여오면 임포터가 루트 노드를 아마추어 오브젝트로 흡수해 버려서 못 가른다(실측).
그래서 UE 에서 직접 읽는다.

실행: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<이 파일>" -unattended -nopause
"""
import json
import os

import unreal

MESH = "/Game/PolygonMilitary/Meshes/Weapons/Modular/Weapon_A/SK_Wep_Mod_A_Body_01"
OUT = "E:/Git_Project/FPSRoguelite/Saved/NeonV/refpose/weapon_skeleton_probe.json"

report = {}


def log(m):
    unreal.log("[WEPSKEL] %s" % m)


mesh = unreal.EditorAssetLibrary.load_asset(MESH)
if mesh is None:
    unreal.log_error("[WEPSKEL] !! %s 를 못 연다" % MESH)
    raise SystemExit(1)

skel = mesh.get_editor_property("skeleton")
report["mesh"] = mesh.get_name()
report["skeleton"] = skel.get_name()

# 본 트리 — 파이썬에서 스켈레톤 본은 컴포넌트를 통해 읽는 게 확실하다
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
TAG = "TEMP_WepSkelProbe"
for a in eas.get_all_level_actors():
    if a.get_actor_label() == TAG:
        eas.destroy_actor(a)
actor = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0),
                                   unreal.Rotator(0, 0, 0))
actor.set_actor_label(TAG)
comp = actor.skeletal_mesh_component
comp.set_skeletal_mesh_asset(mesh)
try:
    bones = [str(b) for b in comp.get_all_socket_names()]  # 소켓 이름
    report["sockets"] = bones
    # SkinnedMeshComponent.h:1066/1087 — 본 목록은 개수 + 인덱스로 읽는다(get_bone_names 는 없다)
    names = [str(comp.get_bone_name(i)) for i in range(comp.get_num_bones())]
    report["bones"] = names
    log("본 %d개: %s" % (len(names), names))
    tree = {}
    for n in names:
        p = comp.get_parent_bone(n)
        tree[n] = str(p) if p and str(p) != "None" else None
    report["bone_parents"] = tree
    roots = [n for n, p in tree.items() if p is None]
    report["roots"] = roots
    log("루트 본: %s" % roots)
    log("소켓: %s" % report["sockets"])
    # 각 소켓이 어느 본에 달렸나 — 탄창을 떨어뜨리려면 어느 본을 움직여야 하는지가 여기서 나온다
    sk_mesh_sockets = {}
    for s in report["sockets"]:
        so = comp.get_socket_by_name(s)
        if so is not None:
            sk_mesh_sockets[s] = str(so.get_editor_property("bone_name"))
    report["socket_bone"] = sk_mesh_sockets
    log("소켓->본: %s" % sk_mesh_sockets)
finally:
    eas.destroy_actor(actor)

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as f:
    json.dump(report, f, ensure_ascii=False, indent=1)
log("기록 -> %s" % OUT)
log("WEPSKEL_PROBE_DONE")
