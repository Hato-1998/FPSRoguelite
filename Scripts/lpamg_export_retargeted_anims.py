"""이미 만들어 둔 리타게터로 PWAS 애니를 LPAMG 팔로 복제한다.

🚨 **커맨드렛에서 돌리면 죽는다.** `UIKRetargetBatchOperation::DuplicateAndRetarget` 이
   진행 대화상자를 띄우려고 Slate 를 건드리는데 커맨드렛에는 Slate 앱이 없다
   (실측: `Assertion failed: CurrentApplication.IsValid()` SlateApplication.h:321).
   리타게팅 계산 자체는 끝나고 압축까지 갔는데 **저장 직전에 죽어서** 결과가 안 남았다.
   그래서 이건 **정식 에디터**에서 돌려야 한다:
     UnrealEditor.exe <uproject> -ExecutePythonScript="<이 파일>"
   또는 에디터 파이썬 콘솔에서 이 파일을 실행.

🪤 `DuplicateAndRetarget` 에는 출력 폴더 인자가 없다 — 결과가 `/Game/` 루트에 떨어진다.
   그래서 만든 뒤 옮긴다.
"""
import unreal

RTG = "/Game/Character/FPArms/Retarget/RTG_PWAS_to_LPAMG"
SRC_MESH = "/Game/ProceduralWeaponAnimationSystem/Demo/FPManny/SK_FP_Manny_Simple"
TGT_MESH = "/Game/LowPolyAnimatedModernGuns/Art/Characters/Meshes/SK_LPAMG_Arms_Base_Smooth"
ANIM_OUT = "/Game/Character/FPArms/Anims_LPAMG"
SUFFIX = "_LPAMG"
ANIMS = [
    "/Game/ProceduralWeaponAnimationSystem/Animations/Poses/Rifle/A_FP_Rifle_Pose",
    "/Game/ProceduralWeaponAnimationSystem/Animations/Poses/Rifle/A_FP_Rifle_AimPose",
    "/Game/ProceduralWeaponAnimationSystem/Animations/Reload/A_FP_RifleReload",
]


def log(m):
    unreal.log("[RTGEXP] %s" % m)


rtg = unreal.EditorAssetLibrary.load_asset(RTG)
src = unreal.EditorAssetLibrary.load_asset(SRC_MESH)
tgt = unreal.EditorAssetLibrary.load_asset(TGT_MESH)
if not (rtg and src and tgt):
    unreal.log_error("[RTGEXP] !! 리타게터/메시를 못 연다 — lpamg_build_retarget.py 를 먼저 돌려라")
    raise SystemExit(1)

unreal.EditorAssetLibrary.make_directory(ANIM_OUT)
ar = unreal.AssetRegistryHelpers.get_asset_registry()
assets = []
for p in ANIMS:
    d = ar.get_asset_by_object_path(p + "." + p.rsplit("/", 1)[-1])
    if d and d.is_valid():
        assets.append(d)
    else:
        unreal.log_error("[RTGEXP] !! %s 없음" % p)
log("대상 %d개" % len(assets))

made = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
    assets, src, tgt, rtg, search="", replace="", prefix="", suffix=SUFFIX,
    include_referenced_assets=False, overwrite_existing_files=True)
names = [str(a.package_name) for a in (made or [])]
log("생성 %d개: %s" % (len(names), names))

moved = 0
for pkg in names:
    leaf = pkg.rsplit("/", 1)[-1]
    dst = "%s/%s" % (ANIM_OUT, leaf)
    if pkg == dst:
        moved += 1
        continue
    if unreal.EditorAssetLibrary.does_asset_exist(dst):
        unreal.EditorAssetLibrary.delete_asset(dst)
    if unreal.EditorAssetLibrary.rename_asset(pkg, dst):
        moved += 1
        log("  이동 %s -> %s" % (pkg, dst))
    else:
        unreal.log_error("[RTGEXP] !! 이동 실패 %s" % pkg)

unreal.EditorAssetLibrary.save_directory(ANIM_OUT, only_if_is_dirty=True, recursive=True)
log("완료 — 생성 %d · 이동 %d -> %s" % (len(names), moved, ANIM_OUT))
log("RTGEXP_DONE")
