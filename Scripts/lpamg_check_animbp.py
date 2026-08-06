"""새 팔 AnimBP 배선 점검 — 읽기 전용.

보는 것: 대상 스켈레톤 · **부모 클래스**(FPSRFirstPersonArmsAnimInstance 여야 값 push 가 산다)
· 플레이어 BP 가 실제로 그 클래스를 쓰는지 · 리디렉터 잔재.
"""
import unreal
CAND = ["/Game/Character/Player/ABP_FP_Base", "/Game/Character/Player/ABP_FPArms"]
PLAYER = "/Game/Character/Player/BP_FPSRPlayer"
def log(m): unreal.log("[CHK] %s" % m)
for p in CAND:
    a = unreal.EditorAssetLibrary.load_asset(p)
    if a is None:
        log("%-42s 없음" % p); continue
    cls = a.get_class().get_name()
    if cls == "ObjectRedirector":
        try:
            dest = a.get_editor_property("destination_object")
            log("%-42s ObjectRedirector -> %s" % (p, dest.get_path_name() if dest else "없음"))
        except Exception as e:
            log("%-42s ObjectRedirector (대상 못 읽음: %s)" % (p, e))
        continue
    skel = a.get_editor_property("target_skeleton")
    gc = a.generated_class()
    cdo = unreal.get_default_object(gc) if gc else None
    ours = isinstance(cdo, unreal.FPSRFirstPersonArmsAnimInstance) if cdo else False
    log("%-42s [%s] 스켈레톤 %s" % (p, cls, skel.get_name() if skel else "?"))
    log("     부모가 FPSRFirstPersonArmsAnimInstance 인가 -> %s" % ("예 ✅" if ours else "**아니오 ❌**"))
    if cdo:
        log("     실제 클래스 체인: %s" % type(cdo).__mro__[1].__name__)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bp = unreal.EditorAssetLibrary.load_asset(PLAYER)
if bp:
    actor = eas.spawn_actor_from_class(bp.generated_class(), unreal.Vector(0,0,0), unreal.Rotator(0,0,0))
    actor.set_actor_label("TEMP_ChkBP")
    try:
        for c in actor.get_components_by_class(unreal.SkeletalMeshComponent):
            if "FirstPersonArms" not in c.get_name(): continue
            m = c.get_skeletal_mesh_asset(); ac = c.get_editor_property("anim_class")
            rel = c.get_relative_transform()
            log("FirstPersonArms — 메시 %s / 애님클래스 %s"
                % (m.get_name() if m else "없음", ac.get_name() if ac else "**없음 ❌**"))
            log("   rest loc %s yaw %.1f scale %.2f · SOCKET_Weapon %s"
                % ([round(v,1) for v in (rel.translation.x,rel.translation.y,rel.translation.z)],
                   rel.rotation.rotator().yaw, rel.scale3d.x, c.does_socket_exist("SOCKET_Weapon")))
            inst = c.get_anim_instance()
            log("   런타임 애님인스턴스: %s" % (type(inst).__name__ if inst else "없음"))
    finally:
        eas.destroy_actor(actor)
log("CHK_DONE")
