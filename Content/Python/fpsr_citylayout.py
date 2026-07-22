"""FPSR CityLayout — 도로/인도/광장 레이아웃 생성 툴 (U22a-B, PolygonScifi 도로 킷).

fpsr_citygen.py(모듈 건물 생성)와 짝을 이루는 도구. 건물은 250x300 격자, 도로는 500 격자로 서로
다른 규약을 쓰므로 별도 모듈로 분리했다(공유 헬퍼 없음, 태그/네이밍 스타일만 citygen을 따름).

사용법: Tools > FPSR CityLayout
  1. Generate Roads  — 도로+인도+광장을 ISM 액터 1개("CityRoads")로 생성(재실행 가능, 기존 결과 대체)
  2. Clear Roads     — 생성물만 삭제(레벨의 다른 액터는 건드리지 않음)
  3. Describe        — 생성 없이 예상 물량 + 빈틈/겹침 검산 결과만 로그로 출력

=== 측정으로 확정된 사실(재유도 금지) ===
[Scifi 도로 타일 공통 규약] (OBJ export 정점 실측)
  로컬 footprint: X∈[0,W], Y∈[-D,0], Z≈0. 피벗=(minX, maxY) 모서리.
  road/walk/corner = 500x500,  bare = 500x250(절반 깊이).

[셀 채우기 공식] 월드 셀 [cx,cx+W] x [cy,cy+D] 를 덮는 액터 피벗:
    yaw=0   -> (cx,     cy+D)      yaw=90  -> (cx,     cy)
    yaw=180 -> (cx+W,   cy)        yaw=270 -> (cx+W,   cy+D)
  (유도: 월드 = 피벗 + R(yaw)*로컬, R(0)=(x,y) R(90)=(-y,x) R(180)=(-x,-y) R(270)=(y,-x))
  ※ 90/270은 W와 D가 뒤바뀐다(정사각형 타일만 회전시키므로 실사용엔 영향 없음. _rect가 정확히 처리).

[로컬 +X 가장자리가 향하는 월드 방향]      yaw 0->+X, 90->+Y, 180->-X, 270->-Y
[로컬 +Y 가장자리(y=0)가 향하는 월드 방향] yaw 0->+Y, 90->-X, 180->-Y, 270->+X
[로컬 -X 가장자리가 향하는 월드 방향]      yaw 0->-X, 90->-Y, 180->+X, 270->+Y

[메시별 특징 면]
  SM_Env_Road_YellowLines_01_SF : 황색 이중선 = 로컬 +X 가장자리(정점 X=453.7~500).
  SM_Env_Sidewalk_Straight_01_SF: 7cm 연석 립  = 로컬 +Y 가장자리(높이>=6 정점의 Y∈[-65.5,-12.4]).
  SM_Env_Sidewalk_Corner_01_SF  : 연석이 로컬 **+Y 와 -X 두 면**을 감싼다(피벗 모서리를 도는 아크).
  SM_Env_Road_Bare_Half_01_SF   : 민무늬, 2 삼각형.

[레이아웃 사양 — 사용자 확정치는 SPEC 딕셔너리 하나로 관리(하드코딩 금지)]
  콜리전은 전부 NoCollision(바닥 평판 'Base'가 충돌을 담당). seed 불필요(완전 결정적).

[레이아웃 구조]
  회랑 반폭 band = road_half + walk. 두 회랑이 원점에서 십자로 교차한다.
    차도  |x|<=road_half 이고 |y|>=plaza_half  (그리고 축을 바꾼 대칭)
    광장  |x|<=plaza_half 이고 |y|<=plaza_half   ← 민무늬 아스팔트
    인도  road_half<=|x|<=band 이고 |y|>=max(plaza_half, band)
    코너  위 셋으로 안 덮인 교차부 잔여 셀       ← 인도 코너 조각
  plaza_half 를 band(1000)로 두면 코너가 필요 없고, road_half(500)로 두면 코너 4장이 생긴다.
  **어느 값이든 audit()이 빈틈 0 / 겹침 0 을 보장한다**(describe()가 자동 실행).
"""
import math
import unreal

# ---------------- 사양 / 에셋 경로 (전부 이름 있는 파라미터로만 참조) ----------------
SPEC = {
    # 2026-07-22 사용자 결정: 264m → **150m로 축소**(B안 = 기존 배치를 유지한 채 첫 교차로 바깥을 잘라냄).
    # 근거: 적 동시 192마리 기준 264m는 밀도가 너무 희석된다(걸을 공간 1마리당 90m² → 50m²).
    # ⚠️ alley_major_center / alley_minor_axis 를 **명시값으로 고정**한 이유 = 자동 산출식
    #    ((band+half)//2//tile)*tile 을 쓰면 지선이 ±4000으로 옮겨가 기존 배치가 달라지기 때문(A안).
    'half':       7500,   # 격자 반경(도시 -half .. +half) = 150m x 150m
    'tile':       500,    # 표준 타일 한 변(=도로/인도 메시 footprint)
    'road_half':  500,    # 차도 반폭(왕복 2차선 = 폭 1000 = 10m)
    'walk':       500,    # 인도 폭(편도, 5m)
    'plaza_half': 1000,   # 교차 광장 반폭(20m x 20m). 500으로 바꾸면 순수 십자 교차로가 된다.
    'z':          1.0,    # 바닥 평판(윗면 z=0)과의 z-파이팅 회피
    # --- 지선 (사분면을 다시 십자로 가른다. S1-R1 = 도로 위계 3단) ---
    # 큰길 20m 회랑  >  남북 지선 10m 2차선  >  동서 골목 5m 1차선
    # 위계가 2단(20m/5m)뿐이면 사분면 안이 전부 똑같아 보여 길을 잃는다. 남북만 넓혀
    # "남북=넓은 길 / 동서=좁은 골목"이라는 방향 단서를 만든다. 동시에 10m 지선이
    # 적 수백의 중간 배수로가 되어 5m 골목의 병목을 완화한다.
    # 2026-07-22 사용자 결정: **지선·골목 철회, 큰길 십자만 유지.**
    # 사분면 안쪽 동선은 Scifi 타일 지선이 아니라 사용자가 직접 놓는 미션 구역 + 연결 도로가 담당한다.
    # (메뉴의 'Generate Roads'가 지선을 되살리지 않도록 기본값을 False로 둔다. True로 되돌리면 복원됨)
    'alley':             False,  # 끄면 큰길 십자만 남는다
    'alley_major_center': 7000,  # 남북 지선의 중심축(+측). None이면 사분면 중앙 자동 산출(위 주석 참고)
    'alley_major_half':  500,    # 남북 지선 차도 반폭(2차선 = 폭 1000 = 10m)
    'alley_major_key':   'road',  # 황색 이중선
    'alley_minor_axis':  6500,   # 동서 골목이 놓이는 셀(+측). None이면 자동(위 주석 참고)
    'alley_minor_key':   'bare',  # 민무늬 아스팔트(사용자 결정: 뒷골목)
    # --- 통로 최소 폭 (S1-R2) ---
    # 플로우필드 셀 200cm + 에이전트 발자국 반경 40cm → **일반 정렬에서 보장되는 하한 = 280cm**.
    # 그보다 좁으면 셀이 통째로 blocked 로 잡혀 적이 아예 안 들어오거나 입구에서 뭉친다
    # (4인 협동에서 한 명이 입구를 막고 무한 학살하는 구도). 여유를 둔 실사용 하한 = 400.
    'min_clear_width':   400,
}
CLEAR_WIDTH_FLOOR = 300   # 절대 하한. 이보다 좁으면 생성을 거부한다.

_ENV = "/Game/PolygonScifi/Meshes/Environments/"
MESH = {
    'road':   _ENV + "SM_Env_Road_YellowLines_01_SF.SM_Env_Road_YellowLines_01_SF",
    'walk':   _ENV + "SM_Env_Sidewalk_Straight_01_SF.SM_Env_Sidewalk_Straight_01_SF",
    'corner': _ENV + "SM_Env_Sidewalk_Corner_01_SF.SM_Env_Sidewalk_Corner_01_SF",
    'bare':   _ENV + "SM_Env_Road_Bare_Half_01_SF.SM_Env_Road_Bare_Half_01_SF",
}
# 로컬 footprint (X크기, Y깊이). _rect()가 월드 점유 범위를 계산할 때 쓴다.
FOOT = {'road': (500, 500), 'walk': (500, 500), 'corner': (500, 500), 'bare': (500, 250)}

TAG = "CityLayout:S1"     # 단계별 되돌리기 규약 — 이 태그가 붙은 액터만 clear_roads()가 지운다.
FOLDER = "City/Roads"
STA = unreal.ComponentMobility.STATIC
AUDIT_GRID = 250          # 빈틈/겹침 검산 해상도(bare 타일이 250이라 250이 최소 단위)


def _M(p):
    return unreal.load_object(None, p) if p else None


