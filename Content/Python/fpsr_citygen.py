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
import unreal, random, re

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
    'setback': True,
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
    for excl in ('_glass', 'beams', 'shutter', 'blockout'):
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

    ⚠️ 카테고리마다 기준이 다르다. 파사드/도어는 **벽 한 칸(250)을 통째로 차지**하므로 폭이 250 배수여야
    하지만, 코너·코니스는 모서리/가장자리에 붙는 **얇은 장식 조각**이라 폭이 250이 아니다
    (여기에 250 배수 규칙을 걸면 지금 잘 쓰고 있는 Corner_Trim_01·Ceiling_Trim_01조차 전부 탈락한다).
    → 코너/코니스는 "한 칸 안에 들어가고 바닥에 앉는가"만 본다. 첫 Collect 로그의 실측값을 보고
    필요하면 기준을 조이는 것이 정상 절차(실측 전 과한 제약 금지)."""
    if cat == 'roofprops':
        return True, ""  # 장식이라 규격 무관
    if cat == 'rooffloors':
        def _mult125(v):
            k = round(v / 125)
            return k >= 1 and abs(v - k * 125) <= TOL
        if not (_mult125(m['size_x']) and _mult125(m['size_y'])):
            return False, f"바닥 125 배수 아님(W={m['size_x']:.1f}, D={m['size_y']:.1f})"
        return True, ""
    if cat in ('corners', 'cornices'):
        if m['size_x'] > CELL + TOL or m['size_y'] > CELL + TOL:
            return False, f"한 칸(250)을 넘음(W={m['size_x']:.1f}, D={m['size_y']:.1f})"
        if abs(m['min_z']) > TOL:
            return False, f"바닥에 앉지 않음(minZ={m['min_z']:.1f})"
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

def generate_from_config(box_actor, config=None, seed=None):
    """설정 딕셔너리(DataAsset 매핑, config)로 모듈 건물을 생성.
    box_actor=None이면 cfg의 width/depth/floors만으로 크기 결정(0이면 3x3x4)."""
    eas = _eas()
    cfg = dict(DEFAULT_CONFIG)
    for k, v in (config or {}).items():
        if v not in (None, [], ''):  # 절반만 채운 config도 동작하도록 빈 값은 기본값 유지
            cfg[k] = v

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

    def _pool(cfg_key, category):
        """cfg[cfg_key]의 메시를 로드→규약 검증. 벗어난 메시는 경고 후 제외(무음 드롭 금지),
        결과가 비면 DEFAULT_CONFIG로 폴백(역시 검증)."""
        def _validated(paths):
            out = []
            for m in _load_list(paths):
                ok, reason = _validate(category, _measure(m))
                if ok:
                    out.append(m)
                else:
                    unreal.log_warning(f"[CityGen] 제외: {m.get_name()} — {reason}")
            return out
        pool = _validated(cfg.get(cfg_key))
        if not pool:
            pool = _validated(DEFAULT_CONFIG[cfg_key])
        return pool

    facades = _pool('facades', 'facades')
    corners = _pool('corners', 'corners')
    doors = _pool('doors', 'doors')
    rooffloors = _pool('rooffloors', 'rooffloors')
    cornices = _pool('cornices', 'cornices')
    roofprops = _pool('roofprops', 'roofprops')

    # 코너/도어/코니스/루프바닥은 건물 하나당 한 번만 뽑아 일관된 외관을 유지(파사드/루프소품은 배치마다 랜덤)
    corner = rnd.choice(corners) if corners else None
    door = rnd.choice(doors) if doors else None
    rooffloor = rnd.choice(rooffloors) if rooffloors else None
    cornice = rnd.choice(cornices) if cornices else None
    if not facades: unreal.log_warning("[CityGen] 파사드 풀이 비어있음 — 벽을 배치할 수 없습니다.")
    if not corners: unreal.log_warning("[CityGen] 코너 풀이 비어있음 — 코너를 배치하지 않습니다.")
    if not doors: unreal.log_warning("[CityGen] 도어 풀이 비어있음 — 도어를 배치하지 않습니다.")
    if not rooffloors: unreal.log_warning("[CityGen] 루프바닥 풀이 비어있음 — 루프바닥을 배치하지 않습니다.")
    if not cornices: unreal.log_warning("[CityGen] 코니스 풀이 비어있음 — 코니스를 배치하지 않습니다.")
    if cfg['roofprop_count'] > 0 and not roofprops:
        unreal.log_warning("[CityGen] 루프소품 풀이 비어있음 — 루프소품을 배치하지 않습니다.")

    parent = eas.spawn_actor_from_class(unreal.Actor, minc)
    parent.set_actor_label(f"Building_Cfg_{seed}")
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

    def wall(x, y, z, yaw, ground=False):
        # door가 없으면 그 칸을 비우지 말고 파사드로 메운다(1층 정면에 구멍이 뚫리는 것보다 낫다)
        if ground and abs(x - di * CELL) < 1 and y == 0 and door:
            P(door, x, y, z, yaw); return
        if facades:
            P(rnd.choice(facades), x, y, z, yaw)

    for f in range(floors):
        sb = 1 if (cfg['setback'] and f >= floors - 2 and W > 2 and D > 2) else 0
        w = W - sb; d = D - sb; ox = sb * CELL // 2; oy = sb * CELL // 2; z = f * FH; g = (f == 0)
        for i in range(w): wall(ox + i * CELL, oy, z, 0, g)
        for j in range(d): wall(ox + w * CELL, oy + j * CELL, z, 90)
        for i in range(w): wall(ox + (i + 1) * CELL, oy + d * CELL, z, 180)
        for j in range(d): wall(ox, oy + (j + 1) * CELL, z, 270)
        for cx, cy, cyaw in [(0, 0, 0), (w * CELL, 0, 90), (w * CELL, d * CELL, 180), (0, d * CELL, 270)]:
            P(corner, ox + cx, oy + cy, z, cyaw)
        for i in range(w):
            P(cornice, ox + i * CELL, oy, z, 0); P(cornice, ox + (i + 1) * CELL, oy + d * CELL, z, 180)
        for j in range(d):
            P(cornice, ox + w * CELL, oy + j * CELL, z, 90); P(cornice, ox, oy + (j + 1) * CELL, z, 270)
    zr = floors * FH
    for ix in range(W * 2):
        for iy in range(D * 2):
            P(rooffloor, ix * 125, iy * 125, zr, 0)
    for _ in range(cfg['roofprop_count']):
        if roofprops: P(rnd.choice(roofprops), rnd.randint(30, W * 250 - 30), rnd.randint(30, D * 250 - 30), zr, 0)
    unreal.log(f"[CityGen] {parent.get_actor_label()} 생성: {W}x{D}x{floors}, 조각 {len(parent.get_attached_actors())}")
    return parent

def generate_building(box_actor, preset='resi', seed=None):
    """(호환용 shim) 프리셋은 폐지됨 — preset 인자는 무시되고 config(DEFAULT_CONFIG)로 생성."""
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
        p = generate_building(box, preset)
        made.append(p); eas.destroy_actor(box)
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
    parent = generate_from_config(box, cfg)
    t = parent.get_editor_property('tags')
    t.append(unreal.Name(PREVIEW_TAG))
    parent.set_editor_property('tags', t)
    parent.set_folder_path(PREVIEW_FOLDER)
    parent.set_actor_label(parent.get_actor_label() + "_Preview")
    _eas().set_selected_level_actors([parent])
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
    menus.refresh_all_widgets()
    unreal.log("[CityGen] 메뉴 등록: Tools > FPSR CityGen")
