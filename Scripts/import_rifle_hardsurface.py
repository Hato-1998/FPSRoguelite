# import_rifle_hardsurface.py — 하드서피스 라이플 파츠 OBJ 임포트
# ⚠️ -run=pythonscript 커맨드렛은 불가 — 임포트 경로가 Slate 를 요구해 어설션 즉사(Troubleshooting D1-b).
# ⚠️ 라이브 에디터 파이썬 임포트도 불가 — 게임 스레드 데드락(D1-a).
#   정식 에디터를 헤드리스로 띄워 실행한다 — 반드시 Scripts/run_import_rifle_hardsurface.bat 으로
#   (PowerShell 직접 호출은 -ExecCmds 따옴표가 벗겨지고, 상대 경로는 엔진 폴더 기준으로 풀린다: D11).
#
# 판정은 종료 코드가 아니라 **완료 마커 ALLDONE** 으로(Troubleshooting C4 / §5-2).
import unreal, os, json

SRC = os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "RifleHardSurface")
DEST = "/Game/Assets/Weapons/RifleHS"
PARTS = ["Body", "Barrel", "Handguard", "Stock", "Mag", "Grip", "SightRed", "Sight2x"]

manifest = {}
mpath = os.path.join(SRC, "manifest.json")
if os.path.exists(mpath):
    with open(mpath, "r", encoding="utf-8") as f:
        manifest = json.load(f)

tasks = []
for p in PARTS:
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", os.path.join(SRC, "SM_RifleHS_%s.obj" % p))
    t.set_editor_property("destination_path", DEST)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", True)
    t.set_editor_property("replace_existing", True)
    tasks.append(t)

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

print("[rifle] ==== verify ====")
ok_all = True
for p in PARTS:
    path = "%s/SM_RifleHS_%s" % (DEST, p)
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        print("[rifle] %-10s MISSING" % p); ok_all = False; continue
    sm = unreal.load_asset(path)
    bb = sm.get_bounding_box()
    size = (bb.max.x - bb.min.x, bb.max.y - bb.min.y, bb.max.z - bb.min.z)
    sections = sm.get_num_sections(0)                       # usemtl 그룹 = 섹션 (§2-3 B안 검증)
    want = len(manifest.get("parts", {}).get(p, {}).get("slots", [])) or "?"
    mark = "OK " if (want == "?" or sections == want) else "!! "
    if want != "?" and sections != want:
        ok_all = False
    print("[rifle] %s%-10s sections=%s (expect %s)  size_cm=(%.1f, %.1f, %.1f)  tris=%d"
          % (mark, p, sections, want, size[0], size[1], size[2], sm.get_num_triangles(0)))

print("[rifle] slots_ok=%s" % ok_all)
print("[rifle] ALLDONE")
unreal.SystemLibrary.quit_editor()
