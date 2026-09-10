# verify_voxel_char_fbx.py — 내보낸 FBX 를 빈 씬에 되임포트해 단위 함정 재발 여부를 잰다.
#
# **왕복 확인 지표 = 아마추어 노드 스케일**(Docs/Troubleshooting.md F1-b). 고치기 전 1.0 → 고친 뒤 0.01.
# 메시 크기는 양쪽 다 같아 보이므로 **크기로는 못 가른다** — 반드시 노드 스케일과 본 로컬 좌표 자릿수로 가른다.
#
# 대조군을 같이 재는 것이 요점이다(메모리 verify-with-control-group): 옛 FBX 와 새 FBX 를
# 같은 절차로 재서 **본 이름·계층·roll 은 같고 스케일만 ×100 다르다**를 보여야 통과다.
# 하나만 재면 "다르다"와 "계측이 고장났다"를 구분할 수 없다.
#
# 사용: blender -b --factory-startup -P verify_voxel_char_fbx.py -- <fbx> [<fbx> ...]

import bpy
import json
import os
import sys

argv = sys.argv[sys.argv.index("--") + 1:]


def log(m):
    print("@@ %s" % m)


def wipe():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def probe(path):
    wipe()
    bpy.ops.import_scene.fbx(filepath=path)
    bpy.context.view_layer.update()

    arms = [o for o in bpy.data.objects if o.type == "ARMATURE"]
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    if len(arms) != 1:
        return {"error": "아마추어 %d개" % len(arms)}
    arm = arms[0]

    bones = {}
    for b in arm.data.bones:
        bones[b.name] = {
            # 미터 리그의 좌표는 0.006 같은 작은 값이라 반올림을 8자리보다 줄이면
            # 비율 대조에서 반올림 오차가 결함처럼 보인다(실측: 6자리 → 0.03% 흔들림).
            "head": [round(v, 8) for v in b.head_local],
            "tail": [round(v, 8) for v in b.tail_local],
            "len": round(b.head_local.length, 8),
            "parent": b.parent.name if b.parent else None,
        }

    verts = sum(len(o.data.vertices) for o in meshes)
    zs = []
    for o in meshes:
        for c in o.bound_box:
            zs.append((o.matrix_world @ __import__("mathutils").Vector(c)).z)

    # 크기 차이의 정체를 가리려면 어떤 정점 데이터가 실렸는지 같이 재야 한다.
    mesh_data = {}
    for o in meshes:
        me = o.data
        mesh_data[o.name] = {
            "verts": len(me.vertices),
            "polys": len(me.polygons),
            "loops": len(me.loops),
            "uv_layers": [u.name for u in me.uv_layers],
            "color_attrs": [c.name for c in getattr(me, "color_attributes", [])],
            "materials": [ms.material.name if ms.material else None for ms in o.material_slots],
            "shade_smooth": sum(1 for p in me.polygons if p.use_smooth),
            "vgroups": len(o.vertex_groups),
        }

    return {
        "file": os.path.basename(path),
        "arm_name": arm.name,
        "arm_scale": [round(v, 8) for v in arm.scale],
        "n_bones": len(arm.data.bones),
        "n_meshes": len(meshes),
        "n_verts": verts,
        "world_z": [round(min(zs), 5), round(max(zs), 5)] if zs else None,
        "bones": bones,
        "meshes": mesh_data,
    }


results = [probe(p) for p in argv]

for r in results:
    if "error" in r:
        log("%s — ERROR %s" % (r.get("file"), r["error"]))
        continue
    log("%-34s 아마추어노드 scale=%s 본=%d 메시=%d 정점=%d 월드z=%s"
        % (r["file"], r["arm_scale"], r["n_bones"], r["n_meshes"], r["n_verts"], r["world_z"]))
    for bn in ("pelvis", "hand_r", "head", "foot_r"):
        if bn in r["bones"]:
            log("     %-8s head_local=%s" % (bn, r["bones"][bn]["head"]))
    for mn, md in sorted(r["meshes"].items()):
        log("     메시 %-12s v=%d p=%d loop=%d uv=%s color=%s smooth=%d vg=%d mat=%s"
            % (mn, md["verts"], md["polys"], md["loops"], md["uv_layers"],
               md["color_attrs"], md["shade_smooth"], md["vgroups"], md["materials"]))

# 두 개를 주면 대조한다 — 첫째=기준(옛), 둘째=새것.
if len(results) == 2 and all("error" not in r for r in results):
    old, new = results
    log("── 대조 (기준=%s, 새것=%s) ──" % (old["file"], new["file"]))
    ok = True

    if old["n_bones"] != new["n_bones"]:
        log("!! 본 개수가 다르다 %d → %d — 스켈레톤이 바뀐다. ABP·리타깃 애니가 전부 깨진다"
            % (old["n_bones"], new["n_bones"]))
        ok = False
    if set(old["bones"]) != set(new["bones"]):
        only_o = sorted(set(old["bones"]) - set(new["bones"]))
        only_n = sorted(set(new["bones"]) - set(old["bones"]))
        log("!! 본 이름이 다르다 — 옛것만: %s / 새것만: %s" % (only_o[:8], only_n[:8]))
        ok = False
    if old["n_verts"] != new["n_verts"]:
        log("!! 정점 수가 다르다 %d → %d" % (old["n_verts"], new["n_verts"]))
        ok = False

    # 계층 + 본 로컬 좌표 비율. 스케일만 달라야 하고 비율은 전 본이 같아야 한다.
    ratios = []
    for bn in sorted(set(old["bones"]) & set(new["bones"])):
        bo, bn_ = old["bones"][bn], new["bones"][bn]
        if bo["parent"] != bn_["parent"]:
            log("!! '%s' 의 부모가 다르다: %s → %s" % (bn, bo["parent"], bn_["parent"]))
            ok = False
        # 좌표가 0 에 가까운 축은 비율이 의미가 없다(0/0). 본 원점까지의 거리로 잰다.
        if bo["len"] > 0.02:
            ratios.append(bn_["len"] / bo["len"])
    if ratios:
        lo, hi = min(ratios), max(ratios)
        log("본 로컬 거리 비율(새/옛): %.6f ~ %.6f (표본 %d)" % (lo, hi, len(ratios)))
        if (hi - lo) / hi > 1e-4:
            log("!! 비율이 본마다 다르다 — 균일 스케일이 아니다(리그가 변형됐다)")
            ok = False

    log("판정: %s" % ("PASS — 스켈레톤 동일 · 스케일만 달라졌다" if ok else "FAIL — 위 항목을 보라"))

print("@@ VERIFY_JSON=" + json.dumps([{k: v for k, v in r.items() if k != "bones"} for r in results]))
log("VERIFY_DONE")
