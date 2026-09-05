# import_voxel_chomper.py — 복셀 유령 "쩝쩝이" OBJ 임포트 (gen_voxel_chomper.py 산출물)
# ⚠️ -run=pythonscript 커맨드렛은 불가 — 임포트 경로가 Slate 를 요구해 어설션 즉사(실측 2026-08-14).
#   정식 에디터를 헤드리스로 띄워 실행한다(에디터는 반드시 꺼져 있어야 한다 — 파일 락·DDC 충돌).
#   실행은 **Scripts/run_voxel_chomper_pipeline.bat** 로(Troubleshooting D11: PowerShell 은 -ExecCmds 따옴표를 벗기고,
#   상대 경로는 엔진 바이너리 폴더 기준으로 풀린다 — 2026-09-05 에 둘 다 다시 밟았다. 절대 경로 + .bat 이 정답).
#   스크립트가 예외로 죽으면 quit_editor 가 안 돌아 에디터가 영원히 idle 이므로 try/finally 로 감싼다.
import unreal, os

SRC_DIR = os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "EnemyVoxel")
DEST = "/Game/Assets/Characters/EnemyVoxel"
FILES = ["SM_EnemyVoxel_Chomper.obj"]   # preview_* 는 Blender 렌더 전용 — 넣지 않는다(usemtl = 다중 섹션)

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
          print(f"[import]   sections={sections} tris={sm.get_num_triangles(0)} verts={sm.get_num_vertices(0)} bounds={sm.get_bounds().box_extent}")
          if sections != 1:
              print("[import]   !! 섹션이 1개가 아니다 — 다이나믹 인스턴싱 병합 자격(ADR 0007) 위반")
except Exception as e:
  import traceback; traceback.print_exc()
  print(f"[import] FAILED {e}")
finally:
  unreal.SystemLibrary.quit_editor()
