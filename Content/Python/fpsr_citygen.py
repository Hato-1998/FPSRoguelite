"""FPSR CityGen — 모듈 조립 건물 생성 툴 (U22a-A).

사용법:
  1) 메시 폴더 스캔 → 250x300 규격 통과분만 Kit DataAsset에 채움 (Tools > FPSR CityGen > 1. Collect Modular Meshes)
  2) Kit → Config로 복사 (2. Fill Config from Kit) — Config는 DA_CityGenConfig(DataAsset)
  3) Config를 열어 프루닝: 마음에 안 드는 메시를 제거 (3. Open Config)
     - 비어있는 항목은 DEFAULT_CONFIG로 자동 대체(빈 DA로도 미리보기 가능)
  4) 뷰포트에 사이징 박스 배치 (4. Place Sizing Box)
  5) 사이징 박스 선택 → 5. Preview from Config → 박스 크기에 맞춰 미리보기 생성(태그 CityGenPreview)
     - 조각은 부모 "Building_Cfg_*" 액터에 attach(그룹) → 부모 이동=통째 이동 / 자식=개별 편집·교체
     - 규격(250x300) 벗어난 메시는 이 시점에 자동 검증되어 경고 후 제외(무음 드롭 없음)
  6) 마음에 들면 6. Confirm Preview(확정) / 다시 하려면 7. Clear Preview
  7) 완성 후 성능 최적화: 건물(부모) 선택 → 8. Bake Selected → 조각을 병합 ISM(Static)으로 굳힘

규약: 모듈 250(가로) x 300(층높이), 피벗 밑변 왼쪽 +X. 콜리전 BlockAll(플로우필드) + stencil 1(셀룩).
프리셋(STYLES)은 폐지 — 이제 DataAsset 기반 설정(config dict)만 사용. generate_building()은 구코드 호환용 shim.
Kit(DA_CityGenKit)=자동수집한 원본 풀, Config(DA_CityGenConfig)=사용자가 프루닝한 실사용 풀
— 둘 다 같은 6종 배열(Facades/Corners/Doors/RoofFloors/CorniceTrims/RoofProps)을 갖는다.
"""
import unreal, random, re, math

BLD  = "/Game/PolygonCyberCity/Meshes/Buildings/"
BASE = "/Game/PolygonCyberCity/Meshes/Base/"
PROP = "/Game/PolygonCyberCity/Meshes/Props/"
CELL, FH = 250, 300
TOL = 5.0  # 측정 허용 오차(유닛)
SCAN_FOLDERS = [
    "/Game/PolygonCyberCity/Meshes/Buildings",
    "/Game/PolygonCyberCity/Meshes/Base",
    "/Game/PolygonCyberCity/Meshes/Props",
]
KW  = unreal.AttachmentRule.KEEP_WORLD
MOV = unreal.ComponentMobility.MOVABLE
STA = unreal.ComponentMobility.STATIC

# 툴 에셋은 /Game/Tools/ 아래로 모은다. 경로가 또 바뀌어도 find_config_asset()/find_kit_asset()이 클래스로 찾아낸다.
CONFIG_DA = "/Game/Tools/CityGen/DA_CityGenConfig.DA_CityGenConfig"
CONFIG_CLASS = ("/Script/FPSRoguelite", "FPSRCityGenConfig")
KIT_DA = "/Game/Tools/CityGen/DA_CityGenKit.DA_CityGenKit"
KIT_CLASS = ("/Script/FPSRoguelite", "FPSRCityGenKit")
PREVIEW_FOLDER = "Buildings/_Preview"
PREVIEW_TAG = "CityGenPreview"

def _M(p):
    return unreal.load_object(None, p) if p else None

# 설정 없이도(빈 DataAsset) 미리보기가 되도록 하는 기본값 — 구 'resi' 프리셋과 동등
DEFAULT_CONFIG = {
    'facades': [
        BLD + "SM_Bld_Wall_Window_01.SM_Bld_Wall_Window_01",
        BLD + "SM_Bld_Wall_Window_02.SM_Bld_Wall_Window_02",
        BLD + "SM_Bld_Wall_Window_03.SM_Bld_Wall_Window_03",
        BLD + "SM_Bld_Wall_Window_04.SM_Bld_Wall_Window_04",
    ],
    'corners': [BLD + "SM_Bld_Corner_Trim_01.SM_Bld_Corner_Trim_01"],
    'doors': [BASE + "SM_Bld_Base_Wall_Door_01.SM_Bld_Base_Wall_Door_01"],
    'rooffloors': [BLD + "SM_Bld_Floor_Small_01.SM_Bld_Floor_Small_01"],
    'cornices': [BLD + "SM_Bld_Ceiling_Trim_01.SM_Bld_Ceiling_Trim_01"],
    'roofprops': [
        PROP + "SM_Prop_Antenna_01.SM_Prop_Antenna_01",
        PROP + "SM_Prop_Antenna_02.SM_Prop_Antenna_02",
        PROP + "SM_Prop_Antenna_04.SM_Prop_Antenna_04",
    ],
    # 셋백(위층을 좁힘) 기본 OFF — 실제 빌딩은 직사각형이 기본이고, 켜면 항아리 모양이 된다(사용자 판정 2026-07-21).
    'setback': False,
    # 벽을 무엇 단위로 고정할지: column=칸별(위로 쭉 같은 창문, 기본) / floor=층별 / building=건물 하나 / random=칸마다 무작위
    'facade_mode': 'column',
    # 블록(거리) 생성: 박스 하나를 폭·높이가 제각각인 여러 채로 나눠 '늘어선 거리'를 만든다.
    # 기본은 OFF = 박스 하나에 건물 한 채(한 채씩 놓아가며 거리를 만드는 방식). 켜면 한 번에 여러 채.
    'block': False,
    'block_min_width': 2, 'block_max_width': 4,   # 채당 폭(칸)
    'floors_min': 0, 'floors_max': 0,             # 0 = 박스 높이에서 유도(절반~전체 사이에서 무작위)
    'roofprop_count': 2,
    'width': 0, 'depth': 0, 'floors': 0,  # 0 = 사이징 박스 바운드에서 유도
}

def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

def _load_list(paths):
    return [m for m in (_M(p) for p in (paths or [])) if m]

