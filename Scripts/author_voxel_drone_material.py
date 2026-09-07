# author_voxel_drone_material.py — 복셀 드론 머티리얼 + MI + 메시 슬롯 배선 (author_voxel_chomper_material.py 복제)
#
# 실행(에디터 닫고, 커맨드렛): Scripts/run_voxel_drone_pipeline.bat 이 import 다음에 부른다.
#   "D:\UnrealEngine\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" <uproject> -run=pythonscript
#     -script="E:\Git_Project\FPSRoguelite\Scripts\author_voxel_drone_material.py" -unattended -nopause -nullrhi -nosplash -nosound
#   ⚠️ nullrhi 커맨드렛은 HLSL 을 컴파일하지 않는다 — Custom 노드 검증은 실 RHI 헤드리스 get_statistics 로(메모리 headless-editor-use-bat-runners).
#
# M_FPSREnemyVoxelDrone — 요소 ID(floor(UV.u)) → 색 LUT 12 + 복셀 격자선 + 코어·라이트 이미시브(텔레그래프 색 보간)
#                         + **로터 회전 WPO**(요소 6, 허브 = floor(UV.v), 로컬 +Z 축 회전 → 월드 벡터 변환).
# MI_EnemyVoxel_Drone   — 인스턴스(오버라이드 0). 색·속도 조정은 여기서(사용자).
# SM_EnemyVoxel_Drone   — 슬롯 0 배선.
# 요소 ID / 허브 인코딩 정본 = Scripts/gen_voxel_drone.py 헤더. 텔레그래프(CPD 상태ID) 연동은 후속 — 지금은 Telegraph 스칼라(0..1).
# 멱등: 기존 에셋을 지우고 다시 만든다. API 주의 = gen_arcade_proto_materials.py 헤더(CustomInput 기본 생성 후 set / 미연결 입력 = 컴파일 에러 / connect_* bool 확인).
import unreal

OUT = "/Game/Assets/Characters/EnemyVoxel"
MAT_NAME, MI_NAME, SM_NAME = "M_FPSREnemyVoxelDrone", "MI_EnemyVoxel_Drone", "SM_EnemyVoxel_Drone"
MEL = unreal.MaterialEditingLibrary
ATH = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
PROBLEMS = []

import os, re
# 허브 중심·로터 속도는 생성기가 OBJ 헤더에 찍는 값을 읽는다(단일 소스). 2026-09-06 실사고: 몸통을 키우며 생성기 HUB_CM 이
# 33.75→41.25 로 바뀌었는데 여기 상수가 남아 로터가 엉뚱한 축으로 돌 뻔했다(실 RHI 검증 로그에서 잡음).
def _from_obj_header(key, default):
    obj = os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "EnemyVoxel", "SM_EnemyVoxel_Drone.obj")
    try:
        with open(obj, encoding="ascii") as fh:
            head = fh.read(400)
        m = re.search(key + r"=([0-9.]+)", head)
        if m:
            return float(m.group(1))
    except OSError:
        pass
    unreal.log_warning(f"[DRONE-MAT] {key} not found in OBJ header — using default {default}")
    return default
HUB_OFFSET_CM = _from_obj_header("HUB_CM", 41.25)          # 기록용(회전 계산엔 더 이상 안 씀)
ROTOR_RATE_TPS = _from_obj_header("ROTOR_RATE_TPS", 3.0)
ROTOR_UV_SPAN_CM = _from_obj_header("ROTOR_UV_SPAN_CM", 60.0)

# 순서 = 요소 ID. 값 = ArtDirection §A/§B-10 번역표(DroneEnemy_ResumePrompt §4). 시안·파랑 없음(§A-3-5 예약).
COLORS = [
    ("ColorBodyTop",   "#3A2748"),
    ("ColorBodySide",  "#2A1E36"),
    ("ColorCoreFrame", "#1A1024"),
    ("ColorCore",      "#FF6B2C"),   # 약점 = 읽힘점(§B-5 텔레그래프 색). 텔레그래프 시 ColorTelegraph 로 보간+펄스
    ("ColorArm",       "#4A2E58"),
    ("ColorHub",       "#2A1E36"),
    ("ColorRotor",     "#B34A70"),   # 몸통(#3A2748)과 확실히 갈리는 밝은 자주 — 사용자 지적 2026-09-07
    ("ColorLight",     "#FF3B4E"),
    ("ColorFin",       "#6E2E44"),
    ("ColorRsv9",      "#3A2748"),
    ("ColorRsv10",     "#3A2748"),
    ("ColorRsv11",     "#3A2748"),
]


