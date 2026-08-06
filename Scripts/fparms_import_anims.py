"""아이들·ADS 포즈를 자체 스켈레톤 AnimSequence 로 임포트 + 검증. 커맨드렛 전용.

Blender 에서 견본(PWAS)의 회전만 우리 리그에 얹어 내보낸 FBX 를 들여온다. 우리 것이 되는
순간부터 PWAS 폴더에 대한 의존이 끊긴다(ADR 0004).

🚨 애니 임포트는 **대상 스켈레톤을 파이프라인에 물려야** 한다. 안 물리면 전용 스켈레톤이
   새로 생긴다(실제로 당한 적 있다). `AssetImportTask.options` 는 레거시 FBX 경로라
   Interchange 가 무시한다 — `ImportAssetParameters.override_pipelines` 를 써야 한다.

## 검증

임포트된 애니의 **로컬 본 회전**을 원본 PWAS 애니와 직접 비교한다. 우리 스켈레톤은 rest
회전이 S_Mannequin 과 같으므로(실측 0.00089도) 로컬 회전이 **그대로 같아야** 한다.
여기가 맞으면 Blender 왕복(전이 -> FBX -> 임포트)이 포즈를 안 망가뜨렸다는 뜻이다.

실행:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<이 파일>" -unattended -nopause
"""
import math

import unreal

SKELETON = "/Game/Character/FPArms/SK_NeonV_FPArms_Skeleton"
DEST = "/Game/Character/FPArms/Anims"
SRC_DIR = "E:/Git_Project/FPSRoguelite/Saved/NeonV/refpose"
PAIRS = {   # 우리 액션 -> 대조할 PWAS 원본
    "FP_Rifle_Idle":
        "/Game/ProceduralWeaponAnimationSystem/Animations/Poses/Rifle/A_FP_Rifle_Pose",
    "FP_Rifle_ADS":
        "/Game/ProceduralWeaponAnimationSystem/Animations/Poses/Rifle/A_FP_Rifle_AimPose",
}
BASE_PIPELINE = "/Interchange/Pipelines/DefaultAssetsPipeline"
PIPE_COPY = "/Game/Character/FPArms/_ImportPipeline_FPArmsAnim"
MAX_ROT_DEG = 0.25          # FBX 왕복 + 16비트 압축을 감안한 한계

fails = []


def log(m):
    unreal.log("[FPANIM] %s" % m)


def fail(m):
    fails.append(m)
    unreal.log_error("[FPANIM] !! %s" % m)


registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Character", "/Game/ProceduralWeaponAnimationSystem",
                                 "/Interchange"], True)
skel = unreal.EditorAssetLibrary.load_asset(SKELETON)
if skel is None:
    fail("대상 스켈레톤 %s 가 없다" % SKELETON)
    raise SystemExit(1)

# --- 파이프라인에 스켈레톤을 물린다 ---
if unreal.EditorAssetLibrary.does_asset_exist(PIPE_COPY):
    unreal.EditorAssetLibrary.delete_asset(PIPE_COPY)
if not unreal.EditorAssetLibrary.duplicate_asset(BASE_PIPELINE, PIPE_COPY):
    fail("파이프라인 복제 실패 (%s)" % BASE_PIPELINE)
    raise SystemExit(1)
pipeline = unreal.EditorAssetLibrary.load_asset(PIPE_COPY)
common = pipeline.get_editor_property("common_skeletal_meshes_and_animations_properties")
common.set_editor_property("skeleton", skel)
common.set_editor_property("import_only_animations", True)
try:
    pipeline.get_editor_property("mesh_pipeline").set_editor_property("import_static_meshes", False)
except Exception:  # noqa: BLE001 — 버전마다 이름이 다르다. 애니만 켜면 충분하다
    pass
unreal.EditorAssetLibrary.save_asset(PIPE_COPY, only_if_is_dirty=False)
got = pipeline.get_editor_property(
    "common_skeletal_meshes_and_animations_properties").get_editor_property("skeleton")
log("파이프라인 스켈레톤 되읽기: %s" % (got.get_path_name() if got else "!! None"))
if got is None:
    fail("파이프라인에 스켈레톤이 안 물렸다 — 전용 스켈레톤이 새로 생긴다")
    raise SystemExit(1)

