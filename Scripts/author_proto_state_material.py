# author_proto_state_material.py — 절차적 적 모션 S4: M_FPSREnemyProto 에 상태 반응 배선
#
# 실행(정식 에디터 헤드리스 — 라이브 에디터에서 돌리지 말 것, 인메모리 상태와 충돌한다):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="Scripts/author_proto_state_material.py"
#                        -unattended -nopause -nullrhi -nosplash -nosound -abslog=<로그>
#
# 무엇을 하나 — C++ 이 CPD 로 싣는 애니 상태를 머티리얼이 실제로 읽게 만든다.
# 계약 정본 = Source/FPSRoguelite/Public/Enemy/FPSRAnimCPDParams.h (슬롯 인덱스는 그 헤더가 단일 편집점).
#   슬롯 0 StateId     Idle=0 / Walk=1 / Attack=2 / Death=3
#   슬롯 1 EnterTime   상태 진입 월드시각(초) — 머티리얼 Time(GameTime)과 같은 시계
#   슬롯 2 Rate        루프=배율 / 원샷=1/지속시간
#   슬롯 3 Phase       인스턴스 위상 (이미 배선돼 있음 — 건드리지 않는다)
#   슬롯 4 LastHitTime 마지막 피격 월드시각, 0=미피격
#
# 멱등(idempotent): 이미 만들어진 파라미터·노드는 다시 만들지 않고 재사용한다. 여러 번 돌려도 안전하다.
import unreal, warnings
warnings.simplefilter("ignore")

MAT_PATH = "/Game/Assets/Characters/EnemyProto/M_FPSREnemyProto"
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary

mat = unreal.load_asset(MAT_PATH)
if not mat:
    raise SystemExit(f"[s4] 머티리얼을 못 찾음: {MAT_PATH}")


def owned(ex):
    o = ex.get_outer()
    while o and o != mat:
        o = o.get_outer()
    return o == mat


def expressions(cls=unreal.MaterialExpression):
    return [e for e in unreal.ObjectIterator(cls) if owned(e)]


def find_scalar(name):
    for e in expressions(unreal.MaterialExpressionScalarParameter):
        if str(e.get_editor_property("parameter_name")) == name:
            return e
    return None


def find_custom(desc):
    for e in expressions(unreal.MaterialExpressionCustom):
        if str(e.get_editor_property("description")) == desc:
            return e
    return None


def find_by_object_name(obj_name):
    """T3D 로 확인한 직렬화 오브젝트명으로 집는다 — 입력 핀(Input)이 protected 라
    파이썬으로 '무엇에 연결됐는지'를 읽을 수 없기 때문에, 그래프 상 위치를 이름으로 특정한다."""
    for e in expressions():
        if e.get_name() == obj_name:
            return e
    return None


def scalar_param(name, default, cpd_slot=None, x=-1800, y=0):
    """스칼라 파라미터를 만들거나(없으면) 기존 것을 재사용. cpd_slot 이 있으면 CPD 로 묶는다."""
    ex = find_scalar(name)
    created = ex is None
    if created:
        ex = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
        ex.set_editor_property("parameter_name", name)
    # 기본값은 재사용 시에도 매번 덮어쓴다 — 이 스크립트가 머티리얼 기본값의 단일 소스다. 처음엔
    # 생성 시에만 넣었는데, 그러면 값을 고쳐 다시 돌려도 "재사용"으로 빠져 조용히 옛 값이 남는다
    # (ShellOpacity 0.35→0.55 상향이 그렇게 한 번 유실됐다). 실사용 조정은 MI 에서 하므로 머티리얼
    # 기본값을 스크립트가 소유해도 사용자 작업을 덮지 않는다.
    ex.set_editor_property("default_value", default)
    if cpd_slot is not None:
        ex.set_editor_property("use_custom_primitive_data", True)
        ex.set_editor_property("primitive_data_index", cpd_slot)
    print(f"[s4] scalar {name:18s} {'생성' if created else '재사용'}"
          f"{'' if cpd_slot is None else f' (CPD slot {cpd_slot})'}")
    return ex


