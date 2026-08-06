"""BP 뷰포트에서 총이 보이나? — CDO 상태를 실제로 확인한다(읽기 전용)."""
import unreal
def log(m): unreal.log("[BPV] %s" % m)
bp = unreal.EditorAssetLibrary.load_asset("/Game/Character/Player/BP_FPSRPlayer")
cdo = unreal.get_default_object(bp.generated_class())
log("CDO 클래스: %s" % cdo.get_class().get_name())
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
a = eas.spawn_actor_from_class(bp.generated_class(), unreal.Vector(0,0,0), unreal.Rotator(0,0,0))
a.set_actor_label("TEMP_BPV")
try:
    sk = a.get_components_by_class(unreal.SkeletalMeshComponent)
    st = a.get_components_by_class(unreal.StaticMeshComponent)
    log("스켈레탈 %d개 / 스태틱 %d개" % (len(sk), len(st)))
    for c in sk:
        m = c.get_skeletal_mesh_asset()
        log("   [SK] %-22s 메시=%s" % (c.get_name(), m.get_name() if m else "**없음**"))
    for c in st:
        m = c.get_editor_property("static_mesh")
        log("   [SM] %-22s 메시=%s" % (c.get_name(), m.get_name() if m else "**없음**"))
finally:
    eas.destroy_actor(a)
da = unreal.EditorAssetLibrary.load_asset("/Game/Weapons/DataTable/DA_Weapon_Rifle")
if da:
    body = da.get_editor_property("weapon_mesh")
    parts = da.get_editor_property("weapon_parts")
    log("무기 DA — 바디 %s · 파츠 %d개 · 소켓 %s · 스케일 %.2f"
        % (body.get_name() if body else "?", len(parts),
           da.get_editor_property("weapon_attach_socket"),
           da.get_editor_property("weapon_attach_scale")))
log("BPV_DONE")