# --- 임포트 ---
mgr = unreal.InterchangeManager.get_interchange_manager_scripted()
for name in PAIRS:
    path = "%s/%s" % (DEST, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    params = unreal.ImportAssetParameters()
    params.set_editor_property("is_automated", True)
    params.set_editor_property("override_pipelines", [unreal.SoftObjectPath(PIPE_COPY)])
    log("임포트 %s -> %s" % (name, mgr.import_asset(DEST, mgr.create_source_data(
        "%s/%s.fbx" % (SRC_DIR, name)), params)))
log("save_directory -> %s"
    % unreal.EditorAssetLibrary.save_directory(DEST, only_if_is_dirty=True, recursive=True))

registry.scan_paths_synchronous([DEST], True)
found = {}
for a in registry.get_assets_by_path(DEST, recursive=True):
    obj = a.get_asset()
    log("   %-52s %s" % (str(a.package_name), type(obj).__name__))
    if isinstance(obj, unreal.AnimSequence):
        found[obj.get_name()] = obj

# --- 검증: 로컬 본 회전을 원본과 대조 ---
log("=== 검증 — 로컬 본 회전 vs PWAS 원본 ===")
opts = unreal.AnimPoseEvaluationOptions()
opts.set_editor_property("evaluation_type", unreal.AnimDataEvalType.SOURCE)


def local_rots(anim):
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_frame(anim, 0, opts)
    out = {}
    for n in unreal.AnimPoseExtensions.get_bone_names(pose):
        t = unreal.AnimPoseExtensions.get_bone_pose(pose, n, unreal.AnimPoseSpaces.LOCAL)
        out[str(n)] = t.rotation
    return out


def quat_deg(a, b):
    d = abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w)
    return math.degrees(2.0 * math.acos(max(-1.0, min(1.0, d))))


for name, ref_path in PAIRS.items():
    ours = found.get(name)
    ref = unreal.EditorAssetLibrary.load_asset(ref_path)
    if ours is None:
        fail("%s 가 임포트되지 않았다" % name)
        continue
    sk = ours.get_editor_property("skeleton")
    log("   %-16s 스켈레톤 %s" % (name, sk.get_name()))
    # get_path_name() 은 `/패키지/이름.오브젝트` 꼴이라 경로와 == 로 비교하면 항상 다르다.
    if not sk.get_path_name().startswith(SKELETON + "."):
        fail("%s 가 %s 에 붙었다 — 자체 스켈레톤이 아니다" % (name, sk.get_path_name()))
        continue
    if ref is None:
        log("      (원본 %s 없음 — 대조 생략)" % ref_path)
        continue
    log("      길이 %.4fs / 프레임 %s (0 이면 애님그래프 블렌드가 0 으로 나눈다)"
        % (ours.get_editor_property("sequence_length"),
           ours.get_editor_property("number_of_sampled_frames")))
    if ours.get_editor_property("sequence_length") <= 0.0:
        fail("%s 의 길이가 0 이다 — Blender 에서 2프레임으로 내보낼 것" % name)
    a, b = local_rots(ours), local_rots(ref)
    shared = sorted(set(a) & set(b))
    worst, wb = 0.0, ""
    for n in shared:
        d = quat_deg(a[n], b[n])
        if d > worst:
            worst, wb = d, n
    log("      본 %d개 대조 · 로컬 회전 최대 차 %.4f도 (%s)" % (len(shared), worst, wb))
    if worst > MAX_ROT_DEG:
        fail("%s 의 포즈가 원본과 %.3f도 어긋난다 (%s)" % (name, worst, wb))

# 임시 파이프라인은 콘텐츠에 남기지 않는다 — delete_asset 만으로는 .uasset 이 디스크에
# 남는 일이 있어 지워졌는지 확인한다(한 번 커밋에 딸려 들어갈 뻔했다).
unreal.EditorAssetLibrary.delete_asset(PIPE_COPY)
if unreal.EditorAssetLibrary.does_asset_exist(PIPE_COPY):
    fail("임시 파이프라인 %s 가 안 지워졌다 — 손으로 지울 것" % PIPE_COPY)
log("=== 결과 ===")
if fails:
    for f in fails:
        log("!! %s" % f)
    raise SystemExit(1)
log("아이들·ADS 포즈 임포트·검증 완료")
log("FPANIM_IMPORT_DONE")
