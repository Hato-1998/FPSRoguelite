"""리타게팅 결과 이름을 프로젝트 규약으로 바꾼다.

`_LPAMG` 접미사는 "LPAMG 팩 애니" 로 읽혀서 오해를 부른다(실제로 사용자가 그렇게 읽었다).
이 애니들의 **동작은 PWAS 원본**이고 바뀐 건 담는 스켈레톤뿐이다. 그래서 출처가 아니라
쓰임으로 이름 짓는다 — 기존 `Content/Character/FPArms/Anims/FP_Rifle_{Idle,ADS}` 와 같은 규약.
"""
import unreal
D = "/Game/Character/FPArms/Anims_LPAMG/"
REN = {"A_FP_Rifle_Pose_LPAMG": "FP_Rifle_Idle",
       "A_FP_Rifle_AimPose_LPAMG": "FP_Rifle_ADS",
       "A_FP_RifleReload_LPAMG": "FP_Rifle_Reload"}
for old, new in REN.items():
    src, dst = D + old, D + new
    if not unreal.EditorAssetLibrary.does_asset_exist(src):
        unreal.log_warning("[REN] 없음 %s" % src); continue
    if unreal.EditorAssetLibrary.does_asset_exist(dst):
        unreal.EditorAssetLibrary.delete_asset(dst)
    ok = unreal.EditorAssetLibrary.rename_asset(src, dst)
    unreal.log("[REN] %s -> %s  %s" % (old, new, "OK" if ok else "실패"))
unreal.EditorAssetLibrary.save_directory(D, only_if_is_dirty=True, recursive=True)
unreal.log("[REN] REN_DONE")
