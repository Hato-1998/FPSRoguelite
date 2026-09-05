# author_voxel_chomper_material.py — 복셀 유령 "쩝쩝이" 최소 머티리얼 + MI + 메시 슬롯 배선
#
# 실행(에디터 닫고, 커맨드렛 — 머티리얼 저작은 Slate 불필요):
#   "D:\UnrealEngine\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "E:\Git_Project\FPSRoguelite\FPSRoguelite.uproject"
#     -run=pythonscript -script="E:\Git_Project\FPSRoguelite\Scripts\author_voxel_chomper_material.py"
#     -unattended -nopause -nullrhi -nosplash -nosound
#   (import_voxel_chomper.py 가 먼저 돌아 SM 이 있어야 슬롯 배선까지 된다. 없으면 머티리얼만 만든다.)
#
# 무엇을 하나
#   M_FPSREnemyVoxelChomper  — 요소 ID(floor(UV.u)) → 색 LUT(요소별 벡터 파라미터) + 복셀 격자 이음선(UV frac)
#                              + 코어 이미시브 + **턱 개폐 WPO**(JawOpen 0..1 → JAW 그룹 -Z JawDropCm).
#   MI_EnemyVoxel_Chomper    — 위 머티리얼의 인스턴스(오버라이드 없음). 색·선폭 조정은 여기서(사용자).
#   SM_EnemyVoxel_Chomper    — 슬롯 0 에 MI 배선.
#
# 요소 ID / 그룹 계약 정본 = Scripts/gen_voxel_chomper.py 헤더. JAW = {1,4,8,9,10,11}.
# JawOpen 은 지금은 **스칼라 파라미터**(에디터에서 슬라이더로 뻐끔 확인용). 런타임 공격 연동은 후속 —
#   FPSRAnimCPDParams.h 슬롯 0(StateId)·1(EnterTime)·2(Rate) 로 JawOpen 을 (Time-EnterTime)*Rate 의 함수로 바꾸면 된다
#   (author_proto_state_material.py S4 배선과 같은 방식). 휴식(JawOpen=0) 변위 0 = C0-at-entry 계약 유지.
#
# 멱등: 기존 에셋은 지우고 다시 만든다(이 스크립트가 머티리얼 그래프의 단일 소스 — MI 오버라이드는 별도 에셋이라 안 덮인다).
# API 주의(가르침: gen_arcade_proto_materials.py): CustomInput 은 기본 생성 후 set_editor_property / 미연결 Custom 입력 =
#   컴파일 에러 / connect_* 는 bool 을 돌려준다 — 무시하면 조용히 검정 머티리얼이 된다.
import unreal

OUT = "/Game/Assets/Characters/EnemyVoxel"
MAT_NAME, MI_NAME, SM_NAME = "M_FPSREnemyVoxelChomper", "MI_EnemyVoxel_Chomper", "SM_EnemyVoxel_Chomper"
MEL = unreal.MaterialEditingLibrary
ATH = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
PROBLEMS = []

# gen_voxel_chomper.py 와 맞춘 계약값(생성기 콘솔이 SEAM_Z_CM / JAW_DROP_CM 을 찍는다)
JAW_DROP_CM = 37.5

# 기본색 — 적 대역(ArtDirection A-3-4 뜨거운 쪽) 안에서 미리보기 값과 같게. 확정은 사용자(MI).
COLORS = [
    ("ColorHead",       (0.98, 0.46, 0.76)),
    ("ColorJaw",        (0.92, 0.38, 0.68)),
    ("ColorSclera",     (0.97, 0.97, 1.00)),
    ("ColorPupil",      (0.16, 0.04, 0.14)),   # 어두운 자두 — 아군 파랑 예약(A-3-5) 회피. 파랑 원하면 MI 에서
    ("ColorCore",       (1.00, 0.12, 0.48)),
    ("ColorUpperTooth", (1.00, 1.00, 0.94)),
    ("ColorDark",       (0.17, 0.05, 0.15)),
    ("ColorArm",        (0.95, 0.42, 0.72)),
    ("ColorSkirt",      (0.86, 0.31, 0.63)),
    ("ColorTongue",     (0.96, 0.16, 0.22)),
    ("ColorLowerTooth", (1.00, 1.00, 0.94)),
    ("ColorDarkJaw",    (0.17, 0.05, 0.15)),
]