def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _merge_spec(spec):
    """부분 사양을 기본 SPEC 위에 얹는다. fpsr_citygen._merge_cfg()와 같은 관례 —
    generate_roads({'plaza_half': 500}) 처럼 바꿀 값만 넘길 수 있어야 한다."""
    s = dict(SPEC)
    if spec:
        s.update(spec)
    return s


# ---------------- 셀 <-> 월드 변환 (위 [셀 채우기 공식] 표를 그대로 옮긴 것) ----------------
# yaw -> (피벗이 셀의 X최소에서 몇 칸, Y최소에서 몇 칸 떨어지는가). if/elif 체인보다 대조·검증이 쉽다.
_CELL_OFFSET = {0: (0, 1), 90: (0, 0), 180: (1, 0), 270: (1, 1)}


def _cell_pivot(key, cx, cy, yaw, z):
    """월드 셀의 최소모서리 (cx,cy)에 key 메시를 yaw로 놓을 때의 액터 피벗 위치."""
    w, d = FOOT[key]
    # 90/270은 로컬 X가 월드 Y로 가므로 셀에서 차지하는 폭/깊이가 뒤바뀐다.
    sx, sy = (w, d) if yaw in (0, 180) else (d, w)
    ox, oy = _CELL_OFFSET[yaw]
    return unreal.Vector(cx + ox * sx, cy + oy * sy, z)


def _rect(key, cx, cy, yaw):
    """그 배치가 실제로 덮는 월드 사각형 (x0, x1, y0, y1). audit() 전용."""
    w, d = FOOT[key]
    sx, sy = (w, d) if yaw in (0, 180) else (d, w)
    return (cx, cx + sx, cy, cy + sy)


