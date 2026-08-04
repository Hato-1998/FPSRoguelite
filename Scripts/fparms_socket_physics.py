"""`SOCKET_Weapon` 저작 + PhysicsAsset 정리 — 새 자체 스켈레톤용. 커맨드렛 전용.

옛 소켓은 `S_Mannequin` 의 것이었고 `hand_r` **identity = 손목**이라 손잡이가 손바닥에서
6.03cm 떴다. 자체 스켈레톤이 되었으니 처음부터 손바닥에 박는다.

값을 내는 방법: 옛 값 (-5.45, 0.75, 2.45) 을 '손바닥 방향 성분'과 '나머지'로 갈라, 방향
성분만 손바닥 길이비(7.88/9.52 = 0.828)로 줄인다. 손이 짧아진 만큼만 당기고 손바닥 표면
쪽으로 치우쳐 저작된 의도는 그대로 지킨다.

🚨 **계산은 전부 UE 공간에서 한다.** 옛 값이 UE 에서 잰 것이라, Blender 축으로 갈랐다가
   섞였다. 실제로 한 번 그렇게 냈고 아래 축 대조가 잡아냈다 — **hand_r 로컬 축도 Y 부호가
   뒤집힌다**(Blender 오른손 -> UE 왼손). 실측: middle_01_r 이 Blender (-7.56, +1.40, 1.74)
   / UE (-7.56, **-1.40**, 1.74). X·Z 는 소수점까지 같다.
   그래서 옛 손 팔목 축도 UE 에서 직접 재고(옛 메시를 같이 스폰한다) 거기서 갈른다.

⚠️ 이 값은 **시작점**이다. 손잡이 굵기·파지감은 수치로 안 나오므로 총을 물려 눈으로 확정한다.

실행:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<이 파일>" -unattended -nopause
"""
import unreal

MESH_ASSET = "/Game/Character/FPArms/SK_NeonV_FPArms"
SKELETON = "/Game/Character/FPArms/SK_NeonV_FPArms_Skeleton"
PHYSICS = "/Game/Character/FPArms/SK_NeonV_FPArms_PhysicsAsset"
OLD_MESH = "/Game/Character/FPArms/NeonV_FPArms"       # S_Mannequin 컨폼 — 옛 축을 재는 용도

SOCKET_NAME = "SOCKET_Weapon"
SOCKET_BONE = "hand_r"
PALM_TIP = "middle_01_r"                                # 손바닥 방향을 정하는 끝점
OLD_SOCKET = unreal.Vector(-5.45, 0.75, 2.45)           # 옛 손 기준 실측(UE hand_r 로컬, cm)

# Blender 에서 hand_r 로컬로 잰 랜드마크(cm). UE 는 Y 부호가 뒤집혀 나와야 정상이다.
EXPECT_LOCAL_BLENDER = {
    "middle_01_r": (-7.56, 1.40, 1.74),
    "index_01_r": (-7.64, 0.28, 3.55),
    "lowerarm_r": (28.66, -0.84, -0.10),
}
MAX_AXIS_CM = 0.05

fails = []


def log(m):
    unreal.log("[FPARMS] %s" % m)


def fail(m):
    fails.append(m)
    unreal.log_error("[FPARMS] !! %s" % m)


mesh = unreal.EditorAssetLibrary.load_asset(MESH_ASSET)
skel = unreal.EditorAssetLibrary.load_asset(SKELETON)
if mesh is None or skel is None:
    fail("메시/스켈레톤을 못 찾는다 — 임포트 먼저")
    raise SystemExit(1)

actors = []


def spawn(asset):
    act = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    actors.append(act)
    c = act.skeletal_mesh_component
    c.set_mobility(unreal.ComponentMobility.MOVABLE)
    c.set_skeletal_mesh_asset(asset)
    return c


def palm_local(comp, bone):
    """hand_r 로컬(UE cm)에서 본 bone 의 머리."""
    hand = comp.get_socket_transform(SOCKET_BONE, unreal.RelativeTransformSpace.RTS_COMPONENT)
    w = comp.get_socket_transform(bone, unreal.RelativeTransformSpace.RTS_COMPONENT).translation
    return hand.inverse_transform_location(w)


comp = spawn(mesh)

