# -*- coding: utf-8 -*-
"""왼엄지 튜닝 (FP_Rifle_Idle) — 2모드.

실행법 (Output Log 하단 Cmd 줄):
    py "E:/Git_Project/FPSRoguelite/Scripts/tune_left_thumb.py"

MODE = "AUTO"   : 엄지를 총열 기준(위/총쪽 각도)으로 곧게 정렬. 기준 회전을 캐시에 저장. (PIE 필요)
MODE = "MANUAL" : AUTO 가 잡은 기준 위에 마디별 숫자를 더해 직접 조정. (PIE 불필요·완전 결정적)

순서: ① AUTO 한 번(기준 확정) → ② MODE="MANUAL" 로 바꾸고 BEND1~3 으로 마디별 조정.
BEND 는 절대값이다 — 실행마다 "기준+그 값"이지, 누적되지 않는다. 0 으로 돌리면 기준으로 복귀.
축 의미(정렬 후 마디 로컬): roll = 엄지 축 비틀기 / pitch·yaw = 서로 직각인 두 굽힘 방향.
부호는 ±5 넣어 화면으로 확인하는 게 가장 정확하다.
"""
import unreal, json, math, os

# ── 모드 ────────────────────────────────────────────────────────────
MODE = "AUTO"          # "AUTO" 또는 "MANUAL"

# ── AUTO 다이얼 (방향 기준) ──────────────────────────────────────────
TILT_UP   = 0.0     # 총열 기준 위로(도)
TILT_GUN  = 20.0    # 총 몸통 쪽으로(도)

# ── MANUAL 다이얼 (마디별 추가 회전 — AUTO 기준에서, (pitch, yaw, roll)) ──
BEND1 = (0.0, 0.0, 0.0)   # thumb_01 (뿌리)
BEND2 = (0.0, 0.0, 0.0)   # thumb_02
BEND3 = (0.0, 0.0, 0.0)   # thumb_03 (끝)

# ── 공통 (크기) ─────────────────────────────────────────────────────
LENGTH    = 0.5     # 길이 배율 (뼈 축)
THICKNESS = 0.7     # 굵기 배율
# ───────────────────────────────────────────────────────────────────

ML = unreal.MathLibrary
SCRATCH = r"C:\Users\koras\AppData\Local\Temp\claude\E--Git-Project-FPSRoguelite\4502cb93-253d-49ce-9de9-5489b03b3bc1\scratchpad"
BASELINE = os.path.join(SCRATCH, "finger_baseline_FP_Rifle_Idle.json")

baseline = json.load(open(BASELINE))
anim = unreal.load_object(None, '/Game/Character/FPArms/Anims_LPAMG/FP_Rifle_Idle')
ctrl = anim.get_editor_property('controller')

def N(v):
    l = v.length(); return unreal.Vector(v.x/l, v.y/l, v.z/l)
def rv(rot, v): return rot.quaternion().rotate_vector(v)
def dot(a,b): return a.x*b.x+a.y*b.y+a.z*b.z
def R(p, y, r):
    o = unreal.Rotator(); o.pitch, o.yaw, o.roll = p, y, r; return o