# ---------------- 순수 계산: 배치 목록 ----------------
def _alley_params(s):
    """지선 사양을 (남북 중심축, 남북 반폭, 동서 골목 셀)로 풀어 준다. 못 쓰면 None.

    남북 지선은 사분면 [band, half] 의 **중앙에 정확히 걸치도록** 중심축을 잡는다(반폭이 있으므로
    가능하다). 동서 골목은 1칸짜리라 중앙에 걸칠 수 없어 한 장 안쪽에 둔다 — 격자를 깨는 것보다
    소블록이 2.5m 어긋나는 편이 낫다(격자를 깨면 나중에 건물이 안 맞는다)."""
    if not s.get('alley'):
        return None
    t, band, hf = s['tile'], s['road_half'] + s['walk'], s['half']
    mid = (band + hf) // 2
    c = s.get('alley_major_center')
    if c is None:
        c = (mid // t) * t
    h = s['alley_major_half']
    ma = s.get('alley_minor_axis')
    if ma is None:
        ma = c - t
    bad = []
    if c % t or h % t or h <= 0 or not (band + h <= c <= hf - h):
        bad.append(f"alley_major_center={c}/half={h}")
    if ma % t or not (band <= ma <= hf - t):   # ma+t == hf(경계에 딱 붙는 배치)도 유효하다
        bad.append(f"alley_minor_axis={ma}")
    if bad:
        unreal.log_warning(f"[CityLayout] 지선 사양 오류: {', '.join(bad)} (tile={t}, 사분면 {band}~{hf})")
        return None
    return c, h, ma


def _check_clear_widths(s):
    """모든 통로의 유효 폭이 플로우필드가 감당할 하한 이상인지 검사한다(S1-R2).
    반환 True면 통과. 하한 미만이면 생성을 거부한다 — 좁은 통로는 눈으로 안 보이고 PIE에서야
    '적이 안 들어온다'로 드러나기 때문에 코드가 먼저 막는다."""
    mn = s['min_clear_width']
    lanes = [("큰길 차도", s['road_half'] * 2), ("인도(편도)", s['walk'])]
    ap = _alley_params(s)
    if ap:
        c, h, ma = ap
        lanes.append(("남북 지선", h * 2))
        lanes.append(("동서 골목", s['tile']))
    ok = True
    for name, w in lanes:
        if w < CLEAR_WIDTH_FLOOR:
            unreal.log_warning(f"[CityLayout] {name} 폭 {w}cm < 절대 하한 {CLEAR_WIDTH_FLOOR}cm "
                               f"— 플로우필드 셀 200 + 발자국 40 때문에 적이 못 들어옵니다. 생성 중단.")
            ok = False
        elif w < mn:
            unreal.log_warning(f"[CityLayout] {name} 폭 {w}cm < 권장 하한 {mn}cm — 적이 뭉칠 수 있습니다.")
    return ok


def build_instances(spec=None):
    """레이아웃 사양으로부터 배치 목록을 계산한다. **부작용 없음**(unreal 액터/컴포넌트를 만들지 않는다)
    — 검산이 이 함수 하나로 가능하도록 분리했다.

    반환: [(key, cell_x, cell_y, yaw), ...]  (cell_x/cell_y = 그 배치가 덮는 셀의 최소 모서리)"""
    s = _merge_spec(spec)
    hf, t, rh, wk, ph = s['half'], s['tile'], s['road_half'], s['walk'], s['plaza_half']
    band = rh + wk                      # 회랑(차도+인도) 반폭

    for name, v in (('half', hf), ('road_half', rh), ('walk', wk), ('plaza_half', ph)):
        if v <= 0 or v % t:
            unreal.log_warning(f"[CityLayout] 사양 오류: {name}={v} 는 tile({t})의 양의 배수여야 합니다.")
            return []
    if ph < rh:
        unreal.log_warning(f"[CityLayout] 사양 오류: plaza_half({ph}) 가 road_half({rh})보다 작을 수 없습니다.")
        return []

    if not _check_clear_widths(s):
        return []

    out = []
    ap = _alley_params(s)

    # 차도 팔: 광장 바깥으로 뻗는다.  인도 팔: 회랑끼리 겹치지 않도록 max(plaza_half, band)부터.
    road_arm = list(range(ph, hf, t)) + list(range(-hf, -ph, t))
    ws = max(ph, band)
    walk_arm = list(range(ws, hf, t)) + list(range(-hf, -ws, t))
    # 인도 띠가 놓이는 가로 방향 셀들(편도 폭이 tile 여러 장일 수도 있으므로 range로)
    strip = list(range(rh, band, t)) + list(range(-band, -rh, t))

    # ---- 차도 ----  남북: 서쪽차선 yaw0 / 동쪽차선 yaw180  (황색선이 둘 다 x=0 으로 모인다)
    for cy in road_arm:
        for cx in range(-rh, rh, t):
            out.append(('road', cx, cy, 0 if cx < 0 else 180))
    #               동서: 남쪽차선 yaw90 / 북쪽차선 yaw270  (황색선이 둘 다 y=0 으로 모인다)
    for cx in road_arm:
        for cy in range(-rh, rh, t):
            out.append(('road', cx, cy, 90 if cy < 0 else 270))

    # ---- 인도 ----  연석(로컬 +Y)이 항상 차도를 향하도록 yaw 를 고른다.
    for cy in walk_arm:                      # 남북도로 좌우 인도
        for cx in strip:
            out.append(('walk', cx, cy, 270 if cx < 0 else 90))   # 연석 +X / -X
    for cx in walk_arm:                      # 동서도로 상하 인도
        for cy in strip:
            out.append(('walk', cx, cy, 0 if cy < 0 else 180))    # 연석 +Y / -Y

    # ---- 광장 ----  bare 는 500x250 이라 한 셀에 위/아래 두 장.
    for cx in range(-ph, ph, t):
        for cy in range(-ph, ph, t):
            out.append(('bare', cx, cy, 0))
            out.append(('bare', cx, cy + t // 2, 0))

    # ---- 지선 ----  각 사분면을 다시 십자로 가른다(S1-R1, 위계 3단).
    #   남북 지선 = 2차선 10m(황색 이중선)  /  동서 골목 = 1차선 5m(민무늬, 인도 없음)
    # 사분면 안(|좌표| >= band)에서만 뻗고, 둘이 만나는 칸은 차선을 지우고 민무늬로 깐다.
    if ap is not None:
        c, h, ma = ap
        mk, nk = s['alley_major_key'], s['alley_minor_key']
        span = list(range(band, hf, t)) + list(range(-hf, -band, t))
        # 남북 지선이 차지하는 x 셀 (+측 중심 c, -측 중심 -c). 황색선이 중심축에 모이도록 yaw 선택.
        major_x = {}
        for ctr in (c, -c):
            for cx in range(ctr - h, ctr + h, t):
                major_x[cx] = 0 if cx < ctr else 180             # +X면이 중심 / -X면이 중심
        minor_y = [ma, -(ma + t)]                                # 동서 골목이 차지하는 y 셀
        cross = {(cx, cy) for cx in major_x for cy in minor_y}

        def fill(key, cx, cy):
            if FOOT[key][1] * 2 == t:                            # bare 처럼 절반 깊이면 위/아래 두 장
                out.append((key, cx, cy, 0)); out.append((key, cx, cy + t // 2, 0))
            else:
                out.append((key, cx, cy, 0))

        for cx, yaw in major_x.items():
            for cy in span:
                if (cx, cy) in cross:
                    continue
                out.append((mk, cx, cy, yaw))
        for cy in minor_y:
            for cx in span:
                if (cx, cy) not in cross:
                    fill(nk, cx, cy)
        for cx, cy in sorted(cross):                             # 교차 칸은 차선 없이 민무늬
            fill(nk, cx, cy)

    # ---- 코너 ----  위 셋으로 안 덮인 교차부 잔여 셀을 인도 코너로 채운다.
    # (plaza_half >= band 면 광장이 전부 삼키므로 0장. plaza_half == road_half 면 4장.)
    covered = set()
    for key, cx, cy, yaw in out:
        x0, x1, y0, y1 = _rect(key, cx, cy, yaw)
        for gx in range(int(x0), int(x1), t):
            for gy in range(int(y0), int(y1), t):
                covered.add((gx, gy))
    for cx in range(-band, band, t):
        for cy in range(-band, band, t):
            if (cx, cy) in covered:
                continue
            east, north = (cx + t // 2) > 0, (cy + t // 2) > 0
            # 연석은 차도(=원점) 쪽 두 면을 향해야 한다. 로컬 +Y·-X 가 향하는 방향으로 yaw 선택.
            yaw = (90 if north else 0) if east else (180 if north else 270)
            out.append(('corner', cx, cy, yaw))

    return out


# ---------------- 검산: 빈틈 / 겹침 ----------------
def audit(spec=None, instances=None):
    """회랑 영역을 AUDIT_GRID 격자로 래스터화해 겹침·빈틈 개수를 센다. 생성 전에 결함을 잡기 위한 것."""
    s = _merge_spec(spec)
    hf, t, rh, wk, ph = s['half'], s['tile'], s['road_half'], s['walk'], s['plaza_half']
    band = rh + wk
    g = AUDIT_GRID
    if instances is None:
        instances = build_instances(s)

    ap = _alley_params(s)
    t = s['tile']

    def wanted(gx, gy):
        """그 서브셀이 '덮여야 하는' 영역인가 = 두 회랑 ∪ 광장 ∪ 남북 지선 ∪ 동서 골목."""
        mx, my = abs(gx + g // 2), abs(gy + g // 2)
        if (mx < band and my < hf) or (my < band and mx < hf) or (mx < ph and my < ph):
            return True
        if ap is not None:
            c, h, ma = ap
            if (c - h < mx < c + h and band < my < hf) or (ma < my < ma + t and band < mx < hf):
                return True
        return False

    count = {}
    for key, cx, cy, yaw in instances:
        x0, x1, y0, y1 = _rect(key, cx, cy, yaw)
        for gx in range(int(x0), int(x1), g):
            for gy in range(int(y0), int(y1), g):
                count[(gx, gy)] = count.get((gx, gy), 0) + 1

    overlap = sum(1 for v in count.values() if v > 1)
    outside = sum(1 for (gx, gy) in count if not wanted(gx, gy))
    gap = sum(1 for gx in range(-hf, hf, g) for gy in range(-hf, hf, g)
              if wanted(gx, gy) and (gx, gy) not in count)
    return {'overlap': overlap, 'gap': gap, 'outside': outside, 'cells': len(count)}


# ---------------- ISM 굽기 (fpsr_citygen.bake_building() 의 SubobjectDataSubsystem 패턴 재사용) ----------------
def _build_isms(instances, z):
    """배치 목록을 액터 1개("CityRoads") 아래 메시별 InstancedStaticMeshComponent로 굳힌다."""
    eas = _eas()
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    holder = eas.spawn_actor_from_class(unreal.Actor, unreal.Vector(0, 0, 0))
    holder.set_actor_label("CityRoads")
    holder.set_folder_path(FOLDER)

    root = sds.k2_gather_subobject_data_for_instance(holder)[0]
    ism_map = {}

    def get_ism(key):
        if key in ism_map:
            return ism_map[key]
        h, _ = sds.add_new_subobject(
            unreal.AddNewSubobjectParams(parent_handle=root, new_class=unreal.InstancedStaticMeshComponent))
        c = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(sds.k2_find_subobject_data_from_handle(h))
        c.set_editor_property('mobility', STA)
        c.set_static_mesh(_M(MESH[key]))
        c.set_collision_profile_name('NoCollision')      # 바닥 평판이 충돌 담당 — 플로우필드 장애물 0
        c.set_editor_property('render_custom_depth', True)
        c.set_editor_property('custom_depth_stencil_value', 1)
        ism_map[key] = c
        return c

    for key, cx, cy, yaw in instances:
        tf = unreal.Transform(location=_cell_pivot(key, cx, cy, yaw, z),
                              rotation=unreal.Rotator(yaw=yaw),   # ⚠ 위치인자는 (roll,pitch,yaw)라 키워드 필수
                              scale=unreal.Vector(1, 1, 1))
        get_ism(key).add_instance(tf, True)

    tags = holder.get_editor_property('tags')
    tags.append(unreal.Name(TAG))
    holder.set_editor_property('tags', tags)
    return holder, ism_map


# ---------------- 공개 함수 ----------------
def clear_roads():
    """태그 'CityLayout:S1' 이 붙은 액터만 찾아 삭제한다. 그 외 레벨 액터는 절대 건드리지 않는다."""
    eas = _eas()
    tgt = [a for a in eas.get_all_level_actors() if a and TAG in [str(t) for t in a.tags]]
    for a in tgt:
        eas.destroy_actor(a)
    unreal.log(f"[CityLayout] 태그 {TAG} 액터 {len(tgt)}개 제거")
    return len(tgt)


def generate_roads(spec=None):
    """도로+인도+광장을 새로 생성한다. 재실행 가능(기존 'CityLayout:S1' 태그 액터를 먼저 지운다).
    빈틈이나 겹침이 있으면 생성하지 않고 멈춘다. 맵은 저장하지 않는다 — 저장은 사용자가 직접."""
    s = _merge_spec(spec)
    instances = build_instances(s)
    if not instances:
        unreal.log_warning("[CityLayout] 생성할 배치가 없습니다(사양을 확인하세요).")
        return None
    a = audit(s, instances)
    if a['overlap'] or a['gap']:
        unreal.log_warning(f"[CityLayout] 검산 실패 — 겹침 {a['overlap']} / 빈틈 {a['gap']} "
                           f"(AUDIT_GRID={AUDIT_GRID}). 생성을 중단합니다.")
        return None
    clear_roads()
    holder, ism_map = _build_isms(instances, s['z'])
    unreal.log(f"[CityLayout] {holder.get_actor_label()} 생성 완료 — ISM {len(ism_map)}종(=드로우콜), "
               f"인스턴스 {len(instances)}개, 겹침 0 / 빈틈 0")
    return holder


def describe(spec=None):
    """생성하지 않고 메시별 예상 물량 + 빈틈/겹침 검산 결과만 로그로 출력한다."""
    s = _merge_spec(spec)
    instances = build_instances(s)
    counts = {}
    for key, _cx, _cy, _yaw in instances:
        counts[key] = counts.get(key, 0) + 1
    unreal.log(f"[CityLayout] === describe() plaza_half={s['plaza_half']} (생성 안 함) ===")
    for key in sorted(counts):
        unreal.log(f"[CityLayout]  {key:7s} {MESH[key].split('/')[-1].split('.')[0]}: {counts[key]}개")
    a = audit(s, instances)
    unreal.log(f"[CityLayout]  합계 {len(instances)}개 · 겹침 {a['overlap']} · 빈틈 {a['gap']} "
               f"· 회랑밖 {a['outside']}")
    return {'counts': counts, 'audit': a}


# ---------------- S2: 사용자 마커 자리를 도로로 포장 ----------------
S2_TAG = "CityLayout:S2"
PAVE = {
    'holder':  'CityPaths',
    'key':     'bare',          # 골목과 같은 민무늬 아스팔트(사용자 결정)
    'z_bump':  0.5,             # 큰길 타일(z=1)보다 0.5cm 위 — 인도와 겹치는 구간의 z-파이팅 회피
    # 마커는 **재질로 식별**한다(라벨 아님). 사용자가 큐브를 회전/스케일해 놓으면 이름이 'Cube'로
    # 남는 경우가 많은데, 라벨 접두어로 잡으면 그런 대각선 도로를 놓쳐 포장이 비는 '유격'이 생긴다.
    'marker_mats': ('MI_Dev_Road', 'MI_Dev_Zone_Mission'),
}


def _is_marker(a, mats=None):
    """이 액터가 포장 대상 마커인가 = StaticMeshActor + 첫 슬롯 재질이 마커 재질."""
    if not a or a.get_class().get_name() != 'StaticMeshActor':
        return False
    c = a.static_mesh_component
    m = c.get_material(0) if c.get_num_materials() else None
    return bool(m) and m.get_name() in (mats or PAVE['marker_mats'])


def _marker_boxes(mats=None):
    """포장할 마커를 **회전까지 반영한** 방향 박스로 읽는다.
    반환 [(라벨, cx, cy, yaw, L, W), ...] — L=길이(로컬X), W=폭(로컬Y), 둘 다 cm.
    마커는 /Engine/BasicShapes/Cube(100cm)이므로 실크기 = 100 x 스케일. 높이는 무시(보기용 1m)."""
    out = []
    for a in _eas().get_all_level_actors():
        if not _is_marker(a, mats):
            continue
        loc = a.get_actor_location(); s = a.get_actor_scale3d(); yaw = a.get_actor_rotation().yaw
        out.append((a.get_actor_label(), loc.x, loc.y, yaw, 100.0 * s.x, 100.0 * s.y))
    return sorted(out, key=lambda r: (r[0], r[1], r[2]))


def clear_paths():
    """포장한 도로(S2)만 지운다. 마커와 큰길(S1)은 건드리지 않는다."""
    eas = _eas()
    n = 0
    for a in list(eas.get_all_level_actors()):
        if a and (S2_TAG in [str(t) for t in a.tags] or a.get_actor_label() == PAVE['holder']):
            eas.destroy_actor(a); n += 1
    unreal.log(f"[CityLayout] 포장 도로 홀더 {n}개 제거")
    return n


def _oriented_transform(cx, cy, yaw, L, W, z, pad=0.0):
    """중심(cx,cy)·회전 yaw·크기 L x W 의 방향 박스를 bare 타일 한 장으로 정확히 덮는 트랜스폼.
    (수학 검증 = scratchpad, 오차 0.0000cm)  bare 로컬 X[0,fw] Y[-fd,0], 피벗=(minX,maxY).

    pad>0이면 사방으로 pad(cm)만큼 부풀린다 — 조각끼리 살짝 겹쳐 하이라인 틈(유격)으로 바닥이
    새는 것을 막는다(민무늬 아스팔트라 겹쳐도 표가 안 남). 중심·회전은 그대로."""
    fw, fd = FOOT[PAVE['key']]
    L2, W2 = L + 2 * pad, W + 2 * pad
    th = math.radians(yaw); cos, sin = math.cos(th), math.sin(th)
    px = cx + (-L2 / 2) * cos - (W2 / 2) * sin
    py = cy + (-L2 / 2) * sin + (W2 / 2) * cos
    return unreal.Transform(location=unreal.Vector(px, py, z),
                            rotation=unreal.Rotator(yaw=yaw),
                            scale=unreal.Vector(L2 / fw, W2 / fd, 1.0))


def pave_markers(spec=None, mats=None, hide_markers=True, pad=10.0):
    """마커가 표시한 자리를 도로 타일로 포장한다(재실행 가능).

    마커는 **재질(MI_Dev_Road / MI_Dev_Zone_Mission)로 식별**하고 **회전·스케일을 그대로 반영**한다 —
    사용자가 큐브를 돌려 대각선 도로를 놓아도(이름이 'Cube'로 남아도) 정확히 그 방향·크기로 깔린다.
    타일을 잘라 붙이지 않고 한 장을 스케일해 덮는다(bare는 마킹 없는 단색 아스팔트라 늘려도 표가 안 난다).

    pad(cm) = 각 조각을 사방으로 살짝 부풀려 이웃과 겹치게 한다(기본 10). 조각이 딱 안 붙어 사이로
    바닥이 새는 '유격'을 막는다 — 특히 대각선이 사각 구역과 만나는 접합부의 얇은 삼각 틈을 덮는다.
    pad=0이면 마커 크기 그대로(겹침 없음)."""
    s = _merge_spec(spec)
    boxes = _marker_boxes(mats)
    if not boxes:
        unreal.log_warning("[CityLayout] 포장할 마커가 없습니다(재질 MI_Dev_Road/MI_Dev_Zone_Mission)."); return None
    eas = _eas()
    clear_paths()
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    holder = eas.spawn_actor_from_class(unreal.Actor, unreal.Vector(0, 0, 0))
    holder.set_actor_label(PAVE['holder']); holder.set_folder_path(FOLDER)
    h, _ = sds.add_new_subobject(unreal.AddNewSubobjectParams(
        parent_handle=sds.k2_gather_subobject_data_for_instance(holder)[0],
        new_class=unreal.InstancedStaticMeshComponent))
    c = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(sds.k2_find_subobject_data_from_handle(h))
    c.set_editor_property('mobility', STA)
    c.set_static_mesh(_M(MESH[PAVE['key']]))
    c.set_collision_profile_name('NoCollision')      # 바닥 평판이 충돌 담당
    c.set_editor_property('render_custom_depth', True)
    c.set_editor_property('custom_depth_stencil_value', 1)

    z = s['z'] + PAVE['z_bump']
    rotated = 0
    for label, cx, cy, yaw, L, W in boxes:
        c.add_instance(_oriented_transform(cx, cy, yaw, L, W, z, pad), True)
        if abs(yaw) > 0.5 and abs(abs(yaw) - 180) > 0.5:
            rotated += 1
    tg = holder.get_editor_property('tags'); tg.append(unreal.Name(S2_TAG))
    holder.set_editor_property('tags', tg)

    hidden = 0
    if hide_markers:
        # 마커는 1m 높이 BlockAll 큐브다. 포장이 끝나면 반드시 **숨김 + 콜리전 해제**를 같이 해야 한다 —
        # 에디터 숨김은 렌더링만 끄므로, 콜리전을 남기면 깔아둔 길 한가운데 1m 벽이 그대로 서 있게 되고
        # 플레이어(점프 최고점 90)도 적(평지 스텝업 60)도 못 넘어 길이 통째로 막힌다(실측 확인).
        # 지우지 않고 꺼두기만 하므로 show_markers()로 언제든 되살릴 수 있다.
        for a in eas.get_all_level_actors():
            if _is_marker(a, mats):
                a.set_is_temporarily_hidden_in_editor(True)
                a.static_mesh_component.set_collision_profile_name('NoCollision')
                hidden += 1
    unreal.log(f"[CityLayout] 마커 {len(boxes)}곳 포장 완료 (회전 {rotated}곳, 마커 {hidden}개 숨김+콜리전 해제)")
    prune_sidewalk_under_paths()   # 포장 밑에서 연석이 삐죽 튀어나온 '깨진 인도' 정리
    return holder


def _inst_aabb(comp, i, fw, fd):
    """ISM 인스턴스 하나의 월드 XY AABB (x0,x1,y0,y1). 임의 yaw 지원(4코너를 회전해 감싸는 상자)."""
    t = comp.get_instance_transform(i, True); l = t.translation; s = t.scale3d
    th = math.radians(t.rotation.euler().z); cos, sin = math.cos(th), math.sin(th)
    xs, ys = [], []
    for lx, ly in [(0, 0), (fw, 0), (fw, -fd), (0, -fd)]:
        ax, ay = lx * s.x, ly * s.y
        xs.append(l.x + ax * cos - ay * sin); ys.append(l.y + ax * sin + ay * cos)
    return min(xs), max(xs), min(ys), max(ys)


def prune_sidewalk_under_paths():
    """포장 도로(CityPaths) 밑에 깔려 연석이 삐죽 튀어나온 큰길 인도 타일을 제거한다.

    인도는 7cm 올라온 연석이 있는데 포장 타일은 z=1.5라, 포장 자리에 인도가 겹치면 어두운 도로 위로
    흰 연석 조각만 삐져나와 '깨진 인도'로 보인다(사용자 지적 2026-07-22). 겹치는 인도 인스턴스를
    지운다 — 인도는 NoCollision이라 지워도 적 이동엔 영향이 없다(바닥은 Base가 담당).
    ⚠️ generate_roads()를 다시 돌리면 인도가 복원되므로, **pave 이후에 호출**해야 한다
    (pave_markers가 자동으로 부른다)."""
    eas = _eas()
    roads = next((a for a in eas.get_all_level_actors() if a and a.get_actor_label() == "CityRoads"), None)
    paths = next((a for a in eas.get_all_level_actors() if a and a.get_actor_label() == PAVE['holder']), None)
    if not roads or not paths:
        return 0
    walk = next((c for c in roads.get_components_by_class(unreal.InstancedStaticMeshComponent)
                 if "Sidewalk" in c.static_mesh.get_name()), None)
    if not walk:
        return 0
    # 포장 자리는 마커의 **방향 박스**로 판정한다(회전 반영). AABB로 하면 대각선 타일이 실제로 안 덮는
    # 큰길 인도까지 잘못 지운다(2026-07-22 버그: 넓힌 대각선이 서·북 인도 9개 오삭제).
    boxes = _marker_boxes()

    def in_box(px, py, cx, cy, yaw, L, W):
        th = math.radians(yaw); c = math.cos(th); s = math.sin(th)
        lx = (px - cx) * c + (py - cy) * s
        ly = -(px - cx) * s + (py - cy) * c
        return abs(lx) <= L / 2 and abs(ly) <= W / 2

    wfw, wfd = FOOT['walk']
    doomed = []
    for i in range(walk.get_instance_count()):
        b = _inst_aabb(walk, i, wfw, wfd)          # 인도는 축정렬이라 AABB=실제 박스
        cx, cy = (b[0] + b[1]) / 2, (b[2] + b[3]) / 2
        # 인도 타일 중심+네 안쪽점 중 3개 이상이 어떤 포장 박스 안이면 = 실제로 밑에 깔림
        samples = [(cx, cy), (cx - 150, cy - 150), (cx + 150, cy - 150), (cx - 150, cy + 150), (cx + 150, cy + 150)]
        covered = sum(1 for px, py in samples
                      if any(in_box(px, py, bx, by, yaw, L, W) for _, bx, by, yaw, L, W in boxes))
        if covered >= 3:
            doomed.append(i)
    for i in sorted(doomed, reverse=True):   # 인덱스가 밀리므로 뒤에서부터
        walk.remove_instance(i)
    unreal.log(f"[CityLayout] 포장 밑 인도 {len(doomed)}개 제거(깨진 인도 정리)")
    return len(doomed)


def show_markers(mats=None, restore_collision=True):
    """숨긴 마커를 다시 보이게(그리고 원래대로 막게) 한다."""
    n = 0
    for a in _eas().get_all_level_actors():
        if _is_marker(a, mats):
            a.set_is_temporarily_hidden_in_editor(False)
            if restore_collision:
                a.static_mesh_component.set_collision_profile_name('BlockAll')
            n += 1
    unreal.log(f"[CityLayout] 마커 {n}개 다시 표시")
    return n


# ---------------- S3: 큰길 정면 건물 줄 (fpsr_citygen 이 실제 조립을 담당) ----------------
S3_TAG = "CityLayout:S3"
FRONTAGE = {
    'setback':  10,     # 인도 바깥선에서 건물 정면까지(사용자가 손으로 놓은 값 실측 = 10)
    'width':    3,      # 모듈 칸 (3 = 750cm)
    'depth':    3,
    'floors':   (6, 10),  # 채마다 이 범위에서 랜덤(스카이라인 요철)
    'pitch':    800,    # 건물 간격 = 깊이 750 + 틈 50
    'seed':     20260722,
    'holder':   'CityBaked_Frontage',
}
# 면별 규약: (yaw, 정면이 놓이는 축, 길이 방향 축, 부모가 길이방향으로 +쪽인가)
#   yaw 0  -> 문이 -Y / 건물은 +X,+Y 로 뻗음      yaw 90  -> 문이 +X / 건물은 -X,+Y
#   yaw 180-> 문이 +Y / 건물은 -X,-Y             yaw 270 -> 문이 -X / 건물은 +X,-Y
# (실측 검증 2026-07-22: 부모를 돌리면 자식 조각이 전부 따라온다)
_SIDES = {
    'N': dict(yaw=0,   front='y', sign=+1, along='x', plus=True),
    'S': dict(yaw=180, front='y', sign=-1, along='x', plus=False),
    'W': dict(yaw=90,  front='x', sign=-1, along='y', plus=True),
    'E': dict(yaw=270, front='x', sign=+1, along='y', plus=False),
}


def _subtract(intervals, holes):
    """[a,b] 구간 목록에서 구멍 구간들을 뺀다. 지선·골목이 지나는 자리에 건물을 세우면 길이 끊긴다."""
    out = list(intervals)
    for h0, h1 in holes:
        nxt = []
        for a, b in out:
            if h1 <= a or h0 >= b:
                nxt.append((a, b)); continue
            if a < h0: nxt.append((a, h0))
            if h1 < b: nxt.append((h1, b))
        out = nxt
    return [(a, b) for a, b in out if b > a]


def frontage_slots(spec=None, fr=None):
    """큰길 4면 정면에 놓을 자리를 계산한다. **부작용 없음**(액터를 만들지 않는다).
    반환 [(side, parent_x, parent_y, yaw), ...] — 순서가 결정적이라 시드 재현이 보장된다."""
    s = _merge_spec(spec)
    f = dict(FRONTAGE); f.update(fr or {})
    t, hf = s['tile'], s['half']
    band = s['road_half'] + s['walk']
    dep = f['depth'] * 250                       # 건물 깊이(cm)
    F = band + f['setback']                      # 정면선
    ap = _alley_params(s)
    holes_x, holes_y = [], []
    if ap:
        c, h, ma = ap
        holes_x = [(c - h, c + h), (-(c + h), -(c - h))]     # 남북 지선이 X를 가로막는 구간
        holes_y = [(ma, ma + t), (-(ma + t), -ma)]           # 동서 골목이 Y를 가로막는 구간

    slots = []
    for side in ('N', 'S', 'W', 'E'):
        d = _SIDES[side]
        if d['along'] == 'x':
            # 동서 큰길변: 모서리를 이쪽이 가져가므로 정면선(F)부터 바로 시작한다.
            free = _subtract([(F, hf), (-hf, -F)], holes_x)
        else:
            # 남북 큰길변: 모서리는 동서 줄이 이미 쓰므로 그 깊이(F+dep)만큼 물러나 시작한다.
            free = _subtract([(F + dep, hf), (-hf, -(F + dep))], holes_y)
        for a, b in free:
            if b - a < dep:
                continue
            n = int((b - a - dep) // f['pitch']) + 1
            for i in range(n):
                t0 = a + i * f['pitch']
                al = t0 if d['plus'] else t0 + dep    # 부모는 뻗는 방향의 반대쪽 끝
                fc = d['sign'] * F
                slots.append((side, al, fc, d['yaw']) if d['along'] == 'x' else (side, fc, al, d['yaw']))
    return slots


def clear_frontage():
    """S3 태그가 붙은 건물과 그 Bake 홀더만 지운다. 손으로 놓은 다른 액터는 건드리지 않는다."""
    eas = _eas()
    n = 0
    for a in list(eas.get_all_level_actors()):
        if not a:
            continue
        tags = [str(x) for x in a.tags]
        if S3_TAG in tags or a.get_actor_label() == FRONTAGE['holder']:
            for k in list(a.get_attached_actors()):
                eas.destroy_actor(k)
            eas.destroy_actor(a); n += 1
    unreal.log(f"[CityLayout] S3 정면 건물/홀더 {n}개 제거")
    return n


def build_frontage(spec=None, fr=None, sides=None, bake=True, cfg_over=None):
    """큰길 정면에 건물 줄을 세운다. **면 하나를 짓고 바로 Bake**해 홀더 하나에 이어붙인다
    — 96채(조각 1.5만)를 한꺼번에 만들면 에디터가 버티지 못하기 때문이다.

    sides=None이면 네 면 전부. 되돌리기는 clear_frontage()."""
    import random
    import fpsr_citygen as CG
    eas = _eas()
    s = _merge_spec(spec)
    f = dict(FRONTAGE); f.update(fr or {})
    cfg = CG._merge_cfg(CG.load_config_from_dataasset())
    cfg.update(cfg_over or {})   # 이 호출에서만 덮어쓸 citygen 설정(층수 범위·경고 상한 등)
    pools = CG._load_pools(cfg)
    slots = [q for q in frontage_slots(s, f) if sides is None or q[0] in sides]
    if not slots:
        unreal.log_warning("[CityLayout] 세울 자리가 없습니다."); return None
    rnd = random.Random(f['seed'])
    fmin, fmax = f['floors']
    holder = next((a for a in eas.get_all_level_actors()
                   if a and a.get_actor_label() == f['holder']), None)
    made_total = 0
    for side in ('N', 'S', 'W', 'E'):
        group = [q for q in slots if q[0] == side]
        if not group:
            continue
        parents = []
        for i, (_sd, px, py, yaw) in enumerate(group):
            floors = rnd.randint(fmin, fmax)
            bseed = rnd.randrange(1 << 30)
            p = CG._build_one(unreal.Vector(px, py, 0), f['width'], f['depth'], floors,
                              pools, cfg, bseed, f"Frontage_{side}_{i:02d}")
            p.set_actor_rotation(unreal.Rotator(yaw=float(yaw)), False)
            tg = p.get_editor_property('tags'); tg.append(unreal.Name(S3_TAG))
            p.set_editor_property('tags', tg)
            parents.append(p)
        made_total += len(parents)
        if bake:
            holder = CG.bake_buildings(parents, f['holder'], holder=holder)
            if holder:
                tg = holder.get_editor_property('tags')
                if S3_TAG not in [str(x) for x in tg]:
                    tg.append(unreal.Name(S3_TAG)); holder.set_editor_property('tags', tg)
        unreal.log(f"[CityLayout] {side}면 {len(parents)}채 완료")
    unreal.log(f"[CityLayout] 정면 건물 총 {made_total}채")
    return holder


# ---------------- S3b: 2트랙 건물 배치 (큰길변 + 미션구역 감싸기) ----------------
# 건물은 BlockAll(적을 실제로 막는 벽)이라 도로·미션구역·큰길을 침범하면 안 된다.
# 정사각형 건물(width==depth)은 회전해도 footprint가 일정하므로 **중심 좌표 + footprint 사각형**으로
# 장애물을 피해 배치하고, 빌드 시에만 부모 위치를 역산한다(_center_to_parent).
BUILD = {
    'width': 3, 'depth': 3,          # 3x3 = 750 x 750 (정사각 → 회전 무관 footprint)
    'floors': (6, 10),               # 채마다 랜덤(스카이라인 요철)
    'front_off': 10,                 # 큰길 정면선: band(1000)에서 바깥으로 10 → 1010
    'wrap_gap': 100,                 # 미션구역 벽에서 감싸는 건물까지 통로(1m)
    'road_margin': 90,               # 도로/미션 조각에서 건물까지 최소 여유(적 통행폭 보존)
    'pitch': 800,                    # 건물 간격(깊이 750 + 틈 50)
    'seed': 20260722,
    'holder': 'CityBaked',
    'edge_margin': 60,               # 맵 벽에서 건물까지
}
S3_BUILD_TAG = "CityLayout:S3b"


def _building_obstacles(margin, drop=None, avenue_margin=0):
    """건물이 피해야 할 축정렬 사각형 목록 (x0,x1,y0,y1).
    = 큰길 회랑(십자) + 모든 마커(도로/미션) AABB를 margin 만큼 부풀림.
    큰길은 이미 5m 인도가 있어 건물이 정면선(1010)에 붙는 게 정상 → avenue_margin(기본 0)만 적용한다
    (서브도로/미션용 margin 을 큰길에도 쓰면 정면 건물이 통째로 탈락한다).
    drop = 제외할 마커 라벨 집합(자기 자신이 감싸는 미션구역은 장애물에서 뺀다)."""
    hf = SPEC['half']; band = SPEC['road_half'] + SPEC['walk']
    b = band + avenue_margin
    obs = [(-b, b, -hf, hf), (-hf, hf, -b, b)]           # N-S 회랑, E-W 회랑
    for lbl, cx, cy, yaw, L, W in _marker_boxes():
        if drop and lbl in drop:
            continue
        th = math.radians(yaw); c = abs(math.cos(th)); s = abs(math.sin(th))
        ex = (L * c + W * s) / 2 + margin               # 회전 마커는 AABB로(보수적=안전)
        ey = (L * s + W * c) / 2 + margin
        obs.append((cx - ex, cx + ex, cy - ey, cy + ey))
    return obs


def _slot_free(cx, cy, half, obstacles):
    """중심(cx,cy)·반변 half 인 정사각 footprint 가 어떤 장애물과도 안 겹치는가."""
    x0, x1, y0, y1 = cx - half, cx + half, cy - half, cy + half
    hf = SPEC['half'] - BUILD['edge_margin']
    if x0 < -hf or x1 > hf or y0 < -hf or y1 > hf:       # 맵 벽 밖으로
        return False
    for ox0, ox1, oy0, oy1 in obstacles:
        if x0 < ox1 and ox0 < x1 and y0 < oy1 and oy0 < y1:
            return False
    return True


def _center_to_parent(mcx, mcy, yaw, W, D):
    """footprint 중심(mcx,mcy)에서 _build_one 이 받을 부모 위치를 역산한다.
    _build_one 은 부모에서 +X,+Y 로 짓고 부모 기준 yaw 회전 → 중심 = 부모 + Rot(yaw)*(W*125, D*125)."""
    hx, hy = W * 125.0, D * 125.0
    th = math.radians(yaw); c, s = math.cos(th), math.sin(th)
    return unreal.Vector(mcx - (hx * c - hy * s), mcy - (hx * s + hy * c), 0.0)


def _door_yaw(mcx, mcy, face):
    """건물 문이 향할 방향(face='N'/'S'/'E'/'W' 또는 (dx,dy))에 맞는 yaw.
    _build_one 문은 로컬 +Y 정면(yaw0=+Y) 기준: 남(-Y)=0, 북(+Y)=180, 서(-X)=90, 동(+X)=270... 는
    build_frontage 실측 규약. 여기선 문이 향하는 월드 방향으로 매핑한다."""
    return {'S': 0, 'N': 180, 'W': 90, 'E': 270}[face]


def frontage_track(bsize=None):
    """트랙1: 큰길 4변 정면에 건물 중심 목록 생성 (부작용 없음).
    반환 [(mcx, mcy, yaw, tag), ...]. 도로/미션/서로 겹치는 자리는 건너뛴다."""
    W = D = (bsize or BUILD['width'])
    half = W * 125.0                                     # footprint 반변 (3→375)
    band = SPEC['road_half'] + SPEC['walk']
    F = band + BUILD['front_off']                        # 정면선 (1010)
    off = F + half                                       # 중심선까지 (1010+375=1385)
    hf = SPEC['half']
    obs = _building_obstacles(BUILD['road_margin'])
    out = []
    # 큰길 팔을 따라 pitch 간격으로. 회랑(|좌표|<band) 안쪽은 반대축 큰길이라 건다.
    def run(fixed_axis, fixed_val, face):
        rng = list(_frange(band + half, hf, BUILD['pitch'])) + list(_frange(-(band + half), -hf, -BUILD['pitch']))
        for v in rng:
            mcx, mcy = (fixed_val, v) if fixed_axis == 'x' else (v, fixed_val)
            if _slot_free(mcx, mcy, half, obs):
                out.append((mcx, mcy, _door_yaw(mcx, mcy, face), 'front'))
    run('x',  off, 'W')     # N-S 큰길 동쪽 변 (문이 서쪽=큰길)
    run('x', -off, 'E')     # 서쪽 변
    run('y',  off, 'S')     # E-W 큰길 북쪽 변 (문이 남쪽=큰길)
    run('y', -off, 'N')     # 남쪽 변
    return out


def wrap_track(bsize=None):
    """트랙2: 각 미션구역을 건물로 감싼 중심 목록 생성 (부작용 없음).
    도로가 연결되는 자리는 장애물에 걸려 자동으로 비어 '입구'가 된다."""
    W = D = (bsize or BUILD['width'])
    half = W * 125.0
    gap = BUILD['wrap_gap']
    out = []
    for lbl, cx, cy, yaw, L, W2 in _marker_boxes():
        if not lbl.startswith("Mission"):
            continue
        # 이 미션구역만 장애물에서 빼고(감싸는 대상이라 인접 허용), 나머지는 유지
        obs = _building_obstacles(BUILD['road_margin'], drop={lbl})
        x0, x1, y0, y1 = cx - L / 2, cx + L / 2, cy - W2 / 2, cy + W2 / 2
        ring_y_top = y1 + gap + half; ring_y_bot = y0 - gap - half
        ring_x_rt = x1 + gap + half; ring_x_lf = x0 - gap - half
        # 상·하 변: x를 zone 폭에 걸쳐 pitch 간격 (양끝 코너 포함)
        for mcx in _span(x0 - gap, x1 + gap, BUILD['pitch']):
            for mcy, face in [(ring_y_top, 'S'), (ring_y_bot, 'N')]:
                if _slot_free(mcx, mcy, half, obs):
                    out.append((mcx, mcy, _door_yaw(mcx, mcy, face), 'wrap'))
        # 좌·우 변: y를 zone 높이에 걸쳐 (코너는 위에서 이미 넣었으니 안쪽만)
        for mcy in _span(y0 - gap + BUILD['pitch'], y1 + gap - BUILD['pitch'], BUILD['pitch']):
            for mcx, face in [(ring_x_rt, 'W'), (ring_x_lf, 'E')]:
                if _slot_free(mcx, mcy, half, obs):
                    out.append((mcx, mcy, _door_yaw(mcx, mcy, face), 'wrap'))
    return out


def _frange(a, b, step):
    v = a
    if step > 0:
        while v < b: yield v; v += step
    else:
        while v > b: yield v; v += step


def _span(a, b, step):
    """[a,b] 를 step 간격으로 덮는 중심점들(양끝 포함, 균등)."""
    if b <= a:
        return [(a + b) / 2]
    n = max(1, int(math.ceil((b - a) / step)))
    return [a + (i + 0.5) * (b - a) / n for i in range(n)]


def _dedupe_slots(slots, mindist=None):
    """겹치는 건물을 제거(트랙1·2가 같은 자리를 낼 수 있고, 인접 미션의 감싸기 링이 만날 수 있다).
    mindist 기본 = 건물 한 변(3*250=750)보다 조금 큰 780 → 두 건물이 절대 안 겹친다.
    front(큰길변) 우선 유지 — 큰길 정면이 더 중요한 룩이다."""
    if mindist is None:
        mindist = BUILD['width'] * 250 + 30
    slots = sorted(slots, key=lambda q: 0 if q[3] == 'front' else 1)
    kept = []
    for mcx, mcy, yaw, tag in slots:
        # 정사각 footprint 는 AABB 겹침으로 판정(중심거리보다 정확). 두 변이 모두 mindist 미만이면 겹침.
        if all(not (abs(mcx - kx) < mindist and abs(mcy - ky) < mindist) for kx, ky, _, _ in kept):
            kept.append((mcx, mcy, yaw, tag))
    return kept


def building_slots(bsize=None):
    """2트랙 통합 건물 중심 목록(중복 제거)."""
    return _dedupe_slots(frontage_track(bsize) + wrap_track(bsize))


def clear_buildings():
    """S3b 태그 건물/홀더만 제거."""
    eas = _eas(); n = 0
    for a in list(eas.get_all_level_actors()):
        if not a:
            continue
        tags = [str(x) for x in a.tags]
        if S3_BUILD_TAG in tags or a.get_actor_label() == BUILD['holder']:
            for k in list(a.get_attached_actors()):
                eas.destroy_actor(k)
            eas.destroy_actor(a); n += 1
    unreal.log(f"[CityLayout] S3b 건물/홀더 {n}개 제거")
    return n


def build_city(bsize=None, quadrants=None, cfg_over=None):
    """2트랙 건물을 사분면 단위로 짓고 바로 Bake(홀더 하나로 병합).
    quadrants=None이면 4사분면 전부. 되돌리기=clear_buildings()."""
    import random
    import fpsr_citygen as CG
    eas = _eas()
    W = D = (bsize or BUILD['width'])
    cfg = CG._merge_cfg(CG.load_config_from_dataasset())
    cfg.update(cfg_over or {'max_mesh_types': 999})
    pools = CG._load_pools(cfg)
    slots = building_slots(bsize)
    fmin, fmax = BUILD['floors']
    rnd = random.Random(BUILD['seed'])
    holder = next((a for a in eas.get_all_level_actors() if a and a.get_actor_label() == BUILD['holder']), None)
    def quad(mcx, mcy): return ('N' if mcy >= 0 else 'S') + ('E' if mcx >= 0 else 'W')
    qs = quadrants or ('NE', 'NW', 'SE', 'SW')
    total = 0
    for q in qs:
        group = [sl for sl in slots if quad(sl[0], sl[1]) == q]
        if not group:
            continue
        parents = []
        for i, (mcx, mcy, yaw, tag) in enumerate(group):
            floors = rnd.randint(fmin, fmax)
            p = CG._build_one(_center_to_parent(mcx, mcy, yaw, W, D), W, D, floors,
                              pools, cfg, rnd.randrange(1 << 30), f"Bld_{q}_{i:02d}")
            p.set_actor_rotation(unreal.Rotator(yaw=float(yaw)), False)
            tg = p.get_editor_property('tags'); tg.append(unreal.Name(S3_BUILD_TAG))
            p.set_editor_property('tags', tg)
            parents.append(p)
        holder = CG.bake_buildings(parents, BUILD['holder'], holder=holder)
        if holder:
            tg = holder.get_editor_property('tags')
            if S3_BUILD_TAG not in [str(x) for x in tg]:
                tg.append(unreal.Name(S3_BUILD_TAG)); holder.set_editor_property('tags', tg)
        total += len(parents)
        unreal.log(f"[CityLayout] {q} {len(parents)}채")
    unreal.log(f"[CityLayout] 2트랙 건물 총 {total}채")
    return holder


# ---------------- S3c: 큐브 마커 기반 건물 (사용자가 Building_N 큐브로 크기·위치 지정) ----------------
# 사용자가 MI_Dev_Building 재질 큐브를 놓으면(가로x세로x높이), 그 크기에 맞는 건물을 짓는다.
# Demo_Interior 맵과 같은 SM_Bld_Base_* 모듈 키트로 조립(스타일 팔레트는 build 인자로 확장).
CUBE_MAT = 'MI_Dev_Building'
S3C_TAG = "CityLayout:S3c"
S3C_HOLDER = "CityBaked_Cubes"
MODULE_W = 250     # 가로 한 칸
MODULE_H = 300     # 층 높이


def cube_buildings():
    """Building_N 큐브(재질 MI_Dev_Building)를 읽어 [(라벨, cx, cy, floor_z, wm, dm, floors, yaw)]로.
    큐브 크기 → 모듈: 가로/세로는 250 반올림(로컬 X/Y), 높이는 300 반올림. 바닥 z = 큐브 중심 - 높이/2.
    **yaw(회전)까지 읽는다** — 큐브를 대각선으로 돌려 놓으면 건물도 그 방향으로 세운다."""
    out = []
    for a in _eas().get_all_level_actors():
        if not a or a.get_class().get_name() != 'StaticMeshActor':
            continue
        c = a.static_mesh_component
        m = c.get_material(0) if c.get_num_materials() else None
        if not (m and m.get_name() == CUBE_MAT):
            continue
        s = a.get_actor_scale3d(); loc = a.get_actor_location(); yaw = a.get_actor_rotation().yaw
        W, D, H = 100.0 * s.x, 100.0 * s.y, 100.0 * s.z   # 로컬 크기(회전 무관)
        wm = max(1, round(W / MODULE_W)); dm = max(1, round(D / MODULE_W)); fl = max(1, round(H / MODULE_H))
        out.append((a.get_actor_label(), loc.x, loc.y, loc.z - H / 2.0, wm, dm, fl, yaw))
    return sorted(out)


def clear_cube_buildings():
    """S3c 태그 건물/홀더만 제거(큐브 마커는 안 건드림)."""
    eas = _eas(); n = 0
    for a in list(eas.get_all_level_actors()):
        if not a:
            continue
        if S3C_TAG in [str(t) for t in a.tags] or a.get_actor_label() == S3C_HOLDER:
            for k in list(a.get_attached_actors()):
                eas.destroy_actor(k)
            eas.destroy_actor(a); n += 1
    unreal.log(f"[CityLayout] S3c 큐브 건물/홀더 {n}개 제거")
    return n


def _set_cubes_hidden(hidden):
    """Building_N 큐브를 숨김+콜리전해제 / 되돌리기. (건물이 자리를 차지하니 숨겨야 안 겹쳐 보인다)"""
    n = 0
    for a in _eas().get_all_level_actors():
        if not a or a.get_class().get_name() != 'StaticMeshActor':
            continue
        c = a.static_mesh_component
        m = c.get_material(0) if c.get_num_materials() else None
        if m and m.get_name() == CUBE_MAT:
            a.set_is_temporarily_hidden_in_editor(hidden)
            c.set_collision_profile_name('NoCollision' if hidden else 'BlockAll')
            n += 1
    return n


def show_cubes():
    """숨긴 Building_N 큐브를 다시 보이게(+콜리전 복원)."""
    n = _set_cubes_hidden(False)
    unreal.log(f"[CityLayout] Building 큐브 {n}개 다시 표시")
    return n


# ── 머티리얼 테마 ──────────────────────────────────────────────────────────
# Demo_Interior 방식: 벽 메시는 하나(SM_Bld_Base_Wall_01)로 두고 **머티리얼만 건물마다 바꿔** 통일감을 낸다.
# 각 테마 = [주 벽면(슬롯0), 보조 벽면(슬롯1)]. 건물 하나는 테마 하나로 통일, 건물끼리는 테마가 달라 다양해진다.
# Config(C++)엔 머티리얼 필드가 없어(추가 시 빌드 필요) 여기 Python에 둔다 — 사용자가 이 목록만 편집하면 된다.
MATERIAL_THEMES = [
    ["M_Wall_Brick_01_A", "M_Wall_20_A_Triplanar"],   # 벽돌 (사용자가 Demo에서 본 조합)
    ["M_Wall_01_A", "M_Wall_11_A"],                    # 콘크리트+패널
    ["M_Wall_01_C", "M_Wall_02_A"],                    # 밝은 벽
    ["M_Wall_05_A", "M_Wall_12_A"],                    # 금속
    ["MI_PolygonCyberCity_02_A", "M_Wall_11_C"],       # 사이버시티 아틀라스 B
    ["M_Wall_11_A", "M_Wall_20_B"],                    # 어두운 패널
]
_THEME_CACHE = {}


def _resolve_mat(name):
    if name in _THEME_CACHE:
        return _THEME_CACHE[name]
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    hits = [str(a.package_name) for a in ar.get_assets_by_path("/Game/PolygonCyberCity", recursive=True)
            if str(a.asset_name) == name]
    m = unreal.load_asset(hits[0]) if hits else None
    _THEME_CACHE[name] = m
    return m


def _theme_materials(idx):
    """테마 인덱스 → [주재질, 보조재질] 실제 에셋. 목록을 순환한다."""
    names = MATERIAL_THEMES[idx % len(MATERIAL_THEMES)]
    return [_resolve_mat(n) for n in names]


def _apply_theme(parent, mats):
    """건물(부모)의 벽 구조 조각에 테마 머티리얼을 일괄 오버라이드한다.
    슬롯0 = 주재질. 그 외 슬롯 중 '벽면(Wall)' 재질인 것 = 보조재질. 유리·문패널·발광·소품은 안 건드린다."""
    prim, sec = (mats + [None, None])[:2]
    for k in parent.get_attached_actors():
        smc = k.static_mesh_component
        mesh = smc.static_mesh
        if not mesh:
            continue
        nm = mesh.get_name()
        # 벽 구조 조각만(파사드/코너/도어/코니스/트림/기둥). 옥상 소품·바닥·프롭은 제외.
        if not any(t in nm for t in ("Wall", "Corner", "Trim", "Pillar", "Ceiling", "Door")):
            continue
        if any(t in nm for t in ("Prop_", "Antenna", "Satellite", "Junk", "Hover", "Metal_Pipe", "Electrical")):
            continue
        n = smc.get_num_materials()
        for i in range(n):
            cur = smc.get_material(i)
            curn = cur.get_name() if cur else ""
            if i == 0 and prim:
                smc.set_material(0, prim)
            elif sec and ("Wall" in curn):     # 다른 벽면 슬롯(트리플래너 등)은 보조재질로
                smc.set_material(i, sec)


# Demo_Interior 스타일: 옥상 잡동사니(전부 minZ≈0=바닥붙음, 반쯤 묻히지 않는 것만 선별).
# 현재 풀(안테나+위성)에 더해 실루엣을 다양화한다. 전부 NoCollision(roofprop_collide=False)이라 플로우필드 무관.
_DEMO_ROOFPROPS = [
    "SM_Prop_Hover_Light_01", "SM_Prop_Hover_Light_03", "SM_Prop_Hover_Light_05",
    "SM_Env_Junk_Pile_02", "SM_Env_Junk_Pile_04",
    "SM_Bld_Metal_Pipe_Frame_02", "SM_Prop_Electrical_Box_01",
]


def _enrich_demo(pools, cfg):
    """옥상 소품 풀을 Demo_Interior 잡동사니로 확장 + 채당 개수 상향(휑한 옥상 → 붐비는 스카이라인)."""
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    idx = {str(a.asset_name): str(a.package_name)
           for a in ar.get_assets_by_path("/Game/PolygonCyberCity", recursive=True)
           if str(a.asset_class_path.asset_name) == "StaticMesh"}
    have = {m.get_name() for m in pools['roofprops']}
    for name in _DEMO_ROOFPROPS:
        if name in idx and name not in have:
            m = unreal.load_asset(idx[name])
            if m:
                pools['roofprops'].append(m)
    cfg['roofprop_count'] = max(cfg.get('roofprop_count', 2), 6)   # 옥상당 6개 → 붐빔
    return pools


def build_from_cubes(cfg_over=None, hide=True, style='demo', bake=False):
    """Building_N 큐브 크기에 맞춰 건물을 **에셋 조각 그대로** 짓는다(기본 Bake 안 함).
    사용자가 조각을 세부 편집할 수 있게 개별 액터로 남긴다 — 굽는 건 bake_cube_buildings()로 따로.
    각 건물 = 부모 액터 1개 + 자식 메시 조각들(태그 S3C). 되돌리기=clear_cube_buildings()."""
    import random
    import fpsr_citygen as CG
    cubes = cube_buildings()
    if not cubes:
        unreal.log_warning("[CityLayout] Building_N 큐브(재질 MI_Dev_Building)가 없습니다."); return None
    cfg = CG._merge_cfg(CG.load_config_from_dataasset())
    cfg.update({'max_mesh_types': 999})
    cfg.update(cfg_over or {})
    pools = CG._load_pools(cfg)
    if style == 'demo':
        _enrich_demo(pools, cfg)
    clear_cube_buildings()
    rnd = random.Random(20260722)
    made = []
    for bi, (lbl, cx, cy, fz, wm, dm, fl, yaw) in enumerate(cubes):
        # 부모(밑변 최소코너) = 중심 - Rot(yaw)*(wm*125, dm*125). 회전 큐브는 그 방향으로 세운다.
        th = math.radians(yaw); cc, ss = math.cos(th), math.sin(th)
        hx, hy = wm * (MODULE_W / 2.0), dm * (MODULE_W / 2.0)
        parent = unreal.Vector(cx - (hx * cc - hy * ss), cy - (hx * ss + hy * cc), fz)
        p = CG._build_one(parent, wm, dm, fl, pools, cfg, rnd.randrange(1 << 30), f"CubeBld_{lbl}")
        p.set_actor_rotation(unreal.Rotator(yaw=float(yaw)), False)
        if style == 'demo':
            # 건물 하나 = 테마 하나로 통일. 건물마다 테마를 순환해 다양하게(재현 위해 인덱스 기반).
            _apply_theme(p, _theme_materials(bi))
        tg = p.get_editor_property('tags'); tg.append(unreal.Name(S3C_TAG))
        p.set_editor_property('tags', tg)
        p.set_folder_path("Buildings/CubeBuildings")
        made.append(p)
    if hide:
        _set_cubes_hidden(True)
    if bake:
        bake_cube_buildings()
    else:
        unreal.log(f"[CityLayout] 큐브 건물 {len(made)}채 조각으로 생성(Bake 안 함 — bake_cube_buildings()로 따로).")
    return made


def bake_cube_buildings():
    """지어둔(안 구운) 큐브 건물 조각들을 홀더 하나(CityBaked_Cubes)로 병합해 드로우콜을 낮춘다.
    사용자가 조각 편집을 끝낸 뒤 명시적으로 호출/버튼. 이미 구운 홀더가 있으면 거기에 이어붙인다."""
    import fpsr_citygen as CG
    eas = _eas()
    holder = next((a for a in eas.get_all_level_actors() if a and a.get_actor_label() == S3C_HOLDER), None)
    parents = [a for a in eas.get_all_level_actors()
               if a and S3C_TAG in [str(t) for t in a.tags]
               and a.get_actor_label() != S3C_HOLDER and a.get_attached_actors()]
    if not parents:
        unreal.log_warning("[CityLayout] 구울 큐브 건물이 없습니다(먼저 build_from_cubes)."); return holder
    holder = CG.bake_buildings(parents, S3C_HOLDER, holder=holder)
    if holder:
        tg = holder.get_editor_property('tags')
        if S3C_TAG not in [str(x) for x in tg]:
            tg.append(unreal.Name(S3C_TAG)); holder.set_editor_property('tags', tg)
    unreal.log(f"[CityLayout] 큐브 건물 {len(parents)}채 Bake 완료 → {S3C_HOLDER}")
    return holder
    return holder


# ---------------- 메뉴 등록 ----------------
def register_menu():
    menus = unreal.ToolMenus.get()
    tools = menus.find_menu("LevelEditor.MainMenu.Tools")
    if not tools:
        unreal.log_warning("[CityLayout] Tools 메뉴를 못 찾음"); return
    tools.add_sub_menu("FPSRCityLayout", "FPSR", "FPSRCityLayout", "FPSR CityLayout", "도로/인도/광장 레이아웃 생성 툴")
    sub = menus.find_menu("LevelEditor.MainMenu.Tools.FPSRCityLayout")

    def entry(name, label, code):
        e = unreal.ToolMenuEntry(name=name, type=unreal.MultiBlockType.MENU_ENTRY)
        e.set_label(label)
        e.set_string_command(unreal.ToolMenuStringCommandType.PYTHON, "", string=code)
        sub.add_menu_entry("CityLayout", e)

    entry("Generate", "1. Generate Roads", "import fpsr_citylayout; fpsr_citylayout.generate_roads()")
    entry("Clear", "2. Clear Roads", "import fpsr_citylayout; fpsr_citylayout.clear_roads()")
    entry("Describe", "3. Describe (검산, 생성 안 함)", "import fpsr_citylayout; fpsr_citylayout.describe()")
    entry("Plaza10", "4. 교차로 10m로 다시 (plaza_half=500)",
          "import fpsr_citylayout; fpsr_citylayout.generate_roads({'plaza_half': 500})")
    entry("Pave", "5. 마커 자리 도로로 포장", "import fpsr_citylayout; fpsr_citylayout.pave_markers()")
    entry("ClearPave", "6. 포장 도로만 지우기", "import fpsr_citylayout; fpsr_citylayout.clear_paths()")
    entry("ShowMarkers", "7. 숨긴 마커 다시 보기", "import fpsr_citylayout; fpsr_citylayout.show_markers()")
    entry("BuildCubes", "8. Building 큐브로 건물 짓기 (조각, Bake 안 함)", "import fpsr_citylayout; fpsr_citylayout.build_from_cubes()")
    entry("BakeCubes", "9. 큐브 건물 Bake (조각 → 병합)", "import fpsr_citylayout; fpsr_citylayout.bake_cube_buildings()")
    entry("ClearCubeBld", "10. 큐브 건물만 지우기", "import fpsr_citylayout; fpsr_citylayout.clear_cube_buildings()")
    entry("ShowCubes", "11. 숨긴 Building 큐브 다시 보기", "import fpsr_citylayout; fpsr_citylayout.show_cubes()")
    menus.refresh_all_widgets()
    unreal.log("[CityLayout] 메뉴 등록: Tools > FPSR CityLayout")
