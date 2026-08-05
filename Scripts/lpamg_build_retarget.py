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

# 🚨 **체인 매핑은 op 에 속한다.** `auto_map_chains(..., InOpName)` 의 op 이름을 안 주면
#    (기본 NAME_None) 기본 `FK Chains` op 에 **안 붙는다** — 그런데 크래시도 경고도 없고,
#    내보내기는 멀쩡히 돌아서 **정적 포즈만 찍힌다**(실측: Idle/ADS/Reload 세 개가 전부
#    같은 값 -56.65/-0.34/111.68 · 서로 다른 애니의 압축 DDC 해시까지 동일).
#    새 리타게터에는 기본 op 6개가 이미 들어 있다(Pelvis Motion / FK Chains / Retarget IK
#    Goals / Run IK Rig / Root Motion / Remap Curves) — **op 을 더 넣지 말고 그걸 찾아 쓴다.**
n_ops = rc.get_num_retarget_ops()
ops = [str(rc.get_op_name(i)) for i in range(n_ops)]
log("기본 op %d개: %s" % (n_ops, ops))
report["ops"] = ops
fk = next((o for o in ops if o.lower().startswith("fk chains")), None)
if not fk:
    fail("FK Chains op 이 없다 — 팔이 안 움직인다")
else:
    # 🚨 `auto_map_chains` 는 스크립트에서 **아무것도 매핑하지 않는다**(실측 0/15 — 체인
    #    이름이 양쪽 완전히 같은데도). 크래시도 경고도 없어서, 매핑이 빈 채로 내보내면
    #    정적 포즈만 나온다. 그러니 **이름으로 직접 건다**. 이름이 같은 것끼리라 안전하다.
    rc.auto_map_chains(unreal.AutoMapChainType.EXACT, True, fk)   # 되면 좋고
    common = sorted(set(src_chains) & set(tgt_chains))
    for cname in common:
        rc.set_source_chain(cname, cname, fk)
    # 🔑 매핑 결과를 **읽어서 게이트로 건다** — 이번 사고의 재발 방지선이다.
    mapped = []
    for cname in common:
        got = rc.get_source_chain(cname, fk)
        if got is not None and str(got) == cname:
            mapped.append(cname)
    log("체인 매핑 %d/%d (op '%s')" % (len(mapped), len(common), fk))
    report["mapped_chains"] = mapped
    if len(mapped) != len(common):
        fail("체인 매핑이 %d/%d 밖에 안 됐다 — 이대로 내보내면 정적 포즈가 나온다"
             % (len(mapped), len(common)))
report["num_ops"] = n_ops
unreal.EditorAssetLibrary.save_loaded_asset(rtg)

# 🪤 내보내기는 여기서 안 한다 — `DuplicateAndRetarget` 은 Slate 를 요구해 커맨드렛에서 죽는다.
#    `Scripts/lpamg_export_retargeted_anims.py` 를 정식 에디터로 돌린다.
os.makedirs(os.path.dirname(REPORT), exist_ok=True)
report["fails"] = fails
with open(REPORT, "w", encoding="utf-8") as f:
    json.dump(report, f, ensure_ascii=False, indent=1)
log("기록 -> %s" % REPORT)
log("RTG_FAIL" if fails else "RTG_DONE")
