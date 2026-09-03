# author_rifle_hs_sockets.py — 하드서피스 라이플 정적 메시에 소켓을 찍는다
#   실행 = **켜져 있는 에디터**에서(VibeUE execute_python_code 로 이 파일을 exec, 또는 에디터 Python 콘솔).
#   에셋 속성 편집이라 라이브에서 안전하다(임포트만 위험 — Troubleshooting D1-a).
#
# 좌표 소스 = Saved/RifleHardSurface/manifest.json (gen_rifle_hardsurface.py 가 계산).
#   전부 **엔진 프레임(+Y 정면)·파츠 로컬(mount 원점)·cm** 라 그대로 찍으면 된다.
#   몸통 = 파츠 마운트 6 + 양손 2 / 총열 = SOCKET_Muzzle / 조준경 2종 = SOCKET_Aim (규약: §3-4).
#
# 멱등: 이미 있는 소켓은 위치만 갱신한다. 판정은 add 반환값이 아니라 **find_socket 재조회**로.
import unreal, os, json

DEST = "/Game/Assets/Weapons/RifleHS"
SRC = os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "RifleHardSurface")
with open(os.path.join(SRC, "manifest.json"), "r", encoding="utf-8") as f:
    M = json.load(f)

PLAN = {"Body": M["body_sockets"]}
for part, d in M["parts"].items():
    if d.get("sockets"):
        PLAN[part] = d["sockets"]

report = []
for part, socks in PLAN.items():
    path = "%s/SM_RifleHS_%s" % (DEST, part)
    sm = unreal.EditorAssetLibrary.load_asset(path)
    if not sm:
        report.append("[sock] %-10s MISSING ASSET %s" % (part, path)); continue
    for name, (x, y, z) in socks.items():
        existing = sm.find_socket(name)
        if existing:
            existing.set_editor_property("relative_location", unreal.Vector(x, y, z))
            action = "upd"
        else:
            s = unreal.StaticMeshSocket(outer=sm)
            s.set_editor_property("socket_name", name)
            s.set_editor_property("relative_location", unreal.Vector(x, y, z))
            s.set_editor_property("relative_rotation", unreal.Rotator(0, 0, 0))
            sm.add_socket(s)
            action = "add"
        report.append("[sock] %-10s %-26s %s (%.1f, %.1f, %.1f)" % (part, name, action, x, y, z))
    sm.modify()
    saved = unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    report.append("[sock] %-10s save=%s" % (part, saved))

# 검증 — 기대한 이름이 전부 실재하는지 재조회
ok = True
for part, socks in PLAN.items():
    sm = unreal.EditorAssetLibrary.load_asset("%s/SM_RifleHS_%s" % (DEST, part))
    have = []
    for name in socks:
        s = sm.find_socket(name) if sm else None
        if s:
            L = s.get_editor_property("relative_location")
            have.append("%s(%.1f,%.1f,%.1f)" % (name, L.x, L.y, L.z))
        else:
            ok = False; have.append("!!MISSING " + name)
    report.append("[sock] verify %-10s %s" % (part, " ".join(have)))

for line in report:
    unreal.log(line)
unreal.log("[sock] sockets_ok=%s" % ok)
unreal.log("[sock] ALLDONE")
