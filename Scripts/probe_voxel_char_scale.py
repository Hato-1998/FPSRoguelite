# probe_voxel_char_scale.py — Interchange 파이프라인 오버라이드로 스켈레톤 ref pose 갱신이 되는지
# 스크래치에서 시험한다. 실제 에셋은 건드리지 않는다.
#
# 왜 Interchange 인가: UE 5.7 의 AssetImportTask 는 Interchange 로 간다
# (AssetTools.cpp:3909·3918 — Options 를 UInterchangePipelineStackOverride 로 캐스팅하고,
#  아니면 InterchangeManager.ConvertImportData 로 변환한다). 레거시 FbxImportUI 를 넘기면
# 변환 과정에서 bUpdateSkeletonReferencePose 가 떨어진다(실측: flag True/False 2군이 동일).
#
# Interchange 쪽 발동 조건 (InterchangeGenericSkeletalMeshPipeline.cpp:722):
#   !bImportGeometryOnlyContent && bUpdateSkeletonReferencePose
#   && CommonSkeletalMeshesAndAnimationsProperties->Skeleton.IsValid()
#   && SkeletalMesh->GetSkeleton() == 그 Skeleton
# → 플래그만으론 안 되고 **Skeleton 프로퍼티를 반드시 채워야** 한다.
import unreal

SCRATCH = "/Game/__scratch_voxelscale"
NEW = "C:/Users/koras/Desktop/voxel+character+3d+model/SKM_VoxelChar.fbx"
OLD = "C:/Users/koras/Desktop/voxel+character+3d+model/_pre_scalefix_backup/SKM_VoxelChar.fbx"


def log(m):
    print("[probe] %s" % m)


def show(obj, label, keys):
    log("  %s: %s" % (label, [k for k in dir(obj) if any(s in k for s in keys)][:14]))


def build_override(skeleton, update_ref_pose):
    gap = unreal.InterchangeGenericAssetsPipeline()
    common_sk = gap.get_editor_property("common_skeletal_meshes_and_animations_properties")
    mesh_p = gap.get_editor_property("mesh_pipeline")
    mat_p = gap.get_editor_property("material_pipeline")

    mesh_p.set_editor_property("update_skeleton_reference_pose", update_ref_pose)
    mesh_p.set_editor_property("import_skeletal_meshes", True)
    mesh_p.set_editor_property("import_static_meshes", False)
    mesh_p.set_editor_property("create_physics_asset", False)
    common_sk.set_editor_property("skeleton", skeleton)     # 조건 ③④ — 없으면 절대 안 돈다
    common_sk.set_editor_property("import_only_animations", False)
    try:
        mat_p.set_editor_property("import_materials", False)
    except Exception as e:
        log("  (material_pipeline.import_materials 못 씀: %s)" % str(e)[:80])
    try:
        gap.get_editor_property("common_meshes_properties").set_editor_property("import_lods", False)
    except Exception:
        pass

    ov = unreal.InterchangePipelineStackOverride()
    ov.add_pipeline(gap)
    return ov


def do_import(fbx, name, options):
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", fbx)
    t.set_editor_property("destination_path", SCRATCH)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", True)
    t.set_editor_property("replace_existing", True)
    if options:
        t.set_editor_property("options", options)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])


def read_sk(path):
    SS = unreal.SkeletonService
    r = SS.get_bone_transform(path, "root", False)
    p = SS.get_bone_transform(path, "pelvis", False)
    return "root sc=%.3f | pelvis z=%.4f" % (r.scale3d.x, p.translation.z)


try:
    log("=== 구조 확인 ===")
    gap = unreal.InterchangeGenericAssetsPipeline()
    show(gap, "GenericAssetsPipeline", ["pipeline", "common", "properties"])
    mp = gap.get_editor_property("mesh_pipeline")
    show(mp, "MeshPipeline", ["skeleton", "update", "physics", "import_sk"])
    cs = gap.get_editor_property("common_skeletal_meshes_and_animations_properties")
    show(cs, "CommonSkelProps", ["skeleton", "animation"])
    show(unreal.InterchangePipelineStackOverride(), "StackOverride", ["pipeline", "add"])

    for arm, flag in (("C", True), ("D", False)):
        name = "EXP_" + arm
        sk_path = "%s/%s_Skeleton" % (SCRATCH, name)
        log("=== %s군 (Interchange, update_skeleton_reference_pose=%s) ===" % (arm, flag))
        do_import(OLD, name, None)                       # 미터 스켈레톤 생성 (기본 파이프라인)
        log("  1) 옛 FBX 로 생성   : %s" % read_sk(sk_path))
        sk = unreal.load_asset(sk_path)
        do_import(NEW, name, build_override(sk, flag))   # 새 FBX 를 같은 스켈레톤에 얹는다
        log("  2) 새 FBX 재임포트 후: %s   <= flag=%s" % (read_sk(sk_path), flag))
except Exception as e:
    import traceback
    traceback.print_exc()
    log("FAILED %s" % e)
finally:
    unreal.SystemLibrary.quit_editor()
