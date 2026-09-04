# census_weapon_da.py — DA_Weapon_* 전수 조사 (읽기 전용)
#   목적: 정적 무기(weapon_mesh 없음)인데 WeaponParts 가 저작된 DA 가 있는지 — 파츠 가드 확장(WPN1)의 회귀 표면.
#   실행 = Scripts/run_census_weapon_da.bat (헤드리스 정식 에디터, Troubleshooting D11). 임포트/저장 없음.
#   판정 마커 = [census] ALLDONE
import unreal

paths = [p for p in unreal.EditorAssetLibrary.list_assets("/Game/Weapons/DataTable", recursive=True, include_folder=False)
         if "/DA_Weapon_" in p]
print("[census] DA_Weapon_* count=%d" % len(paths))
print("[census] %-22s %-5s %-6s %-16s %-5s %-4s %-10s %-12s" % ("DA", "skel", "static", "attach_socket", "parts", "ADS", "aim_socket", "muzzle"))
risk = []
for p in sorted(paths):
    da = unreal.EditorAssetLibrary.load_asset(p.split(".")[0])
    if not da:
        print("[census] %-22s LOAD FAIL" % p); continue
    skel = da.get_editor_property("weapon_mesh")
    stat = da.get_editor_property("weapon_mesh_static")
    parts = da.get_editor_property("weapon_parts")
    try:
        has_ads = da.get_editor_property("base_stats").get_editor_property("has_ads")
    except Exception:
        has_ads = "?"
    row = (da.get_name(), "Y" if skel else "-", "Y" if stat else "-",
           str(da.get_editor_property("weapon_attach_socket")), len(parts), has_ads,
           str(da.get_editor_property("aim_socket")), str(da.get_editor_property("muzzle_socket")))
    print("[census] %-22s %-5s %-6s %-16s %-5d %-4s %-10s %-12s" % row)
    if not skel and len(parts) > 0:
        risk.append(da.get_name())
print("[census] STATIC-WITH-PARTS (guard change would newly attach parts): %s" % (risk if risk else "none"))
print("[census] ALLDONE")
unreal.SystemLibrary.quit_editor()
