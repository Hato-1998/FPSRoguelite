# author_fparms_handsonly_material.py — 1인칭 팔을 손목에서 잘라 손만 그리는 마스크 머티리얼을 저작한다 (ARM1)
#   실행 = 헤드리스 정식 에디터(run_author_fparms_handsonly_material.bat, Troubleshooting D11) 또는 켜진 에디터에서 exec.
#   판정은 종료 코드가 아니라 마커 "[hands] result=OK" + "[hands] ALLDONE" 으로(Troubleshooting C4).
#
# 왜 머티리얼인가: 팔 메시 SK_LPAMG_Arms_Base_Smooth 는 섹션이 Arms 하나(+Nails)라 섹션 숨기기로 팔/손을 못 가르고,
# 손은 전완의 자식 본이라 HideBone 도 손까지 지운다. 스키닝 전 정점 위치(PreSkinnedPosition = 바인드 포즈, 컴포넌트 공간)로
# 팔마다 손목 평면을 하나씩 두고 전완 쪽을 클립한다. 절단면 속이 비치지 않게 양면 렌더.
#
# 수치 출처(2026-09-04 실측, SKEL_LPAMG_Character 기준 포즈 컴포넌트 공간, cm):
#   왼손목 hand_l = (56.6, -0.3, 111.7), 전완 방향(팔꿈치→손목) = (0.72, 0.43, -0.55). 오른쪽은 x 부호 반전.
#   조정은 파라미터로만: HandsOnly_CutOffset(+ = 손가락 쪽으로 절단 이동, cm). 값은 머티리얼 인스턴스에서 바꾼다.
import unreal, traceback

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
SRC_M  = "/Game/LowPolyAnimatedModernGuns/Art/Characters/Materials/Arms/M_LPAMG_Character_Base"
SRC_MI = "/Game/LowPolyAnimatedModernGuns/Art/Characters/Materials/Arms/MI_LPAMG_Character_Skin_002"
DIR    = "/Game/Character/FPArms/Materials"
DST_M  = DIR + "/M_FPArms_HandsOnly"
DST_MI = DIR + "/MI_FPArms_HandsOnly"

WRIST_L = (56.6, -0.3, 111.7); DIR_L = (0.72, 0.43, -0.55)
WRIST_R = (-56.6, -0.3, 111.7); DIR_R = (-0.72, 0.43, -0.55)
CUT_OFFSET_CM = 1.0

def say(msg):
    unreal.log("[hands] " + msg)

def lc(v):
    return unreal.LinearColor(v[0], v[1], v[2], 0.0)