def log(m):
    unreal.log("[CHOMPER-MAT] " + str(m))


def bad(m):
    unreal.log_warning("[CHOMPER-MAT] !! " + str(m))
    PROBLEMS.append(str(m))


def link(src, src_pin, dst, dst_pin, label):
    ok = MEL.connect_material_expressions(src, src_pin, dst, dst_pin)
    if not ok:
        bad(f"wire failed: {label} (pin '{dst_pin}')")
    return ok


def link_prop(src, prop, label):
    ok = MEL.connect_material_property(src, "", prop)
    if not ok:
        bad("property wire failed: " + label)
    return ok


def vector(mat, pname, rgb, x, y):
    n = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    n.set_editor_property("parameter_name", pname)
    n.set_editor_property("default_value", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    return n


def scalar(mat, pname, value, x, y):
    n = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    n.set_editor_property("parameter_name", pname)
    n.set_editor_property("default_value", value)
    return n


def custom_node(mat, desc, code, input_names, out_type, x, y):
    ex = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    ex.set_editor_property("description", desc)
    ex.set_editor_property("code", code)
    ex.set_editor_property("output_type", out_type)
    ins = []
    for n in input_names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    ex.set_editor_property("inputs", ins)
    have = [str(ci.get_editor_property("input_name")) for ci in ex.get_editor_property("inputs")]
    if have != list(input_names):
        bad(f"Custom '{desc}' input list mismatch: wanted {input_names} got {have}")
    return ex


def fresh_asset(name, cls, factory):
    path = f"{OUT}/{name}"
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    a = ATH.create_asset(name, OUT, cls, factory)
    if a is None:
        bad("create_asset failed: " + name)
    return a


def build_material():
    mat = fresh_asset(MAT_NAME, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        return None

    uv = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1900, -600)

    # 색 LUT ------------------------------------------------------------------------------------
    color_nodes = [vector(mat, n, c, -1900, -400 + i * 110) for i, (n, c) in enumerate(COLORS)]
    lut_inputs = ["UV"] + [f"C{i}" for i in range(len(COLORS))]
    lut_code = "int id = (int)floor(UV.x);\nfloat3 c = C0;\n" + "".join(
        f"if (id == {i}) c = C{i};\n" for i in range(1, len(COLORS))) + "return c;"
    lut = custom_node(mat, "ElemColor", lut_code, lut_inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT3, -1300, -200)
    link(uv, "", lut, "UV", "UV->ElemColor")
    for i, cn in enumerate(color_nodes):
        link(cn, "", lut, f"C{i}", f"{COLORS[i][0]}->ElemColor")

    # 복셀 격자 이음선(레퍼런스 램프의 타일 결) — 면마다 UV 가 [id+0.01, id+0.99]×[0.01,0.99] 한 타일이라
    # frac 가장자리 = 복셀 경계. 가장자리 폭 LineWidth(0.05 = 복셀 7.5cm 의 ~0.4mm… 실제로는 화면상 픽셀 몇 개).
    p_lw = scalar(mat, "LineWidth", 0.06, -1900, 1000)
    p_ld = scalar(mat, "LineDarken", 0.35, -1900, 1100)
    p_le = scalar(mat, "LineEmissive", 0.0, -1900, 1200)
    c_line = vector(mat, "ColorLine", (1.0, 0.55, 0.85), -1900, 1300)
    grid = custom_node(mat, "GridMask",
                       "float2 f = frac(UV);\nfloat2 e = min(f, 1.0 - f);\nreturn (min(e.x, e.y) < W) ? 1.0 : 0.0;",
                       ["UV", "W"], unreal.CustomMaterialOutputType.CMOT_FLOAT1, -1300, 1000)
    link(uv, "", grid, "UV", "UV->GridMask")
    link(p_lw, "", grid, "W", "LineWidth->GridMask")

    base = custom_node(mat, "BaseShade", "return Col * (1.0 - Mask * Darken);",
                       ["Col", "Mask", "Darken"], unreal.CustomMaterialOutputType.CMOT_FLOAT3, -800, -200)
    link(lut, "", base, "Col", "ElemColor->BaseShade")
    link(grid, "", base, "Mask", "GridMask->BaseShade")
    link(p_ld, "", base, "Darken", "LineDarken->BaseShade")

    # 이미시브: 코어(요소 4) + 선 발광(옵션, 기본 0) ------------------------------------------------
    p_ce = scalar(mat, "CoreEmissive", 6.0, -1900, 1400)
    emis = custom_node(mat, "ChomperEmissive",
                       "int id = (int)floor(UV.x);\n"
                       "float3 e = (id == 4) ? CoreColor * CoreEmissive : float3(0, 0, 0);\n"
                       "return e + LineColor * LineEmissive * Mask;",
                       ["UV", "CoreColor", "CoreEmissive", "LineColor", "LineEmissive", "Mask"],
                       unreal.CustomMaterialOutputType.CMOT_FLOAT3, -800, 300)
    link(uv, "", emis, "UV", "UV->Emissive")
    link(color_nodes[4], "", emis, "CoreColor", "ColorCore->Emissive")
    link(p_ce, "", emis, "CoreEmissive", "CoreEmissive->Emissive")
    link(c_line, "", emis, "LineColor", "ColorLine->Emissive")
    link(p_le, "", emis, "LineEmissive", "LineEmissive->Emissive")
    link(grid, "", emis, "Mask", "GridMask->Emissive")

    # 턱 개폐 WPO: JAW 그룹 = {1,4,8,9,10,11} (gen_voxel_chomper.py 계약) -----------------------------
    p_open = scalar(mat, "JawOpen", 0.0, -1900, 1550)
    p_drop = scalar(mat, "JawDropCm", JAW_DROP_CM, -1900, 1650)
    wpo = custom_node(mat, "ChomperWPO",
                      "int id = (int)floor(UV.x);\n"
                      "bool jaw = (id == 1 || id == 4 || id == 8 || id == 9 || id == 10 || id == 11);\n"
                      "return jaw ? float3(0, 0, -DropCm * saturate(Open)) : float3(0, 0, 0);",
                      ["UV", "Open", "DropCm"], unreal.CustomMaterialOutputType.CMOT_FLOAT3, -800, 800)
    link(uv, "", wpo, "UV", "UV->WPO")
    link(p_open, "", wpo, "Open", "JawOpen->WPO")
    link(p_drop, "", wpo, "DropCm", "JawDropCm->WPO")

    p_rough = scalar(mat, "Roughness", 0.6, -800, 1200)

    link_prop(base, unreal.MaterialProperty.MP_BASE_COLOR, "BaseColor")
    link_prop(emis, unreal.MaterialProperty.MP_EMISSIVE_COLOR, "Emissive")
    link_prop(wpo, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET, "WPO")
    link_prop(p_rough, unreal.MaterialProperty.MP_ROUGHNESS, "Roughness")

    MEL.recompile_material(mat)
    EAL.save_asset(mat.get_path_name())
    log(f"{MAT_NAME} built")
    return mat


def build_mi(mat):
    mi = fresh_asset(MI_NAME, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    if mi is None:
        return None
    MEL.set_material_instance_parent(mi, mat)
    EAL.save_asset(mi.get_path_name())
    log(f"{MI_NAME} built (no overrides)")
    return mi


def wire_mesh(mi):
    path = f"{OUT}/{SM_NAME}"
    if not EAL.does_asset_exist(path):
        bad(f"{path} 없음 — import_voxel_chomper.py 를 먼저 돌릴 것(머티리얼만 만들었다)")
        return
    sm = unreal.load_asset(path)
    if sm.get_num_sections(0) != 1:
        bad(f"{SM_NAME} sections={sm.get_num_sections(0)} (1 이어야 한다 — ADR 0007)")
    sm.set_material(0, mi)
    EAL.save_asset(path)
    log(f"{SM_NAME} slot0 -> {MI_NAME}  (tris={sm.get_num_triangles(0)}, verts={sm.get_num_vertices(0)})")


mat = build_material()
if mat:
    mi = build_mi(mat)
    if mi:
        wire_mesh(mi)
if PROBLEMS:
    unreal.log_warning(f"[CHOMPER-MAT] {len(PROBLEMS)} problem(s): " + " | ".join(PROBLEMS))
else:
    log("DONE errors=0")
