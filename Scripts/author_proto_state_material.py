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
    // Attack: 수축 → 전방(로컬 +X) 타격 → 복귀. sin(pi*prog) 라 양 끝이 0 이다.
    float w = sin(prog * 3.14159265);
    q *= (1.0 - w * AttackSquash);
    q.x += w * AttackLunge;
}
return q - p;"""

WPO_NEW_INPUTS = [("StateId", p_state), ("EnterTime", p_enter), ("Rate", p_rate),
                  ("AttackLunge", p_lunge), ("AttackSquash", p_squash)]

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

# ── 5. 고아 노드 정리 ────────────────────────────────────────────────────────────────────────────
# 위상 소스를 오브젝트 위치 해시 → CPD 슬롯3 으로 갈아끼울 때(fix_proto_material_phase.py) 입력만
# 교체하고 노드는 남겨 둔 잔재다. 동작엔 영향 없지만 다음 사람이 "위상이 위치에서 온다"로 오독한다.
orphans = expressions(unreal.MaterialExpressionObjectPositionWS)
for o in orphans:
    # UMaterial 의 표현식 배열은 5.x 에서 프로퍼티로 노출되지 않는다(`expression_collection` 없음) —
    # 정식 경로는 MaterialEditingLibrary 의 삭제 API 다.
    mel.delete_material_expression(mat, o)
print(f"[s4] 고아 ObjectPositionWS 제거: {len(orphans)}개")

# ── 6. 컴파일 + 저장 ─────────────────────────────────────────────────────────────────────────────
# ⚠️ 머티리얼 무음 컴파일 실패가 이 저장소의 상습 함정이다(If 노드 스칼라 전용 / MaterialAttributes
#    핀 미연결 시 WPO 상수 0). recompile 후 별도 검증 스크립트로 인스트럭션 수를 확인할 것.
mel.recompile_material(mat)
saved = eal.save_asset(MAT_PATH)
print(f"[s4] recompile 완료, save_asset = {saved}")
print("[s4] DONE")
