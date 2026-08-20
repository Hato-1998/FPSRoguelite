# convert_enemies_ranged.py — 전 몬스터 원거리화(사용자 결정 2026-08-14): 쌍뿔(멜리 BP)을 원거리 베이스로 전환
# 실행(정식 에디터 헤드리스): UnrealEditor-Cmd.exe <uproject> -nullrhi -unattended -nosplash
#   -ExecCmds="py Scripts/convert_enemies_ranged.py"
import unreal, warnings
warnings.simplefilter("ignore")
eal = unreal.EditorAssetLibrary

# 1) 랭드 BP의 투사체 설정을 복사 소스로
r_cls = unreal.load_class(None, "/Game/Character/Enemy/BP_EnemyRangedBase.BP_EnemyRangedBase_C")
r_cdo = unreal.get_default_object(r_cls)
proj_cls = r_cdo.get_editor_property("ProjectileClass")
cfg = {p: r_cdo.get_editor_property(p) for p in
       ["ProjectileDamage", "ProjectileSpeed", "ProjectileLifetime", "ProjectileGravityScale",
        "RangedEngageRange", "RangedChargeTime", "RangedFireCooldown"]}
print(f"[ranged] source cfg: proj={proj_cls.get_name() if proj_cls else None} {cfg}")

# 2) 멜리 BP 리페어런트 -> AFPSRRangedEnemyBase
bp = unreal.load_asset("/Game/Character/Enemy/BP_EnemyMeleeBase")
new_parent = unreal.load_class(None, "/Script/FPSRoguelite.FPSRRangedEnemyBase")
unreal.BlueprintEditorLibrary.reparent_blueprint(bp, new_parent)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
print("[ranged] reparented BP_EnemyMeleeBase -> FPSRRangedEnemyBase")

# 3) 투사체 설정 복사 + 총구 = 코어(캡슐 중심, 부양은 액터가 이미 떠 있음)
m_cls = unreal.load_class(None, "/Game/Character/Enemy/BP_EnemyMeleeBase.BP_EnemyMeleeBase_C")
m_cdo = unreal.get_default_object(m_cls)
if proj_cls:
    m_cdo.set_editor_property("ProjectileClass", proj_cls)
for k, v in cfg.items():
    m_cdo.set_editor_property(k, v)
m_cdo.set_editor_property("MuzzleOffset", unreal.Vector(0.0, 0.0, 0.0))
r_cdo.set_editor_property("MuzzleOffset", unreal.Vector(0.0, 0.0, 0.0))  # 원자큐브도 핵(중심)에서 발사

for p in ["/Game/Character/Enemy/BP_EnemyMeleeBase", "/Game/Character/Enemy/BP_EnemyRangedBase"]:
    b = unreal.load_asset(p)
    unreal.BlueprintEditorLibrary.compile_blueprint(b)
    print(f"[ranged] {b.get_name()} saved={eal.save_asset(p)}")

# 4) 검증 출력: 부모·투사체·부양값
for cls_path, name in [("/Game/Character/Enemy/BP_EnemyMeleeBase.BP_EnemyMeleeBase_C", "Bipyramid(구 멜리)"),
                       ("/Game/Character/Enemy/BP_EnemyRangedBase.BP_EnemyRangedBase_C", "AtomCubes")]:
    c = unreal.load_class(None, cls_path)
    d = unreal.get_default_object(c)
    print(f"[ranged] {name}: proj={d.get_editor_property('ProjectileClass').get_name() if d.get_editor_property('ProjectileClass') else None}"
          f" hover={d.get_editor_property('HoverHeight')} muzzle={d.get_editor_property('MuzzleOffset')}")

unreal.SystemLibrary.quit_editor()
