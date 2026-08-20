# apply_enemy_hover.py — 부양을 데이터(HoverHeight)로 이관하고 시각 오프셋을 원복 (프로토 판정·시각 정렬)
# 실행(정식 에디터 헤드리스): UnrealEditor-Cmd.exe <uproject> -nullrhi -unattended -nosplash
#   -ExecCmds="py Scripts/apply_enemy_hover.py"
import unreal, warnings
warnings.simplefilter("ignore")
eal = unreal.EditorAssetLibrary
sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
lib = unreal.SubobjectDataBlueprintFunctionLibrary

for bp_path, cls_name in [("/Game/Character/Enemy/BP_EnemyMeleeBase", "BP_EnemyMeleeBase_C"),
                          ("/Game/Character/Enemy/BP_EnemyRangedBase", "BP_EnemyRangedBase_C")]:
    cls = unreal.load_class(None, bp_path + "." + cls_name)
    cdo = unreal.get_default_object(cls)
    cdo.set_editor_property("HoverHeight", 50.0)          # 부양 = 판정 캡슐째 (데이터)
    mesh = cdo.get_editor_property("Mesh")
    mesh.set_editor_property("relative_location", unreal.Vector(0.0, 0.0, 0.0))  # 시각 오프셋 원복
    print(f"[hover] {cls_name}: HoverHeight=50, mesh Z=0")

bp = unreal.load_asset("/Game/Character/Enemy/BP_EnemyMeleeBase")
for h in sds.k2_gather_subobject_data_for_blueprint(bp):
    obj = lib.get_object(lib.get_data(h))
    if obj and obj.get_class().get_name() == "FPSRWeakpointComponent":
        obj.set_editor_property("relative_location", unreal.Vector(0.0, 0.0, 0.0))  # 약점도 캡슐 중심(=코어)으로
        print("[hover] weakpoint -> (0,0,0)")
        break

for p in ["/Game/Character/Enemy/BP_EnemyMeleeBase", "/Game/Character/Enemy/BP_EnemyRangedBase"]:
    b = unreal.load_asset(p)
    unreal.BlueprintEditorLibrary.compile_blueprint(b)
    print(f"[hover] {b.get_name()} saved={eal.save_asset(p)}")

unreal.SystemLibrary.quit_editor()
