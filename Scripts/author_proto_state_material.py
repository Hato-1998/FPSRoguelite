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


def ensure_custom(desc, input_names, x=0, y=0, required=False, prune=False):
    """Custom 노드를 만들거나 재사용하되, **입력 핀 목록을 항상 계약과 일치시킨다.**

    재사용 경로에서 핀을 맞추지 않으면, 나중에 계약이 늘었을 때 이미 만들어진 노드엔 그 핀이 없어
    connect 가 조용히 False 를 낸다(실사고 2026-08-25: OpacityMask 에 FrameWidthFrac·TexScale 을
    더했는데 재사용 분기가 핀을 안 붙여 스크립트가 중단됐다 — ProcWPO 만 보정 로직이 있었다).
    기존 항목은 순서도 연결도 건드리지 않고, 없는 핀만 뒤에 덧붙인다.

    required=True 면 노드가 없을 때 생성하지 않고 중단한다(그래프에 이미 있어야 하는 노드용).

    prune=True 면 계약에 없는 핀을 **제거**한다. 이 스크립트가 그 노드의 입력을 100% 소유할 때만 켤 것 —
    ProcWPO 는 최초 저작 때부터 있던 입력 9개(LocalPos·UV·PhaseIn·T·MeshType·SpinSpeed·BobAmp·
    BobSpeed·OrbitSpeed)를 이 스크립트가 모르므로 절대 켜면 안 된다. 죽은 핀을 남겨 두는 것이 왜
    위험한가: UMaterialExpressionCustom 은 이름 붙은 입력이 **연결되지 않으면 컴파일 에러**를 낸다
    (`Custom material %s missing input`). 즉 코드가 더는 안 쓰는 핀이라도 연결이 끊기는 순간 머티리얼이
    통째로 죽는다 — 실제로 OpacityMask 에 프레넬 방식을 기하 모서리 계산으로 갈아엎을 때 Fresnel 핀만
    남아 그 상태였다.
    """
    ex = find_custom(desc)
    created = ex is None
    if created:
        if required:
            raise SystemExit(f"[s4] {desc} Custom 노드를 못 찾음")
        ex = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
        ex.set_editor_property("description", desc)
    have = [str(ci.get_editor_property("input_name")) for ci in ex.get_editor_property("inputs")]
    missing = [n for n in input_names if n not in have]
    stale = [n for n in have if n not in input_names] if prune else []
    if missing or stale:
        ins = [ci for ci in ex.get_editor_property("inputs")
               if str(ci.get_editor_property("input_name")) not in stale]
        for n in missing:
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", n)
            ins.append(ci)
        ex.set_editor_property("inputs", ins)
    print(f"[s4] Custom {desc:12s} {'생성' if created else '재사용'}"
          f"{'' if not missing else f' + 입력 추가 {missing}'}"
          f"{'' if not stale else f' + 죽은 핀 제거 {stale}'}")
    return ex


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
p_frame  = scalar_param("FrameWidthFrac",   0.10, x=-1900, y=800)  # 모서리 프레임 폭(요소 반크기 대비)
p_texscale= scalar_param("TexScale",        0.01, x=-1900, y=900)  # 로컬 위치 → UV 배율(질감 타일링)

# ── 3. ProcWPO 에 상태 입력 추가 + HLSL 교체 ─────────────────────────────────────────────────────
# 입력 핀 보정은 WPO_NEW_INPUTS 를 정의한 뒤(아래) ensure_custom 으로 한다.

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

# ProcWPO 는 머티리얼 최초 저작 때부터 있는 노드다 — 여기서 만들면 원래 입력(LocalPos·T·SpinSpeed…)이
# 없는 빈 노드가 되므로 required=True 로 부재를 오류 처리한다.
wpo = ensure_custom("ProcWPO", [n for n, _ in WPO_NEW_INPUTS], required=True)