# ---------------- 메시 측정 + 규약 검증 ----------------
_CATS = ('facades', 'corners', 'doors', 'rooffloors', 'cornices', 'roofprops')

def _measure(mesh):
    """메시의 로컬 바운드에서 크기와 피벗 위치를 잰다. dict 반환."""
    b = mesh.get_bounds(); o, e = b.origin, b.box_extent
    size = (e.x * 2, e.y * 2, e.z * 2); minc = (o.x - e.x, o.y - e.y, o.z - e.z)
    return dict(size_x=size[0], size_y=size[1], size_z=size[2], min_x=minc[0], min_y=minc[1], min_z=minc[2])

def _classify(name):
    """에셋 이름(대소문자 무시)으로 카테고리를 추정. 규약 밖 잡부품은 None."""
    n = name.lower()
    # _arm_/_dish_/_wing_ = Satellite_01의 분해 부품. 단독 배치하면 옥상에 날개만 떠 있게 된다
    # (실측 2026-07-21: SM_Prop_Satellite_02_Wing_06이 실제로 미리보기 옥상에 홀로 배치됨).
    # _45_ = 45도로 잘린 코너 전용 조각. 평평한 타일이 아니라 지붕 격자에 깔면 사선으로 어긋난다.
    for excl in ('_glass', 'beams', 'shutter', 'blockout', '_arm_', '_dish_', '_wing_', '_45_'):
        if excl in n:
            return None
    if 'wall_window' in n or 'wall_shop' in n:
        return 'facades'
    if 'corner_trim' in n or 'base_pillar' in n:  # 'pillar' 단독은 Railing_Pillar_*(난간)까지 삼킴
        return 'corners'
    if 'wall_door' in n:  # 문이 뚫린 '벽' (문짝 단품 아님)
        return 'doors'
    if 'ceiling_trim' in n or 'floor_trim' in n or 'wall_trim' in n:
        return 'cornices'
    if 'floor' in n:  # trim 규칙보다 뒤에 둬야 함(Floor_Trim이 먼저 걸리도록)
        return 'rooffloors'
    if 'antenna' in n or 'satellite' in n:
        return 'roofprops'
    return None

def _validate(cat, m):
    """카테고리별 모듈 규약 검증. (ok, 실패사유 한글문자열) 반환 — 성공 시 사유는 빈 문자열.

    ⚠️ 카테고리마다 기준이 다르다(2026-07-21 첫 Collect 실측으로 확정):
      - 파사드/도어 = 벽 한 칸(250)을 통째로 차지 → 폭 250 + 피벗 밑변왼쪽.
      - 코니스 = 벽 한 칸에 붙는 **가로 띠 장식**이라 X 규약은 파사드와 같고(실측: 전부 폭 250·minX 0)
        **Z 오프셋만 자유**다. Ceiling_Trim_01의 minZ=261.2는 '층 천장에 붙는다'는 뜻이라 정상이므로,
        여기에 바닥 조건을 걸면 진짜 코니스가 오히려 탈락한다(실측 전 기준의 오류였음).
      - 코너 = 모서리에 세우는 **중앙 피벗 기둥**(실측: minX 음수) → 한 칸 안 + 바닥에서 시작 + 층높이(300).
        Pillar_Half_*(높이 150 = 반 층)는 층마다 기둥이 절반만 차서 규격 밖이다."""
    if cat == 'roofprops':
        return True, ""  # 장식이라 규격 무관
    if cat == 'rooffloors':
        def _mult125(v):
            k = round(v / 125)
            return k >= 1 and abs(v - k * 125) <= TOL
        if not (_mult125(m['size_x']) and _mult125(m['size_y'])):
            return False, f"바닥 125 배수 아님(W={m['size_x']:.1f}, D={m['size_y']:.1f})"
        return True, ""
    if cat == 'cornices':
        if abs(m['size_x'] - CELL) > TOL:
            return False, f"폭이 한 칸(250)이 아님(W={m['size_x']:.1f})"
        if abs(m['min_x']) > TOL:
            return False, f"피벗 X가 왼쪽 끝이 아님(minX={m['min_x']:.1f})"
        return True, ""
    if cat == 'corners':
        if m['size_x'] > CELL + TOL or m['size_y'] > CELL + TOL:
            return False, f"한 칸(250)을 넘음(W={m['size_x']:.1f}, D={m['size_y']:.1f})"
        if abs(m['min_z']) > TOL:
            return False, f"바닥에 앉지 않음(minZ={m['min_z']:.1f})"
        if abs(m['size_z'] - FH) > TOL:
            return False, f"높이가 층높이(300) 아님(H={m['size_z']:.1f}) — 층마다 빈틈이 생김"
        return True, ""
    # facades / doors — 벽 한 칸을 통째로 차지하므로 폭·피벗 모두 엄격
    cells = round(m['size_x'] / CELL)
    if not (cells >= 1 and abs(m['size_x'] - cells * CELL) <= TOL):
        return False, f"폭 250 배수 아님(W={m['size_x']:.1f})"
    if cells != 1:
        # 배치 로직이 한 칸 단위라 2칸 벽을 한 칸에 넣으면 옆칸으로 삐져나온다(구 260 가드의 이유).
        # 이제는 조용히 버리지 않고 이유를 밝힌다 — 멀티칸 배치는 후속 확장.
        return False, f"{cells}칸 폭(W={m['size_x']:.1f}) — 멀티칸 배치 미지원, 250폭만 사용"
    if not (abs(m['min_x']) <= TOL and abs(m['min_z']) <= TOL):
        return False, f"피벗이 밑변왼쪽 아님(minX={m['min_x']:.1f},minZ={m['min_z']:.1f})"
    return True, ""

def _load_pools(cfg):
    """cfg의 6개 카테고리를 로드→규약 검증해 풀 dict로 만든다.
    규격 밖은 경고 후 제외(무음 드롭 금지), 결과가 비면 DEFAULT_CONFIG로 폴백(역시 검증)."""
    def _validated(paths, category):
        out = []
        for m in _load_list(paths):
            ok, reason = _validate(category, _measure(m))
            if ok:
                out.append(m)
            else:
                unreal.log_warning(f"[CityGen] 제외: {m.get_name()} — {reason}")
        return out
    pools = {}
    for c in _CATS:
        p = _validated(cfg.get(c), c)
        if not p:
            p = _validated(DEFAULT_CONFIG[c], c)
        pools[c] = p
    for c, label in (('facades', '파사드'), ('corners', '코너'), ('doors', '도어'),
                     ('rooffloors', '루프바닥'), ('cornices', '코니스')):
        if not pools[c]:
            unreal.log_warning(f"[CityGen] {label} 풀이 비어있음 — 배치하지 않습니다.")
    if cfg['roofprop_count'] > 0 and not pools['roofprops']:
        unreal.log_warning("[CityGen] 루프소품 풀이 비어있음 — 루프소품을 배치하지 않습니다.")
    return pools

