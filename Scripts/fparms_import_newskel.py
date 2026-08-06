"""1인칭 팔 — **새 스켈레톤을 만드는** FBX 임포트 + 게이트 1~4. 헤드리스 커맨드렛 전용.

ADR 0004 로 팔이 `S_Mannequin` 을 떠난다. 그래서 이번 임포트는 기존 스켈레톤에 붙이는 게
아니라 **새로 만든다** — 덮어쓸 대상이 없으니 "Skeleton Conflicts" 로 541개 에셋을 오염시킬
위험이 구조적으로 없다. 반대로 새로 생기는 만큼 처음으로 재야 할 것이 생겼다(게이트 3).

🚨 **에디터 안에서 Interchange 임포트를 부르면 게임스레드가 굳는다.** 반드시 커맨드렛에서.
🚨 `AssetTools.import_asset_tasks` 는 콘텐츠 브라우저 동기화에서 죽는다 —
   `InterchangeManager.import_asset` 를 직접 쓴다.
🚨 임포트만 하고 `save_directory` 를 안 하면 메모리에만 남고 프로세스 종료와 함께 사라진다.
🚨 **언등록 컴포넌트는 트랜스폼이 전부 0** 이라 rest pose 를 재려면 액터를 스폰해야 한다.

## 게이트

  1 단위      Approx Size. m->cm 우회가 새 스켈레톤에서도 맞는지. 기존 스켈레톤에 붙일 땐
              아마추어 스케일이 통째로 버려졌지만(그래서 100배 작게 들어왔다) 이번엔 안 버려진다
  2 뼈        65개 · 이름 · 계층이 S_Mannequin 과 완전 일치
  3 rest pose 위치는 Blender 스펙(JSON)과, **회전은 S_Mannequin 과 UE 안에서 직접** 대조.
              평행이동만 했으므로 로컬 회전은 한 톨도 안 변해야 한다. UE-UE 비교라 좌표계
              변환을 아예 안 거친다. **대조군(같은 메시 두 번)을 같이 잰다** — 계측이 유효한지
              먼저 보이지 않으면 0 이 나와도 못 믿는다
  4 메시      섹션·머티리얼 슬롯·정점 수

실행:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<이 파일>" -unattended -nopause
"""
import json
import math

import unreal

FBX = "E:/Git_Project/FPSRoguelite/Saved/NeonV/SK_NeonV_FPArms.fbx"
SPEC = "E:/Git_Project/FPSRoguelite/Saved/NeonV/fparms_own_spec.json"
DEST = "/Game/Character/FPArms"
MESH_ASSET = DEST + "/SK_NeonV_FPArms"
# 회전 대조용 = S_Mannequin 을 쓰는 기존 메시(폐기 예정이지만 아직 살아 있다)
REF_MESH = DEST + "/NeonV_FPArms"
REF_SKELETON = "/Game/ProceduralWeaponAnimationSystem/Demo/FPManny/S_Mannequin"

# 기대 크기는 상수로 박지 않고 **스펙 JSON 에서 읽는다**. 내보낼 때 재킷·장식을 빼므로
# 셋을 합친 바운드로 잡으면 기대치가 실제보다 커진다(한 번 그렇게 잡아 헛나갔다).
# 이 게이트가 잡는 건 100배/1만배 사고라 정확한 일치를 요구하지 않는다.
EXPORTED_MESH = "Body_FPArm"
SIZE_TOL = 0.15
MAX_POS_CM = 0.05
MAX_ROT_DEG = 0.05

fails = []


def log(m):
    unreal.log("[FPARMS] %s" % m)


def fail(m):
    fails.append(m)
    unreal.log_error("[FPARMS] !! %s" % m)


# ---------------------------------------------------------------- 임포트
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(
    [DEST, "/Game/ProceduralWeaponAnimationSystem", "/Interchange"], True)

if unreal.EditorAssetLibrary.does_asset_exist(MESH_ASSET):
    log("기존 %s 삭제 후 재임포트" % MESH_ASSET)
    # 🪤 스켈레톤은 **일부러 안 지운다** — 지우면 이 스켈레톤을 쓰는 AnimBP·소켓·애니가 전부 끊긴다.
    #    대신 UE 는 기존 스켈레톤의 ref pose 를 유지한 채 메시만 갈아끼우므로, Blender 에서
    #    뼈 위치를 바꿔 다시 임포트하면 **스켈레톤이 옛 자리에 남는다**. 게이트 3 이 그걸 잡는다.
    #    뼈를 정말 옮겼다면 스켈레톤까지 지우고 배선을 다시 잡아야 한다.
    unreal.EditorAssetLibrary.delete_asset(MESH_ASSET)

