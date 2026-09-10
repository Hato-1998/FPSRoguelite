# export_voxel_char_fbx.py — VoxelChar 리그를 UE 로 보낼 FBX 9개(머지 1 + 파츠 8)로 내보낸다.
#
# ⚠️ 이 스크립트가 존재하는 이유 = **미터/센티 단위 함정**(Docs/Troubleshooting.md F1-b).
# Blender 는 미터, UE 는 센티미터인데 UE 는 그 ×100 변환을 지오메트리가 아니라 **아마추어 스케일**로 얹는다.
# 새 스켈레톤을 만드는 임포트에서는 본이 scale 100 으로 들어와 rest pose 는 멀쩡해 보이다가
# **scale-1 애니(매너퀸 클립)가 재생되는 순간 캐릭터가 1/100 로 붕괴**한다. 이 프로젝트에서 세 번째다
# (Blu 리그 · 1인칭 팔 · VoxelChar). 그냥 내보내면 반드시 재발한다 — 손으로 내보내지 말 것.
#
# 검증된 우회법(블랜더 repo 커밋 e8f329a · NeonV_scripts/fp_arms_export_fbx.py 와 동일):
#   지오메트리(본 head/tail + 정점)에 ×100 을 굽고, **최상위 아마추어 오브젝트 스케일만 0.01**로 둔다.
#   Blender 안에서는 둘이 상쇄돼 보이는 게 그대로고, 임포트 때 UE 의 ×100 이 0.01 을 상쇄해
#   「아마추어 스케일 1 + 이미 cm 인 지오메트리」가 된다.
#   `apply_scale_options="FBX_SCALE_NONE"` 이어야 0.01 이 FBX 노드에 살아남는다.
#   `FBX_SCALE_ALL` 도, 0.01 없이 ×100 만 굽는 것도 실패한다(전자는 안 먹고 후자는 이중 스케일).
#
# **왕복 확인 지표 = FBX 아마추어 노드 스케일**(고치기 전 1.0 → 고친 뒤 0.01).
# 메시 크기는 양쪽 다 같아 보이므로 **크기로는 못 가른다.** 검증 = Scripts/verify_voxel_char_fbx.py
#
# 굽기는 **메모리에서만** 한다 — .blend 는 미터 그대로 남긴다(Blender 네이티브 단위 유지 +
# 재실행이 멱등. 저장해 버리면 두 번째 실행이 ×10000 을 굽는다).
#
# 이 리그의 전제(fp_arms 와 다른 점):
#   * 아마추어 오브젝트가 이미 `root` 로 개명돼 있고 `root` **본은 없다** → UE 가 아마추어 노드를
#     `root` 본으로 읽어 88 + 1 = **89본**이 된다. fp_arms 스크립트의 개명·root본삭제 단계는 여기서 불필요하다.
#   * 메시 8개가 전부 아마추어의 자식이다 → 0.01 은 **아마추어에만**. 메시에 또 주면 0.0001 로 이중 적용된다.
#
# 사용: blender -b <입력.blend> -P export_voxel_char_fbx.py -- <출력디렉터리>

import bpy
import os
import sys

argv = sys.argv[sys.argv.index("--") + 1:]
OUT_DIR = argv[0]
os.makedirs(OUT_DIR, exist_ok=True)

CM = 100.0
MERGED_NAME = "SKM_VoxelChar"
PART_PREFIX = "VX_"          # 메시 이름 접두사 → 파츠 FBX 이름은 SKM_VoxelChar_<나머지>


def log(m):
    print("@@ %s" % m)


def fail(m):
    log("!! %s" % m)
    raise SystemExit(1)


# ── 전제 확인 ────────────────────────────────────────────────────────────────
arms = [o for o in bpy.data.objects if o.type == "ARMATURE"]
meshes = [o for o in bpy.data.objects if o.type == "MESH"]
extra = [o for o in bpy.data.objects if o.type not in ("ARMATURE", "MESH")]

if len(arms) != 1:
    fail("아마추어가 %d개다 — 1개여야 한다" % len(arms))
arm = arms[0]
if not meshes:
    fail("메시가 없다")
if extra:
    # 카메라·라이트가 남아 있으면 object_types 필터로 빠지지만, 씬이 예상과 다르다는 신호다.
    log("!! 아마추어·메시가 아닌 오브젝트 %d개: %s"
        % (len(extra), ", ".join("%s(%s)" % (o.name, o.type) for o in extra)))
if arm.data.bones.get("root"):
    fail("`root` 본이 있다 — 이 스크립트는 「아마추어 노드가 곧 root」인 리그를 전제한다. "
         "NeonV_scripts/fp_arms_export_fbx.py 의 개명·root본삭제 단계를 먼저 보라")
if arm.name != "root":
    fail("아마추어 오브젝트 이름이 '%s' 다 — UE 는 이 노드를 본 이름으로 읽으므로 'root' 여야 한다" % arm.name)
if tuple(round(v, 6) for v in arm.scale) != (1.0, 1.0, 1.0):
    fail("아마추어 스케일이 %s 다 — 미터 원본(1,1,1)에서 시작해야 한다. 이미 구운 .blend 를 다시 굽고 있지 않은지 확인하라"
         % tuple(arm.scale))

log("아마추어 '%s' 본 %d개 (UE 에서 %d본) / 메시 %d개"
    % (arm.name, len(arm.data.bones), len(arm.data.bones) + 1, len(meshes)))

strays = [o.name for o in meshes if o.parent is not arm]
if strays:
    log("!! 아마추어의 자식이 아닌 메시: %s — 이 메시들은 0.01 을 자체 적용해야 한다" % ", ".join(strays))