def _build_one(minc, W, D, floors, pools, cfg, seed, label, skip_w=0, skip_e=0):
    """건물 한 채를 조립하고 부모 액터를 반환.

    seed는 이 채 전용이다(공유 Random을 넘기지 않는다) — 같은 seed면 같은 조합이 그대로 재현되므로
    편집 도구가 '룩은 그대로 두고 층수만 바꾸기'를 할 수 있다.

    skip_w/skip_e = **그 층수 미만에서는 서/동 면의 벽·코니스·코너를 만들지 않는다.** 도시 블록은
    건물이 맞붙어 있어 이웃과 닿는 면은 어차피 보이지 않는다 — 안 만들면 그만큼 조각(=드로우콜)이
    준다(적 200~300 예산이 우선인 프로젝트라 중요). 코너까지 빼는 이유는, 맞붙은 두 채의 코너 기둥이
    월드에서 **같은 자리**라 남겨두면 그대로 z-fighting이 나기 때문이다.
    이웃보다 높이 솟은 층은 하늘에 노출되므로 그 위로는 정상적으로 벽을 만든다."""
    eas = _eas()
    rnd = random.Random(seed)
    facades = pools['facades']; roofprops = pools['roofprops']
    # 코너/도어/코니스/루프바닥은 한 채당 한 번만 뽑아 그 건물 안에서는 일관되게 쓴다
    corner = rnd.choice(pools['corners']) if pools['corners'] else None
    door = rnd.choice(pools['doors']) if pools['doors'] else None
    rooffloor = rnd.choice(pools['rooffloors']) if pools['rooffloors'] else None
    cornice = rnd.choice(pools['cornices']) if pools['cornices'] else None

    parent = eas.spawn_actor_from_class(unreal.Actor, minc)
    parent.set_actor_label(label)
    parent.set_folder_path("Buildings")
    parent.root_component.set_editor_property('mobility', MOV)
    fol = f"Buildings/{parent.get_actor_label()}"
    di = W // 2

    def P(mesh, x, y, z, yaw):
        if not mesh:
            return
        a = eas.spawn_actor_from_class(unreal.StaticMeshActor, minc + unreal.Vector(x, y, z))
        a.static_mesh_component.set_static_mesh(mesh)
        a.static_mesh_component.set_editor_property('mobility', MOV)
        a.static_mesh_component.set_collision_profile_name('BlockAll')
        a.static_mesh_component.set_editor_property('render_custom_depth', True)
        a.static_mesh_component.set_editor_property('custom_depth_stencil_value', 1)
        a.set_actor_rotation(unreal.Rotator(yaw=yaw), False)
        a.attach_to_actor(parent, "", KW, KW, KW, False)
        a.set_folder_path(fol)

    # ---- 파사드 배치 규칙 ----
    # ① 지상층 분리: Synty 규약상 이름에 'Base_'가 붙은 벽은 1층용 디자인이다(도어도 전부 Base_ 계열).
    #    상층에 섞이면 건물이 뒤죽박죽으로 보인다. 한쪽이 비면 나누지 않고 전체 풀을 쓴다.
    # ② 수직 일관: 실제 빌딩은 같은 자리의 창문이 위로 쭉 이어진다. 칸마다 새로 뽑으면 층마다 제각각이
    #    되어 건물로 안 보인다. facade_mode로 '무엇을 고정할지'를 고른다.
    ground_facades = [m for m in facades if '_base_' in m.get_name().lower()]
    upper_facades = [m for m in facades if m not in ground_facades]
    if not ground_facades or not upper_facades:
        ground_facades = upper_facades = facades
    mode = cfg.get('facade_mode', 'column')
    _fpick = {}

    def pick_facade(pool, tag, key):
        """같은 key에는 항상 같은 벽을 돌려준다(mode='random'이면 매번 새로 뽑는다)."""
        if not pool:
            return None
        if mode == 'random':
            return rnd.choice(pool)
        ck = (tag, key)
        if ck not in _fpick:
            _fpick[ck] = rnd.choice(pool)
        return _fpick[ck]

    def wall(x, y, z, yaw, floor_i, col, ground=False):
        # door가 없으면 그 칸을 비우지 말고 파사드로 메운다(1층 정면에 구멍이 뚫리는 것보다 낫다)
        if ground and abs(x - di * CELL) < 1 and y == 0 and door:
            P(door, x, y, z, yaw); return
        key = col if mode == 'column' else (floor_i if mode == 'floor' else 0)
        P(pick_facade(ground_facades if ground else upper_facades, 'g' if ground else 'u', key), x, y, z, yaw)

    top_w, top_d, top_ox, top_oy = W, D, 0, 0
    for f in range(floors):
        sb = 1 if (cfg['setback'] and f >= floors - 2 and W > 2 and D > 2) else 0
        # 들여쓰기는 0 — 종전엔 반 칸(125)을 밀어 중앙정렬했는데, 그러면 아래층 벽 격자(0/250/500)와
        # 어긋나 층 사이가 어색해진다. 한 칸 단위로 두 면만 들이는 편이 모듈 격자와 맞다.
        # (양쪽 대칭으로 들이고 싶으면 sb=2 · ox=oy=CELL로 확장 — 그때도 아래 지붕이 top_* 를 따라간다.)
        w = W - sb; d = D - sb; ox = 0; oy = 0; z = f * FH; g = (f == 0)
        top_w, top_d, top_ox, top_oy = w, d, ox, oy
        # col=(면, 칸번호) — 이 값이 같은 칸은 층이 달라도 같은 벽이 온다(수직 일관).
        # g(지상층)는 네 면 모두에 넘긴다 — 1층은 건물 전체가 지상층 디자인이어야 한다.
        # 도어는 wall() 안에서 '정면(y=0) 가운데 칸'으로 한 번 더 좁히므로 옆·뒷면에는 생기지 않는다.
        # 이웃과 맞닿은 면은 이 층에서 통째로 생략한다(벽·코니스·그 쪽 코너 2개)
        hide_e = f < skip_e; hide_w = f < skip_w
        for i in range(w): wall(ox + i * CELL, oy, z, 0, f, ('S', i), g)
        for i in range(w): wall(ox + (i + 1) * CELL, oy + d * CELL, z, 180, f, ('N', i), g)
        if not hide_e:
            for j in range(d): wall(ox + w * CELL, oy + j * CELL, z, 90, f, ('E', j), g)
        if not hide_w:
            for j in range(d): wall(ox, oy + (j + 1) * CELL, z, 270, f, ('W', j), g)
        for cx, cy, cyaw, hidden in [(0, 0, 0, hide_w), (w * CELL, 0, 90, hide_e),
                                     (w * CELL, d * CELL, 180, hide_e), (0, d * CELL, 270, hide_w)]:
            if not hidden: P(corner, ox + cx, oy + cy, z, cyaw)
        for i in range(w):
            P(cornice, ox + i * CELL, oy, z, 0); P(cornice, ox + (i + 1) * CELL, oy + d * CELL, z, 180)
        if not hide_e:
            for j in range(d): P(cornice, ox + w * CELL, oy + j * CELL, z, 90)
        if not hide_w:
            for j in range(d): P(cornice, ox, oy + (j + 1) * CELL, z, 270)
    zr = floors * FH
    if rooffloor:
        # 타일 간격은 고정값이 아니라 **뽑힌 메시의 실측 크기**여야 한다. 125로 못박으면 250짜리 바닥
        # (Base_Floor/Base_45_Floor 등)이 X·Y 각 2배씩, 즉 4겹으로 겹쳐 깔리고 건물 밖으로도 삐져나온다
        # (실측 2026-07-21: 250 타일을 125 간격 6x6=36장 → 실제 필요 9장, 커버 875 vs 건물 750).
        # 범위는 W/D가 아니라 **최상층 크기**(top_*) 기준 — 셋백으로 좁아진 층 위에 원래 폭으로 깔면
        # 사방으로 튀어나온 처마가 된다.
        rm = _measure(rooffloor)
        sx = max(1, int(round(rm['size_x']))); sy = max(1, int(round(rm['size_y'])))
        for ix in range(max(1, math.ceil(top_w * CELL / sx))):
            for iy in range(max(1, math.ceil(top_d * CELL / sy))):
                P(rooffloor, top_ox + ix * sx, top_oy + iy * sy, zr, 0)
    for _ in range(cfg['roofprop_count']):
        # 지붕 타일과 같은 이유로 범위는 top_* 기준 — W/D로 뿌리면 좁아진 옥상 바깥 허공에 소품이 뜬다.
        if roofprops:
            P(rnd.choice(roofprops), top_ox + rnd.randint(30, max(31, top_w * CELL - 30)),
              top_oy + rnd.randint(30, max(31, top_d * CELL - 30)), zr, 0)
    # 크기와 시드를 부모 태그에 남긴다 — 편집 도구(다시 굴리기·층수 조절)가 이 값으로 같은 자리에
    # 같은 크기로 재생성한다. 자식 조각의 바운드로 역산하면 셋백·지붕 때문에 부정확하다.
    meta = parent.get_editor_property('tags')
    meta.append(unreal.Name(f"CityGenSize:{W}x{D}x{floors}"))
    meta.append(unreal.Name(f"CityGenSeed:{seed}"))
    parent.set_editor_property('tags', meta)
    unreal.log(f"[CityGen] {parent.get_actor_label()} 생성: {W}x{D}x{floors}, 조각 {len(parent.get_attached_actors())}")
    return parent

