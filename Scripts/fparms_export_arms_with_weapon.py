"""팔 + 조립된 소총을 한 FBX 로 내보낸다 — Blender 저작 씬의 입력.

**왜 같이 내보내나**: Blender 쪽 `fp_arms_make_authoring.py` 는 들여온 FBX 와 우리 팔의
`hand_r` 를 포개서 총 위치를 잡는다. 총이 팔과 같은 파일에 이미 제자리로 들어 있으면
좌표를 한 줄도 옮겨 적지 않아도 된다 — UE(왼손)/Blender(오른손) Y 반전에서 회전이
틀어지는 사고가 아예 안 생긴다.

🚨 옛 저작 씬은 *"소켓이 hand_r 와 동일하므로 총 원점과 hand_r 거리는 0"* 을 전제했다.
   ADR 0004 이후 `SOCKET_Weapon` 은 손바닥으로 옮겨졌고 회전도 들어갔다(yaw 90).
   그래서 그 전제는 깨졌고, 이 스크립트가 그걸 대체한다.

무기 구성은 **무기 DA 에서 읽는다**(파츠 목록을 여기 적지 않는다 — 데이터가 바뀌면 따라온다).

⚠️ 열려 있는 레벨에 임시 액터를 스폰한다. 반드시 지우고 나온다 — 남으면 사용자가
   레벨을 저장할 때 딸려 들어간다. 끝에 검사한다.

실행: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<이 파일>" -unattended -nopause
      (또는 열린 에디터에서 실행 — 내보내기는 임포트와 달리 데드락을 안 낸다)
"""
import os

import unreal

ARMS = "/Game/Character/FPArms/SK_NeonV_FPArms"
WEAPON_DA = "/Game/Weapons/DataTable/DA_Weapon_Rifle"
OUT = "E:/Git_Project/FPSRoguelite/Saved/NeonV/refpose/FPArms_with_Rifle.fbx"

fails = []


def log(m):
    unreal.log("[ARMSGUN] %s" % m)


def fail(m):
    fails.append(m)
    unreal.log_error("[ARMSGUN] !! %s" % m)


os.makedirs(os.path.dirname(OUT), exist_ok=True)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if les.is_in_play_in_editor():
    fail("PIE 실행 중이면 에셋을 못 연다. 멈추고 다시 실행할 것")
    raise SystemExit(1)

arms_mesh = unreal.EditorAssetLibrary.load_asset(ARMS)
da = unreal.EditorAssetLibrary.load_asset(WEAPON_DA)
if arms_mesh is None or da is None:
    fail("팔 메시 / 무기 DA 를 못 연다")
    raise SystemExit(1)

socket = str(da.get_editor_property("weapon_attach_socket"))
scale = float(da.get_editor_property("weapon_attach_scale"))
body_mesh = da.get_editor_property("weapon_mesh")
parts = da.get_editor_property("weapon_parts")
log("무기 = %s · 소켓 %s · 스케일 %.3f · 파츠 %d개"
    % (body_mesh.get_name(), socket, scale, len(parts)))

TAG = "TEMP_FPArmsWithRifle"
# 앞선 실행이 도중에 죽으면 임시 액터가 레벨에 남는다. 먼저 치우고 시작한다.
for a in eas.get_all_level_actors():
    if a.get_actor_label() == TAG:
        log("이전 실행이 남긴 %s 제거" % TAG)
        eas.destroy_actor(a)

actor = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0),
                                   unreal.Rotator(0, 0, 0))
actor.set_actor_label(TAG)
arms_comp = actor.skeletal_mesh_component
arms_comp.set_mobility(unreal.ComponentMobility.MOVABLE)
arms_comp.set_skeletal_mesh_asset(arms_mesh)
if not arms_comp.does_socket_exist(socket):
    fail("팔 메시에 %s 가 없다" % socket)