ok = True
fails = []
try:
    if not EAL.does_directory_exist(DIR):
        say("mkdir=%s" % EAL.make_directory(DIR))
    # 마스터 경로는 추정하지 않고 팩 스킨 MI 의 base material 에서 읽는다
    # (2026-09-04 1차 실행: 'Arms/' 폴더로 추정했다가 DuplicateAsset 이 소스를 못 찾았다 — 실물은 'Materials/' 바로 아래).
    if not EAL.does_asset_exist(SRC_MI):
        raise RuntimeError("pack skin MI missing: " + SRC_MI)
    SRC_M = EAL.load_asset(SRC_MI).get_base_material().get_path_name().split(".")[0]
    say("master source resolved from MI: %s" % SRC_M)
    if EAL.does_asset_exist(DST_M):
        mat = EAL.load_asset(DST_M); say("master: reuse %s" % DST_M)
    else:
        mat = EAL.duplicate_asset(SRC_M, DST_M); say("master: duplicated=%s" % (mat is not None))
    if not mat:
        raise RuntimeError("master missing")
    if mat.get_editor_property("use_material_attributes"):
        # 머티리얼 어트리뷰트 경로면 OpacityMask 를 직접 못 꽂는다(Troubleshooting material-attributes-pin-trap) — 사람이 본다.
        raise RuntimeError("master uses material attributes; wire OpacityMask via SetMaterialAttributes instead")

    def link(a, ao, b, bi):
        # 이름을 못 찾으면 조용히 False 다(Troubleshooting D10) — 모아서 판정에 넣는다.
        r = MEL.connect_material_expressions(a, ao, b, bi)
        if not r:
            fails.append("%s.%s -> %s.%s" % (a.get_name(), ao, b.get_name(), bi))
        return r

    def mk(cls, x, y, **props):
        e = MEL.create_material_expression(mat, cls, x, y)
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    have = [str(n) for n in MEL.get_scalar_parameter_names(mat)]
    if "HandsOnly_CutOffset" in have:
        say("wiring: already present, skipped (idempotent)")
    else:
        X, Y = -2400, 900
        pos    = mk(unreal.MaterialExpressionPreSkinnedPosition, X, Y, desc="bind-pose vertex position (component space)")
        wristL = mk(unreal.MaterialExpressionVectorParameter, X, Y + 150, parameter_name="HandsOnly_WristL", default_value=lc(WRIST_L), group="HandsOnly")
        dirL   = mk(unreal.MaterialExpressionVectorParameter, X, Y + 300, parameter_name="HandsOnly_ForearmDirL", default_value=lc(DIR_L), group="HandsOnly")
        wristR = mk(unreal.MaterialExpressionVectorParameter, X, Y + 450, parameter_name="HandsOnly_WristR", default_value=lc(WRIST_R), group="HandsOnly")
        dirR   = mk(unreal.MaterialExpressionVectorParameter, X, Y + 600, parameter_name="HandsOnly_ForearmDirR", default_value=lc(DIR_R), group="HandsOnly")
        cut    = mk(unreal.MaterialExpressionScalarParameter, X, Y + 750, parameter_name="HandsOnly_CutOffset", default_value=CUT_OFFSET_CM, group="HandsOnly",
                    desc="cm along the forearm: + moves the cut toward the fingers")
        subL = mk(unreal.MaterialExpressionSubtract, X + 300, Y + 100); dotL = mk(unreal.MaterialExpressionDotProduct, X + 520, Y + 100)
        subR = mk(unreal.MaterialExpressionSubtract, X + 300, Y + 450); dotR = mk(unreal.MaterialExpressionDotProduct, X + 520, Y + 450)
        mx   = mk(unreal.MaterialExpressionMax, X + 760, Y + 270)
        d    = mk(unreal.MaterialExpressionSubtract, X + 960, Y + 270, desc="signed cm past the wrist plane (either arm)")
        step = mk(unreal.MaterialExpressionStep, X + 1160, Y + 270, const_y=0.0, desc="1 = hand side, 0 = forearm side -> OpacityMask")
        # keep = dot(P - Wrist, ForearmDir) - CutOffset >= 0  (either arm)
        link(pos, "", subL, "A"); link(wristL, "", subL, "B"); link(subL, "", dotL, "A"); link(dirL, "", dotL, "B")
        link(pos, "", subR, "A"); link(wristR, "", subR, "B"); link(subR, "", dotR, "A"); link(dirR, "", dotR, "B")
        link(dotL, "", mx, "A"); link(dotR, "", mx, "B")
        link(mx, "", d, "A"); link(cut, "", d, "B")
        link(d, "", step, "X")
        if not MEL.connect_material_property(step, "", unreal.MaterialProperty.MP_OPACITY_MASK):
            fails.append("step -> OpacityMask")
        say("wiring: link fails=%s" % (fails or "none"))

    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", True)
    MEL.recompile_material(mat)
    say("saved master=%s" % EAL.save_asset(DST_M, only_if_is_dirty=False))

    # 검증 — 재조회: OpacityMask 입력 노드 클래스 · 블렌드 · 양면 · 파라미터 이름
    inp = MEL.get_material_property_input_node(mat, unreal.MaterialProperty.MP_OPACITY_MASK)
    inp_cls = inp.get_class().get_name() if inp else None
    scalars = [str(n) for n in MEL.get_scalar_parameter_names(mat)]
    vectors = [str(n) for n in MEL.get_vector_parameter_names(mat)]
    say("verify master: opacity_mask_input=%s blend=%s two_sided=%s" % (inp_cls, mat.get_editor_property("blend_mode"), mat.get_editor_property("two_sided")))
    say("verify params: scalars=%s vectors=%s" % (scalars, vectors))
    if inp_cls != "MaterialExpressionStep" or fails or mat.get_editor_property("blend_mode") != unreal.BlendMode.BLEND_MASKED:
        ok = False
    for want in ("HandsOnly_CutOffset",):
        if want not in scalars: ok = False
    for want in ("HandsOnly_WristL", "HandsOnly_ForearmDirL", "HandsOnly_WristR", "HandsOnly_ForearmDirR"):
        if want not in vectors: ok = False

    # MI — 팩 스킨 인스턴스를 복제해 새 마스터로 재부모(파라미터 이름이 같아 텍스처·색이 그대로 따라온다)
    if EAL.does_asset_exist(DST_MI):
        mi = EAL.load_asset(DST_MI); say("MI: reuse %s" % DST_MI)
    else:
        mi = EAL.duplicate_asset(SRC_MI, DST_MI); say("MI: duplicated=%s" % (mi is not None))
    if not mi:
        raise RuntimeError("MI missing")
    MEL.set_material_instance_parent(mi, mat)
    MEL.update_material_instance(mi)
    say("saved MI=%s" % EAL.save_asset(DST_MI, only_if_is_dirty=False))
    parent = mi.get_editor_property("parent")
    say("verify MI: parent=%s tex=%s" % (parent.get_name() if parent else None,
        [(str(p.parameter_info.name), p.parameter_value.get_name() if p.parameter_value else None) for p in mi.get_editor_property("texture_parameter_values")]))
    if not parent or parent.get_path_name().split(".")[0] != DST_M:
        ok = False
except Exception:
    ok = False
    say("EXCEPTION:\n" + traceback.format_exc())
finally:
    say("result=%s" % ("OK" if ok else "FAIL"))
    say("ALLDONE")
    # 헤드리스(.bat, -unattended)일 때만 에디터를 끝낸다 — 켜진 에디터에서 exec 하면 사용자 세션을 닫아 버린다.
    if "-unattended" in unreal.SystemLibrary.get_command_line():
        unreal.SystemLibrary.quit_editor()
