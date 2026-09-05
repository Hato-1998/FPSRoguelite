# gen_voxel_chomper.py — 복셀 유령 적 "쩝쩝이(Chomper)" 절차 생성 (보드: 복셀 유령 적 메시 — 입 개폐 공격 구조)
#
# 완전 신규 데포르메 유령. 레퍼런스(팩맨 고스트 램프)는 모티브(유령·복셀·아케이드)만 가져왔다.
# 스켈레톤·VAT 없음 — 요소(Element) ID 를 UV.u 정수 타일로 인코딩(ElementId = floor(u)) 하고,
# 머티리얼(Custom HLSL WPO)이 ID 그룹별로 움직인다(gen_enemy_proto_meshes.py 와 같은 계약).
# 단일 머티리얼·단일 섹션(ADR 0007 다이나믹 인스턴싱 병합 자격) — usemtl 을 UE 용 OBJ 에 쓰지 않는다.
#
# ── 요소 ID 계약 (정본 = 이 주석) ──────────────────────────────────────────────
#   0  머리·상부 몸통          6  어두운 면 — 머리 소속(입천장·눈썹·닫힌 입선 홈 안벽)
#   1  턱 몸통(하부)           7  팔(좌우 ㄱ자 뭉툭 팔)
#   2  눈 흰자(표면 플러시)    8  치마(하단 3층, 아래 2층 파형 이빨)
#   3  동공                    9  혀
#   4  코어(약점 시각체·발광) 10  아랫니
#   5  윗니                   11  어두운 면 — 턱 소속(입 안쪽 보울 바닥·벽)
#   그룹: HEAD = {0,2,3,5,6,7}  /  JAW = {1,4,8,9,10,11}  — **ID 만으로 그룹이 정해진다**(머티리얼 WPO 가 그대로 씀).
#   어두운 면을 6/11 로 가른 이유: 보울 벽 윗변과 입천장이 같은 z(절단면)에 있어 정점 z 로는 못 가른다.
#   공격(Attack) = JAW 그룹을 -Z 로 JAW_DROP_CM 만큼 민다 → 입이 벌어져 이빨·혀·코어가 드러난다.
#   휴식 변위 0 = 닫힌 입(FPSRAnimCPDParams.h C0-at-entry 계약과 동일 — 상태 진입/이탈에 팝 없음).
#
# ── 입 구조(왜 이렇게 생겼나) ────────────────────────────────────────────────
#   절단면 Zc(바닥에서 5층)에서 턱과 머리가 맞닿는다. 평행이동 WPO 만으로 "입 속"이 보여야 하므로
#   입 속 디테일은 **턱 윗면**에 몰았다(플레이어 눈높이가 입보다 위 → 내려다보면 턱 윗면이 보인다):
#   · 턱 윗층에 1복셀 깊이 **보울**(어두움) → 그 안에 혀(붉음)·코어(발광) 가 놓인다.
#   · **아랫니**는 턱 테두리에서 1복셀 위로 솟아 머리 밑층의 구멍에 끼워지고, **윗니**는 머리 밑면에서
#     1복셀 아래로 내려와 턱 테두리의 구멍에 끼워진다(맞물림). 닫힘 = 완전 밀봉, 열림 = 위아래 이빨 노출.
#   · 정면 ±80° 에 1복셀 **홈**(닫힌 입선) — 닫힌 상태에서도 입이 있다는 게 읽히고 아랫니가 살짝 비친다(그린).
#
# 출력(Saved/EnemyVoxel/):
#   SM_EnemyVoxel_Chomper.obj            UE 임포트용. 그룹 경계면(턱 윗면·머리 밑면·이빨 옆면)을 **포함**
#                                        — 휴식 시엔 안 보이지만 WPO 로 턱이 내려가면 드러나야 한다.
#   SM_EnemyVoxel_Chomper_preview_closed.obj/.mtl   Blender 렌더 전용(요소별 색), 닫힘
#   SM_EnemyVoxel_Chomper_preview_open.obj/.mtl     Blender 렌더 전용, 턱 -JAW_DROP 적용
#
# ⚠️ UE OBJ 임포터는 Y 를 부호 반전한다(Troubleshooting D12). 이 메시는 좌우 대칭이라 무영향이지만, 비대칭 디테일을
#    넣는 순간 gen_rifle_hardsurface.py 의 to_file()(Y 선반전 + 삼각 인덱스 반전)을 이식해야 한다.
# 사용: python Scripts/gen_voxel_chomper.py [출력폴더=Saved/EnemyVoxel]  (전체 파이프라인 = Scripts/run_voxel_chomper_pipeline.bat)
# 단위 cm, +Z 상방, +X 정면. 임포트는 헤드리스 에디터(import_voxel_chomper.py) — 라이브 에디터 임포트 = 데드락.
import math, os, sys
from collections import deque