# --- 리시버를 소켓에 ---
# 🪤 파이썬에는 런타임 컴포넌트 추가가 없다(`SkeletalMeshComponent(actor)`+`register_component`,
#    `add_component_by_class` 둘 다 부재). 대신 **액터 단위로 조립**한다 — 액터 부착도
#    소켓 이름을 그대로 받으므로 결과 배치는 동일하다.
SNAP = unreal.AttachmentRule.SNAP_TO_TARGET
spawned = [actor]


def add_actor(cls, mesh_asset, prop, parent, sock):
    a = eas.spawn_actor_from_class(cls, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    a.set_actor_label(TAG)
    spawned.append(a)
    comp = a.root_component
    comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    comp.set_editor_property(prop, mesh_asset)
    a.attach_to_actor(parent, sock, SNAP, SNAP, SNAP, False)
    return a, comp


gun_actor, gun = add_actor(unreal.SkeletalMeshActor, body_mesh, "skeletal_mesh_asset",
                           actor, socket)
gun_actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
log("리시버 부착: %s (스케일 %.2f)" % (body_mesh.get_name(), scale))

# --- 파츠를 각 마운트 소켓에 ---
made, grip_comp = 0, None
for p in parts:
    part = p.get_editor_property("part")
    sock = str(p.get_editor_property("socket"))
    if part is None:
        continue
    if not gun.does_socket_exist(sock):
        fail("리시버에 %s 가 없다 (%s)" % (sock, part.get_name()))
        continue
    _, c = add_actor(unreal.StaticMeshActor, part, "static_mesh", gun_actor, sock)
    if c.does_socket_exist("SOCKET_LeftHand"):
        grip_comp = c
    made += 1
log("파츠 %d/%d 부착" % (made, len(parts)))

# --- 왼손 그립 지점이 실제로 어디인지 기록 (Blender 검증용) ---
if grip_comp:
    g = grip_comp.get_socket_transform("SOCKET_LeftHand", unreal.RelativeTransformSpace.RTS_WORLD)
    h = arms_comp.get_socket_transform("hand_r", unreal.RelativeTransformSpace.RTS_WORLD)
    log("SOCKET_LeftHand 월드 %s · hand_r 에서 %.2f cm"
        % (g.translation, (g.translation - h.translation).length()))
else:
    fail("핸드가드에서 SOCKET_LeftHand 를 못 찾았다")

# --- 내보내기 ---
eas.set_selected_level_actors(spawned)
opts = unreal.FbxExportOption()
opts.set_editor_property("ascii", False)
opts.set_editor_property("collision", False)
opts.set_editor_property("level_of_detail", False)
task = unreal.AssetExportTask()
# 🪤 LevelExporterFBX 의 대상은 **World** 다. Level 을 넘기면 조용히 False 만 돌아온다.
task.set_editor_property("object", unreal.get_editor_subsystem(
    unreal.UnrealEditorSubsystem).get_editor_world())
task.set_editor_property("filename", OUT)
task.set_editor_property("automated", True)
task.set_editor_property("prompt", False)
task.set_editor_property("selected", True)
task.set_editor_property("options", opts)
task.set_editor_property("exporter", unreal.LevelExporterFBX())
ok = unreal.Exporter.run_asset_export_task(task)
size = os.path.getsize(OUT) / 1024.0 if os.path.exists(OUT) else 0.0
log("내보냄 %s -> %s (%.0f KB)" % (ok, OUT, size))
if not ok or size <= 0:
    fail("FBX 내보내기 실패")

# --- 임시 액터 제거 (남으면 레벨에 저장된다) ---
for a in reversed(spawned):
    eas.destroy_actor(a)
left = [a for a in eas.get_all_level_actors() if a.get_actor_label() == TAG]
log("임시 액터 정리 — 남은 것 %d개" % len(left))
if left:
    fail("임시 액터가 안 지워졌다 — 레벨 저장 전에 손으로 지울 것")

log("=== 결과 ===")
if fails:
    for f in fails:
        log("!! %s" % f)
    raise SystemExit(1)
log("ARMSGUN_EXPORT_DONE")