# ── 1. CPD 파라미터 (C++ 이 쓰는 쪽) ─────────────────────────────────────────────────────────────
# 기본값은 전부 0 이어야 한다. FPSRAnimCPDParams.h 의 계약 = "슬롯 기본값 0 = Idle·루프·큰 (T-0)에 관용".
# 클라 첫 스폰 시 BeginPlay 가 슬롯 3만 쓰므로 0/1/2 가 0인 순간이 실제로 존재한다 — Rate=0 이면
# 아래 HLSL 의 prog 가 0 이 되어 상태 오버레이가 통째로 소거된다(곱-소거 의존).
p_state = scalar_param("StateId",        0.0, cpd_slot=0, x=-1900, y=-600)
p_enter = scalar_param("StateEnterTime", 0.0, cpd_slot=1, x=-1900, y=-500)
p_rate  = scalar_param("StateRate",      0.0, cpd_slot=2, x=-1900, y=-400)
p_hit   = scalar_param("LastHitTime",    0.0, cpd_slot=4, x=-1900, y=300)

# ── 2. 튜닝 파라미터 (per-MI 상수, CPD 아님) ──────────────────────────────────────────────────────
# ⚠️ AttackLunge 는 캡슐 여유 안으로 제한할 것. WPO 는 시각만 밀고 트레이스는 캡슐에 맞으므로,
#    크게 잡으면 플레이어가 보이는 곳을 쐈는데 안 맞는다. 사망 산개가 없는 것도 같은 이유
#    (WPO 는 렌더 바운드를 키우지 않아 화면 가장자리에서 컬링 팝이 난다) — 수렴 소멸로 간다.
p_lunge  = scalar_param("AttackLunge",       15.0, x=-1900, y=-300)   # cm, 로컬 +X = 액터 전방
p_squash = scalar_param("AttackSquash",       0.18, x=-1900, y=-200)  # 0..1 수축 비율
p_hitdur = scalar_param("HitFlashDuration",   0.18, x=-1900, y=400)   # 초
p_hitint = scalar_param("HitFlashIntensity", 12.0,  x=-1900, y=500)

# 공격 모션 (사용자 결정 2026-08-25): 휴식 = 닫힌 껍질, 공격 = 벌어짐. 메시 쪽 GAP 을 0 으로 닫았고
# (gen_enemy_proto_meshes.py) 벌리는 일은 전부 여기서 한다 — 그래야 휴식 변위가 0 이라 C0-at-entry 가 성립한다.
p_open   = scalar_param("AttackOpen",      22.0, x=-1900, y=-100)  # cm, 쌍뿔 상/하 분리 거리
p_epull  = scalar_param("ElectronPull",     0.55, x=-1900, y=0)    # 0..1 전자가 핵으로 빨려드는 정도
p_esnap  = scalar_param("ElectronSnap",     0.45, x=-1900, y=100)  # 0..1 발사 순간 튕겨나가는 정도
p_shellop= scalar_param("ShellOpacity",     0.55, x=-1900, y=600)  # 십자 창의 불투명도 (그 밖은 불투명)
p_ramp   = scalar_param("OpenRampFrac",     0.12, x=-1900, y=-60)  # 열리는 데 쓰는 진행도 비율
p_close  = scalar_param("CloseRampFrac",    0.10, x=-1900, y=-20)  # 발사 직전 닫히는 비율
p_band   = scalar_param("WindowBandFrac",   0.30, x=-1900, y=700)  # 십자 창 폭(요소 반크기 대비)
p_frame  = scalar_param("FramePower",       3.0,  x=-1900, y=800)  # 외곽 프레임(프레넬) 날카로움

# ── 3. ProcWPO 에 상태 입력 추가 + HLSL 교체 ─────────────────────────────────────────────────────
wpo = find_custom("ProcWPO")
if not wpo:
    raise SystemExit("[s4] ProcWPO Custom 노드를 못 찾음")

