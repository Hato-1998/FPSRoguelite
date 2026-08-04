"""자체 스켈레톤 팔 — 읽기 전용 최종 확인. 커맨드렛 전용. 아무것도 저장하지 않는다.

임포트·소켓·애니를 각각 다른 스크립트가 만들었으므로, 마지막에 **한 번에 되읽어** 서로
어긋난 게 없는지 본다. 저장을 안 하므로 몇 번을 돌려도 git diff 가 안 생긴다.

실행:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<이 파일>" -unattended -nopause
"""
import unreal

MESH = "/Game/Character/FPArms/SK_NeonV_FPArms"
SKELETON = "/Game/Character/FPArms/SK_NeonV_FPArms_Skeleton"
ANIMS = ("/Game/Character/FPArms/Anims/FP_Rifle_Idle",
         "/Game/Character/FPArms/Anims/FP_Rifle_ADS")
SOCKET = "SOCKET_Weapon"
EXPECT_SOCKET = unreal.Vector(-4.37, 0.52, 3.34)

fails = []


def log(m):
    unreal.log("[VERIFY] %s" % m)


def fail(m):
    fails.append(m)
    unreal.log_error("[VERIFY] !! %s" % m)


mesh = unreal.EditorAssetLibrary.load_asset(MESH)
if mesh is None:
    fail("%s 를 못 연다" % MESH)
    raise SystemExit(1)

log("메시 %s / 스켈레톤 %s" % (mesh.get_name(), mesh.skeleton.get_name()))
if not mesh.skeleton.get_path_name().startswith(SKELETON + "."):
    fail("스켈레톤이 %s 가 아니다" % SKELETON)

for m in mesh.materials:
    mi = m.material_interface
    log("머티리얼 %-14s %s" % (str(m.material_slot_name), mi.get_path_name() if mi else "!! None"))
    if mi is None:
        fail("머티리얼 슬롯 %s 가 비었다" % m.material_slot_name)

act = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
comp = act.skeletal_mesh_component
comp.set_mobility(unreal.ComponentMobility.MOVABLE)
comp.set_skeletal_mesh_asset(mesh)
log("본 %d개" % comp.get_num_bones())
names = [str(n) for n in comp.get_all_socket_names()]
log("소켓 %s" % names)
if SOCKET in names:
    hd = comp.get_socket_transform("hand_r", unreal.RelativeTransformSpace.RTS_COMPONENT)
    st = comp.get_socket_transform(SOCKET, unreal.RelativeTransformSpace.RTS_COMPONENT)
    loc = hd.inverse_transform_location(st.translation)
    d = (loc - EXPECT_SOCKET).length()
    log("%s -> hand_r 로컬 (%.2f, %.2f, %.2f)  기대와 차 %.4f cm" % (SOCKET, loc.x, loc.y, loc.z, d))
    if d > 0.01:
        fail("소켓 위치가 기대와 %.3fcm 다르다" % d)
else:
    fail("%s 소켓이 없다" % SOCKET)
unreal.EditorLevelLibrary.destroy_actor(act)

for path in ANIMS:
    a = unreal.EditorAssetLibrary.load_asset(path)
    if a is None:
        fail("%s 를 못 연다" % path)
        continue
    sk = a.get_editor_property("skeleton")
    ln = a.get_editor_property("sequence_length")
    log("애니 %-16s 스켈레톤 %-28s 길이 %.4fs / 프레임 %s"
        % (a.get_name(), sk.get_name(), ln,
           a.get_editor_property("number_of_sampled_frames")))
    if not sk.get_path_name().startswith(SKELETON + "."):
        fail("%s 가 다른 스켈레톤에 붙어 있다" % a.get_name())
    if ln <= 0.0:
        fail("%s 의 길이가 0 이다" % a.get_name())

log("=== 결과 ===")
if fails:
    for f in fails:
        log("!! %s" % f)
    raise SystemExit(1)
log("전부 정상")
log("VERIFY_FPARMS_DONE")
