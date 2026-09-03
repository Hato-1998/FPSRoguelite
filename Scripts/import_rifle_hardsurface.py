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
    sections = sm.get_num_sections(0)                       # usemtl 그룹 = 섹션 (§2-3 B안 검증)
    want = len(manifest.get("parts", {}).get(p, {}).get("slots", [])) or "?"
    good = (want == "?" or sections == want)
    # 🔴 size 가 아니라 min/max — UE OBJ 임포터가 Y 를 부호 반전한다(Troubleshooting D12). size 는 부호를 못 본다.
    print("[rifle] %s%-10s sections=%s (expect %s)  X %.1f..%.1f  Y %.1f..%.1f  Z %.1f..%.1f  tris=%d"
          % ("OK " if good else "!! ", p, sections, want,
             bb.min.x, bb.max.x, bb.min.y, bb.max.y, bb.min.z, bb.max.z, sm.get_num_triangles(0)))
    if not good:
        ok_all = False
    if p == "Body":
        # 몸통은 그립 마운트가 원점, 정면 = +Y. 기대 Y −8.0..22.6 — 뒤집혔으면 −22.6..8.0 이 나온다.
        fwd_ok = bb.max.y > 15.0 and bb.min.y > -15.0
        print("[rifle] %sBody forward=+Y check (expect Y -8.0..22.6): %s"
              % ("OK " if fwd_ok else "!! ", "pass" if fwd_ok else "FLIPPED"))
        if not fwd_ok:
            ok_all = False

print("[rifle] slots_ok=%s" % ok_all)
print("[rifle] ALLDONE")
unreal.SystemLibrary.quit_editor()