# ── 튜닝 노브 ─────────────────────────────────────────────────────────────────
VOXEL = 7.5            # cm. 격자 한 칸. 예산이 걱정되면 9~10 으로(형태는 그대로, 크기만 커진다 — 층수 고정이라).
R_BODY = 8.0           # 복셀 단위 몸통 반경(직경 16칸 = 120cm). 높이 22층 = 165cm — 폭보다 키가 커야 유령으로 읽힌다
SEAM_IZ = 6            # 절단면 = 이 층의 바닥. 아래 = JAW, 위 = HEAD
JAW_DROP_VOX = 5       # 공격 시 턱 하강(복셀). 37.5cm. 3복셀(24cm)은 렌더에서 "틈"으로만 보여 공격이 안 읽혔다(2026-09-05)
SKIRT_TEETH = 9        # 치마 파형 이빨 수(원주 방향)
GROOVE_HALF_DEG = 80   # 닫힌 입선 홈의 정면 반각

# 층별 반경(복셀). 0..2 치마(0,1 은 파형 링), 3..5 턱, 6..13 몸통, 14..19 돔, 20..21 정수리 술
DOME = {14: 7.6, 15: 7.0, 16: 6.2, 17: 5.0, 18: 3.5, 19: 1.8}
BODY_TOP_IZ = 19
TOP_IZ = 21
BOWL_R, TEETH_R, GROOVE_R, SKIRT_HOLE_R = 5.0, 6.5, 7.0, 6.0

E_HEAD, E_JAW, E_SCLERA, E_PUPIL, E_CORE, E_UTOOTH, E_DARK, E_ARM, E_SKIRT, E_TONGUE, E_LTOOTH, E_DARKJ = range(12)
N_ELEM = 12
G_HEAD, G_JAW = 0, 1

# 미리보기 색(Kd). UE 머티리얼 기본값 제안이기도 하다 — 확정은 사용자(ArtDirection A-0 재매핑).
PREVIEW_KD = {
    E_HEAD:   (0.98, 0.46, 0.76),
    E_JAW:    (0.92, 0.38, 0.68),
    E_SCLERA: (0.97, 0.97, 1.00),
    E_PUPIL:  (0.16, 0.04, 0.14),
    E_CORE:   (1.00, 0.12, 0.48),   # 발광(렌더 스크립트가 Emission 으로 올린다)
    E_UTOOTH: (1.00, 1.00, 0.94),
    E_DARK:   (0.17, 0.05, 0.15),
    E_ARM:    (0.95, 0.42, 0.72),
    E_SKIRT:  (0.86, 0.31, 0.63),
    E_TONGUE: (0.96, 0.16, 0.22),
    E_LTOOTH: (1.00, 1.00, 0.94),
    E_DARKJ:  (0.17, 0.05, 0.15),
}
ELEM_NAME = ["Head", "Jaw", "Sclera", "Pupil", "Core", "UpperTooth", "Dark", "Arm", "Skirt", "Tongue", "LowerTooth", "DarkJaw"]


# ── 격자 ─────────────────────────────────────────────────────────────────────
def cc(i):
    """셀 인덱스 → 셀 중심(복셀 단위). 짝수 직경이라 중심은 항상 ±0.5 계열."""
    return i + 0.5


def radius_at(iz):
    if iz in DOME:
        return DOME[iz]
    if iz <= BODY_TOP_IZ:
        return R_BODY
    return 0.0