WPO_CODE = """float3 p = LocalPos;
int id = (int)floor(UV.x);
float ph = PhaseIn * 6.28318;
float3 q = p;
if (MeshType < 0.5)
{
    float w = SpinSpeed * ((id == 2) ? -0.5 : 1.0);
    float a = w * T + ph;
    float s = sin(a), c = cos(a);
    q = float3(c*p.x - s*p.y, s*p.x + c*p.y, p.z);
}
else
{
    if (id == 0)
    {
        float a = SpinSpeed * 0.5 * T + ph;
        float s = sin(a), c = cos(a);
        q = float3(c*p.x - s*p.y, s*p.x + c*p.y, p.z);
    }
    else
    {
        float3 ax = (id == 1) ? float3(0,0,1) : ((id == 2) ? float3(0,0.70711,0.70711) : float3(0.70711,0,-0.70711));
        float sp = (id == 1) ? 1.0 : ((id == 2) ? 0.8 : 1.3);
        float a = OrbitSpeed * sp * T + ph * (float)id;
        float s = sin(a), c = cos(a);
        q = p*c + cross(ax, p)*s + ax*dot(ax, p)*(1.0-c);
    }
}
q.z += sin(BobSpeed * T + ph) * BobAmp;

// --- 상태 오버레이 (FPSRAnimCPDParams.h 계약) -------------------------------------------------
// prog = 원샷 진행도. Rate=0(슬롯 기본값 / 거리 LOD 프리즈)이면 0 이 되어 오버레이가 통째로 소거된다.
// 모든 항이 prog==0 에서 변위 0 이다(C0-at-entry) — 그래서 상태 전환에 이전상태 슬롯 없이도 팝이 없다.
float prog = saturate((T - EnterTime) * Rate);
if (StateId > 2.5)
{
    // Death: 요소가 원점으로 수렴하며 소멸. 끝(prog=1)에서 스케일 0 이라, 체류 종료 hide 의
    // 복제 지연이 비임계화된다 — 늦게 도착해도 이미 안 보인다.
    q = lerp(q, float3(0.0, 0.0, 0.0), prog * prog);
}
else if (StateId > 1.5)
{
    // Attack. 형태마다 다른 텔레그래프를 쓴다 — 종전의 공용 수축+전진은 1400cm 밖 원거리 적에게
    // 화면상 몇 픽셀이라 "조준 중"이 읽히지 않았다(PIE 2026-08-25).
    // 열림 곡선: 빠르게 열고 → 발사까지 **열린 채 유지** → 발사 순간 닫힌다(사용자 결정 2026-08-25).
    // 종전 sin(pi*prog) 는 한가운데서만 최대라 "조준 중"이 한순간 스쳐 지나갔다. 양 끝은 여전히 0 이라
    // 진입/이탈 팝은 없다(C0-at-entry/exit).
    float w = smoothstep(0.0, OpenRampFrac, prog) * (1.0 - smoothstep(1.0 - CloseRampFrac, 1.0, prog));
    if (MeshType < 0.5)
    {
        // 쌍뿔 = 조개. 상뿔(id 0)은 +Z, 하뿔(id 1)은 -Z 로 벌어지고 코어(id 2)는 제자리에 남아
        // 드러난다. 휴식 시엔 껍질이 닫혀 있으므로(메시 GAP=0) 벌어지는 것 자체가 공격 신호다.
        float dir = (id == 0) ? 1.0 : ((id == 1) ? -1.0 : 0.0);
        q.z += dir * w * AttackOpen;
        q *= (1.0 - w * AttackSquash);
        q.x += w * AttackLunge;
    }
    else
    {
        // 원자 큐브 = 차징. 전자(id 1..3)가 핵으로 빨려 들어갔다가 발사 순간 튕겨 나간다.
        // pull 은 앞 80%를 삼각(0->1->0), kick 은 마지막 20%에만 사인으로 실린다 — 발사 시점
        // (prog=1)에 정확히 방출이 끝나고, 양 끝 변위가 0 이라 여기서도 팝이 없다.
        float pull = (prog < 0.8) ? (prog / 0.8) : (1.0 - (prog - 0.8) / 0.2);
        float kick = (prog < 0.8) ? 0.0 : sin((prog - 0.8) / 0.2 * 3.14159265);
        if (id > 0)
        {
            q *= (1.0 - pull * ElectronPull + kick * ElectronSnap);
        }
    }
}
return q - p;"""

WPO_NEW_INPUTS = [("StateId", p_state), ("EnterTime", p_enter), ("Rate", p_rate),
                  ("AttackLunge", p_lunge), ("AttackSquash", p_squash),
                  ("AttackOpen", p_open), ("ElectronPull", p_epull), ("ElectronSnap", p_esnap),
                  ("OpenRampFrac", p_ramp), ("CloseRampFrac", p_close)]

existing = [str(ci.get_editor_property("input_name")) for ci in wpo.get_editor_property("inputs")]
to_add = [(n, e) for (n, e) in WPO_NEW_INPUTS if n not in existing]
if to_add:
    ins = list(wpo.get_editor_property("inputs"))
    for name, _ in to_add:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        ins.append(ci)
    wpo.set_editor_property("inputs", ins)
    print(f"[s4] ProcWPO 입력 추가: {[n for n, _ in to_add]}")
else:
    print("[s4] ProcWPO 입력 이미 존재 — 추가 없음")

