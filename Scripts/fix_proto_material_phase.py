# fix_proto_material_phase.py — 프로토 머티리얼 위상 소스 교체: 오브젝트 위치 해시(이동 시 회전 버그) → CPD 슬롯 3
# 실행(정식 에디터 헤드리스): UnrealEditor-Cmd.exe <uproject> -nullrhi -unattended -nosplash
#   -ExecCmds="py Scripts/fix_proto_material_phase.py"
import unreal, warnings
warnings.simplefilter("ignore")
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
DEST = "/Game/Assets/Characters/EnemyProto"
mat = unreal.load_asset(f"{DEST}/M_FPSREnemyProto")

wpo = None
for ex in unreal.ObjectIterator(unreal.MaterialExpressionCustom):
    o = ex.get_outer()
    while o and o != mat:
        o = o.get_outer()
    if o == mat and str(ex.get_editor_property("description")) == "ProcWPO":
        wpo = ex
        break
if not wpo:
    raise SystemExit("[phasefix] ProcWPO custom node not found")

# 위상 파라미터: CPD 슬롯 3(FPSRVATAnim::CPDSlot_Phase 계약) 읽기 — BeginPlay가 1회 기록
phase_param = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1400, 1200)
phase_param.set_editor_property("parameter_name", "InstancePhase")
phase_param.set_editor_property("default_value", 0.0)
phase_param.set_editor_property("use_custom_primitive_data", True)
phase_param.set_editor_property("primitive_data_index", 3)

# HLSL: ObjPos 해시 제거, PhaseIn(0..1) 사용
code = str(wpo.get_editor_property("code"))
code = code.replace("float ph = frac(dot(ObjPos, float3(0.0137, 0.0119, 0.0093))) * 6.28318;",
                    "float ph = PhaseIn * 6.28318;")
wpo.set_editor_property("code", code)

# 입력 교체: ObjPos -> PhaseIn (나머지 유지)
ins = list(wpo.get_editor_property("inputs"))
new_ins = []
for ci in ins:
    name = str(ci.get_editor_property("input_name"))
    if name == "ObjPos":
        ci2 = unreal.CustomInput()
        ci2.set_editor_property("input_name", "PhaseIn")
        new_ins.append(ci2)
    else:
        new_ins.append(ci)
wpo.set_editor_property("inputs", new_ins)
ok = mel.connect_material_expressions(phase_param, "", wpo, "PhaseIn")
print(f"[phasefix] PhaseIn connected={ok}")

mel.recompile_material(mat)
print(f"[phasefix] saved={eal.save_asset(f'{DEST}/M_FPSREnemyProto')}")
unreal.SystemLibrary.quit_editor()