def in_disc(ix, iy, r):
    return cc(ix) ** 2 + cc(iy) ** 2 <= r * r


def angle_deg(ix, iy):
    return math.degrees(math.atan2(cc(iy), cc(ix)))


def ring_cell_at(radius, deg):
    a = math.radians(deg)
    return (math.floor(radius * math.cos(a)), math.floor(radius * math.sin(a)))


def build_cells():
    """{(ix,iy,iz): (elem, group)} — 닫힌(휴식) 배치."""
    cells = {}
    bowl, groove = set(), set()   # 빈 셀 집합 — 이 칸을 향하는 몸통 면은 어두운 면(E_DARK)
    span = int(R_BODY) + 3

    def put(ix, iy, iz, elem, grp):
        cells[(ix, iy, iz)] = (elem, grp)

    # 1) 회전체 몸통
    for iz in range(0, BODY_TOP_IZ + 1):
        r = radius_at(iz)
        for ix in range(-span, span):
            for iy in range(-span, span):
                if not in_disc(ix, iy, r):
                    continue
                if iz <= 2:
                    # 치마: 0,1 층은 바깥 링(r>7)만 + 원주 파형(위로 갈수록 넓어지는 이빨), 2층은 꽉 찬 바닥
                    if iz <= 1:
                        if in_disc(ix, iy, SKIRT_HOLE_R):
                            continue
                        t = ((angle_deg(ix, iy) / 360.0) % 1.0) * SKIRT_TEETH % 1.0
                        keep = t < (0.75 if iz == 1 else 0.45)
                        if not keep:
                            continue
                    put(ix, iy, iz, E_SKIRT, G_JAW)
                elif iz < SEAM_IZ:
                    put(ix, iy, iz, E_JAW, G_JAW)
                else:
                    put(ix, iy, iz, E_HEAD, G_HEAD)

    # 2) 정수리 술(불꽃)
    for ix in (-1, 0):
        for iy in (-1, 0):
            put(ix, iy, 20, E_HEAD, G_HEAD)
    put(0, 0, 21, E_HEAD, G_HEAD)
    put(-1, 0, 21, E_HEAD, G_HEAD)

    # 3) 턱 윗층 보울(1복셀 깊이, 앞쪽으로 치우침) → 혀·코어
    jaw_top = SEAM_IZ - 1
    for ix in range(-span, span):
        for iy in range(-span, span):
            if in_disc(ix, iy, BOWL_R) and ix >= -3 and (ix, iy, jaw_top) in cells:
                del cells[(ix, iy, jaw_top)]
                bowl.add((ix, iy, jaw_top))
    tongue = [(ix, iy) for ix in range(-1, 4) for iy in (-1, 0)] + [(ix, iy) for ix in range(0, 3) for iy in (-2, 1)]
    for (ix, iy) in tongue:
        put(ix, iy, jaw_top, E_TONGUE, G_JAW)
    for ix in (-3, -2):
        for iy in (-1, 0):
            put(ix, iy, jaw_top, E_CORE, G_JAW)

    # 4) 이빨(맞물림). 아랫니 = 턱 소속, 머리 밑층(SEAM_IZ)의 구멍에 끼움 / 윗니 = 머리 소속, 턱 윗층 구멍에 끼움
    for deg in (0, 40, -40, 80, -80, 120, -120):
        ix, iy = ring_cell_at(TEETH_R, deg)
        put(ix, iy, SEAM_IZ, E_LTOOTH, G_JAW)
    for deg in (20, -20, 60, -60, 100, -100):
        ix, iy = ring_cell_at(TEETH_R, deg)
        put(ix, iy, jaw_top, E_UTOOTH, G_HEAD)

    # 5) 닫힌 입선 홈: 머리 밑층 바깥 껍질(r>8) 정면 ±GROOVE_HALF_DEG 제거
    for (ix, iy, iz) in [k for k in cells if k[2] == SEAM_IZ]:
        if cells[(ix, iy, iz)][0] == E_HEAD and not in_disc(ix, iy, GROOVE_R) and abs(angle_deg(ix, iy)) <= GROOVE_HALF_DEG:
            del cells[(ix, iy, iz)]
            groove.add((ix, iy, iz))

    # 6) 얼굴: 눈 흰자·동공·눈썹 — 정면(+X) 표면 셀을 요소만 바꿔 **플러시**로 그린다
    #    (돌출시켜 봤더니 곡면 계단 때문에 흰자 옆면이 조각나 보였다 — 2026-09-05 렌더 판정)
    def front_ix(iy, iz):
        best = None
        for ix in range(-span, span):
            if (ix, iy, iz) in cells and cells[(ix, iy, iz)][0] == E_HEAD:
                best = ix
        return best

    def eye(cols_inner_to_outer):
        c = cols_inner_to_outer  # 안쪽→바깥쪽 4칸. 눈 4×5, 바깥 위 모서리 깎아 화난 눈
        mask = {10: c[:], 11: c[:], 12: c[:], 13: c[:3], 14: c[:2]}
        pupils = {(iz, iy) for iz in (10, 11) for iy in c[:2]}
        brows = [(14, iy) for iy in c[2:]] + [(15, iy) for iy in c[:2]]
        for iz, cols in mask.items():
            for iy in cols:
                fx = front_ix(iy, iz)
                if fx is not None:
                    put(fx, iy, iz, E_PUPIL if (iz, iy) in pupils else E_SCLERA, G_HEAD)
        for iz, iy in brows:
            fx = front_ix(iy, iz)
            if fx is not None:
                put(fx, iy, iz, E_DARK, G_HEAD)

    eye([1, 2, 3, 4])             # 오른눈(+Y)
    eye([-2, -3, -4, -5])         # 왼눈(-Y)

    # 7) 팔: 옆구리에서 나와 앞으로 뻗은 ㄱ자 뭉툭 팔(덤벼드는 자세). 좌우 대칭 = iy → -1-iy
    def mirror(iy):
        return -1 - iy
    for side in (1, -1):
        m = (lambda iy: iy) if side > 0 else mirror
        for iz in (8, 9):
            for ix in (0, 1):
                for iy in (8, 9):          # 위팔(옆으로)
                    put(ix, m(iy), iz, E_ARM, G_HEAD)
            for ix in (2, 3):
                for iy in (9, 10):         # 앞팔(앞으로)
                    put(ix, m(iy), iz, E_ARM, G_HEAD)
        for ix in (4,):
            for iy in (9, 10):             # 손끝 한 칸(아래로 살짝)
                put(ix, m(iy), 8, E_ARM, G_HEAD)

    return cells, bowl | groove