def _merge_cfg(config):
    """DEFAULT_CONFIG 위에 config를 얹는다. 빈 값은 기본값을 유지해 절반만 채운 설정도 동작하게 한다."""
    cfg = dict(DEFAULT_CONFIG)
    for k, v in (config or {}).items():
        if v not in (None, [], ''):
            cfg[k] = v
    return cfg

def _read_meta(actor):
    """건물 부모 태그에 기록해 둔 (W, D, floors, seed)를 읽는다. 건물이 아니면 None."""
    size = seed = None
    for t in actor.tags:
        s = str(t)
        if s.startswith("CityGenSize:"):
            try:
                w, d, f = s.split(':', 1)[1].split('x')
                size = (int(w), int(d), int(f))
            except Exception:
                pass
        elif s.startswith("CityGenSeed:"):
            try:
                seed = int(s.split(':', 1)[1])
            except Exception:
                pass
    return (size[0], size[1], size[2], seed) if size else None

def generate_from_config(box_actor, config=None, seed=None):
    """설정으로 건물을 만든다. 반환 = 생성한 부모 액터 **리스트**.

    기본은 **박스 하나 = 건물 한 채**다(한 채씩 놓아가며 거리를 만드는 방식).
    cfg['block']을 켜면 박스 폭(X)을 따라 폭·높이가 제각각인 여러 채로 나눠 거리를 한 번에 만든다.
    어느 쪽이든 **채마다 부모 액터가 따로** 생기고 조각도 각각 별개 액터라, 생성한 뒤에 건물 단위로
    옮기거나 지우고 조각 단위로 메시를 바꿀 수 있다.
    box_actor=None이면 cfg의 width/depth/floors만으로 크기 결정(0이면 3x3x4)."""
    cfg = _merge_cfg(config)
    if seed is None:
        seed = (box_actor.get_name().__hash__() & 0xffff) if box_actor is not None else 0
    rnd = random.Random(seed)

    if box_actor is not None:
        o, e = box_actor.get_actor_bounds(False)
        minc = unreal.Vector(o.x - e.x, o.y - e.y, o.z - e.z)
        W = max(1, round(2 * e.x / CELL)); D = max(1, round(2 * e.y / CELL)); floors = max(1, round(2 * e.z / FH))
    else:
        minc = unreal.Vector(0, 0, 0)
        W = D = floors = 0
    if cfg['width'] > 0: W = cfg['width']
    if cfg['depth'] > 0: D = cfg['depth']
    if cfg['floors'] > 0: floors = cfg['floors']
    if box_actor is None:
        W = W or 3; D = D or 3; floors = floors or 4

    pools = _load_pools(cfg)
    wmin = max(1, int(cfg['block_min_width'])); wmax = max(wmin, int(cfg['block_max_width']))
    if not cfg['block'] or W < wmin * 2:
        return [_build_one(minc, W, D, floors, pools, cfg, seed, f"Building_Cfg_{seed}")]

    # 폭을 따라 채를 자른다. 남은 폭이 최소폭보다 작아지면 그 채가 흡수한다(한 칸짜리 자투리 방지).
    widths = []; left = W
    while left > 0:
        w = min(rnd.randint(wmin, wmax), left)
        if left - w < wmin:
            w = left
        widths.append(w); left -= w
    fmin = int(cfg['floors_min']) or max(2, floors // 2)
    fmax = int(cfg['floors_max']) or floors
    fmin = min(fmin, fmax)
    heights = [rnd.randint(fmin, fmax) for _ in widths]

    made = []; x = 0
    for i, w in enumerate(widths):
        # 이웃과 맞닿아 보이지 않는 층까지만 옆면을 생략(이웃보다 솟은 층은 하늘에 노출되므로 벽을 만든다)
        skip_w = min(heights[i], heights[i - 1]) if i > 0 else 0
        skip_e = min(heights[i], heights[i + 1]) if i < len(widths) - 1 else 0
        made.append(_build_one(minc + unreal.Vector(x * CELL, 0, 0), w, D, heights[i], pools, cfg,
                               rnd.randrange(1 << 30), f"Building_Cfg_{seed}_{i}", skip_w, skip_e))
        x += w
    unreal.log(f"[CityGen] 블록 {len(made)}채 — 폭 {widths}, 층 {heights}, "
               f"조각 합계 {sum(len(p.get_attached_actors()) for p in made)}")
    return made

def generate_building(box_actor, preset='resi', seed=None):
    """(호환용 shim) 프리셋은 폐지됨 — preset 인자는 무시되고 config(DEFAULT_CONFIG)로 생성.
    반환은 generate_from_config와 같은 **리스트**."""
    return generate_from_config(box_actor, None, seed)

def _selected():
    return list(unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_selected_level_actors())

def generate_from_selection(preset='resi'):
    """선택한 사이징 박스(들)에 대해 건물 생성. 생성 후 박스는 소비(삭제)."""
    eas = _eas(); sel = _selected()
    if not sel:
        unreal.log_warning("[CityGen] 사이징 박스를 먼저 선택하세요.")
        return
    made = []
    for box in sel:
        made.extend(generate_building(box, preset)); eas.destroy_actor(box)
    eas.set_selected_level_actors(made)

def place_sizing_box():
    """원점 근처에 기본 사이징 박스(3x3칸·4층=750x750x1200)를 배치하고 선택."""
    eas = _eas()
    box = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 600))
    box.static_mesh_component.set_static_mesh(_M("/Engine/BasicShapes/Cube.Cube"))
    box.set_actor_scale3d(unreal.Vector(7.5, 7.5, 12))
    box.static_mesh_component.set_collision_profile_name('NoCollision')
    box.set_actor_label("SizingBox")
    box.set_folder_path("Buildings/_SizingBoxes")
    eas.set_selected_level_actors([box])
    unreal.log("[CityGen] SizingBox 배치됨 — 크기 조절 후 Generate.")