mgr = unreal.InterchangeManager.get_interchange_manager_scripted()
params = unreal.ImportAssetParameters()
params.set_editor_property("is_automated", True)
# 파이프라인을 덮지 않는다 — 스켈레톤을 물리지 **않는** 게 이번 목적이라 기본 스택이 정답이다.
log("import_asset -> %s" % (mgr.import_asset(DEST, mgr.create_source_data(FBX), params),))
# 🪤 only_if_is_dirty=False 로 저장하면 같은 폴더의 **안 건드린 텍스처·머티리얼까지 통째로
#    재직렬화**돼 git diff 에 남는다(실제로 7개가 딸려 나왔다). 더티한 것만 저장한다.
log("save_directory -> %s"
    % unreal.EditorAssetLibrary.save_directory(DEST, only_if_is_dirty=True, recursive=True))

registry.scan_paths_synchronous([DEST], True)
mesh = None
for a in registry.get_assets_by_path(DEST, recursive=True):
    obj = a.get_asset()
    log("   %-52s %s" % (str(a.package_name), type(obj).__name__))
    if isinstance(obj, unreal.SkeletalMesh) and "SK_NeonV" in str(a.package_name):
        mesh = obj
if mesh is None:
    fail("스켈레탈 메시가 생기지 않았다 — 위 목록 확인")
    raise SystemExit(1)
skel = mesh.skeleton
log("메시 %s / 스켈레톤 %s" % (mesh.get_path_name(), skel.get_path_name()))
if skel.get_path_name().startswith(REF_SKELETON):
    fail("S_Mannequin 에 붙었다 — 새 스켈레톤이 안 생겼다")

# ---------------------------------------------------------------- 게이트 1 : 단위
log("=== 게이트 1 — 단위 (Approx Size) ===")
spec = json.load(open(SPEC, encoding="utf-8"))
expect = spec["meshes"][EXPORTED_MESH]["size_ue_cm"]
ext = mesh.get_bounds().box_extent
size = (ext.x * 2, ext.y * 2, ext.z * 2)
log("   Approx Size (cm) %.1f x %.1f x %.1f   (Blender %s %.1f x %.1f x %.1f)"
    % (size + (EXPORTED_MESH,) + tuple(expect)))
for i, axis in enumerate("XYZ"):
    if not (expect[i] * (1 - SIZE_TOL) < size[i] < expect[i] * (1 + SIZE_TOL)):
        fail("단위 이상 — %s %.1f 이 기대 %.1f 에서 크게 벗어난다 (m->cm 우회 확인)"
             % (axis, size[i], expect[i]))

# ---------------------------------------------------------------- rest pose 읽기
# 스켈레톤 에셋에서 직접 읽는 안정적인 파이썬 길이 없다. 액터를 스폰해서 컴포넌트로 읽는다.
actors = []


def spawn(mesh_asset, tag):
    act = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    if act is None:
        fail("액터 스폰 실패 (%s) — 커맨드렛에 편집 월드가 없다" % tag)
        return None
    actors.append(act)
    comp = act.skeletal_mesh_component
    comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    comp.set_skeletal_mesh_asset(mesh_asset)
    return comp


def read_bones(comp):
    """{이름: (부모, 컴포넌트공간 트랜스폼, 부모본공간 트랜스폼)}

    로컬 회전은 쿼터니언을 손으로 뒤집지 말고 **RTS_PARENT_BONE_SPACE 로 직접 받는다**
    (`unreal.Quat` 에는 `inverse()` 가 없다). 엔진이 계산한 값이라 더 정확하기도 하다.
    """
    out = {}
    for i in range(comp.get_num_bones()):
        n = str(comp.get_bone_name(i))
        p = str(comp.get_parent_bone(n))
        out[n] = (None if p in ("None", "") else p,
                  comp.get_socket_transform(n, unreal.RelativeTransformSpace.RTS_COMPONENT),
                  comp.get_socket_transform(n, unreal.RelativeTransformSpace.RTS_PARENT_BONE_SPACE))
    return out


ours_c = spawn(mesh, "ours")
ctrl_c = spawn(mesh, "control")                                    # 대조군 = 같은 메시 두 번
ref_obj = unreal.EditorAssetLibrary.load_asset(REF_MESH)
ref_c = spawn(ref_obj, "S_Mannequin ref") if ref_obj else None
if ours_c is None:
    raise SystemExit(1)
ours, ctrl = read_bones(ours_c), read_bones(ctrl_c) if ctrl_c else {}
theirs = read_bones(ref_c) if ref_c else {}

# ---------------------------------------------------------------- 게이트 2 : 뼈
log("=== 게이트 2 — 뼈 이름·계층 vs S_Mannequin ===")
log("   본 %d개 (S_Mannequin 메시 %d개)" % (len(ours), len(theirs)))
if len(ours) != 65:
    fail("본이 %d개다 (65 이어야 한다)" % len(ours))
