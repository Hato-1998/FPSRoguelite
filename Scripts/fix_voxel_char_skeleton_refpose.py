# fix_voxel_char_skeleton_refpose.py — USkeleton 의 ref pose 만 메시(cm)에 맞춰 갱신한다.
#
# 상황: Scripts/reimport_voxel_char.py 로 메시 9개는 cm 가 됐다(root sc=1.0 · pelvis z=95.8968).
#       USkeleton 만 미터(root sc=100 · pelvis z=0.959)로 남아 갈라져 있다.
#       재임포트는 메시 ref pose 를 **항상** 갱신하고, 스켈레톤 쪽은 플래그가 있어야만 갱신하기 때문이다.
#
# 갱신을 실제로 하는 코드 (InterchangeGenericSkeletalMeshPipeline.cpp:722, PostImportSkeletalMesh):
#   !bImportGeometryOnlyContent && bUpdateSkeletonReferencePose
#   && CommonSkeletalMeshesAndAnimationsProperties->Skeleton.IsValid()
#   && SkeletalMesh->GetSkeleton() == 그 Skeleton
#       -> SkeletalMesh->GetSkeleton()->UpdateReferencePoseFromMesh(SkeletalMesh)
# `UpdateReferencePoseFromMesh` 는 UFUNCTION 이 아니라 리플렉션으로 직접 못 부른다. 임포트를 태워야 한다.
#
# 앞서 실패한 경로들(전부 무반응, 실측):
#   ① 레거시 FbxImportUI + update_skeleton_reference_pose  — UI 객체엔 붙지만 Interchange 변환에서 떨어진다
#   ② AssetImportTask.options = InterchangePipelineStackOverride  — 이유 미상
#   ③ 라이브 에디터에서 InterchangeProjectSettings 를 Python 으로 수정 — 호출 간에 유지되지 않는다(되돌아간다)
#
# 그래서 여기서는 **스택이 이미 가리키고 있는 파이프라인 에셋을 메모리에서 고친다.**
# SoftObjectPath 를 만들 필요가 없다(Python 에 노출된 필드가 없어 유효성 확인조차 안 된다 — 실측).
# 저장하지 않으므로 엔진 콘텐츠는 디스크에서 안 바뀐다.
#
# 🪤 계측 함정 2종 (2026-09-10 실제로 밟았다):
#   * 존재하지 않는 에셋 경로에 SkeletonService.get_bone_transform 를 물으면 예외가 아니라 **쓰레기 값**이 온다.
#     -> 모든 측정 앞에 does_asset_exist 가드.
#   * SoftObjectPath 는 유효하든 아니든 항상 `{}` 로 찍힌다 -> "비었다"의 증거가 못 된다.
import unreal

BASE = "/Game/Characters/VoxelChar"
SK_PATH = BASE + "/SKM_VoxelChar_Skeleton"
MESH_PATH = BASE + "/SKM_VoxelChar"
SRC = "C:/Users/koras/Desktop/voxel+character+3d+model/SKM_VoxelChar.fbx"

SS = unreal.SkeletonService


def log(m):
    print("[fix] %s" % m)


def vs(v):
    return "(%.4f, %.4f, %.4f)" % (v.x, v.y, v.z)


def read_pose(path, label):
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        log("  !! %s 경로가 없다: %s — 이 경로의 측정은 전부 무효" % (label, path))
        return None
    out = {}
    for bn in ("root", "pelvis", "hand_r"):
        t = SS.get_bone_transform(path, bn, False)
        out[bn] = (t.translation, t.scale3d)
    log("  %-13s root sc=%.3f | pelvis z=%.4f | hand_r x=%.4f"
        % (label, out["root"][1].x, out["pelvis"][0].z, out["hand_r"][0].x))
    return out