def _find_sizing_boxes():
    """레벨에 있는 사이징 박스(라벨이 SizingBox로 시작)를 모두 찾는다."""
    out = []
    for a in _eas().get_all_level_actors():
        if not a:
            continue
        try:
            if a.get_actor_label().startswith("SizingBox"):
                out.append(a)
        except Exception:
            pass
    return out

def _set_sizing_boxes_hidden(hidden, boxes=None):
    """사이징 박스를 뷰포트에서 숨기거나(True) 되돌린다(False). boxes=None이면 레벨 전체 대상.

    미리보기 건물은 박스 안쪽에 생성되므로, 숨기지 않으면 박스의 기본 회색 큐브가 건물을 통째로 가린다
    (2026-07-21 실측: 750x750x2500 박스가 3x3x8 건물을 완전히 감싸 '회색 덩어리'로 보임).
    삭제가 아니라 숨김이라 Clear/Confirm하면 박스가 그대로 돌아와 크기를 다시 조절할 수 있다."""
    tgt = _find_sizing_boxes() if boxes is None else boxes
    for a in tgt:
        a.set_is_temporarily_hidden_in_editor(hidden)
    return len(tgt)

def bake_building(parent):
    """건물 부모의 자식 조각들을 메시별 병합 ISM(Static)으로 굳혀 드로우콜을 낮춘다."""
    eas = _eas()
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    kids = list(parent.get_attached_actors())
    if not kids:
        unreal.log_warning("[CityGen] Bake: 자식 조각이 없습니다(이미 baked?)."); return
    holder = eas.spawn_actor_from_class(unreal.Actor, parent.get_actor_location())
    holder.set_actor_label(parent.get_actor_label() + "_Baked"); holder.set_folder_path("Buildings")
    root = sds.k2_gather_subobject_data_for_instance(holder)[0]
    ism_map = {}
    def get_ism(mesh, collide):
        key = (mesh.get_path_name(), collide)
        if key in ism_map: return ism_map[key]
        h, _ = sds.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=root, new_class=unreal.InstancedStaticMeshComponent))
        c = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(sds.k2_find_subobject_data_from_handle(h))
        c.set_editor_property('mobility', STA)
        c.set_static_mesh(mesh)
        c.set_collision_profile_name('BlockAll' if collide else 'NoCollision')
        c.set_editor_property('render_custom_depth', True); c.set_editor_property('custom_depth_stencil_value', 1)
        ism_map[key] = c; return c
    for k in kids:
        smc = k.static_mesh_component; mesh = smc.static_mesh
        if not mesh: continue
        collide = smc.get_collision_profile_name() == 'BlockAll'
        get_ism(mesh, collide).add_instance(k.get_actor_transform(), True)
    for k in kids: eas.destroy_actor(k)
    eas.destroy_actor(parent)
    unreal.log(f"[CityGen] Bake 완료: {holder.get_actor_label()} — ISM {len(ism_map)}개(=드로우콜)")
    return holder

