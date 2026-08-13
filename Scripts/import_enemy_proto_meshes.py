# import_enemy_proto_meshes.py — 프로토 메시 OBJ 임포트
# ⚠️ -run=pythonscript 커맨드렛은 불가 — 임포트 경로가 Slate를 요구해 어설션 즉사(실측 2026-08-14).
#   정식 에디터를 헤드리스로 띄워 실행한다:
#   UnrealEditor-Cmd.exe <uproject> -nullrhi -unattended -nosplash -ExecCmds="py Scripts/import_enemy_proto_meshes.py"
#   (스크립트 말미에서 quit_editor로 자동 종료)
import unreal, os

SRC_DIR = os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "EnemyProto")
DEST = "/Game/Assets/Characters/EnemyProto"
FILES = ["SM_EnemyProto_Bipyramid.obj", "SM_EnemyProto_AtomCubes.obj"]

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
        print(f"[import]   sections={sm.get_num_sections(0)}")

unreal.SystemLibrary.quit_editor()