try:
    log("=== 전 ===")
    read_pose(SK_PATH, "USkeleton")
    read_pose(MESH_PATH, "SkeletalMesh")

    skeleton = unreal.load_asset(SK_PATH)
    if not skeleton:
        raise RuntimeError("스켈레톤 로드 실패: " + SK_PATH)
    merged_before = unreal.load_asset(MESH_PATH)
    if not merged_before:
        raise RuntimeError("메시 로드 실패: " + MESH_PATH)

    # 프로젝트가 실제로 쓰는 스택과 그 파이프라인을 찾는다.
    settings = unreal.get_default_object(unreal.InterchangeProjectSettings)
    cis = settings.get_editor_property("content_import_settings")
    stack_name = cis.get_editor_property("default_pipeline_stack")
    stacks = cis.get_editor_property("pipeline_stacks")
    log("기본 스택 = '%s'" % stack_name)

    stack = stacks[stack_name]
    pipeline_paths = stack.get_editor_property("pipelines")
    log("스택 파이프라인 %d개" % len(pipeline_paths))

    # SoftObjectPath 를 문자열로 못 읽으므로, 알려진 엔진 기본 경로를 직접 연다.
    # (스택이 이걸 가리키고 있다 — default_pipeline_stack='Assets' 실측)
    patched = []
    for cand in ("/Interchange/Pipelines/DefaultAssetsPipeline",
                 "/Interchange/Pipelines/DefaultSceneAssetsPipeline"):
        if not unreal.EditorAssetLibrary.does_asset_exist(cand):
            continue
        pl = unreal.load_asset(cand)
        if not pl:
            continue
        try:
            mesh_p = pl.get_editor_property("mesh_pipeline")
            common_sk = pl.get_editor_property("common_skeletal_meshes_and_animations_properties")
        except Exception as e:
            log("  %s 는 대상이 아니다(%s)" % (cand, str(e)[:60]))
            continue
        mesh_p.set_editor_property("update_skeleton_reference_pose", True)
        mesh_p.set_editor_property("create_physics_asset", False)
        common_sk.set_editor_property("skeleton", skeleton)
        common_sk.set_editor_property("import_only_animations", False)
        patched.append(cand)
        log("  패치: %s  (update_skeleton_reference_pose=%s, skeleton=%s)"
            % (cand, mesh_p.get_editor_property("update_skeleton_reference_pose"),
               common_sk.get_editor_property("skeleton").get_name()))
    if not patched:
        raise RuntimeError("패치할 파이프라인을 못 찾았다")

    # 🔑 결정적 한 수 — 플래그를 **에셋에 저장된 임포트 데이터**에 박는다.
    #
    # 변환기가 두 갈래다(InterchangeFbxAssetImportDataConverter.cpp):
    #   * AssetImportTask.options 로 UFbxImportUI 를 주면 :873 의
    #     FillInterchangeGenericAssetsPipelineFromFbxMeshImportData(**베이스** 함수)가 돈다
    #     → 여기엔 bUpdateSkeletonReferencePose 대입이 **없다**. 그래서 옵션으로 주면 조용히 사라진다.
    #   * 옵션 없이 재임포트하면 :1045 가 SkeletalMesh->GetAssetImportData() 를 변환하고,
    #     그쪽은 FillInterchangeGenericAssetsPipelineFromFbxSkeletalMeshImportData(:342)라
    #     :361 에서 bUpdateSkeletonReferencePose 를 **실어 나른다**.
    # → 플래그는 에셋에 저장돼 있어야 하고, 재임포트에는 옵션을 주면 **안 된다**.
    aid = merged_before.get_editor_property("asset_import_data")
    log("에셋 저장 임포트 데이터: %s" % type(aid).__name__)
    aid.set_editor_property("update_skeleton_reference_pose", True)
    # Geometry 전용이면 bImportGeometryOnlyContent 로 막힌다(:721) — All 이어야 한다.
    try:
        aid.set_editor_property("import_content_type", unreal.FBXImportContentType.FBXICT_ALL)
    except Exception as e:
        log("  import_content_type 설정 생략: %s" % str(e)[:100])
    unreal.EditorAssetLibrary.save_asset(MESH_PATH)
    log("  저장 후 되읽기: update_skeleton_reference_pose=%s / import_content_type=%s"
        % (aid.get_editor_property("update_skeleton_reference_pose"),
           aid.get_editor_property("import_content_type")))

    # 옵션을 주지 않는다 — 위 갈래를 타야 플래그가 살아서 간다.
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", SRC)
    t.set_editor_property("destination_path", BASE)
    t.set_editor_property("destination_name", "SKM_VoxelChar")
    t.set_editor_property("automated", True)
    t.set_editor_property("save", True)
    t.set_editor_property("replace_existing", True)
    log("재임포트(옵션 없음 = 패치된 기본 스택 사용)...")
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])

    log("=== 후 ===")
    after_sk = read_pose(SK_PATH, "USkeleton")
    read_pose(MESH_PATH, "SkeletalMesh")

    unreal.EditorAssetLibrary.save_asset(SK_PATH)
    unreal.EditorAssetLibrary.save_asset(MESH_PATH)

    log("=== 목표 수치 대조 ===")
    fails = []

    def check(label, got, want, tol):
        ok = got is not None and abs(got - want) <= tol
        log("  %-26s %-12s (목표 %s) %s"
            % (label, round(got, 4) if got is not None else "N/A", want, "OK" if ok else "!! FAIL"))
        if not ok:
            fails.append(label)

    if after_sk:
        check("root 로컬 scale", after_sk["root"][1].x, 1.0, 0.001)
        check("pelvis 로컬 z", after_sk["pelvis"][0].z, 95.9, 0.1)
        check("hand_r 로컬 x", after_sk["hand_r"][0].x, -13.63, 0.1)
        check("hand_r 컴포넌트 scale", SS.get_bone_transform(SK_PATH, "hand_r", True).scale3d.x, 1.0, 0.001)
    else:
        fails.append("스켈레톤 측정 불가")

    merged = unreal.load_asset(MESH_PATH)
    check("SKM_VoxelChar extent z", merged.get_bounds().box_extent.z, 90.0, 0.5)

    mats = [(x.material_interface.get_name() if x.material_interface else "None")
            for x in merged.get_editor_property("materials")]
    pa = merged.get_editor_property("physics_asset")
    log("  머티리얼=%s / PhysicsAsset=%s / 소켓=%d"
        % (mats, pa.get_name() if pa else None, merged.num_sockets()))
    if mats != ["tripo_mat_12bcd575"]:
        fails.append("머티리얼 소실 %s" % mats)
    if merged.num_sockets() < 1:
        fails.append("SOCKET_Weapon 소실")
    if not pa:
        fails.append("PhysicsAsset 소실")

    log("판정: %s" % ("PASS — 목표 수치 전부 충족" if not fails else "FAIL — " + ", ".join(fails)))
except Exception as e:
    import traceback
    traceback.print_exc()
    log("FAILED %s" % e)
finally:
    unreal.SystemLibrary.quit_editor()