def bake_selection():
    for a in _selected():
        if a.get_attached_actors():
            bake_building(a)

# ---------------- 편집 도구 (생성한 뒤 마음에 들 때까지 다듬기) ----------------
def _regen(actor, pools, cfg, W, D, floors, seed):
    """건물 액터를 지우고 같은 자리에 다시 만든다(라벨·폴더·미리보기 태그는 그대로 유지)."""
    eas = _eas()
    label = actor.get_actor_label(); folder = actor.get_folder_path()
    loc = actor.get_actor_location()
    was_preview = PREVIEW_TAG in [str(t) for t in actor.tags]
    for c in list(actor.get_attached_actors()):
        eas.destroy_actor(c)
    eas.destroy_actor(actor)
    p = _build_one(loc, W, D, floors, pools, cfg, seed, label)
    if was_preview:
        t = p.get_editor_property('tags'); t.append(unreal.Name(PREVIEW_TAG))
        p.set_editor_property('tags', t)
    p.set_folder_path(folder)
    return p

def _selected_buildings():
    """선택된 액터 중 CityGen이 만든 건물 부모만 (액터, W, D, floors, seed)로 돌려준다."""
    out = []
    for a in _selected():
        meta = _read_meta(a)
        if meta:
            out.append((a,) + meta)
    return out

def _editing_context():
    """편집 도구가 공통으로 쓰는 (cfg, pools)."""
    cfg = _merge_cfg(load_config_from_dataasset())
    return cfg, _load_pools(cfg)

def reroll_selected():
    """선택한 건물을 같은 자리·같은 크기로 다시 굴린다(창문·코너·지붕 조합만 새로 뽑는다).
    마음에 드는 조합이 나올 때까지 반복해서 누르면 된다."""
    tgt = _selected_buildings()
    if not tgt:
        unreal.log_warning("[CityGen] 다시 굴릴 건물을 선택하세요(조각이 아니라 건물 액터).")
        return
    cfg, pools = _editing_context()
    made = [_regen(a, pools, cfg, W, D, fl, random.randrange(1 << 30)) for a, W, D, fl, _ in tgt]
    _eas().set_selected_level_actors(made)
    unreal.log(f"[CityGen] {len(made)}채 다시 굴림")

def change_floors(delta):
    """선택한 건물의 층수를 delta만큼 바꾼다.
    **시드를 유지**하므로 창문 조합은 그대로고 높이만 달라진다(층수만 손보고 싶을 때)."""
    tgt = _selected_buildings()
    if not tgt:
        unreal.log_warning("[CityGen] 층수를 바꿀 건물을 선택하세요.")
        return
    cfg, pools = _editing_context()
    made = []
    for a, W, D, floors, seed in tgt:
        lbl = a.get_actor_label(); nf = max(1, floors + delta)
        made.append(_regen(a, pools, cfg, W, D, nf, seed if seed is not None else random.randrange(1 << 30)))
        unreal.log(f"[CityGen] {lbl}: {floors}층 → {nf}층")
    _eas().set_selected_level_actors(made)

def cycle_piece_mesh(step=1):
    """선택한 조각의 메시를 같은 카테고리의 다음 후보로 바꾼다(창문 A → B → C).
    카테고리는 지금 붙어 있는 메시 이름으로 판정하고, 후보는 Config에 남겨 둔 목록을 쓴다
    — 그래서 Config를 프루닝해 두면 '내가 남긴 것들 안에서만' 돌아간다."""
    cfg, pools = _editing_context()
    n = 0
    for a in _selected():
        if not isinstance(a, unreal.StaticMeshActor):
            continue
        smc = a.static_mesh_component; cur = smc.static_mesh
        if not cur:
            continue
        cat = _classify(cur.get_name())
        pool = pools.get(cat) or []
        if len(pool) < 2:
            unreal.log_warning(f"[CityGen] {cur.get_name()}: 바꿀 후보가 없습니다"
                               f"(카테고리 {cat}) — Config에 후보를 2개 이상 남겨두세요.")
            continue
        names = [m.get_path_name() for m in pool]
        try:
            i = names.index(cur.get_path_name())
        except ValueError:
            i = -1  # 풀 밖의 메시였다면 첫 번째부터
        smc.set_static_mesh(pool[(i + step) % len(pool)])
        n += 1
    unreal.log(f"[CityGen] 조각 {n}개 메시 교체")

# ---------------- DataAsset 브릿지 + 미리보기 라이프사이클 ----------------
def _snake(s):
    return re.sub(r'(?<!^)(?=[A-Z])', '_', s).lower()

def _find_asset(path, class_tuple, label):
    """DataAsset을 찾는다: ①고정 경로 ②실패 시 클래스로 전역 검색(폴더 재정리 대비). find_config_asset/find_kit_asset 공용."""
    da = unreal.EditorAssetLibrary.load_asset(path)
    if da:
        return da
    try:
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
        found = ar.get_assets_by_class(unreal.TopLevelAssetPath(*class_tuple), True)
        if found:
            p = str(found[0].package_name)
            unreal.log_warning(f"[CityGen] {path} 없음 → 클래스로 찾은 {label} 사용: {p}")
            return unreal.EditorAssetLibrary.load_asset(p)
    except Exception as e:
        unreal.log_warning(f"[CityGen] {label} 클래스 검색 실패: {e}")
    return None

def find_config_asset(da_path=CONFIG_DA):
    """설정 DataAsset을 찾는다: ①CONFIG_DA 경로 ②실패 시 클래스로 전역 검색(폴더 재정리 대비)."""
    return _find_asset(da_path, CONFIG_CLASS, "설정")

def find_kit_asset(path=KIT_DA):
    """Kit DataAsset을 찾는다: ①KIT_DA 경로 ②실패 시 클래스로 전역 검색(폴더 재정리 대비)."""
    return _find_asset(path, KIT_CLASS, "Kit")

def _pkg_path(obj):
    """오브젝트의 풀 패스에서 '.에셋이름' 접미사를 뗀 패키지 경로(save_asset 인자용)."""
    return obj.get_path_name().split('.')[0]

