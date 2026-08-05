"""PWAS(S_Mannequin, UE5) -> LPAMG 팔(SKEL_LPAMG_Character, UE4 규약) 리타게팅 경로를 만든다.

왜 리타게터가 필요한가 — 두 스켈레톤은 **이름 규약은 같은데 계층이 다르다**(실측):
  · 마네킹에만 있는 본 14개 — 메타카팔 8 · lowerarm/upperarm_twist_02 4 · spine_04/05
  · 이름은 같은데 부모가 다른 본 10개 — clavicle(spine_05 vs spine_03) · 손가락 01
    (마네킹은 메타카팔을 거치고 LPAMG 는 hand 직결)
그래서 애니를 그냥 틀 수 없고, 체인 대 체인으로 옮겨야 한다. UE4<->UE5 마네킹은 엔진이
가장 잘 지원하는 경로다.

🚨 체인의 **뼈 이름을 하드코딩하지 않는다.** 같은 체인이라도 시작 뼈가 리그마다 다르다
   (index 는 마네킹이 metacarpal, LPAMG 가 01 부터). 각 리그에서 있는 뼈로 유도한다 —
   안 그러면 한쪽에서 조용히 빈 체인이 만들어지고 손가락이 안 따라온다.

실행: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<이 파일>" -unattended -nopause
"""
import json
import os

import unreal

SRC_MESH = "/Game/ProceduralWeaponAnimationSystem/Demo/FPManny/SK_FP_Manny_Simple"
TGT_MESH = "/Game/LowPolyAnimatedModernGuns/Art/Characters/Meshes/SK_LPAMG_Arms_Base_Smooth"
OUT_DIR = "/Game/Character/FPArms/Retarget"
ANIM_OUT = "/Game/Character/FPArms/Anims_LPAMG"
ANIMS = [
    "/Game/ProceduralWeaponAnimationSystem/Animations/Poses/Rifle/A_FP_Rifle_Pose",
    "/Game/ProceduralWeaponAnimationSystem/Animations/Poses/Rifle/A_FP_Rifle_AimPose",
    "/Game/ProceduralWeaponAnimationSystem/Animations/Reload/A_FP_RifleReload",
]
REPORT = "E:/Git_Project/FPSRoguelite/Saved/NeonV/lpamg_retarget.json"

report, fails = {}, []


def log(m):
    unreal.log("[RTG] %s" % m)


def fail(m):
    fails.append(m)
    unreal.log_error("[RTG] !! %s" % m)


def bones_of(mesh):
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    a = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0),
                                   unreal.Rotator(0, 0, 0))
    a.set_actor_label("TEMP_RtgBones")
    try:
        c = a.skeletal_mesh_component
        c.set_skeletal_mesh_asset(mesh)
        return [str(c.get_bone_name(i)) for i in range(c.get_num_bones())]
    finally:
        eas.destroy_actor(a)


def first_present(names, cands):
    for c in cands:
        if c in names:
            return c
    return None


def chain_defs(names):
    """체인 이름 -> (시작뼈, 끝뼈). 없는 체인은 뺀다."""
    out = {}
    spine = [b for b in ("spine_01", "spine_02", "spine_03", "spine_04", "spine_05") if b in names]
    if spine:
        out["Spine"] = (spine[0], spine[-1])
    for s in ("l", "r"):
        tag = s.upper()
        if ("clavicle_%s" % s) in names:
            out["Clavicle" + tag] = ("clavicle_%s" % s, "clavicle_%s" % s)
        if ("upperarm_%s" % s) in names and ("hand_%s" % s) in names:
            out["Arm" + tag] = ("upperarm_%s" % s, "hand_%s" % s)
        for f in ("thumb", "index", "middle", "ring", "pinky"):
            start = first_present(names, ["%s_metacarpal_%s" % (f, s), "%s_01_%s" % (f, s)])
            end = first_present(names, ["%s_03_%s" % (f, s), "%s_02_%s" % (f, s)])
            if start and end:
                out[f.capitalize() + tag] = (start, end)
    return out


