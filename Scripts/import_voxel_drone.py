# import_voxel_drone.py — 복셀 드론 OBJ 임포트 (gen_voxel_drone.py 산출물)
# ⚠️ -run=pythonscript 커맨드렛은 불가 — 임포트 경로가 Slate 를 요구해 어설션 즉사(Troubleshooting D1-b).
#   정식 에디터를 헤드리스로 띄워 실행한다(에디터는 반드시 꺼져 있어야 한다 — 파일 락·DDC 충돌).
#   실행은 Scripts/run_voxel_drone_pipeline.bat 로(D11: PowerShell 따옴표 유실 · 상대 경로 · 예외 시 quit 미도달).
import unreal, os

SRC_DIR = os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "EnemyVoxel")
DEST = "/Game/Assets/Characters/EnemyVoxel"
FILES = ["SM_EnemyVoxel_Drone.obj"]   # *_preview.obj 는 Blender 렌더 전용(usemtl = 다중 섹션) — 넣지 않는다

try:
    tasks = []
    for fname in FILES:
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", os.path.join(SRC_DIR, fname))
        t.set_editor_property("destination_path", DEST)
        t.set_editor_property("automated", True)
        t.set_editor_property("save", True)
        t.set_editor_property("replace_existing", True)
        tasks.append(t)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    for fname in FILES:
        name = fname.rsplit(".", 1)[0]
        path = f"{DEST}/{name}"
        ok = unreal.EditorAssetLibrary.does_asset_exist(path)
        print(f"[import] {path} -> {'OK' if ok else 'MISSING'}")
        if ok:
            sm = unreal.load_asset(path)
            sections = sm.get_num_sections(0)
            bb = sm.get_bounding_box()
            print(f"[import]   sections={sections} tris={sm.get_num_triangles(0)} verts={sm.get_num_vertices(0)} "
                  f"bbox min=({bb.min.x:.1f},{bb.min.y:.1f},{bb.min.z:.1f}) max=({bb.max.x:.1f},{bb.max.y:.1f},{bb.max.z:.1f})")
            if sections != 1:
                print("[import]   !! sections != 1 — ADR 0007 dynamic-instancing eligibility broken")
except Exception as e:
    import traceback; traceback.print_exc()
    print(f"[import] FAILED {e}")
finally:
    unreal.SystemLibrary.quit_editor()
