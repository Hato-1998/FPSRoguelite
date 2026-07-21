"""FPSR CityGen — 모듈 조립 건물 생성 툴 (U22a-A).

사용법:
  1) 뷰포트에 사이징 박스 배치 (Tools > FPSR CityGen > 1. Place Sizing Box)
  2) DA_CityGenConfig(DataAsset)에서 파사드/코너/도어/코니스/루프소품 메시 지정 (2. Open Config)
     - 비어있는 항목은 DEFAULT_CONFIG로 자동 대체(빈 DA로도 미리보기 가능)
  3) 사이징 박스 선택 → 3. Preview from Config → 박스 크기에 맞춰 미리보기 생성(태그 CityGenPreview)
     - 조각은 부모 "Building_Cfg_*" 액터에 attach(그룹) → 부모 이동=통째 이동 / 자식=개별 편집·교체
  4) 마음에 들면 4. Confirm Preview(확정) / 다시 하려면 5. Clear Preview
  5) 완성 후 성능 최적화: 건물(부모) 선택 → 6. Bake Selected → 조각을 병합 ISM(Static)으로 굳힘

규약: 모듈 250(가로) x 300(층높이), 피벗 밑변 왼쪽 +X. 콜리전 BlockAll(플로우필드) + stencil 1(셀룩).
프리셋(STYLES)은 폐지 — 이제 DataAsset 기반 설정(config dict)만 사용. generate_building()은 구코드 호환용 shim.
"""
import unreal, random, re

BLD  = "/Game/PolygonCyberCity/Meshes/Buildings/"
BASE = "/Game/PolygonCyberCity/Meshes/Base/"
PROP = "/Game/PolygonCyberCity/Meshes/Props/"
CELL, FH = 250, 300
KW  = unreal.AttachmentRule.KEEP_WORLD
MOV = unreal.ComponentMobility.MOVABLE
STA = unreal.ComponentMobility.STATIC

CONFIG_DA = "/Game/_CityGen/DA_CityGenConfig.DA_CityGenConfig"
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
    'corner': BLD + "SM_Bld_Corner_Trim_01.SM_Bld_Corner_Trim_01",
    'door': BASE + "SM_Bld_Base_Wall_Door_01.SM_Bld_Base_Wall_Door_01",
    'rooffloor': BLD + "SM_Bld_Floor_Small_01.SM_Bld_Floor_Small_01",
    'cornice': BLD + "SM_Bld_Ceiling_Trim_01.SM_Bld_Ceiling_Trim_01",
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

    # 250폭 벽만 (500폭이 섞이면 옆칸으로 삐져나오므로 가드) — 필터 후 빈 풀이면 기본 파사드로 폴백
    pool = [m for m in _load_list(cfg['facades']) if m.get_bounds().box_extent.x * 2 <= 260]
    if not pool:
        pool = [m for m in _load_list(DEFAULT_CONFIG['facades']) if m.get_bounds().box_extent.x * 2 <= 260]
    door = _M(cfg['door']) or _M(DEFAULT_CONFIG['door'])
    corner = _M(cfg['corner']) or _M(DEFAULT_CONFIG['corner'])
    rooffloor = _M(cfg['rooffloor']) or _M(DEFAULT_CONFIG['rooffloor'])
    cornice = _M(cfg['cornice']) or _M(DEFAULT_CONFIG['cornice'])
    roofprops = _load_list(cfg['roofprops'])

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
        if ground and abs(x - di * CELL) < 1 and y == 0:
            P(door, x, y, z, yaw); return
        P(rnd.choice(pool), x, y, z, yaw)

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

def load_config_from_dataasset(da_path=CONFIG_DA):
    """DA_CityGenConfig(UFPSRCityGenConfig)를 config dict로 변환. 로드 실패 시 None.
    UE Python의 get_editor_property는 스네이크/불린 b-접두 제거 등 이름을 정규화할 수 있으므로
    C++ 프로퍼티명의 여러 표기(원본·snake·b제거)를 순서대로 시도한다."""
    da = unreal.EditorAssetLibrary.load_asset(da_path)
    if not da:
        unreal.log_warning(f"[CityGen] DataAsset 로드 실패: {da_path} — DEFAULT_CONFIG로 진행")
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
    for prop, key in (('Corner', 'corner'), ('Door', 'door'), ('RoofFloor', 'rooffloor'), ('CorniceTrim', 'cornice')):
        v = _prop(prop)
        if v is missing: continue
        cfg[key] = v.get_path_name() if v else None
    for prop, key in (('Facades', 'facades'), ('RoofProps', 'roofprops')):
        v = _prop(prop)
        if v is missing: continue
        cfg[key] = [m.get_path_name() for m in v if m] if v else None
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
    entry("PlaceBox", "1. Place Sizing Box", "import fpsr_citygen; fpsr_citygen.place_sizing_box()")
    entry("OpenConfig", "2. Open Config (DA_CityGenConfig)",
          "import fpsr_citygen, unreal\n"
          "_da = unreal.EditorAssetLibrary.load_asset(fpsr_citygen.CONFIG_DA)\n"
          "unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([_da]) if _da else "
          "unreal.log_warning('[CityGen] DA_CityGenConfig 없음 — 먼저 생성하세요: ' + fpsr_citygen.CONFIG_DA)")
    entry("PreviewCfg", "3. Preview from Config", "import fpsr_citygen; fpsr_citygen.preview_from_config()")
    entry("ConfirmPreview", "4. Confirm Preview", "import fpsr_citygen; fpsr_citygen.confirm_preview()")
    entry("ClearPreview", "5. Clear Preview", "import fpsr_citygen; fpsr_citygen.clear_preview()")
    entry("Bake", "6. Bake Selected (merge ISM)", "import fpsr_citygen; fpsr_citygen.bake_selection()")
    menus.refresh_all_widgets()
    unreal.log("[CityGen] 메뉴 등록: Tools > FPSR CityGen")