def make_ik_rig(pkg_name, mesh, label):
    path = "%s/%s" % (OUT_DIR, pkg_name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    rig = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        pkg_name, OUT_DIR, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    if rig is None:
        fail("%s 생성 실패" % pkg_name)
        return None, {}
    ctrl = unreal.IKRigController.get_controller(rig)
    ctrl.set_skeletal_mesh(mesh)
    names = bones_of(mesh)
    root = first_present(names, ["pelvis", "root"])
    ctrl.set_retarget_root(root)
    chains = chain_defs(names)
    for cname, (a, b) in sorted(chains.items()):
        ctrl.add_retarget_chain(cname, a, b, "None")
    log("%-16s 본 %3d · 리타겟루트 %s · 체인 %d개" % (label, len(names), root, len(chains)))
    unreal.EditorAssetLibrary.save_loaded_asset(rig)
    return rig, chains


src_mesh = unreal.EditorAssetLibrary.load_asset(SRC_MESH)
tgt_mesh = unreal.EditorAssetLibrary.load_asset(TGT_MESH)
if src_mesh is None or tgt_mesh is None:
    fail("소스/타깃 메시를 못 연다")
    raise SystemExit(1)
unreal.EditorAssetLibrary.make_directory(OUT_DIR)
unreal.EditorAssetLibrary.make_directory(ANIM_OUT)

src_rig, src_chains = make_ik_rig("IK_PWASManny", src_mesh, "소스(PWAS)")
tgt_rig, tgt_chains = make_ik_rig("IK_LPAMGArms", tgt_mesh, "타깃(LPAMG)")
if src_rig is None or tgt_rig is None:
    raise SystemExit(1)

only_src = sorted(set(src_chains) - set(tgt_chains))
only_tgt = sorted(set(tgt_chains) - set(src_chains))
report["chains_common"] = sorted(set(src_chains) & set(tgt_chains))
report["chains_only_source"] = only_src
report["chains_only_target"] = only_tgt
log("체인 공통 %d · 소스전용 %s · 타깃전용 %s"
    % (len(report["chains_common"]), only_src or "없음", only_tgt or "없음"))
if only_src or only_tgt:
    fail("체인 집합이 다르다 — 매핑이 비게 된다")

# --- 리타게터 ---
rtg_path = "%s/RTG_PWAS_to_LPAMG" % OUT_DIR
if unreal.EditorAssetLibrary.does_asset_exist(rtg_path):
    unreal.EditorAssetLibrary.delete_asset(rtg_path)
rtg = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    "RTG_PWAS_to_LPAMG", OUT_DIR, unreal.IKRetargeter, unreal.IKRetargetFactory())
if rtg is None:
    fail("리타게터 생성 실패")
    raise SystemExit(1)
rc = unreal.IKRetargeterController.get_controller(rtg)
rc.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, src_rig)
rc.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, tgt_rig)
rc.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
unreal.EditorAssetLibrary.save_loaded_asset(rtg)
log("리타게터 생성 · 체인 자동매핑(EXACT)")

# --- 애니 3개 복제+리타게팅 ---
ar = unreal.AssetRegistryHelpers.get_asset_registry()
assets = []
for p in ANIMS:
    d = ar.get_asset_by_object_path(p + "." + p.rsplit("/", 1)[-1])
    if d is None or not d.is_valid():
        fail("애니 %s 를 못 찾았다" % p)
        continue
    assets.append(d)
log("리타게팅 대상 %d개" % len(assets))

made = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
    assets, src_mesh, tgt_mesh, rtg,
    search="", replace="", prefix="", suffix="_LPAMG",
    include_referenced_assets=False, overwrite_existing_files=True)
names = [str(a.package_name) for a in made] if made else []
report["created"] = names
log("생성된 애니 %d개" % len(names))
for n in names:
    log("   %s" % n)
if len(names) < len(assets):
    fail("리타게팅 결과가 대상보다 적다 (%d/%d)" % (len(names), len(assets)))

os.makedirs(os.path.dirname(REPORT), exist_ok=True)
report["fails"] = fails
with open(REPORT, "w", encoding="utf-8") as f:
    json.dump(report, f, ensure_ascii=False, indent=1)
log("기록 -> %s" % REPORT)
log("RTG_FAIL" if fails else "RTG_DONE")