if MODE == "AUTO":
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not w:
        raise SystemExit("AUTO 모드는 PIE 를 먼저 켜야 한다 (라이브 총 프레임 필요)")
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    arms = pawn.get_editor_property('FirstPersonArms')
    wpn = pawn.get_editor_property('WeaponMesh')
    WS = unreal.RelativeTransformSpace.RTS_WORLD

    gp = wpn.get_socket_transform('SOCKET_Mount_Grip_0', WS).translation
    bp = wpn.get_socket_transform('SOCKET_Mount_Barrel_0', WS).translation
    barrel = N(bp - gp)
    up = unreal.Vector(0,0,1)
    upp = N(unreal.Vector(up.x-barrel.x*dot(up,barrel), up.y-barrel.y*dot(up,barrel), up.z-barrel.z*dot(up,barrel)))
    tpos = arms.get_socket_transform('thumb_01_l', WS).translation
    rel_t = unreal.Vector(tpos.x-gp.x, tpos.y-gp.y, tpos.z-gp.z)
    c = unreal.Vector(gp.x+barrel.x*dot(rel_t,barrel), gp.y+barrel.y*dot(rel_t,barrel), gp.z+barrel.z*dot(rel_t,barrel))
    lat = N(unreal.Vector(c.x-tpos.x, c.y-tpos.y, c.z-tpos.z))
    tU, tL = math.tan(math.radians(TILT_UP)), math.tan(math.radians(TILT_GUN))
    D = N(unreal.Vector(barrel.x+upp.x*tU+lat.x*tL, barrel.y+upp.y*tU+lat.y*tL, barrel.z+upp.z*tU+lat.z*tL))

    def wrot(b): return arms.get_socket_transform(b, WS).rotation.rotator()
    def clip_local(b): return unreal.AnimationLibrary.get_bone_pose_for_frame(anim, b, 0, False).rotation.rotator()
    Wh, W1, W2, W3 = wrot('hand_l'), wrot('thumb_01_l'), wrot('thumb_02_l'), wrot('thumb_03_l')
    r1c, r2c, r3c = clip_local('thumb_01_l'), clip_local('thumb_02_l'), clip_local('thumb_03_l')
    def rel(parent, own): return ML.compose_rotators(own, ML.negate_rotator(parent))
    B1 = ML.compose_rotators(ML.negate_rotator(r1c), rel(Wh, W1))
    B2 = ML.compose_rotators(ML.negate_rotator(r2c), rel(W1, W2))
    B3 = ML.compose_rotators(ML.negate_rotator(r3c), rel(W2, W3))

    def find_between(a, b):
        a, b = N(a), N(b)
        d = max(-1.0, min(1.0, dot(a,b)))
        ang = math.degrees(math.acos(d))
        ax = unreal.Vector(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x)
        if ax.length() < 1e-6: return unreal.Rotator()
        return ML.rotator_from_axis_and_angle(N(ax), ang)

    off2 = unreal.Vector(*baseline['thumb_02_l'][0]['loc'])
    off3 = unreal.Vector(*baseline['thumb_03_l'][0]['loc'])
    W1p = ML.compose_rotators(W1, find_between(rv(W1, off2), D))
    r1 = ML.compose_rotators(ML.compose_rotators(W1p, ML.negate_rotator(Wh)), ML.negate_rotator(B1))
    W2t = ML.compose_rotators(B2, W1p)
    W2p = ML.compose_rotators(W2t, find_between(rv(W2t, off3), D))
    r2 = ML.compose_rotators(ML.compose_rotators(W2p, ML.negate_rotator(W1p)), ML.negate_rotator(B2))
    r3 = ML.negate_rotator(B3)

    # 기준 캐시 → MANUAL 이 이 위에서 시작
    baseline['thumb_base_l'] = {k: [rr.pitch, rr.yaw, rr.roll] for k, rr in (('r1', r1), ('r2', r2), ('r3', r3))}
    json.dump(baseline, open(BASELINE, 'w'), indent=1)
    rots = (r1, r2, r3)
    unreal.log("[AUTO] 정렬 + 기준 캐시 (위 %.1f / 총쪽 %.1f)" % (TILT_UP, TILT_GUN))

else:  # MANUAL — PIE 불필요, 완전 결정적
    if 'thumb_base_l' not in baseline:
        raise SystemExit("먼저 MODE=AUTO 로 한 번 실행해 기준을 캐시하라")
    b = baseline['thumb_base_l']
    rots = [ML.compose_rotators(R(*bend), R(*b[key]))
            for key, bend in (('r1', BEND1), ('r2', BEND2), ('r3', BEND3))]
    unreal.log("[MANUAL] 기준+델타: %s %s %s" % (BEND1, BEND2, BEND3))

sc = unreal.Vector(LENGTH, THICKNESS, THICKNESS)
ctrl.open_bracket("tune left thumb")
for i, bone in enumerate(('thumb_01_l', 'thumb_02_l', 'thumb_03_l')):
    rr = rots[i]
    pos = [unreal.Vector(*baseline[bone][fr]['loc']) for fr in (0,1)]
    scl = [sc, sc] if i == 0 else [unreal.Vector(*baseline[bone][fr]['scale']) for fr in (0,1)]
    ctrl.set_bone_track_keys(bone, pos, [rr.quaternion(), rr.quaternion()], scl)
ctrl.close_bracket()
unreal.log("[tune_left_thumb] 완료 — 길이 %.3f / 굵기 %.3f" % (LENGTH, THICKNESS))