wpo.set_editor_property("code", WPO_CODE)
for name, ex in WPO_NEW_INPUTS:
    ok = mel.connect_material_expressions(ex, "", wpo, name)
    print(f"[s4] connect {name:14s} -> ProcWPO : {ok}")
    if not ok:
        raise SystemExit(f"[s4] ProcWPO.{name} 연결 실패 — 중단(반환값 검사, 조용한 실패 방지)")

# ── 4. 피격 플래시: 새 Custom 노드 → 기존 이미시브에 Add ──────────────────────────────────────────
# 기존 배선(T3D 실측): EmissiveColor <- Multiply_1( Multiply_0(CoreColor × CoreEmissive) × ElemMask ).
# 플래시는 코어 마스크와 무관하게 몸 전체가 번쩍여야 하므로 곱이 아니라 Add 로 얹는다.
flash = find_custom("HitFlash")
if not flash:
    flash = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -1200, 400)
    flash.set_editor_property("description", "HitFlash")
    ins = []
    for n in ("T", "LastHitTime", "Duration", "Intensity", "Tint"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    flash.set_editor_property("inputs", ins)
    print("[s4] HitFlash Custom 노드 생성")
else:
    print("[s4] HitFlash Custom 노드 재사용")

flash.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
flash.set_editor_property("code", """// LastHitTime <= 0 = 이 생에서 아직 안 맞음. 이 가드가 없으면 레벨 시작 직후
// (T - 0) 이 커서가 아니라, 슬롯 기본값 0 이 "0초에 맞았다"로 읽혀 오발 플래시가 난다.
if (LastHitTime <= 0.0) { return float3(0.0, 0.0, 0.0); }
float a = saturate(1.0 - (T - LastHitTime) / max(Duration, 0.0001));
return Tint * (a * a * Intensity);""")

t_node = find_by_object_name("MaterialExpressionTime_0")
base_color = find_by_object_name("MaterialExpressionVectorParameter_0")  # T3D 실측: BaseColor 출력의 소스
emissive_mul = find_by_object_name("MaterialExpressionMultiply_1")       # T3D 실측: 기존 EmissiveColor 소스
for nm, node in (("Time", t_node), ("BaseColor", base_color), ("Multiply_1", emissive_mul)):
    if not node:
        raise SystemExit(f"[s4] 그래프에서 {nm} 노드를 못 찾음 — 배선이 바뀌었다면 T3D 를 다시 떠서 이름을 맞출 것")

for src, pin in ((t_node, "T"), (p_hit, "LastHitTime"), (p_hitdur, "Duration"),
                 (p_hitint, "Intensity"), (base_color, "Tint")):
    ok = mel.connect_material_expressions(src, "", flash, pin)
    print(f"[s4] connect -> HitFlash.{pin:12s} : {ok}")
    if not ok:
        raise SystemExit(f"[s4] HitFlash.{pin} 연결 실패 — 중단")

add = None
for e in expressions(unreal.MaterialExpressionAdd):
    add = e  # 이 머티리얼엔 Add 가 이것 하나뿐이다(멱등 재실행 시 재사용)
    break
if not add:
    add = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, -900, 200)
    print("[s4] Emissive Add 노드 생성")
else:
    print("[s4] Emissive Add 노드 재사용")

for src, pin in ((emissive_mul, "A"), (flash, "B")):
    ok = mel.connect_material_expressions(src, "", add, pin)
    print(f"[s4] connect -> Add.{pin} : {ok}")
    if not ok:
        raise SystemExit(f"[s4] Add.{pin} 연결 실패 — 중단")

