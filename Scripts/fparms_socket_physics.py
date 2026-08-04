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
BODY_MESH = "/Game/Characters/Blu/SkeletalMeshes/Blu_-_Rigged_Non_Constraint"   # 파지 규약 정답지

SOCKET_NAME = "SOCKET_Weapon"
SOCKET_BONE = "hand_r"
# 해부 기준틀을 만들 본 (손목, 중지뿌리, 검지뿌리, 새끼뿌리) — 리그마다 이름이 다르다
BODY_HAND = ("hand_R", "middle_proximal_R", "index_proximal_R", "little_proximal_R")
ARMS_HAND = ("hand_r", "middle_01_r", "index_01_r", "pinky_01_r")

# 🚨 이 값은 **계산이 아니라 사용자가 눈으로 확정한 것**이다. 자동 산출하려던 두 시도가 다 틀렸다:
#   ① 옛 팔 소켓(-5.45, 0.75, 2.45)을 손바닥 길이비로 이동  -> 그 옛 값 자체가 회전 identity
#      상태에서 잰 것이라 전제가 깨져 있었다(총이 90도 꺾여 나옴)
#   ② 바디 소켓을 해부 기준틀로 이식             -> 총이 더 꺾였다(원인 미규명, 아래 참고 로그)
# 손잡이 굵기·파지감은 수치로 안 나온다. **눈이 최종 판정자다.**
SOCKET_LOC = unreal.Vector(-8.07, 2.82, 4.34)
SOCKET_ROT = unreal.Rotator(0.0, 0.0, 90.0)      # Rotator(roll, pitch, yaw)

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

# ---------------------------------------------------------------- 소켓 값 산출
#
# 🚨 옛 팔 소켓(`S_Mannequin` 의 것)에서 값을 끌어오면 안 된다. 그건 **회전이 identity** 였고,
#    바디 소켓과 비교해 보면 100도 넘게 틀어져 있었다. 그 상태에서 잰 "손잡이가 손바닥에서
#    6.03cm 뜬다" 도 전제가 깨진 수치다. 그걸 비례 이동해 썼다가 총이 90도 꺾여 나왔다.
#
# 정답지는 **바디(3인칭) 소켓**이다. 거기선 총이 제대로 잡혀 있고, 무엇보다 1인칭과 3인칭이
# 같은 무기 데이터 한 벌을 쓰므로 **두 소켓이 같은 파지 규약을 가져야** 동료 화면과 내 화면이
# 어긋나지 않는다.
#
# 두 리그(Blu 바디 / Manny 이름의 팔)는 뼈 로컬 축 규약이 다르므로 값을 그대로 못 옮긴다.
# 대신 **손 모양으로 해부 기준틀**을 만들어 옮긴다 — 축 규약과 무관해진다.
#   X = 손목->중지뿌리(손바닥 방향) · Z = 손바닥 법선 · Y = Z x X
log("=== 소켓 값 (사용자 시각 검증값) ===")
log("   ★ 위치 %s · 회전 %s" % (SOCKET_LOC, SOCKET_ROT))
log("   아래는 참고용 계산일 뿐 — 값을 덮지 않는다")
body_obj = unreal.EditorAssetLibrary.load_asset(BODY_MESH)
if body_obj is None:
    log("   (바디 메시가 없어 참고 계산 생략)")
    body = None
else:
    body = spawn(body_obj, "body")


def head(c, n):
    return c.get_socket_transform(n, unreal.RelativeTransformSpace.RTS_COMPONENT).translation


def anat_frame(c, wrist, mid, idx, pky):
    o = head(c, wrist)
    x = head(c, mid) - o
    x = x / x.length()
    z = (head(c, idx) - o).cross(head(c, pky) - o)
    z = z / z.length()
    y = z.cross(x)
    y = y / y.length()
    z = x.cross(y)
    return unreal.Matrix(unreal.Plane(x.x, x.y, x.z, 0.0), unreal.Plane(y.x, y.y, y.z, 0.0),
                         unreal.Plane(z.x, z.y, z.z, 0.0),
                         unreal.Plane(o.x, o.y, o.z, 1.0)).transform()


if body is not None and body.does_socket_exist(SOCKET_NAME):
    Fb = anat_frame(body, *BODY_HAND)
    Fa = anat_frame(comp, *ARMS_HAND)
    bl = (head(body, BODY_HAND[1]) - head(body, BODY_HAND[0])).length()
    al = (head(comp, ARMS_HAND[1]) - head(comp, ARMS_HAND[0])).length()
    log("   [참고] 손바닥 길이: 바디 %.2f / 팔 %.2f cm — 같은 캐릭터라 일치해야 정상" % (bl, al))
    Sb = body.get_socket_transform(SOCKET_NAME, unreal.RelativeTransformSpace.RTS_COMPONENT)
    Sa = Sb.multiply(Fb.inverse()).multiply(Fa)
    hand = comp.get_socket_transform(SOCKET_BONE, unreal.RelativeTransformSpace.RTS_COMPONENT)
    gl = hand.inverse_transform_location(Sa.translation)
    gr = (hand.rotation.inversed() * Sa.rotation).rotator()
    log("   [참고] 바디에서 이식하면 위치 (%.2f, %.2f, %.2f) 회전 p%.1f y%.1f r%.1f"
        % (gl.x, gl.y, gl.z, gr.pitch, gr.yaw, gr.roll))
    log("   [참고] 위 값은 **눈으로 확인했을 때 총이 더 꺾였다**(2026-08-04). 쓰지 말 것.")
    log("          원인 미규명 — 프레임 수식 버그이거나, 바디 쪽 총이 원래 틀어져 있거나.")
    log("          바디(3인칭) 무기 방향은 PIE 로 확인된 적이 없다. 그것부터 봐야 한다.")

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
    unreal.Transform(location=SOCKET_LOC, rotation=SOCKET_ROT, scale=unreal.Vector(1, 1, 1)))
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