wpo.set_editor_property("code", WPO_CODE)
for name, ex in WPO_NEW_INPUTS:
    ok = mel.connect_material_expressions(ex, "", wpo, name)
    print(f"[s4] connect {name:14s} -> ProcWPO : {ok}")
    if not ok:
        raise SystemExit(f"[s4] ProcWPO.{name} 연결 실패 — 중단(반환값 검사, 조용한 실패 방지)")

# ── 4. 피격 플래시: 새 Custom 노드 → 기존 이미시브에 Add ──────────────────────────────────────────
# 기존 배선(T3D 실측): EmissiveColor <- Multiply_1( Multiply_0(CoreColor × CoreEmissive) × ElemMask ).
# 플래시는 코어 마스크와 무관하게 몸 전체가 번쩍여야 하므로 곱이 아니라 Add 로 얹는다.
flash = ensure_custom("HitFlash", ("T", "LastHitTime", "Duration", "Intensity", "Tint"), -1200, 400, prune=True)

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

# ── 4-b. 껍질 = 불투명, 십자 창만 뚫린다 (사용자 결정 2026-08-25 "십자 창으로만 (원래 설계)") ───────
# 껍질을 닫아 놓으면(메시 GAP=0) 코어가 안 보이므로 십자 창으로 들여다보게 한다. 불투명도는 요소별로
# 나눈다: 코어(= EmissiveElementId 가 가리키는 요소)는 1.0, 껍질은 ShellOpacity. ElemMask 가 이미
# "이 픽셀이 코어인가"를 0/1 로 돌려주므로 그것으로 lerp 한다.
#
# 🔄 **Translucent -> Masked 전환 (PIE 2026-08-25 사용자 보고 "코어에 그림자 얼룩")**
# 종전 Translucent 가 실제로 낸 그림은 "반투명 창"이 아니라 얼룩이었다. **UE 반투명은 메시 안에서
# 깊이 정렬을 하지 않는다** — 껍질과 코어 중 누가 앞인지 정할 근거가 없어, 카메라가 움직일 때마다
# 승패가 뒤집히며 코어에 그림자처럼 보이는 반점이 생긴다. (종전 주석은 "생성기가 껍질을 먼저, 코어를
# 나중에 쓰므로 코어가 위로 비친다"고 적어 뒀는데, 그 제출 순서 의존이 바로 여기서 깨졌다.)
#
# 조명이 아니라 정렬이 원인이라는 증거: 코어는 CoreColor x CoreEmissive(30) 이미시브인데
# **이미시브는 조명 계산이 끝난 뒤 더해지므로 그림자가 어둡게 만들 수 없다.** 게다가 실측 당시 두 MI
# 모두 ShellOpacity=1.0(창 닫힘)이라 껍질이 코어를 완전히 가려야 했는데 코어가 뚫고 보였다 — 정렬이
# 무너졌다는 직접 증거다.
#
# Masked 는 깊이를 쓰므로 정렬이 정확해져 얼룩이 사라지고, 베이스 패스에 남아 early-z 를 되찾는다 —
# 적 200~300 이 뭉치는 게임이라 제1원리("적 수백을 싸게")·ADR 0007 예산(4ms)에 직접 걸리는 차이다.
# Masked 는 이분법(그리거나 자르거나)이라 중간 불투명도는 **디더**로 낸다(아래 WindowDither):
# ShellOpacity 0 = 완전히 잘린 진짜 구멍(디더 노이즈 0) / 1 = 통짜 불투명 / 사이 = TSR 이 풀어 주는
# 확률적 반투명. 즉 종전 Translucent 가 하던 일의 상위집합이면서 정렬이 산다.
# ⚠️ 껍질이 한 겹이라(two_sided=False) 창 너머로 코어가 안 채우는 각도에선 뒷배경이 비칠 수 있다.
#    그때의 손잡이는 two_sided=True 지만 껍질 셰이딩 비용이 2배라 스웜엔 비싸다 — PIE 로 먼저 볼 것.
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
op_mask = ensure_custom("OpacityMask",
                        ("LocalPos", "UV", "MeshType", "CoreMask", "ShellOpacity", "BandFrac",
                         "FrameWidthFrac", "TexScale"), -1200, 700, prune=True)