if theirs:
    missing = sorted(set(theirs) - set(ours))
    extra = sorted(set(ours) - set(theirs))
    if missing:
        fail("빠진 본 %d개: %s" % (len(missing), ", ".join(missing[:10])))
    if extra:
        fail("여분 본 %d개: %s" % (len(extra), ", ".join(extra[:10])))
    bad = [n for n in sorted(set(ours) & set(theirs)) if ours[n][0] != theirs[n][0]]
    if bad:
        fail("부모가 다른 본 %d개: %s" % (len(bad), ", ".join(bad[:10])))
    if not (missing or extra or bad):
        log("   이름·계층 완전 일치")

# ---------------------------------------------------------------- 게이트 3 : rest pose
log("=== 게이트 3 — rest pose (위치=Blender 스펙 / 회전=S_Mannequin) ===")
want = {b["name"]: b["loc_ue_cm"] for b in spec["bones"]}


def local_rot(table, n):
    """부모 기준 로컬 회전 — 엔진이 준 값을 그대로 쓴다."""
    return table[n][2].rotation


def quat_deg(a, b):
    d = abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w)
    return math.degrees(2.0 * math.acos(max(-1.0, min(1.0, d))))


def report(tag, names, pos_fn, rot_fn):
    wp = wr = 0.0
    bp = br = ""
    for n in names:
        p, r = pos_fn(n), rot_fn(n)
        if p > wp:
            wp, bp = p, n
        if r > wr:
            wr, br = r, n
    log("   %-26s 위치 최대 %.5f cm (%s) · 회전 최대 %.5f 도 (%s)" % (tag, wp, bp, wr, br))
    return wp, wr


shared_spec = sorted(set(ours) & set(want))
report("[대조군] 같은 메시 두 번", sorted(set(ours) & set(ctrl)),
       lambda n: (ours[n][1].translation - ctrl[n][1].translation).length(),
       lambda n: quat_deg(local_rot(ours, n), local_rot(ctrl, n)))

wp, _ = report("vs Blender 스펙(위치만)", shared_spec,
               lambda n: (ours[n][1].translation
                          - unreal.Vector(*want[n])).length(),
               lambda n: 0.0)
if wp > MAX_POS_CM:
    fail("rest 위치가 Blender 스펙과 %.4fcm 어긋난다" % wp)

if theirs:
    shared = sorted(set(ours) & set(theirs))
    _, wr = report("vs S_Mannequin(회전만)", shared,
                   lambda n: 0.0,
                   lambda n: quat_deg(local_rot(ours, n), local_rot(theirs, n)))
    if wr > MAX_ROT_DEG:
        fail("로컬 회전이 S_Mannequin 과 %.4f도 어긋난다 — FBX 임포터가 축을 다시 넣었다" % wr)
    # 위치는 우리가 일부러 옮겼다. 얼마나 옮겼는지 기록해 둔다(A′ 의 실제 반영분).
    moved = sorted(((ours[n][1].translation - theirs[n][1].translation).length(), n)
                   for n in shared)
    log("   (참고) S_Mannequin 대비 옮겨진 본 상위: "
        + " · ".join("%s %.2fcm" % (n, d) for d, n in moved[-6:][::-1]))
    log("   (참고) 안 움직인 본 %d개" % sum(1 for d, _ in moved if d < 0.01))

for a in actors:
    unreal.EditorLevelLibrary.destroy_actor(a)

# ---------------------------------------------------------------- 게이트 4 : 메시
log("=== 게이트 4 — 섹션·머티리얼·정점 ===")
sub = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
lods = sub.get_lod_count(mesh)
log("   LOD %d / 정점 %s / 섹션 %s"
    % (lods, [sub.get_num_verts(mesh, i) for i in range(lods)],
       [sub.get_num_sections(mesh, i) for i in range(lods)]))
blender_verts = spec["meshes"][EXPORTED_MESH]["verts"]
ue_verts = sub.get_num_verts(mesh, 0)
log("   Blender 정점 %d vs UE %d (UE 는 노멀·UV 분리로 보통 더 많다)" % (blender_verts, ue_verts))
if ue_verts < blender_verts:
    log("   (참고) UE 가 %d개 적다 — 어느 면에도 안 붙은 뜬 정점은 임포트에서 빠진다"
        % (blender_verts - ue_verts))
for m in mesh.materials:
    log("   머티리얼 %-18s %s"
        % (str(m.material_slot_name),
           m.material_interface.get_path_name() if m.material_interface else None))
if sub.get_num_sections(mesh, 0) != 1:
    fail("섹션이 %d개다 — 몸만 내보냈으므로 1 이어야 한다" % sub.get_num_sections(mesh, 0))

log("=== 결과 ===")
if fails:
    for f in fails:
        log("!! %s" % f)
    raise SystemExit(1)
log("게이트 1~4 통과")
log("IMPORT_NEWSKEL_DONE")
