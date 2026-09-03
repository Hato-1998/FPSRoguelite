# import_rifle_voxel.py — 복셀 라이플 파츠 OBJ 임포트
# ⚠️ -run=pythonscript 커맨드렛은 불가 — 임포트 경로가 Slate 를 요구해 어설션 즉사(Troubleshooting D1-b).
# ⚠️ 라이브 에디터 파이썬 임포트도 불가 — 게임 스레드 데드락(D1-a).
#   정식 에디터를 헤드리스로 띄워 실행한다:
#   UnrealEditor-Cmd.exe <uproject> -nullrhi -unattended -nosplash -ExecCmds="py Scripts/import_rifle_voxel.py"
#   (스크립트 말미에서 quit_editor 로 자동 종료)
#
# 판정은 종료 코드가 아니라 **완료 마커 ALLDONE** 으로(Troubleshooting C4 / §5-2).
import unreal, os, json

SRC = os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "RifleVoxel")
DEST = "/Game/Assets/Weapons/RifleVoxel"
PARTS = ["Body", "Barrel", "Handguard", "Stock", "Mag", "Grip", "Reddot"]

manifest = {}
mpath = os.path.join(SRC, "manifest.json")
if os.path.exists(mpath):
    with open(mpath, "r", encoding="utf-8") as f:
        manifest = json.load(f)

tasks = []
for p in PARTS:
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", os.path.join(SRC, "SM_RifleVoxel_%s.obj" % p))
    t.set_editor_property("destination_path", DEST)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", True)
    t.set_editor_property("replace_existing", True)
    tasks.append(t)

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

print("[rifle] ==== verify ====")
ok_all = True
for p in PARTS:
    path = "%s/SM_RifleVoxel_%s" % (DEST, p)
    exists = unreal.EditorAssetLibrary.does_asset_exist(path)
    if not exists:
        print("[rifle] %-10s MISSING" % p); ok_all = False; continue
    sm = unreal.load_asset(path)
    bb = sm.get_bounding_box()
    size = (bb.max.x - bb.min.x, bb.max.y - bb.min.y, bb.max.z - bb.min.z)
    # 섹션 수 = usemtl 그룹이 살아 들어왔는지(§2-3 B안의 핵심 검증)
    sections = sm.get_num_sections(0)
    want = len(manifest.get("parts", {}).get(p, {}).get("slots", [])) or "?"
    mark = "OK " if (want == "?" or sections == want) else "‼ "
    if want != "?" and sections != want:
        ok_all = False
    print("[rifle] %s%-10s sections=%s (기대 %s)  size_cm=(%.1f, %.1f, %.1f)"
          % (mark, p, sections, want, size[0], size[1], size[2]))

print("[rifle] slots_ok=%s" % ok_all)
print("[rifle] ALLDONE")
unreal.SystemLibrary.quit_editor()
