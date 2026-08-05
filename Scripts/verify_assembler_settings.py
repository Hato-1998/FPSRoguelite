"""어셈블러 "팔 보기" 프리뷰의 **선행조건**을 확인한다 — 읽기 전용.

설정 클래스가 파이썬에 노출되는지와 무관하게, 툴이 실제로 필요로 하는 것 셋을 직접 잰다:
  ① ini 에 적힌 팔 메시·포즈가 존재하나  ② 둘의 스켈레톤이 같나(다르면 포즈가 안 붙는다)
  ③ 팔 메시에 무기 부착 소켓이 있나(없으면 '손 위치 저장'이 실패한다)
"""
import re, unreal
def log(m): unreal.log("[ASM] %s" % m)
ini = open("E:/Git_Project/FPSRoguelite/Config/DefaultEditor.ini", encoding="utf-8-sig").read()
def val(key):
    m = re.search(r"^%s=(.+)$" % key, ini, re.M)
    return m.group(1).strip().split(".")[0] if m else None
mp, pp = val("PreviewArmsMesh"), val("PreviewArmsPose")
log("ini PreviewArmsMesh = %s" % mp)
log("ini PreviewArmsPose = %s" % pp)
ok = True
# 설정 클래스가 노출되는지도 같이 본다(되면 좋고, 안 돼도 툴 동작과는 무관)
cls = unreal.load_class(None, "/Script/FPSRogueliteEditor.FPSRWeaponAssemblerSettings")
log("설정 클래스 로드: %s" % ("성공 — 에디터 모듈 올라옴 ✅" if cls else "실패(파이썬 노출 없음 — 툴 동작과 무관)"))
m = unreal.EditorAssetLibrary.load_asset(mp) if mp else None
p = unreal.EditorAssetLibrary.load_asset(pp) if pp else None
if not m: log("!! 팔 메시를 못 연다"); ok = False
if not p: log("!! 포즈를 못 연다"); ok = False
if m and p:
    ms, ps = m.get_editor_property("skeleton"), p.get_editor_property("skeleton")
    same = ms == ps
    log("팔 스켈레톤 %s / 포즈 스켈레톤 %s -> %s" % (ms.get_name(), ps.get_name(), "일치 ✅" if same else "**불일치 ❌**"))
    ok = ok and same
    has = m.find_socket("SOCKET_Weapon") is not None
    log("팔에 SOCKET_Weapon: %s" % ("있음 ✅" if has else "**없음 ❌ '손 위치 저장'이 실패한다**"))
    ok = ok and has
    da = unreal.EditorAssetLibrary.load_asset("/Game/Weapons/DataTable/DA_Weapon_Rifle")
    if da:
        log("무기 DA 부착소켓 = %s · 부착스케일 %.2f"
            % (da.get_editor_property("weapon_attach_socket"), da.get_editor_property("weapon_attach_scale")))
log("ASM_OK" if ok else "ASM_FAIL")