op_mask.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
op_mask.set_editor_property("code", """// 요소별 중심/반크기 — 메시 생성기(gen_enemy_proto_meshes.py)와의 계약이다. 값이 바뀌면 양쪽을 같이 고칠 것.
int id = (int)floor(UV.x);
float3 c = float3(0.0, 0.0, 0.0);
float hs = 75.0;
bool isCube = (MeshType >= 0.5);
if (isCube)
{
    if (id == 1 || id == 2) { c = float3(80.0, 0.0, 0.0); hs = 10.0; }
    else if (id == 3)       { c = float3(0.0, 80.0, 0.0); hs = 10.0; }
    else if (id == 4)       { hs = 14.0; }
    else                    { hs = 30.0; }
}

float3 lp = LocalPos - c;
float3 a = abs(lp);

// ── 십자 창 ──────────────────────────────────────────────────────────────────────────────────
// 요소 로컬에서 세 축 중 하나라도 중심선에 가까우면 창. 면 위의 점은 지배축이 반크기에 붙어 있으니
// 나머지 두 축 중 하나가 가까울 때만 참 = 그림의 십자.
float band = BandFrac * hs;
float inCross = (min(a.x, min(a.y, a.z)) < band) ? 1.0 : 0.0;

// ── 기하 모서리 프레임 ───────────────────────────────────────────────────────────────────────
// 프레넬(시선 각도)로는 정면에서 본 능선이 안 잡힌다 — 실제 모서리를 형태별로 계산한다.
float edge = 0.0;
float fw = FrameWidthFrac;
if (isCube)
{
    // 큐브: 면=한 축만 반크기에 붙음 / 변=두 축 / 꼭짓점=세 축. 두 축 이상이면 모서리다.
    float3 n = a / max(hs, 1e-4);
    float cnt = (n.x > 1.0 - fw ? 1.0 : 0.0) + (n.y > 1.0 - fw ? 1.0 : 0.0) + (n.z > 1.0 - fw ? 1.0 : 0.0);
    edge = (cnt >= 2.0) ? 1.0 : 0.0;
}
else
{
    // 오각 쌍뿔: 모서리 셋 — ①꼭짓점에서 적도 링 정점으로 내려오는 능선 5개 ②적도 림 ③꼭짓점.
    // 능선은 방위각으로 잡는다: 링 정점이 2*pi*k/5 마다 있으므로 그 격자에 가까우면 능선이다.
    float ang = atan2(lp.y, lp.x) * 5.0 / 6.28318530718;   // 정점마다 정수
    float f = abs(frac(ang) - 0.5) * 2.0;                   // 정점에서 1, 면 한가운데서 0
    float rimZ = abs(lp.z) / 75.0;                          // 적도 0, 꼭짓점 1
    edge = (f > 1.0 - fw * 3.0) ? 1.0 : 0.0;                // ① 능선
    edge = max(edge, (rimZ < fw) ? 1.0 : 0.0);              // ② 적도 림
    edge = max(edge, (rimZ > 1.0 - fw) ? 1.0 : 0.0);        // ③ 꼭짓점 근처
}

// ── 불투명도 ─────────────────────────────────────────────────────────────────────────────────
// 기본 불투명(돌/블럭) · 십자 창만 반투명 · 모서리와 코어는 다시 불투명.
float o = lerp(1.0, ShellOpacity, inCross);
o = max(o, edge);
o = lerp(o, 1.0, saturate(CoreMask));

// ── 질감용 투영 UV ───────────────────────────────────────────────────────────────────────────
// 메시 UV 는 요소 ID 인코딩이라 텍스처를 물릴 수 없다. 지배 노멀 축으로 평면 투영해 UV 를 만든다 —
// 3중 투영(triplanar)의 1/3 비용이고, 블럭·돌처럼 결이 강하지 않은 질감엔 45도 이음매가 안 보인다.
float2 puv;
if (a.x >= a.y && a.x >= a.z)      puv = lp.yz;
else if (a.y >= a.z)               puv = lp.xz;
else                               puv = lp.xy;

return float4(o, edge, puv * TexScale);""")