ok = mel.connect_material_property(add, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
print(f"[s4] connect Add -> EmissiveColor : {ok}")
if not ok:
    raise SystemExit("[s4] EmissiveColor 연결 실패 — 중단")

# ── 4-b. 반투명 껍질 — 코어가 항상 비쳐 보이게 (사용자 결정 2026-08-25, "(다) 반투명") ──────────────
# 껍질을 닫아 놓으면(메시 GAP=0) 불투명일 때 코어가 아예 안 보인다. 그래서 블렌드를 Translucent 로
# 바꾸고, 불투명도를 요소별로 나눈다: 코어(= EmissiveElementId 가 가리키는 요소)는 1.0, 껍질은
# ShellOpacity. ElemMask 가 이미 "이 픽셀이 코어인가"를 0/1 로 돌려주므로 그것으로 lerp 한다.
#
# ⚠️ 성능 — 반투명은 별도 패스라 early-z 를 잃고 겹칠수록 오버드로우가 쌓인다. 적 200~300 이 뭉치는
#    게임이라 제1원리("적 수백을 싸게")·ADR 0007 예산(4ms)에 직접 걸린다. 하니스 A/B 로 재고,
#    예산을 넘기면 Masked+디더(베이스 패스 유지, 훨씬 쌈)로 내려가는 것이 대안이다.
# ℹ️ 메시 내부 정렬 — 반투명은 삼각형 단위로 정렬되지 않는다. 다만 생성기가 껍질을 먼저, 코어 구를
#    나중에 쓰므로(gen_enemy_proto_meshes.py) 코어가 뒤에 그려져 껍질 위로 비친다 — 원하는 그림이다.
elem_mask = find_custom("ElemMask")
if not elem_mask:
    raise SystemExit("[s4] ElemMask Custom 노드를 못 찾음 — 불투명도 분기를 걸 수 없다")

# 불투명도 마스크 = 십자 창 + 외곽 프레임 (사용자 그림 2026-08-25).
#   · 기본은 **불투명**(돌/블럭). 그림의 회색 부분.
#   · 각 요소의 중앙을 지나는 **십자 띠**만 반투명 — 그 창으로 내부 코어가 비친다. 그림의 노란 부분.
#   · **외곽 프레임**은 항상 불투명. 그림의 검은 테두리. 기하학적 모서리를 셰이더에서 찾는 대신
#     프레넬(시선에 스치는 픽셀)로 만든다 — 어떤 형상에도 통하고 3연산이면 끝난다.
#   · 코어 요소는 항상 불투명(EmissiveElementId, ElemMask 가 이미 판별해 준다).
# 십자 판정은 **회전 전 로컬 위치**(LocalPos)로 한다 — 전자가 공전해도 마스크가 표면에 붙어 함께 돈다.
op_mask = find_custom("OpacityMask")
if not op_mask:
    op_mask = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -1200, 700)
    op_mask.set_editor_property("description", "OpacityMask")
    ins = []
    for n in ("LocalPos", "UV", "MeshType", "CoreMask", "ShellOpacity", "BandFrac", "Fresnel"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    op_mask.set_editor_property("inputs", ins)
    print("[s4] OpacityMask Custom 노드 생성")
else:
    print("[s4] OpacityMask Custom 노드 재사용")

op_mask.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
op_mask.set_editor_property("code", """// 요소별 중심/반크기 — 메시 생성기(gen_enemy_proto_meshes.py)와의 계약이다. 값이 바뀌면 양쪽을 같이 고칠 것.
int id = (int)floor(UV.x);
float3 c = float3(0.0, 0.0, 0.0);
float hs = 75.0;
if (MeshType < 0.5)
{
    // 쌍뿔: 상뿔/하뿔/코어 전부 원점 기준. 반크기 = 꼭짓점 높이.
    hs = 75.0;
}
else
{
    // 원자 큐브: 핵(half 30) · 전자 3개(half 10, 궤도 반경 80) · 코어 구(반지름 14).
    if (id == 1 || id == 2) { c = float3(80.0, 0.0, 0.0); hs = 10.0; }
    else if (id == 3)       { c = float3(0.0, 80.0, 0.0); hs = 10.0; }
    else if (id == 4)       { hs = 14.0; }
    else                    { hs = 30.0; }
}

// 십자 띠: 요소 로컬에서 세 축 중 **하나라도** 중심선에 가까우면 창이다. 면 위의 점은 지배축이
// 반크기에 붙어 있으므로, 나머지 두 축 가운데 하나가 가까울 때만 참이 된다 = 그림의 십자.
float3 a = abs(LocalPos - c);
float band = BandFrac * hs;
float inCross = (min(a.x, min(a.y, a.z)) < band) ? 1.0 : 0.0;

// 기본 불투명, 십자 창만 반투명. 프레임(프레넬)과 코어는 다시 불투명으로 끌어올린다.
float o = lerp(1.0, ShellOpacity, inCross);
o = max(o, Fresnel);
return lerp(o, 1.0, saturate(CoreMask));""")

fres = None
for e in expressions(unreal.MaterialExpressionFresnel):
    fres = e
    break
if not fres:
    fres = mel.create_material_expression(mat, unreal.MaterialExpressionFresnel, -1500, 800)
    print("[s4] Fresnel 노드 생성")
ok = mel.connect_material_expressions(p_frame, "", fres, "ExponentIn")
print(f"[s4] connect FramePower -> Fresnel : {ok}")

t_coord = find_by_object_name("MaterialExpressionTextureCoordinate_0")
local_pos = find_by_object_name("MaterialExpressionLocalPosition_0")
mesh_type = find_scalar("MeshType")
for nm, node in (("TexCoord", t_coord), ("LocalPosition", local_pos), ("MeshType", mesh_type)):
    if not node:
        raise SystemExit(f"[s4] {nm} 노드를 못 찾음")

for src, pin in ((local_pos, "LocalPos"), (t_coord, "UV"), (mesh_type, "MeshType"),
                 (elem_mask, "CoreMask"), (p_shellop, "ShellOpacity"), (p_band, "BandFrac"),
                 (fres, "Fresnel")):
    ok = mel.connect_material_expressions(src, "", op_mask, pin)
    print(f"[s4] connect -> OpacityMask.{pin:12s} : {ok}")
    if not ok:
        raise SystemExit(f"[s4] OpacityMask.{pin} 연결 실패 — 중단")

mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
# 반투명 기본값은 조명을 받지 않는 Unlit 이라 셰이딩 모델을 명시적으로 되돌려 놔야 한다.
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
mat.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)