def hexc(h):
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (1, 3, 5))


def log(m):
    unreal.log("[DRONE-MAT] " + str(m))


def bad(m):
    unreal.log_warning("[DRONE-MAT] !! " + str(m))
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


def vector(mat, pname, hexcol, x, y):
    n = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    n.set_editor_property("parameter_name", pname)
    r, g, b = hexc(hexcol)
    n.set_editor_property("default_value", unreal.LinearColor(r, g, b, 1.0))
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


F3, F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT3, unreal.CustomMaterialOutputType.CMOT_FLOAT1


def build_material():
    mat = fresh_asset(MAT_NAME, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        return None
    uv = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -2100, -700)

    # 색 LUT --------------------------------------------------------------------------------------------
    color_nodes = [vector(mat, n, c, -2100, -500 + i * 100) for i, (n, c) in enumerate(COLORS)]
    lut_inputs = ["UV"] + [f"C{i}" for i in range(len(COLORS))]
    lut_code = "int id = (int)floor(UV.x);\nfloat3 c = C0;\n" + "".join(
        f"if (id == {i}) c = C{i};\n" for i in range(1, len(COLORS))) + "return c;"
    lut = custom_node(mat, "ElemColor", lut_code, lut_inputs, F3, -1500, -200)
    link(uv, "", lut, "UV", "UV->ElemColor")
    for i, cn in enumerate(color_nodes):
        link(cn, "", lut, f"C{i}", f"{COLORS[i][0]}->ElemColor")

    # 복셀 격자선(면마다 UV 한 타일) ----------------------------------------------------------------------
    p_lw = scalar(mat, "LineWidth", 0.06, -2100, 800)
    p_ld = scalar(mat, "LineDarken", 0.35, -2100, 900)
    grid = custom_node(mat, "GridMask",
                       "if ((int)floor(UV.x) == 6) return 0.0;\n"   # 로터 면의 UV 소수부는 오프셋 인코딩이라 격자선 없음
                       "float2 f = frac(UV);\nfloat2 e = min(f, 1.0 - f);\nreturn (min(e.x, e.y) < W) ? 1.0 : 0.0;",
                       ["UV", "W"], F1, -1500, 800)
    link(uv, "", grid, "UV", "UV->GridMask")
    link(p_lw, "", grid, "W", "LineWidth->GridMask")
    base = custom_node(mat, "BaseShade", "return Col * (1.0 - Mask * Darken);", ["Col", "Mask", "Darken"], F3, -1000, -200)
    link(lut, "", base, "Col", "ElemColor->BaseShade")
    link(grid, "", base, "Mask", "GridMask->BaseShade")
    link(p_ld, "", base, "Darken", "LineDarken->BaseShade")

    # 이미시브: 코어(3)·라이트(7). Telegraph 0..1 → 색을 텔레그래프 색으로 보간 + 펄스 ------------------------
    p_ce = scalar(mat, "CoreEmissive", 8.0, -2100, 1000)
    p_le = scalar(mat, "LightEmissive", 3.0, -2100, 1100)
    p_tel = scalar(mat, "Telegraph", 0.0, -2100, 1200)
    c_tel = vector(mat, "ColorTelegraph", "#FFB347", -2100, 1300)   # 텔레그래프 = 코어보다 더 뜨겁게(밝은 주황) + 펄스
    tnode = MEL.create_material_expression(mat, unreal.MaterialExpressionTime, -2100, 1450)
    emis = custom_node(mat, "DroneEmissive",
                       "int id = (int)floor(UV.x);\n"
                       "float pulse = lerp(1.0, 0.6 + 0.4 * sin(T * 18.0), saturate(Tel));\n"
                       "float3 hot = lerp(CoreCol, TelCol, saturate(Tel));\n"
                       "if (id == 3) return hot * CoreE * pulse;\n"
                       "if (id == 7) return lerp(LightCol, TelCol, saturate(Tel)) * LightE * pulse;\n"
                       "return float3(0, 0, 0);",
                       ["UV", "CoreCol", "LightCol", "TelCol", "CoreE", "LightE", "Tel", "T"], F3, -1000, 400)
    link(uv, "", emis, "UV", "UV->Emissive")
    link(color_nodes[3], "", emis, "CoreCol", "ColorCore->Emissive")
    link(color_nodes[7], "", emis, "LightCol", "ColorLight->Emissive")
    link(c_tel, "", emis, "TelCol", "ColorTelegraph->Emissive")
    link(p_ce, "", emis, "CoreE", "CoreEmissive->Emissive")
    link(p_le, "", emis, "LightE", "LightEmissive->Emissive")
    link(p_tel, "", emis, "Tel", "Telegraph->Emissive")
    link(tnode, "", emis, "T", "Time->Emissive")

    # 로터 회전 WPO: 요소 6, 허브 k = floor(UV.y), 허브 오프셋 d = (frac(UV) - 0.5) × Span (생성기가 UV 소수부에 인코딩).
    # 정점 위치·좌표 변환을 쓰지 않는다 — 2026-09-07 실사고: WorldPos→Local 경로는 정점별 로컬 위치가 상수로 들어와
    # 십자 4개가 통째로 드론 중심을 돌았다. 로컬 오프셋 → Transform(Local→World 벡터) 만 남긴다.
    p_rate = scalar(mat, "RotorRate", ROTOR_RATE_TPS, -2100, 1600)
    p_span = scalar(mat, "RotorUvSpanCm", ROTOR_UV_SPAN_CM, -2100, 1700)
    rot = custom_node(mat, "RotorWPO",
                      "int id = (int)floor(UV.x);\n"
                      "if (id != 6) return float3(0, 0, 0);\n"
                      "int k = (int)floor(UV.y);\n"
                      "float2 d = (frac(UV) - 0.5) * Span;\n"
                      "float a = T * Rate * 6.2831853 + k * 1.5707963;\n"
                      "float2 r = float2(d.x * cos(a) - d.y * sin(a), d.x * sin(a) + d.y * cos(a));\n"
                      "return float3(r - d, 0);",
                      ["UV", "Span", "Rate", "T"], F3, -1500, 1700)
    link(uv, "", rot, "UV", "UV->RotorWPO")
    link(p_span, "", rot, "Span", "RotorUvSpanCm->RotorWPO")
    link(p_rate, "", rot, "Rate", "RotorRate->RotorWPO")
    link(tnode, "", rot, "T", "Time->RotorWPO")
    to_world = MEL.create_material_expression(mat, unreal.MaterialExpressionTransform, -1000, 1700)
    to_world.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    to_world.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    link(rot, "", to_world, "", "RotorWPO->LocalToWorld")

    p_rough = scalar(mat, "Roughness", 0.6, -1000, 1000)
    link_prop(base, unreal.MaterialProperty.MP_BASE_COLOR, "BaseColor")
    link_prop(emis, unreal.MaterialProperty.MP_EMISSIVE_COLOR, "Emissive")
    link_prop(to_world, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET, "WPO")
    link_prop(p_rough, unreal.MaterialProperty.MP_ROUGHNESS, "Roughness")
    MEL.recompile_material(mat)
    ok = EAL.save_asset(mat.get_path_name())
    log(f"{MAT_NAME} built (save={ok}, RotorUvSpanCm={ROTOR_UV_SPAN_CM}, RotorRate={ROTOR_RATE_TPS})")
    return mat


def build_mi(mat):
    mi = fresh_asset(MI_NAME, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    if mi is None:
        return None
    MEL.set_material_instance_parent(mi, mat)
    ok = EAL.save_asset(mi.get_path_name())
    log(f"{MI_NAME} built (no overrides, save={ok})")
    return mi


def wire_mesh(mi):
    path = f"{OUT}/{SM_NAME}"
    if not EAL.does_asset_exist(path):
        bad(f"{path} missing — run import_voxel_drone.py first (material only)")
        return
    sm = unreal.load_asset(path)
    if sm.get_num_sections(0) != 1:
        bad(f"{SM_NAME} sections={sm.get_num_sections(0)} (must be 1 — ADR 0007)")
    sm.set_material(0, mi)
    ok = EAL.save_asset(path)
    log(f"{SM_NAME} slot0 -> {MI_NAME} (tris={sm.get_num_triangles(0)}, verts={sm.get_num_vertices(0)}, save={ok})")


mat = build_material()
if mat:
    mi = build_mi(mat)
    if mi:
        wire_mesh(mi)
if PROBLEMS:
    unreal.log_warning(f"[DRONE-MAT] {len(PROBLEMS)} problem(s): " + " | ".join(PROBLEMS))
else:
    log("DONE errors=0")