def collect_modular_meshes(kit_path=KIT_DA, write=True):
    """SCAN_FOLDERS를 재귀 스캔해 이름으로 카테고리를 추정하고, 250x300 모듈 규약을 만족하는
    메시만 골라 Kit DataAsset(FPSRCityGenKit)의 6개 배열에 채운다. write=False면 저장 없이 결과만 반환(드라이런).
    반환: dict(accepted={카테고리: [메시,...]}, rejected=[(이름,카테고리,사유),...])"""
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    accepted = {c: [] for c in _CATS}
    rejected = []
    seen = set()
    for f in SCAN_FOLDERS:
        try:
            assets = ar.get_assets_by_path(unreal.Name(f), True)
        except Exception as e:
            unreal.log_warning(f"[CityGen] 폴더 스캔 실패({f}): {e}")
            continue
        for a in assets:
            if str(a.asset_class_path.asset_name) != 'StaticMesh':
                continue
            pkg = str(a.package_name)
            if pkg in seen:
                continue
            seen.add(pkg)
            name = str(a.asset_name)
            cat = _classify(name)
            if cat is None:
                continue
            mesh = unreal.EditorAssetLibrary.load_asset(pkg)
            if not mesh:
                continue
            ok, reason = _validate(cat, _measure(mesh))
            if ok:
                accepted[cat].append(mesh)
            else:
                rejected.append((name, cat, reason))

    unreal.log("[CityGen] === Collect Modular Meshes 결과 ===")
    for cat in _CATS:
        lst = accepted[cat]
        unreal.log(f"[CityGen]  {cat}: {len(lst)}개 채택")
        if lst:
            m0 = _measure(lst[0])
            unreal.log(f"[CityGen]    예시={lst[0].get_name()} size=({m0['size_x']:.1f},{m0['size_y']:.1f},{m0['size_z']:.1f})")
    if rejected:
        unreal.log(f"[CityGen]  제외 {len(rejected)}개 (최대 25개 표시)")
        for name, cat, reason in rejected[:25]:
            unreal.log(f"[CityGen]    {name} [{cat}] — {reason}")

    if write:
        kit = find_kit_asset(kit_path)
        if kit:
            kit.set_editor_property('Facades', accepted['facades'])
            kit.set_editor_property('Corners', accepted['corners'])
            kit.set_editor_property('Doors', accepted['doors'])
            kit.set_editor_property('RoofFloors', accepted['rooffloors'])
            kit.set_editor_property('CorniceTrims', accepted['cornices'])
            kit.set_editor_property('RoofProps', accepted['roofprops'])
            unreal.EditorAssetLibrary.save_asset(_pkg_path(kit))
            unreal.log(f"[CityGen] Kit 저장 완료: {_pkg_path(kit)}")
        else:
            unreal.log_warning(f"[CityGen] Kit DataAsset이 없습니다 — 먼저 만드세요: 클래스 FPSRCityGenKit, 경로 {kit_path}")

    return dict(accepted=accepted, rejected=rejected)

def fill_config_from_kit():
    """Kit(DA_CityGenKit)의 6개 배열을 Config(DA_CityGenConfig)에 그대로 복사(덮어쓰기)한다.
    이후 사용자가 Config를 열어 프루닝하는 것이 전제(자동 프루닝 없음)."""
    kit = find_kit_asset(); cfg_da = find_config_asset()
    if not kit:
        unreal.log_warning(f"[CityGen] Kit DataAsset이 없습니다 — 먼저 '1. Collect Modular Meshes' 실행: {KIT_DA}")
        return
    if not cfg_da:
        unreal.log_warning(f"[CityGen] Config DataAsset이 없습니다 — 먼저 생성하세요: {CONFIG_DA}")
        return
    counts = {}
    for prop in ('Facades', 'Corners', 'Doors', 'RoofFloors', 'CorniceTrims', 'RoofProps'):
        v = kit.get_editor_property(prop)
        cfg_da.set_editor_property(prop, list(v) if v else [])
        counts[prop] = len(v) if v else 0
    cfg_da.set_editor_property('Kit', kit)
    unreal.EditorAssetLibrary.save_asset(_pkg_path(cfg_da))
    unreal.log(
        f"[CityGen] Kit → Config 채움: facades {counts['Facades']}, corners {counts['Corners']}, "
        f"doors {counts['Doors']}, rooffloors {counts['RoofFloors']}, cornices {counts['CorniceTrims']}, "
        f"roofprops {counts['RoofProps']} — Config를 열어 프루닝하세요.")

def load_config_from_dataasset(da_path=CONFIG_DA):
    """DA_CityGenConfig(UFPSRCityGenConfig)를 config dict로 변환. 못 찾으면 None.
    UE Python의 get_editor_property는 스네이크/불린 b-접두 제거 등 이름을 정규화할 수 있으므로
    C++ 프로퍼티명의 여러 표기(원본·snake·b제거)를 순서대로 시도한다."""
    da = find_config_asset(da_path)
    if not da:
        unreal.log_warning(f"[CityGen] 설정 DataAsset을 못 찾음({da_path}) — DEFAULT_CONFIG로 진행")
        return None
    missing = object()
    def _prop(name):
        cands = [name, _snake(name)]
        if name[:1] == 'b' and len(name) > 1 and name[1].isupper():  # bSetback -> setback / Setback
            base = name[1:]; cands += [_snake(base), base]
        for n in cands:
            try:
                return da.get_editor_property(n)
            except Exception:
                continue
        return missing
    cfg = {}
    for prop, key in (('Facades', 'facades'), ('Corners', 'corners'), ('Doors', 'doors'),
                      ('RoofFloors', 'rooffloors'), ('CorniceTrims', 'cornices'), ('RoofProps', 'roofprops')):
        v = _prop(prop)
        if v is missing: continue
        cfg[key] = [m.get_path_name() for m in v if m] if v else None
    # 'Kit'(단일 오브젝트 프로퍼티)은 메시 카테고리가 아니므로 config dict에는 포함하지 않는다.
    for prop, key in (('Width', 'width'), ('Depth', 'depth'), ('Floors', 'floors'), ('bSetback', 'setback'), ('RoofPropCount', 'roofprop_count')):
        v = _prop(prop)
        if v is missing: continue
        cfg[key] = v
    return cfg