# ── 면 추출 ─────────────────────────────────────────────────────────────────
DIRS = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]


def shifted(cells, dark, jaw_dz):
    out = {}
    for (ix, iy, iz), (e, g) in cells.items():
        out[(ix, iy, iz + (jaw_dz if g == G_JAW else 0))] = (e, g)
    # 어두운 빈칸: 보울(턱 소속, 함께 내려감) / 홈(머리 소속, 고정)
    dark_o = {(x, y, z + (jaw_dz if z == SEAM_IZ - 1 else 0)) for (x, y, z) in dark}
    return out, dark_o


def outside_reachable(cells):
    """바깥 공기와 이어진 빈 셀 집합(플러드필). 밀봉 검사용."""
    xs = [k[0] for k in cells]; ys = [k[1] for k in cells]; zs = [k[2] for k in cells]
    lo = (min(xs) - 1, min(ys) - 1, min(zs) - 1)
    hi = (max(xs) + 1, max(ys) + 1, max(zs) + 1)
    start = lo
    seen = {start}
    q = deque([start])
    while q:
        c = q.popleft()
        for d in DIRS:
            n = (c[0] + d[0], c[1] + d[1], c[2] + d[2])
            if not (lo[0] <= n[0] <= hi[0] and lo[1] <= n[1] <= hi[1] and lo[2] <= n[2] <= hi[2]):
                continue
            if n in seen or n in cells:
                continue
            seen.add(n)
            q.append(n)
    return seen