t_coord = find_by_object_name("MaterialExpressionTextureCoordinate_0")
local_pos = find_by_object_name("MaterialExpressionLocalPosition_0")
mesh_type = find_scalar("MeshType")
for nm, node in (("TexCoord", t_coord), ("LocalPosition", local_pos), ("MeshType", mesh_type)):
    if not node:
        raise SystemExit(f"[s4] {nm} 노드를 못 찾음")

for src, pin in ((local_pos, "LocalPos"), (t_coord, "UV"), (mesh_type, "MeshType"),
                 (elem_mask, "CoreMask"), (p_shellop, "ShellOpacity"), (p_band, "BandFrac"),
                 (p_frame, "FrameWidthFrac"), (p_texscale, "TexScale")):
    ok = mel.connect_material_expressions(src, "", op_mask, pin)
    print(f"[s4] connect -> OpacityMask.{pin:12s} : {ok}")
    if not ok:
        raise SystemExit(f"[s4] OpacityMask.{pin} 연결 실패 — 중단")

mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
# translucency_lighting_mode 는 Masked 에선 컴파일에 들어가지 않으므로 손대지 않는다(남은 값은 무해).
# 반대로 opacity_mask_clip_value 는 여기서 살아난다 — DitherTemporalAA 는 엔진 기본값(0.3333) 기준으로
# 설계된 함수라 그대로 둔다.

# 마스크 노드는 float4(x=불투명도, y=모서리, zw=투영UV)를 낸다 — 세 소비처가 같은 계산을 공유하도록
# 한 노드에 모았다(요소 중심·반크기 판정이 셋 다 필요하다). 성분별로 갈라 쓴다.
def component_mask(name, r, g, b_, a_, x, y):
    for e in expressions(unreal.MaterialExpressionComponentMask):
        if str(e.get_editor_property("desc")) == name:
            return e
    cm = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, x, y)
    cm.set_editor_property("desc", name)
    for prop, val in (("r", r), ("g", g), ("b", b_), ("a", a_)):
        cm.set_editor_property(prop, val)
    ok = mel.connect_material_expressions(op_mask, "", cm, "")
    print(f"[s4] ComponentMask {name} 생성, connect={ok}")
    if not ok:
        raise SystemExit(f"[s4] ComponentMask {name} 연결 실패 — 중단")
    return cm

m_opacity = component_mask("MaskOpacity", True, False, False, False, -900, 700)
m_edge    = component_mask("MaskEdge",    False, True, False, False, -900, 800)
m_uv      = component_mask("MaskUV",      False, False, True, True,  -900, 900)

# 디더 — 엔진이 정확히 이 용도로 내놓는 함수를 쓴다(직접 HLSL 로 짜면 View.StateFrameIndexMod8 같은
# 엔진 내부 심볼에 의존해 버전마다 깨진다). 실측 핀 이름: 입력 "Alpha Threshold"/"Random", 출력 "Result".
# "Random" 은 선택 입력이라 비워 두면 함수가 기본 시퀀스를 쓴다.
DITHER_FN = "/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA"
dither = None
for e in expressions(unreal.MaterialExpressionMaterialFunctionCall):
    if str(e.get_editor_property("desc")) == "WindowDither":
        dither = e
        break
if not dither:
    fn = unreal.load_asset(DITHER_FN)
    if not fn:
        raise SystemExit(f"[s4] 디더 함수를 못 찾음: {DITHER_FN}")
    dither = mel.create_material_expression(mat, unreal.MaterialExpressionMaterialFunctionCall, -700, 700)
    dither.set_editor_property("desc", "WindowDither")
    # set_material_function 을 쓴다 — set_editor_property 로 넣으면 핀 배열이 재구축되지 않아
    # 아래 connect 가 조용히 False 를 낸다.
    dither.set_material_function(fn)
    print("[s4] WindowDither(DitherTemporalAA) 생성")