ok = mel.connect_material_property(op_mask, "", unreal.MaterialProperty.MP_OPACITY)
print(f"[s4] connect OpacityMask -> Opacity : {ok}")
if not ok:
    raise SystemExit("[s4] Opacity 출력 연결 실패 — 중단")

# ── 5. 고아 노드 정리 ────────────────────────────────────────────────────────────────────────────
# 위상 소스를 오브젝트 위치 해시 → CPD 슬롯3 으로 갈아끼울 때(fix_proto_material_phase.py) 입력만
# 교체하고 노드는 남겨 둔 잔재다. 동작엔 영향 없지만 다음 사람이 "위상이 위치에서 온다"로 오독한다.
orphans = expressions(unreal.MaterialExpressionObjectPositionWS)
for o in orphans:
    # UMaterial 의 표현식 배열은 5.x 에서 프로퍼티로 노출되지 않는다(`expression_collection` 없음) —
    # 정식 경로는 MaterialEditingLibrary 의 삭제 API 다.
    mel.delete_material_expression(mat, o)
print(f"[s4] 고아 ObjectPositionWS 제거: {len(orphans)}개")

# ── 5-b. MI 의 코어 요소 지정 갱신 ──────────────────────────────────────────────────────────────
# 원자 큐브에 내부 코어 구(element 4)가 새로 생겼으므로(gen_enemy_proto_meshes.py) "어느 요소가
# 코어인가"를 가리키는 EmissiveElementId 를 핵 큐브(0) → 코어 구(4) 로 옮긴다. 이 값이 이미시브와
# 불투명도 양쪽의 코어 판정(ElemMask)을 동시에 몰기 때문에, 안 옮기면 껍질인 핵 큐브가 빛나고
# 정작 코어 구는 십자 창 너머로 어둡게 남는다. 쌍뿔은 종전대로 element 2 라 건드리지 않는다.
MI_CORE_ELEMENT = {"MI_EnemyProto_AtomCubes": 4.0}
for mi_name, elem_id in MI_CORE_ELEMENT.items():
    mi_path = f"{'/'.join(MAT_PATH.split('/')[:-1])}/{mi_name}"
    mi = unreal.load_asset(mi_path)
    if not mi:
        print(f"[s4] ⚠️ MI 없음, 건너뜀: {mi_path}")
        continue
    mel.set_material_instance_scalar_parameter_value(mi, "EmissiveElementId", elem_id)
    saved_mi = eal.save_asset(mi_path)
    print(f"[s4] MI {mi_name}.EmissiveElementId = {elem_id} (save={saved_mi})")

# ── 6. 컴파일 + 저장 ─────────────────────────────────────────────────────────────────────────────
# ⚠️ 머티리얼 무음 컴파일 실패가 이 저장소의 상습 함정이다(If 노드 스칼라 전용 / MaterialAttributes
#    핀 미연결 시 WPO 상수 0). recompile 후 별도 검증 스크립트로 인스트럭션 수를 확인할 것.
mel.recompile_material(mat)
saved = eal.save_asset(MAT_PATH)
print(f"[s4] recompile 완료, save_asset = {saved}")
print("[s4] DONE")
