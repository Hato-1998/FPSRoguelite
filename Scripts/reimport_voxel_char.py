# reimport_voxel_char.py — VoxelChar 스켈레탈 메시 9개(머지 1 + 파츠 8) 재임포트로 미터 리그를 cm 로 바꾼다.
#
# 선행: Scripts/export_voxel_char_fbx.py 로 ×100 구운 FBX 를 먼저 내보냈어야 한다(아마추어 노드 scale 0.01).
#
# ⚠️ -run=pythonscript 커맨드렛은 불가 — 임포트 경로가 Slate 를 요구해 어설션 즉사(실측 2026-08-14).
#   정식 에디터를 헤드리스로 띄운다. **에디터는 반드시 꺼져 있어야 한다**(파일 락 · DDC 충돌 ·
#   라이브 에디터 Python 임포트 = 게임 스레드 데드락). 실행은 Scripts/run_voxel_char_reimport.bat 로.
#   예외로 죽으면 quit_editor 가 안 돌아 에디터가 영원히 idle 이므로 try/finally 로 감싼다.
#
# 🔑 이 스크립트의 핵심 = `update_skeleton_reference_pose = True`
#   엔진 소스(FbxSkeletalMeshImport.cpp:2286)를 읽어 확인한 것: 본 이름·계층이 그대로면
#   `Skeleton->MergeAllBonesToBoneTree()` 가 **성공**하고, 그러면 스켈레톤 ref pose 를 갱신하는
#   `Skeleton->UpdateReferencePoseFromMesh()` 는 **이 플래그가 켜져야만** 돈다.
#   끄고 돌리면 「메시는 cm · USkeleton 은 미터」로 갈라져 지금보다 나빠진다.
#   (헤더 툴팁: "Mesh's reference pose is always updated." — 갱신 안 되는 건 스켈레톤 쪽이다.)
#   이 UPROPERTY 는 EditAnywhere 지만 BlueprintReadWrite 가 아니라 **dir() 에 안 보인다.**
#   그래도 `set_editor_property('update_skeleton_reference_pose', True)` 는 먹는다(실측).
#
# 머티리얼 보존: `import_materials=False` 로 두면 엔진이 SaveExistingSkelMeshData/RestoreExistingSkelMeshData
#   경로로 기존 머티리얼 슬롯을 되살린다(FbxSkeletalMeshImport.cpp:1964·2177 — 두 번째 인자가 bSaveMaterials).
#   True 로 주면 FBX 안의 머티리얼로 덮어써 `tripo_mat_12bcd575` 배선이 날아간다.
import unreal
import os

BASE = "/Game/Characters/VoxelChar"
SK_PATH = BASE + "/SKM_VoxelChar_Skeleton"
SRC_DIR = "C:/Users/koras/Desktop/voxel+character+3d+model"
PARTS = ["Foot_L", "Foot_R", "Hand_L", "Hand_R", "Head", "Thigh_L", "Thigh_R", "Torso"]

# 머지 메시가 **먼저**다 — 스켈레톤 ref pose 를 여기서 받아야 파츠들이 같은 기준으로 머지된다.
JOBS = [("SKM_VoxelChar", BASE, "SKM_VoxelChar.fbx")] + [
    ("SKM_VoxelChar_" + p, BASE + "/Parts", "SKM_VoxelChar_%s.fbx" % p) for p in PARTS]


def log(m):
    print("[reimport] %s" % m)


def vs(v):
    return "(%.4f, %.4f, %.4f)" % (v.x, v.y, v.z)


def make_options(skeleton):
    """재임포트 전 기준선(2026-09-10 실측)을 그대로 재현하고 두 가지만 바꾼다:
       update_skeleton_reference_pose=True(위 설명) · create_physics_asset=False(기존 것 유지)."""
    d = unreal.FbxSkeletalMeshImportData()
    d.set_editor_property("import_uniform_scale", 1.0)
    d.set_editor_property("import_translation", unreal.Vector(0.0, 0.0, 0.0))
    d.set_editor_property("import_rotation", unreal.Rotator(0.0, 0.0, 0.0))
    d.set_editor_property("convert_scene", True)
    d.set_editor_property("force_front_x_axis", False)
    d.set_editor_property("convert_scene_unit", False)
    d.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_COMPUTE_NORMALS)
    d.set_editor_property("normal_generation_method", unreal.FBXNormalGenerationMethod.MIKK_T_SPACE)
    d.set_editor_property("compute_weighted_normals", True)
    d.set_editor_property("preserve_smoothing_groups", True)
    d.set_editor_property("import_meshes_in_bone_hierarchy", True)
    d.set_editor_property("import_morph_targets", False)
    d.set_editor_property("use_t0_as_ref_pose", False)
    d.set_editor_property("update_skeleton_reference_pose", True)   # dir() 에 없지만 먹는다

    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", True)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    ui.set_editor_property("skeleton", skeleton)          # 없으면 새 스켈레톤을 만들어 버린다
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("create_physics_asset", False)  # 기존 SKM_VoxelChar_PhysicsAsset 유지
    ui.set_editor_property("import_materials", False)      # 기존 머티리얼 슬롯 복원 경로를 탄다
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("skeletal_mesh_import_data", d)
    return ui