def extract_faces(cells, dark_cells, group_aware):
    """[(cell, dir, elem)] — 이웃이 비었으면 면을 낸다.
    group_aware=True: 이웃이 **다른 그룹**이면 채워져 있어도 면을 낸다(UE 메시 — 턱이 내려가면 드러나는 면)."""
    faces = []
    for (ix, iy, iz), (e, g) in cells.items():
        for d in DIRS:
            n = (ix + d[0], iy + d[1], iz + d[2])
            nb = cells.get(n)
            if nb is not None and not (group_aware and nb[1] != g):
                continue
            elem = e
            # 어두운 면 덮어쓰기(몸통 요소만): 보울/홈 빈칸을 향한 면, 머리 밑면(입천장)
            if e in (E_HEAD, E_JAW, E_SKIRT):
                if n in dark_cells or (e == E_HEAD and d == (0, 0, -1) and iz == SEAM_IZ):
                    elem = E_DARK if g == G_HEAD else E_DARKJ
            faces.append(((ix, iy, iz), d, elem, g))
    return faces


def quad_corners(cell, d, jaw_dz_cm=0.0, grp=G_HEAD):
    ix, iy, iz = cell
    V = VOXEL
    x0, y0, z0 = ix * V, iy * V, iz * V - (TOP_IZ + 1) * V / 2.0
    x1, y1, z1 = x0 + V, y0 + V, z0 + V
    if grp == G_JAW:
        z0 += jaw_dz_cm; z1 += jaw_dz_cm
    if d == (1, 0, 0):   return [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)]
    if d == (-1, 0, 0):  return [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)]
    if d == (0, 1, 0):   return [(x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0)]
    if d == (0, -1, 0):  return [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)]
    if d == (0, 0, 1):   return [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    return [(x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (x1, y0, z0)]


def check_winding(corners, d):
    (ax, ay, az), (bx, by, bz), (cx, cy, cz) = corners[0], corners[1], corners[2]
    ux, uy, uz = bx - ax, by - ay, bz - az
    wx, wy, wz = cx - ax, cy - ay, cz - az
    n = (uy * wz - uz * wy, uz * wx - ux * wz, ux * wy - uy * wx)
    return n[0] * d[0] + n[1] * d[1] + n[2] * d[2] > 0


def write_obj(path, faces, name, jaw_dz_cm=0.0, with_mtl=False):
    lines = [f"# FPSR voxel enemy mesh: {name} (generated by gen_voxel_chomper.py)",
             f"# ElementId = floor(UV.u). SEAM_Z_CM={SEAM_IZ * VOXEL - (TOP_IZ + 1) * VOXEL / 2.0:.2f} JAW_DROP_CM={JAW_DROP_VOX * VOXEL:.1f}"]
    if with_mtl:
        lines.append(f"mtllib {os.path.basename(path)[:-4]}.mtl")
    lines.append(f"o {name}")
    v, vt, vn, f = [], [], [], []
    cur_mtl = None
    faces_sorted = sorted(faces, key=lambda t: t[2]) if with_mtl else faces
    for (cell, d, elem, grp) in faces_sorted:
        corners = quad_corners(cell, d, jaw_dz_cm, grp)
        assert check_winding(corners, d), (cell, d)
        bv, bt, bn = len(v), len(vt), len(vn)
        v.extend(corners)
        u0, u1 = elem + 0.01, elem + 0.99
        vt.extend([(u0, 0.01), (u1, 0.01), (u1, 0.99), (u0, 0.99)])
        vn.append(d)
        if with_mtl and cur_mtl != elem:
            cur_mtl = elem
            f.append(f"usemtl E{elem}_{ELEM_NAME[elem]}")
        f.append("f " + " ".join(f"{bv + i + 1}/{bt + i + 1}/{bn + 1}" for i in (0, 1, 2)))
        f.append("f " + " ".join(f"{bv + i + 1}/{bt + i + 1}/{bn + 1}" for i in (0, 2, 3)))
    with open(path, "w", encoding="ascii") as fh:
        fh.write("\n".join(lines) + "\n")
        for p in v:
            fh.write(f"v {p[0]:.3f} {p[1]:.3f} {p[2]:.3f}\n")
        for t in vt:
            fh.write(f"vt {t[0]:.4f} {t[1]:.4f}\n")
        for n in vn:
            fh.write(f"vn {n[0]} {n[1]} {n[2]}\n")
        fh.write("\n".join(f) + "\n")
    if with_mtl:
        with open(path[:-4] + ".mtl", "w", encoding="ascii") as mh:
            for elem, kd in PREVIEW_KD.items():
                mh.write(f"newmtl E{elem}_{ELEM_NAME[elem]}\nKd {kd[0]:.3f} {kd[1]:.3f} {kd[2]:.3f}\n")
                if elem == E_CORE:
                    mh.write("Ke 8.0 0.9 3.8\n")
                mh.write("\n")
    xs = [p[0] for p in v]; ys = [p[1] for p in v]; zs = [p[2] for p in v]
    print(f"[gen] {path}  quads={len(faces)} tris={len(faces) * 2}  "
          f"bounds X[{min(xs):.0f},{max(xs):.0f}] Y[{min(ys):.0f},{max(ys):.0f}] Z[{min(zs):.0f},{max(zs):.0f}]")


def report_elements(tag, faces):
    counts = {}
    for (_, _, e, _) in faces:
        counts[e] = counts.get(e, 0) + 1
    print(f"[gen] {tag} faces by element: " + ", ".join(f"{ELEM_NAME[e]}={counts.get(e, 0)}" for e in range(N_ELEM)))
    return counts


def main(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    cells, dark = build_cells()
    print(f"[gen] cells={len(cells)}  head={sum(1 for c in cells.values() if c[1] == G_HEAD)} jaw={sum(1 for c in cells.values() if c[1] == G_JAW)}")

    # UE 메시: 휴식 위치 + 그룹 경계면 포함
    ue_faces = extract_faces(cells, dark, group_aware=True)
    write_obj(os.path.join(out_dir, "SM_EnemyVoxel_Chomper.obj"), ue_faces, "SM_EnemyVoxel_Chomper")
    report_elements("UE", ue_faces)

    # 닫힘 미리보기: 바깥 공기에 닿는 면만(밀봉 검사 겸)
    reach = outside_reachable(cells)
    closed_all = extract_faces(cells, dark, group_aware=False)
    closed_vis = [fc for fc in closed_all if (fc[0][0] + fc[1][0], fc[0][1] + fc[1][1], fc[0][2] + fc[1][2]) in reach]
    write_obj(os.path.join(out_dir, "SM_EnemyVoxel_Chomper_preview_closed.obj"), closed_vis, "Chomper_closed", with_mtl=True)
    c = report_elements("closed(visible)", closed_vis)
    for e in (E_CORE, E_UTOOTH, E_TONGUE):
        assert c.get(e, 0) == 0, f"닫힘 상태에서 {ELEM_NAME[e]} 가 보인다({c.get(e)}면) — 입이 밀봉되지 않았다"
    print(f"[gen] 닫힘 밀봉 OK (코어/윗니/혀 노출 0, 아랫니 홈 비침={c.get(E_LTOOTH, 0)}면)")

    # 열림 미리보기: 턱 그룹 -JAW_DROP
    opened, dark_o = shifted(cells, dark, -JAW_DROP_VOX)
    reach_o = outside_reachable(opened)
    open_all = extract_faces(opened, dark_o, group_aware=False)
    open_vis = [fc for fc in open_all if (fc[0][0] + fc[1][0], fc[0][1] + fc[1][1], fc[0][2] + fc[1][2]) in reach_o]
    write_obj(os.path.join(out_dir, "SM_EnemyVoxel_Chomper_preview_open.obj"), open_vis, "Chomper_open", with_mtl=True)
    co = report_elements("open(visible)", open_vis)
    for e in (E_CORE, E_UTOOTH, E_TONGUE, E_LTOOTH):
        assert co.get(e, 0) > 0, f"열림 상태에서 {ELEM_NAME[e]} 가 안 보인다"
    print(f"[gen] 열림 노출 OK  (SEAM_Z_CM={SEAM_IZ * VOXEL - (TOP_IZ + 1) * VOXEL / 2.0:.1f}, JAW_DROP_CM={JAW_DROP_VOX * VOXEL:.1f})")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join("Saved", "EnemyVoxel"))