# ---------------------------------------------------------------- 축 대조
# Blender 값과 **(x, -y, z)** 로 맞아야 정상이다. 그냥 같으면 오히려 뭔가 틀린 것이다.
log("=== 축 대조 (Blender -> UE 는 (x, -y, z)) ===")
worst = 0.0
for bone, b in EXPECT_LOCAL_BLENDER.items():
    local = palm_local(comp, bone)
    want = unreal.Vector(b[0], -b[1], b[2])
    d = (local - want).length()
    worst = max(worst, d)
    log("   %-14s UE (%7.2f, %7.2f, %7.2f)  Blender->UE (%7.2f, %7.2f, %7.2f)  차 %.4f cm"
        % (bone, local.x, local.y, local.z, want.x, want.y, want.z, d))
if worst > MAX_AXIS_CM:
    fail("hand_r 로컬 축이 %.3fcm 어긋난다 — (x,-y,z) 규칙이 안 맞는다. 저작 중단" % worst)
    for a in actors:
        unreal.EditorLevelLibrary.destroy_actor(a)
    raise SystemExit(1)
log("   (x,-y,z) 로 일치 — 두 리그의 hand_r 축이 대응한다")

# ---------------------------------------------------------------- 소켓 값 산출 (전부 UE 공간)
log("=== 소켓 값 산출 ===")
old_obj = unreal.EditorAssetLibrary.load_asset(OLD_MESH)
if old_obj is None:
    fail("옛 메시 %s 가 없다 — 옛 손바닥 축을 못 잰다" % OLD_MESH)
    for a in actors:
        unreal.EditorLevelLibrary.destroy_actor(a)
    raise SystemExit(1)
old_axis = palm_local(spawn(old_obj), PALM_TIP)
new_axis = palm_local(comp, PALM_TIP)
old_len, new_len = old_axis.length(), new_axis.length()
log("   손바닥 길이(손목->%s): 옛 %.2f -> 새 %.2f cm (비 %.3f)"
    % (PALM_TIP, old_len, new_len, new_len / old_len))

ou, nu = old_axis / old_len, new_axis / new_len
along = OLD_SOCKET.dot(ou)
perp = OLD_SOCKET - ou * along
SOCKET_LOC = nu * (along * new_len / old_len) + perp
log("   옛 소켓 (%.2f, %.2f, %.2f) = 손바닥방향 %.2f + 나머지 %.2f"
    % (OLD_SOCKET.x, OLD_SOCKET.y, OLD_SOCKET.z, along, perp.length()))
log("   ★ 새 소켓 (%.2f, %.2f, %.2f)  — 손목에서 %.2fcm (옛 %.2fcm)"
    % (SOCKET_LOC.x, SOCKET_LOC.y, SOCKET_LOC.z, SOCKET_LOC.length(), OLD_SOCKET.length()))

for a in actors:
    unreal.EditorLevelLibrary.destroy_actor(a)
actors = []

# ---------------------------------------------------------------- 소켓 저작
log("=== SOCKET_Weapon 저작 ===")
# 🪤 `Skeleton.Sockets` 는 파이썬에서 protected 다(읽기조차 막힌다). 소켓 API 는 **메시** 쪽에만
#    있다(add_socket / find_socket / num_sockets). 런타임 부착은 메시 소켓을 먼저 찾으므로
#    문제없고, 1P 팔은 이 스켈레톤에 메시가 하나뿐이라 메시 소유가 오히려 맞다.
# 🪤 `socket_name` 과 `bone_name` 은 파이썬에서 read-only 다. 대신 전용 메서드를 쓴다:
#    add_socket -> rename_socket(옛이름, 새이름) -> set_socket_parent(mesh, bone)
#    -> set_socket_local_transform(transform)
sock = mesh.find_socket(SOCKET_NAME)
if sock is None:
    sock = unreal.SkeletalMeshSocket(mesh)
    mesh.add_socket(sock, False)
    cur = str(sock.get_editor_property("socket_name"))
    log("   소켓 신규 생성 (기본 이름 %r) -> rename %s" % (cur, SOCKET_NAME))
    if not mesh.rename_socket(cur, SOCKET_NAME):
        fail("소켓 이름을 %r -> %s 로 못 바꿨다. 에디터에서 손으로 만들 것 "
             "(hand_r · 상대위치 %.2f, %.2f, %.2f)"
             % (cur, SOCKET_NAME, SOCKET_LOC.x, SOCKET_LOC.y, SOCKET_LOC.z))