try:
    skeleton = unreal.load_asset(SK_PATH)
    if not skeleton:
        raise RuntimeError("스켈레톤을 못 찾았다: " + SK_PATH)

    SS = unreal.SkeletonService
    log("전: root comp scale=%s" % vs(SS.get_bone_transform(SK_PATH, "root", True).scale3d))

    tasks = []
    for name, dest, fname in JOBS:
        src = os.path.join(SRC_DIR, fname).replace("\\", "/")
        if not os.path.exists(src):
            raise RuntimeError("소스 FBX 가 없다: " + src)
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", src)
        t.set_editor_property("destination_path", dest)
        t.set_editor_property("destination_name", name)
        t.set_editor_property("automated", True)
        t.set_editor_property("save", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("options", make_options(skeleton))
        tasks.append(t)

    # 한 번에 넘기면 머지 메시가 먼저 처리되는 순서가 보장되지 않을 수 있어 하나씩 돌린다.
    at = unreal.AssetToolsHelpers.get_asset_tools()
    for t, (name, dest, fname) in zip(tasks, JOBS):
        log("임포트: %s <- %s" % (name, fname))
        at.import_asset_tasks([t])

    # ── 검증: 보드 「목표 상태」 표 대조 ───────────────────────────────────────
    log("=== 목표 수치 대조 ===")
    fails = []

    def check(label, got, want, tol):
        ok = abs(got - want) <= tol
        log("  %-26s %-14s (목표 %s) %s" % (label, round(got, 4), want, "OK" if ok else "!! FAIL"))
        if not ok:
            fails.append(label)

    root_t = SS.get_bone_transform(SK_PATH, "root", False)
    pelvis_t = SS.get_bone_transform(SK_PATH, "pelvis", False)
    hand_l_t = SS.get_bone_transform(SK_PATH, "hand_r", False)
    hand_c = SS.get_bone_transform(SK_PATH, "hand_r", True)

    check("root 로컬 scale", root_t.scale3d.x, 1.0, 0.001)
    check("pelvis 로컬 z", pelvis_t.translation.z, 95.9, 0.1)
    check("hand_r 로컬 x", hand_l_t.translation.x, -13.63, 0.1)
    check("hand_r 컴포넌트 scale", hand_c.scale3d.x, 1.0, 0.001)

    merged = unreal.load_asset(BASE + "/SKM_VoxelChar")
    check("SKM_VoxelChar extent z", merged.get_bounds().box_extent.z, 90.0, 0.5)

    # 부수 확인 — 소켓·머티리얼·스켈레톤 배선이 살아남았는가
    log("  mesh sockets=%d" % merged.num_sockets())
    for i in range(merged.num_sockets()):
        s = merged.get_socket_by_index(i)
        log("    소켓 %s 본=%s outer=%s rel_scale=%s"
            % (s.socket_name, s.bone_name, s.get_outer().get_name(), vs(s.relative_scale)))
    if merged.num_sockets() < 1:
        fails.append("SOCKET_Weapon 소실")

    for name, dest, fname in JOBS:
        m = unreal.load_asset("%s/%s" % (dest, name))
        sk = m.get_editor_property("skeleton")
        mats = [(x.material_interface.get_name() if x.material_interface else "None")
                for x in m.get_editor_property("materials")]
        pa = m.get_editor_property("physics_asset")
        log("  %-26s skel=%s ext=%s mats=%s phys=%s"
            % (name, sk.get_name() if sk else None, vs(m.get_bounds().box_extent), mats,
               pa.get_name() if pa else None))
        if not sk or sk.get_name() != "SKM_VoxelChar_Skeleton":
            fails.append(name + " 스켈레톤 이탈")
        if mats != ["tripo_mat_12bcd575"]:
            fails.append(name + " 머티리얼 소실")

    unreal.EditorAssetLibrary.save_asset(SK_PATH)
    log("판정: %s" % ("PASS — 목표 수치 전부 충족" if not fails else "FAIL — " + ", ".join(fails)))
except Exception as e:
    import traceback
    traceback.print_exc()
    log("FAILED %s" % e)
finally:
    unreal.SystemLibrary.quit_editor()
