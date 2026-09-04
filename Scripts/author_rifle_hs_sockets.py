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
# 회전(엔진 프레임 P/Y/R). 매니페스트에 없는 소켓은 (0,0,0). 팔 IK 가 손 소켓 회전으로 손바닥 방향을 잡는다
# (2026-09-04 PIE 실측 — 회전 누락 = 손이 엉뚱한 방향). unreal.Rotator(...) 는 (Roll, Pitch, Yaw) 순서다.
ROT = {"Body": M.get("body_socket_rotations", {})}
def rot_for(part, name):
    p, y, r = ROT.get(part, {}).get(name, (0.0, 0.0, 0.0))
    return unreal.Rotator(r, p, y)

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
            existing.set_editor_property("relative_rotation", rot_for(part, name))
            action = "upd"
        else:
            s = unreal.StaticMeshSocket(outer=sm)
            s.set_editor_property("socket_name", name)
            s.set_editor_property("relative_location", unreal.Vector(x, y, z))
            s.set_editor_property("relative_rotation", rot_for(part, name))
            sm.add_socket(s)
            action = "add"
        rr = rot_for(part, name)
        report.append("[sock] %-10s %-26s %s loc(%.1f, %.1f, %.1f) rotPYR(%.1f, %.1f, %.1f)"
                      % (part, name, action, x, y, z, rr.pitch, rr.yaw, rr.roll))
    sm.modify()
    saved = unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    report.append("[sock] %-10s save=%s" % (part, saved))

# 검증 — 기대한 이름이 전부 실재하고 위치·회전이 매니페스트와 일치하는지 재조회
ok = True
for part, socks in PLAN.items():
    sm = unreal.EditorAssetLibrary.load_asset("%s/SM_RifleHS_%s" % (DEST, part))
    have = []
    for name, (x, y, z) in socks.items():
        s = sm.find_socket(name) if sm else None
        if not s:
            ok = False; have.append("!!MISSING " + name); continue
        L = s.get_editor_property("relative_location"); R = s.get_editor_property("relative_rotation")
        want = rot_for(part, name)
        good = (abs(L.x-x) < 0.05 and abs(L.y-y) < 0.05 and abs(L.z-z) < 0.05
                and abs(R.pitch-want.pitch) < 0.05 and abs(R.yaw-want.yaw) < 0.05 and abs(R.roll-want.roll) < 0.05)
        if not good: ok = False
        have.append("%s%s(%.1f,%.1f,%.1f|%.0f,%.0f,%.0f)" % ("" if good else "!!", name, L.x, L.y, L.z, R.pitch, R.yaw, R.roll))
    report.append("[sock] verify %-10s %s" % (part, " ".join(have)))

for line in report:
    unreal.log(line)
unreal.log("[sock] sockets_ok=%s" % ok)
unreal.log("[sock] ALLDONE")