# 포즈가 rest 가 아니면 익스포트 결과가 rest pose 와 어긋난다.
def is_posed(pb):
    m = pb.matrix_basis
    for r in range(4):
        for c in range(4):
            if abs(m[r][c] - (1.0 if r == c else 0.0)) > 1e-5:
                return True
    return False


posed = [pb.name for pb in arm.pose.bones if is_posed(pb)]
if posed:
    log("!! 포즈가 rest 가 아닌 본 %d개: %s ..." % (len(posed), posed[:8]))

# ── 굽기 전 기준값(월드) ──────────────────────────────────────────────────────
bpy.context.view_layer.update()
before_verts = [(o, [o.matrix_world @ v.co for v in o.data.vertices]) for o in meshes]
before_bones = {b.name: (arm.matrix_world @ b.head_local).copy() for b in arm.data.bones}

# ── ① 지오메트리에 ×100 굽기 ──────────────────────────────────────────────────
bpy.ops.object.select_all(action="DESELECT")
bpy.context.view_layer.objects.active = arm
arm.select_set(True)
bpy.ops.object.mode_set(mode="EDIT")
ebs = arm.data.edit_bones

# 연결된(use_connect) 본의 head 는 부모 tail 의 별칭이라, 순서에 따라 값이 덮인다.
# 스냅샷 → 연결 해제 → 일괄 대입 → 연결 원복 으로 순서 의존을 없앤다.
# (부모 tail 과 자식 head 가 같은 배율로 커지므로 원복해도 여전히 맞물린다.)
snap = {eb.name: (eb.head.copy(), eb.tail.copy(), eb.roll, eb.use_connect) for eb in ebs}
n_conn = sum(1 for v in snap.values() if v[3])
for eb in ebs:
    eb.use_connect = False
for eb in ebs:
    head, tail, roll, _ = snap[eb.name]
    eb.head = head * CM
    eb.tail = tail * CM
    eb.roll = roll          # roll 은 head/tail 대입으로 안 바뀌지만 명시적으로 되돌려 못 박는다
for eb in ebs:
    eb.use_connect = snap[eb.name][3]
bpy.ops.object.mode_set(mode="OBJECT")
log("본 head/tail ×%g 적용 (연결된 본 %d개는 해제→원복)" % (CM, n_conn))

for o in meshes:
    for v in o.data.vertices:
        v.co = v.co * CM
log("정점 ×%g 적용 (%d 메시 / %d 정점)" % (CM, len(meshes), sum(len(o.data.vertices) for o in meshes)))

# ── ② 최상위 아마추어에만 0.01 ───────────────────────────────────────────────
arm.scale = (1.0 / CM, 1.0 / CM, 1.0 / CM)
for o in meshes:
    if o.parent is not arm:
        o.scale = (1.0 / CM, 1.0 / CM, 1.0 / CM)
        log("  %s 는 아마추어의 자식이 아니라 자체 스케일 적용" % o.name)
bpy.context.view_layer.update()

# ── ③ 상쇄 자체검사 — 틀리면 100배/1만배로 나간다 ──────────────────────────────
worst_v = 0.0
for o, pts in before_verts:
    now = [o.matrix_world @ v.co for v in o.data.vertices]
    worst_v = max(worst_v, max((a - b).length for a, b in zip(pts, now)) * CM)
worst_b = max((((arm.matrix_world @ b.head_local) - before_bones[b.name]).length * CM)
              for b in arm.data.bones)
log("상쇄 검사 — 월드 변화: 정점 %.5fcm / 본 %.5fcm" % (worst_v, worst_b))
if worst_v > 0.01 or worst_b > 0.01:
    fail("상쇄가 안 맞는다 — 이중 스케일이다. 중단")

# ── ④ 내보내기 ───────────────────────────────────────────────────────────────
def export(path, sel_meshes):
    bpy.ops.object.select_all(action="DESELECT")
    arm.select_set(True)
    for o in sel_meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True,
        apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE", global_scale=1.0,
        add_leaf_bones=False, object_types={"ARMATURE", "MESH"},
        bake_anim=False, mesh_smooth_type="FACE", use_tspace=True)
    # 본 축 옵션(primary/secondary_bone_axis)은 건드리지 않는다 — 기본값이 정답이다(fp_arms 실측).
    #
    # use_tspace=True 는 **원본 익스포트에 맞춘 것**이다. 이 재임포트의 목적은 스케일 하나만 바꾸는 것이라,
    # 다른 축이 같이 움직이면 "무엇이 달라져서 그런지"를 나중에 가릴 수 없다. 기본값(False)으로 내보내면
    # Tangents·Binormals 레이어가 빠져 FBX 가 절반 크기가 되고(실측: Head 1.28MB → 0.64MB),
    # UE 의 Normal Import Method 가 ImportNormalsAndTangents 일 때 탄젠트가 조용히 재계산으로 바뀐다.
    log("  내보냄: %s (%.2f MB)" % (os.path.basename(path), os.path.getsize(path) / 1024 / 1024))


log("머지 메시 + 파츠 %d개 내보내는 중 → %s" % (len(meshes), OUT_DIR))
export(os.path.join(OUT_DIR, MERGED_NAME + ".fbx"), meshes)
for o in sorted(meshes, key=lambda m: m.name):
    if not o.name.startswith(PART_PREFIX):
        fail("메시 '%s' 에 접두사 '%s' 가 없다 — 파츠 FBX 이름을 정할 수 없다" % (o.name, PART_PREFIX))
    export(os.path.join(OUT_DIR, "%s_%s.fbx" % (MERGED_NAME, o.name[len(PART_PREFIX):])), [o])

log("EXPORT_FBX_DONE")