else:
    log("   기존 소켓 갱신")
sock.set_socket_parent(mesh, SOCKET_BONE)
sock.set_socket_local_transform(
    unreal.Transform(location=SOCKET_LOC, rotation=unreal.Rotator(0, 0, 0),
                     scale=unreal.Vector(1, 1, 1)))
sock.set_editor_property("force_always_animated", True)

names = [str(mesh.get_socket_by_index(i).get_editor_property("socket_name"))
         for i in range(mesh.num_sockets())]
log("   메시 소켓 %d개: %s" % (len(names), names))
if SOCKET_NAME not in names:
    fail("소켓이 안 붙었다")

# ---------------------------------------------------------------- PhysicsAsset
# 1인칭 팔은 충돌이 필요 없다. 자동 생성 바디가 그대로 남으면 바운드·그림자·컬링에만
# 영향을 주고 득이 없다. 바디를 지우지는 않는다(지우면 바운드가 메시 바운드로 떨어져
# 오히려 컬링이 거칠어질 수 있다) — **충돌만 끈다.**
log("=== PhysicsAsset ===")
pa = unreal.EditorAssetLibrary.load_asset(PHYSICS)
if pa is None:
    log("   (없음 — 자동 생성이 안 됐다)")
else:
    # 프로퍼티 이름이 버전마다 다르다. 추측하지 말고 실제로 있는 것을 찾는다.
    bodies = None
    for cand in ("skeletal_body_setups", "skeletal_bodies_setups", "bodies_setup"):
        try:
            bodies = pa.get_editor_property(cand)
            log("   바디 목록 프로퍼티 = %s" % cand)
            break
        except Exception:  # noqa: BLE001
            continue
    if bodies is None:
        log("   바디 목록 프로퍼티를 못 찾았다. PhysicsAsset 후보: %s"
            % [n for n in dir(type(pa)) if "bod" in n.lower() or "setup" in n.lower()][:12])
        bodies = []
    log("   바디 %d개: %s"
        % (len(bodies), ", ".join(str(b.get_editor_property("bone_name")) for b in bodies[:8])))
    # 1P 팔은 충돌이 필요 없다. 바디를 지우지는 않는다 — 지우면 바운드가 거칠어져
    # 그림자·컬링이 오히려 나빠질 수 있다. **충돌만** 끈다.
    off = 0
    for b in bodies:
        try:
            b.set_editor_property(
                "collision_reponse", unreal.BodyCollisionResponse.BODY_COLLISION_DISABLED)
            off += 1
        except Exception as e:  # noqa: BLE001 — 어느 프로퍼티가 막히는지 남긴다
            log("   (충돌 끄기 실패 %s: %s)" % (b.get_editor_property("bone_name"), e))
            break
    log("   충돌 비활성 %d/%d" % (off, len(bodies)))

for path in (MESH_ASSET, SKELETON, PHYSICS):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        log("   저장 %s -> %s" % (path, unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)))

# ---------------------------------------------------------------- 되읽어 확인
log("=== 되읽기 ===")
unreal.EditorAssetLibrary.load_asset(SKELETON)
act2 = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
c2 = act2.skeletal_mesh_component
c2.set_mobility(unreal.ComponentMobility.MOVABLE)
c2.set_skeletal_mesh_asset(mesh)
if SOCKET_NAME in [str(n) for n in c2.get_all_socket_names()]:
    st = c2.get_socket_transform(SOCKET_NAME, unreal.RelativeTransformSpace.RTS_COMPONENT)
    hd = c2.get_socket_transform(SOCKET_BONE, unreal.RelativeTransformSpace.RTS_COMPONENT)
    back = hd.inverse_transform_location(st.translation)
    log("   %s -> hand_r 로컬 (%.2f, %.2f, %.2f)" % (SOCKET_NAME, back.x, back.y, back.z))
    if (back - SOCKET_LOC).length() > 0.01:
        fail("되읽은 소켓 위치가 다르다")
else:
    fail("되읽기에서 소켓이 안 보인다")
unreal.EditorLevelLibrary.destroy_actor(act2)

log("=== 결과 ===")
if fails:
    for f in fails:
        log("!! %s" % f)
    raise SystemExit(1)
log("소켓·피직스에셋 완료")
log("SOCKET_PHYSICS_DONE")