else:
    print("[s4] WindowDither 재사용")

ok = mel.connect_material_expressions(m_opacity, "", dither, "Alpha Threshold")
print(f"[s4] connect MaskOpacity -> WindowDither.Alpha Threshold : {ok}")
if not ok:
    raise SystemExit("[s4] WindowDither 입력 연결 실패 — 중단")

ok = mel.connect_material_property(dither, "", unreal.MaterialProperty.MP_OPACITY_MASK)
print(f"[s4] connect WindowDither -> OpacityMask : {ok}")
if not ok:
    raise SystemExit("[s4] OpacityMask 출력 연결 실패 — 중단")

# ── 4-c. 질감 소켓 + 모서리 어둡게 ───────────────────────────────────────────────────────────────
# 사용자가 돌/블럭 텍스처를 찾아 넣을 자리를 미리 판다((가) 채택, 2026-08-25). 기본값은 흰색
# 엔진 텍스처라 지금은 BaseColor 가 종전과 동일하게 보이고, MI 에서 텍스처만 지정하면 켜진다.
# UV 는 메시 UV 가 아니라 마스크 노드가 낸 **투영 UV** 를 쓴다 — 메시 UV 는 요소 ID 인코딩이라
# 텍스처를 물리면 뭉개진다.
tex = None
for e in expressions(unreal.MaterialExpressionTextureSampleParameter2D):
    tex = e
    break
if not tex:
    tex = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -700, 900)
    tex.set_editor_property("parameter_name", "BaseTexture")
    white = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    if white:
        tex.set_editor_property("texture", white)
    print("[s4] BaseTexture 파라미터 생성(기본=흰색, MI 에서 교체)")
else:
    print("[s4] BaseTexture 파라미터 재사용")
ok = mel.connect_material_expressions(m_uv, "", tex, "UVs")
print(f"[s4] connect 투영UV -> BaseTexture.UVs : {ok}")
if not ok:
    raise SystemExit("[s4] BaseTexture.UVs 연결 실패 — 중단")

# BaseColor = (저작 색 × 질감) 을 모서리에서 FrameColor 로 대체. 그림의 검은 테두리에 해당한다.
frame_color = None
for e in expressions(unreal.MaterialExpressionVectorParameter):
    if str(e.get_editor_property("parameter_name")) == "FrameColor":
        frame_color = e
        break
if not frame_color:
    frame_color = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -1200, 1000)
    frame_color.set_editor_property("parameter_name", "FrameColor")
    frame_color.set_editor_property("default_value", unreal.LinearColor(0.02, 0.02, 0.03, 1.0))
    print("[s4] FrameColor 파라미터 생성")

tinted = None
for e in expressions(unreal.MaterialExpressionMultiply):
    if str(e.get_editor_property("desc")) == "BaseTinted":
        tinted = e
        break
if not tinted:
    tinted = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -500, 850)
    tinted.set_editor_property("desc", "BaseTinted")
    print("[s4] BaseTinted Multiply 생성")
for src, pin in ((base_color, "A"), (tex, "B")):
    ok = mel.connect_material_expressions(src, "", tinted, pin)
    if not ok:
        raise SystemExit(f"[s4] BaseTinted.{pin} 연결 실패 — 중단")

edge_lerp = None
for e in expressions(unreal.MaterialExpressionLinearInterpolate):
    if str(e.get_editor_property("desc")) == "EdgeTint":
        edge_lerp = e
        break
if not edge_lerp:
    edge_lerp = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -300, 850)
    edge_lerp.set_editor_property("desc", "EdgeTint")
    print("[s4] EdgeTint Lerp 생성")
for src, pin in ((tinted, "A"), (frame_color, "B"), (m_edge, "Alpha")):
    ok = mel.connect_material_expressions(src, "", edge_lerp, pin)
    if not ok:
        raise SystemExit(f"[s4] EdgeTint.{pin} 연결 실패 — 중단")