def _clear_actors_by_tag(tag):
    eas = _eas()
    tgt = [a for a in eas.get_all_level_actors() if a and tag in [str(t) for t in a.tags]]
    for a in tgt:
        for c in list(a.get_attached_actors()):
            eas.destroy_actor(c)
        eas.destroy_actor(a)
    return len(tgt)

def clear_preview():
    n = _clear_actors_by_tag(PREVIEW_TAG)
    _set_sizing_boxes_hidden(False)  # 미리보기가 없어졌으니 박스를 되돌려 크기를 다시 조절할 수 있게 한다
    unreal.log(f"[CityGen] Preview {n}개 제거")

def preview_from_config():
    """DA_CityGenConfig로부터 미리보기 건물을 생성(태그 CityGenPreview, _Preview 폴더)."""
    clear_preview()
    cfg = load_config_from_dataasset() or {}
    sel = _selected(); box = None
    for a in sel:  # 1순위: "SizingBox" 라벨
        try:
            if a.get_actor_label().startswith("SizingBox"):
                box = a; break
        except Exception:
            pass
    if box is None:  # 2순위: 선택된 아무 StaticMeshActor
        for a in sel:
            if isinstance(a, unreal.StaticMeshActor):
                box = a; break
    if box is None:  # 3순위: 선택이 아예 없으면 레벨의 사이징 박스를 자동으로 찾는다.
        # 미리보기 중에는 박스를 숨기고 부모를 대신 선택하므로 박스 선택이 풀린다 → 이 폴백이 없으면
        # Preview를 두 번째 돌릴 때 조용히 기본 크기(3x3x4)로 떨어진다.
        boxes = _find_sizing_boxes()
        if boxes:
            box = boxes[0]
            if len(boxes) > 1:
                unreal.log_warning(
                    f"[CityGen] 사이징 박스 {len(boxes)}개 중 선택된 것이 없어 '{box.get_actor_label()}'을 사용합니다.")
    parents = generate_from_config(box, cfg)
    if box is not None:
        _set_sizing_boxes_hidden(True, [box])  # 회색 큐브가 미리보기를 가리지 않게 숨김(Clear/Confirm 시 복원)
    for parent in parents:
        t = parent.get_editor_property('tags')
        t.append(unreal.Name(PREVIEW_TAG))
        parent.set_editor_property('tags', t)
        parent.set_folder_path(PREVIEW_FOLDER)
        parent.set_actor_label(parent.get_actor_label() + "_Preview")
    _eas().set_selected_level_actors(parents)
    unreal.log("[CityGen] Preview 생성 — Confirm/Bake로 확정")

def confirm_preview():
    """미리보기 태그가 붙은 부모들을 확정: 태그 제거·폴더 이동·라벨에서 _Preview 제거(굽지는 않음)."""
    n = 0
    for a in _eas().get_all_level_actors():
        if not (a and PREVIEW_TAG in [str(t) for t in a.tags]):
            continue
        t = [x for x in a.get_editor_property('tags') if str(x) != PREVIEW_TAG]
        a.set_editor_property('tags', t)
        a.set_folder_path("Buildings")
        lbl = a.get_actor_label()
        if lbl.endswith("_Preview"):
            a.set_actor_label(lbl[:-len("_Preview")])
        n += 1
    _set_sizing_boxes_hidden(False)  # 확정했으니 박스를 되돌린다(다음 건물에 재사용)
    unreal.log(f"[CityGen] Preview {n}개 확정(Confirm)")

# ---------------- 메뉴 등록 ----------------
def register_menu():
    menus = unreal.ToolMenus.get()
    tools = menus.find_menu("LevelEditor.MainMenu.Tools")
    if not tools:
        unreal.log_warning("[CityGen] Tools 메뉴를 못 찾음"); return
    tools.add_sub_menu("FPSRCityGen", "FPSR", "FPSRCityGen", "FPSR CityGen", "모듈 건물 생성 툴")
    sub = menus.find_menu("LevelEditor.MainMenu.Tools.FPSRCityGen")
    def entry(name, label, code):
        e = unreal.ToolMenuEntry(name=name, type=unreal.MultiBlockType.MENU_ENTRY)
        e.set_label(label)
        e.set_string_command(unreal.ToolMenuStringCommandType.PYTHON, "", string=code)
        sub.add_menu_entry("CityGen", e)
    entry("CollectKit", "1. Collect Modular Meshes (→Kit)", "import fpsr_citygen; fpsr_citygen.collect_modular_meshes()")
    entry("FillFromKit", "2. Fill Config from Kit", "import fpsr_citygen; fpsr_citygen.fill_config_from_kit()")
    entry("OpenConfig", "3. Open Config (DA_CityGenConfig)",
          "import fpsr_citygen, unreal\n"
          "_da = fpsr_citygen.find_config_asset()\n"
          "unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([_da]) if _da else "
          "unreal.log_warning('[CityGen] DA_CityGenConfig 없음 — 먼저 생성하세요: ' + fpsr_citygen.CONFIG_DA)")
    entry("PlaceBox", "4. Place Sizing Box", "import fpsr_citygen; fpsr_citygen.place_sizing_box()")
    entry("PreviewCfg", "5. Preview from Config", "import fpsr_citygen; fpsr_citygen.preview_from_config()")
    entry("ConfirmPreview", "6. Confirm Preview", "import fpsr_citygen; fpsr_citygen.confirm_preview()")
    entry("ClearPreview", "7. Clear Preview", "import fpsr_citygen; fpsr_citygen.clear_preview()")
    entry("Bake", "8. Bake Selected (merge ISM)", "import fpsr_citygen; fpsr_citygen.bake_selection()")
    entry("Reroll", "9. 건물 다시 굴리기 (선택 건물)", "import fpsr_citygen; fpsr_citygen.reroll_selected()")
    entry("FloorUp", "10. 층 +1 (선택 건물)", "import fpsr_citygen; fpsr_citygen.change_floors(1)")
    entry("FloorDown", "11. 층 -1 (선택 건물)", "import fpsr_citygen; fpsr_citygen.change_floors(-1)")
    entry("CyclePiece", "12. 조각 메시 바꾸기 (선택 조각)", "import fpsr_citygen; fpsr_citygen.cycle_piece_mesh(1)")
    menus.refresh_all_widgets()
    unreal.log("[CityGen] 메뉴 등록: Tools > FPSR CityGen")