# ── 4-d. 코어를 조명에서 떼어낸다 (사용자 결정 2026-08-25) ───────────────────────────────────────
# 요구의 실체 = "생길 이유가 없는 명암이 코어에 생기는 게 문제"(밝기가 문자 그대로 불변이어야 한다는
# 뜻은 아니다). 코어 픽셀의 BaseColor 와 Specular 를 0 으로 눌러 **조명 항을 통째로 없앤다** — 남는
# 것이 이미시브뿐이라 광원도 그림자도 시선 각도도 코어를 건드릴 수 없다. ElemMask 가 이미 계산돼
# 있으므로 OneMinus 하나 + 곱 둘이면 끝난다(사실상 공짜).
# ℹ️ 위 Masked 전환(얼룩=정렬)과는 별개의 조치다. 둘 다 필요하다 — 정렬을 고쳐도 조명은 남고,
#    조명을 떼어도 껍질의 명암은 그대로 정렬을 타기 때문이다.
one_minus_core = None
for e in expressions(unreal.MaterialExpressionOneMinus):
    if str(e.get_editor_property("desc")) == "ShellOnly":
        one_minus_core = e
        break
if not one_minus_core:
    one_minus_core = mel.create_material_expression(mat, unreal.MaterialExpressionOneMinus, -1000, 1150)
    one_minus_core.set_editor_property("desc", "ShellOnly")
    print("[s4] ShellOnly(OneMinus CoreMask) 생성")
ok = mel.connect_material_expressions(elem_mask, "", one_minus_core, "")
if not ok:
    raise SystemExit("[s4] ShellOnly 입력 연결 실패 — 중단")

core_unlit = None
for e in expressions(unreal.MaterialExpressionMultiply):
    if str(e.get_editor_property("desc")) == "CoreUnlitBase":
        core_unlit = e
        break
if not core_unlit:
    core_unlit = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -150, 850)
    core_unlit.set_editor_property("desc", "CoreUnlitBase")
    print("[s4] CoreUnlitBase Multiply 생성")
for src, pin in ((edge_lerp, "A"), (one_minus_core, "B")):
    ok = mel.connect_material_expressions(src, "", core_unlit, pin)
    if not ok:
        raise SystemExit(f"[s4] CoreUnlitBase.{pin} 연결 실패 — 중단")

ok = mel.connect_material_property(core_unlit, "", unreal.MaterialProperty.MP_BASE_COLOR)
print(f"[s4] connect CoreUnlitBase -> BaseColor : {ok}")
if not ok:
    raise SystemExit("[s4] BaseColor 연결 실패 — 중단")

# Specular 는 여태 미연결이었다 = 엔진 기본값 0.5. 그 0.5 를 껍질엔 그대로 두고 코어만 0 으로 만든다
# (안 그러면 BaseColor 를 0 으로 눌러도 스페큘러 하이라이트가 시선 각도에 따라 코어 위에서 움직인다).
spec = None
for e in expressions(unreal.MaterialExpressionMultiply):
    if str(e.get_editor_property("desc")) == "CoreUnlitSpecular":
        spec = e
        break
if not spec:
    spec_const = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 1150)
    spec_const.set_editor_property("r", 0.5)  # 엔진 Specular 기본값 — 껍질 룩을 그대로 보존한다
    spec_const.set_editor_property("desc", "DefaultSpecular")
    spec = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -150, 1150)
    spec.set_editor_property("desc", "CoreUnlitSpecular")
    for src, pin in ((spec_const, "A"), (one_minus_core, "B")):
        ok = mel.connect_material_expressions(src, "", spec, pin)
        if not ok:
            raise SystemExit(f"[s4] CoreUnlitSpecular.{pin} 연결 실패 — 중단")
    print("[s4] CoreUnlitSpecular 생성 (껍질 0.5 / 코어 0)")
ok = mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
print(f"[s4] connect CoreUnlitSpecular -> Specular : {ok}")
if not ok:
    raise SystemExit("[s4] Specular 연결 실패 — 중단")

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
