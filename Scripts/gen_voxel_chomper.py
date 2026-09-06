# gen_voxel_chomper.py — 복셀 유령 적 "쩝쩝이(Chomper)" 절차 생성 (ADR 0016 D7 ① 경로의 기준 구현)
#
# 2026-09-06 재설계: 컨셉 시트(Scripts/gen_concept_sheet_arcade_pixel.mjs 의 CHOMPER_CLOSED / CHOMPER_OPEN 16×18)를
# **단일 소스**로 삼아 3D 로 올린다. 실루엣 = 스프라이트 행 반폭을 반경으로 한 회전체, 얼굴 = 정면(+X) 표면에 스프라이트
# 열을 투영, 밑단 = 스프라이트 하단 2행의 홈 패턴을 원주 방향으로 반복. 1차 디자인(팔·정수리 술·눈썹·입선 홈·이빨 13개)은
# 사용자 판정 "너무 디테일하다"(2026-09-06)로 폐기 — ArtDirection §B-5 "덩어리가 먼저, 디테일은 복셀 1칸 단위로만".
#
# 스켈레톤·VAT 없음 — 요소(Element) ID 를 UV.u 정수 타일로 인코딩(ElementId = floor(u)) 하고 머티리얼(Custom HLSL WPO)이
# ID 그룹별로 움직인다. 단일 머티리얼·단일 섹션(ADR 0007/0016 I3). 텍스처 없음(0016 I7) — 색 = 요소ID LUT.
#
# ── 요소 ID 계약 (정본 = 이 주석, M_FPSREnemyVoxelChomper 의 LUT 순서와 같다) ─────────────────
#   0  머리·상부 몸통(H)        6  어두운 면 — 머리 소속(입천장)
#   1  턱 몸통·밑단(J)          7  (예약 — 1차 디자인의 팔. 미사용)
#   2  눈 흰자(W)               8  입선 띠(S) — 닫힌 입 = 턱 윗층 한 줄, 턱 소속
#   3  동공(P)                  9  혀(R)
#   4  코어(C, 텔레그래프 색)  10  아랫니(T, 턱 소속)
#   5  윗니(T, 머리 소속)      11  어두운 면 — 턱 소속(입 안 보울 바닥·벽)
#   그룹: HEAD = {0,2,3,5,6}  /  JAW = {1,4,8,9,10,11}  — ID 만으로 그룹이 정해진다(머티리얼 WPO 가 그대로 씀).
#   공격(Attack) = JAW 그룹을 -Z 로 JAW_DROP_CM 만큼 민다 → 윗니·아랫니·혀·코어가 드러난다(열림 스프라이트의 4행).
#   휴식 변위 0 = 닫힌 입(FPSRAnimCPDParams.h C0-at-entry 계약 — 상태 진입/이탈에 팝 없음).
#
# ── 입 구조 ──────────────────────────────────────────────────────────────────
#   턱 윗층(입선 띠 S) 안쪽에 1복셀 보울을 파고 혀·코어를 놓는다. 아랫니는 턱 테두리에서 1칸 위로 솟아 머리 밑층 구멍에,
#   윗니는 머리 밑면에서 1칸 아래로 내려와 입선 띠의 구멍에 끼워진다(맞물림) → 닫힘 = 완전 밀봉(플러드필 단언).
#   UE 메시는 그룹 경계면(턱 윗면·입천장·이빨 옆면)을 **포함**한다 — 휴식 시엔 안 보이지만 WPO 로 벌어지면 드러나야 한다.
#
# ⚠️ UE OBJ 임포터는 Y 를 부호 반전한다(Troubleshooting D12). 이 메시는 좌우 대칭이라 무영향. 비대칭 디테일을 넣는 순간
#    gen_rifle_hardsurface.py 의 to_file()(Y 선반전 + 삼각 인덱스 반전)을 이식할 것.
#
# 출력(Saved/EnemyVoxel/): SM_EnemyVoxel_Chomper.obj(UE) · *_preview_closed/open.obj+.mtl(Blender 렌더 전용, usemtl 금지 이유 = 다중 섹션)
# 사용: python Scripts/gen_voxel_chomper.py [출력폴더=Saved/EnemyVoxel]  (전체 파이프라인 = Scripts/run_voxel_chomper_pipeline.bat)
import math, os, sys
from collections import deque

# ── 튜닝 노브 ─────────────────────────────────────────────────────────────────
VOXEL = 7.5            # cm. ArtDirection §B-3 적 격자. 스프라이트 18행 → 135cm. (라인업 표기 "165·22층"은 1차 디자인 값 — 시트 스프라이트가 정본)
JAW_DROP_VOX = 4       # 공격 시 턱 하강(복셀) = 열림 스프라이트의 입 안 4행. 30cm
SKIRT_TEETH = 9        # 밑단 홈 반복 수(원주 방향). 정면 16칸에 홈 3개 = 스프라이트와 같은 밀도
BOWL_R, HOLLOW_R = 5.5, 6.0

# 컨셉 시트 스프라이트(정본 = gen_concept_sheet_arcade_pixel.mjs). 위가 정수리. H 머리 J 턱 S 입선/음영 W 흰자 P 동공.
CHOMPER_CLOSED = """
......HHHH......
....HHHHHHHH....
...HHHHHHHHHH...
..HHHHHHHHHHHH..
.HHHHHHHHHHHHHH.
.HHWWWHHHHWWWHH.
HHHWPWHHHHWPWHHH
HHHWWWHHHHWWWHHH
HHHHHHHHHHHHHHHS
HHHHHHHHHHHHHHHS
HHHHHHHHHHHHHHHS
SSSSSSSSSSSSSSSS
JJJJJJJJJJJJJJJS
JJJJJJJJJJJJJJJS
JJJJJJJJJJJJJJJS
JJJJJJJJJJJJJJJS
JJ.JJJJ..JJJJ.JS
J...JJJ..JJJ...S"""
SEAM_ROW = 11          # 'S' 한 줄 = 닫힌 입선. 이 행부터 아래가 JAW
TOOTH_COLS = (1, 5, 10, 14)   # 열림 스프라이트 이빨 열(윗니 = 행 10, 아랫니 = 행 13)

E_HEAD, E_JAW, E_SCLERA, E_PUPIL, E_CORE, E_UTOOTH, E_DARK, E_ARM, E_SEAM, E_TONGUE, E_LTOOTH, E_DARKJ = range(12)
N_ELEM = 12
G_HEAD, G_JAW = 0, 1


def hexc(h):
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (1, 3, 5))


# 컨셉 시트 팔레트(LEG_CH / P.*). UE 머티리얼 LUT 기본값과 동일 — 확정은 사용자(MI).
PREVIEW_KD = {
    E_HEAD: hexc("#9E4560"), E_JAW: hexc("#8A3A52"), E_SCLERA: hexc("#E8E4F0"), E_PUPIL: hexc("#1A1024"),
    E_CORE: hexc("#FF6B2C"), E_UTOOTH: hexc("#EADFE8"), E_DARK: hexc("#2A0E18"), E_ARM: hexc("#9E4560"),
    E_SEAM: hexc("#6E2E44"), E_TONGUE: hexc("#C8324A"), E_LTOOTH: hexc("#EADFE8"), E_DARKJ: hexc("#2A0E18"),
}
ELEM_NAME = ["Head", "Jaw", "Sclera", "Pupil", "Core", "UpperTooth", "Dark", "Arm", "Seam", "Tongue", "LowerTooth", "DarkJaw"]


# ── 스프라이트 → 격자 ────────────────────────────────────────────────────────
def sprite_rows():
    rows = CHOMPER_CLOSED.strip("\n").split("\n")
    w = max(len(r) for r in rows)
    return [r.ljust(w, ".") for r in rows], w


ROWS, SPR_W = sprite_rows()
N_LAYERS = len(ROWS)                       # 18
SEAM_IZ = N_LAYERS - 1 - SEAM_ROW          # 스프라이트 행 → 층(iz, 바닥 0). 입선 행 = 턱 최상층
HEAD_IZ = SEAM_IZ + 1                      # 머리 최하층
TOP_IZ = N_LAYERS - 1


def row_of(iz):
    return ROWS[N_LAYERS - 1 - iz]


def cc(i):
    return i + 0.5


def in_disc(ix, iy, r):
    return cc(ix) ** 2 + cc(iy) ** 2 <= r * r


def angle_deg(ix, iy):
    return math.degrees(math.atan2(cc(iy), cc(ix)))


def row_radius(row):
    filled = [i for i, ch in enumerate(row) if ch != "."]
    return (max(filled) - min(filled) + 1) / 2.0


def col_to_iy(c):
    return c - SPR_W // 2          # 열 0..15 → iy -8..7 (스프라이트 좌우 = 3D ±Y)


def ring_cell_at(radius, deg):
    a = math.radians(deg)
    return (math.floor(radius * math.cos(a)), math.floor(radius * math.sin(a)))


def build_cells():
    cells, bowl = {}, set()
    span = SPR_W // 2 + 2

    def put(ix, iy, iz, e, g):
        cells[(ix, iy, iz)] = (e, g)

    # 1) 회전체: 행 반폭 = 반경. 밑단 2행은 바깥 링만 + 원주 홈 패턴(스프라이트 홈 폭 2/3 → 9회 반복)
    for iz in range(N_LAYERS):
        row = row_of(iz)
        r = row_radius(row)
        grp = G_JAW if iz <= SEAM_IZ else G_HEAD
        base = E_SEAM if iz == SEAM_IZ else (E_JAW if grp == G_JAW else E_HEAD)
        hem = iz <= 1
        for ix in range(-span, span):
            for iy in range(-span, span):
                if not in_disc(ix, iy, r):
                    continue
                if hem:
                    if in_disc(ix, iy, HOLLOW_R):
                        continue
                    t = ((angle_deg(ix, iy) / 360.0) % 1.0) * SKIRT_TEETH % 1.0
                    if t >= (0.7 if iz == 1 else 0.45):
                        continue
                put(ix, iy, iz, base, grp)

    # 2) 얼굴: 정면(+X) 표면 셀을 스프라이트 열/행대로 요소만 바꾼다(플러시)
    def front_ix(iy, iz):
        best = None
        for ix in range(-span, span):
            if (ix, iy, iz) in cells and cells[(ix, iy, iz)][0] == E_HEAD:
                best = ix
        return best
    for iz in range(HEAD_IZ, N_LAYERS):
        row = row_of(iz)
        for c, ch in enumerate(row):
            if ch in "WP":
                fx = front_ix(col_to_iy(c), iz)
                if fx is not None:
                    put(fx, col_to_iy(c), iz, E_SCLERA if ch == "W" else E_PUPIL, G_HEAD)

    # 3) 입 안: 입선 띠 안쪽 보울(1칸) + 혀(앞) + 코어(뒤 중앙 2×2, 열림 스프라이트의 C)
    for ix in range(-span, span):
        for iy in range(-span, span):
            if in_disc(ix, iy, BOWL_R) and ix >= -3 and (ix, iy, SEAM_IZ) in cells:
                del cells[(ix, iy, SEAM_IZ)]
                bowl.add((ix, iy, SEAM_IZ))
    for ix in range(1, 5):
        for iy in range(-3, 3):
            if (ix, iy, SEAM_IZ) in bowl:
                put(ix, iy, SEAM_IZ, E_TONGUE, G_JAW)
    for ix in (-2, -1):
        for iy in (-1, 0):
            put(ix, iy, SEAM_IZ, E_CORE, G_JAW)

    # 4) 이빨 맞물림: 아랫니(턱) = 머리 밑층 구멍에 / 윗니(머리) = 입선 띠 구멍에. 정면 4개씩(스프라이트 열 1·5·10·14)
    r_teeth = row_radius(row_of(SEAM_IZ)) - 1.5
    for c in TOOTH_COLS:
        deg = math.degrees(math.asin(max(-1.0, min(1.0, cc(col_to_iy(c)) / r_teeth))))
        ix, iy = ring_cell_at(r_teeth, deg)
        put(ix, iy, HEAD_IZ, E_LTOOTH, G_JAW)
        ix2, iy2 = ring_cell_at(r_teeth, deg + (9 if deg >= 0 else -9))   # 윗니는 옆으로 반 칸 어긋나게
        put(ix2, iy2, SEAM_IZ, E_UTOOTH, G_HEAD)
    return cells, bowl


# ── 면 추출 ─────────────────────────────────────────────────────────────────
DIRS = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]


def shifted(cells, dark, jaw_dz):
    out = {(ix, iy, iz + (jaw_dz if g == G_JAW else 0)): (e, g) for (ix, iy, iz), (e, g) in cells.items()}
    return out, {(x, y, z + jaw_dz) for (x, y, z) in dark}


def outside_reachable(cells):
    xs = [k[0] for k in cells]; ys = [k[1] for k in cells]; zs = [k[2] for k in cells]
    lo = (min(xs) - 1, min(ys) - 1, min(zs) - 1)
    hi = (max(xs) + 1, max(ys) + 1, max(zs) + 1)
    seen, q = {lo}, deque([lo])
    while q:
        c = q.popleft()
        for d in DIRS:
            n = (c[0] + d[0], c[1] + d[1], c[2] + d[2])
            if not (lo[0] <= n[0] <= hi[0] and lo[1] <= n[1] <= hi[1] and lo[2] <= n[2] <= hi[2]):
                continue
            if n in seen or n in cells:
                continue
            seen.add(n); q.append(n)
    return seen


def extract_faces(cells, dark_cells, group_aware):
    """[(cell, dir, elem, group)] — 이웃이 비었으면 면. group_aware: 이웃이 다른 그룹이면 채워져 있어도 면(UE 메시)."""
    faces = []
    for (ix, iy, iz), (e, g) in cells.items():
        for d in DIRS:
            n = (ix + d[0], iy + d[1], iz + d[2])
            nb = cells.get(n)
            if nb is not None and not (group_aware and nb[1] != g):
                continue
            elem = e
            if e in (E_HEAD, E_JAW, E_SEAM):
                if n in dark_cells or (e == E_HEAD and d == (0, 0, -1) and iz == HEAD_IZ):
                    elem = E_DARK if g == G_HEAD else E_DARKJ
            faces.append(((ix, iy, iz), d, elem, g))
    return faces


def quad_corners(cell, d, jaw_dz_cm=0.0, grp=G_HEAD):
    ix, iy, iz = cell
    V = VOXEL
    x0, y0, z0 = ix * V, iy * V, iz * V - N_LAYERS * V / 2.0
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


def seam_z_cm():
    return HEAD_IZ * VOXEL - N_LAYERS * VOXEL / 2.0


def write_obj(path, faces, name, jaw_dz_cm=0.0, with_mtl=False):
    lines = [f"# FPSR voxel enemy mesh: {name} (generated by gen_voxel_chomper.py)",
             f"# ElementId = floor(UV.u). SEAM_Z_CM={seam_z_cm():.2f} JAW_DROP_CM={JAW_DROP_VOX * VOXEL:.1f}"]
    if with_mtl:
        lines.append(f"mtllib {os.path.basename(path)[:-4]}.mtl")
    lines.append(f"o {name}")
    v, vt, vn, f = [], [], [], []
    cur = None
    for (cell, d, elem, grp) in (sorted(faces, key=lambda t: t[2]) if with_mtl else faces):
        corners = quad_corners(cell, d, jaw_dz_cm, grp)
        assert check_winding(corners, d), (cell, d)
        bv, bt, bn = len(v), len(vt), len(vn)
        v.extend(corners)
        u0, u1 = elem + 0.01, elem + 0.99
        vt.extend([(u0, 0.01), (u1, 0.01), (u1, 0.99), (u0, 0.99)])
        vn.append(d)
        if with_mtl and cur != elem:
            cur = elem
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
                    mh.write("Ke 8.0 2.6 1.0\n")
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
    print(f"[gen] sprite {SPR_W}x{N_LAYERS} -> cells={len(cells)} head={sum(1 for c in cells.values() if c[1] == G_HEAD)} "
          f"jaw={sum(1 for c in cells.values() if c[1] == G_JAW)}  height={N_LAYERS * VOXEL:.1f}cm width={2 * row_radius(row_of(SEAM_IZ)) * VOXEL:.0f}cm")

    ue_faces = extract_faces(cells, dark, group_aware=True)
    write_obj(os.path.join(out_dir, "SM_EnemyVoxel_Chomper.obj"), ue_faces, "SM_EnemyVoxel_Chomper")
    report_elements("UE", ue_faces)

    reach = outside_reachable(cells)
    closed_vis = [fc for fc in extract_faces(cells, dark, group_aware=False)
                  if (fc[0][0] + fc[1][0], fc[0][1] + fc[1][1], fc[0][2] + fc[1][2]) in reach]
    write_obj(os.path.join(out_dir, "SM_EnemyVoxel_Chomper_preview_closed.obj"), closed_vis, "Chomper_closed", with_mtl=True)
    c = report_elements("closed(visible)", closed_vis)
    for e in (E_CORE, E_UTOOTH, E_TONGUE, E_LTOOTH):
        assert c.get(e, 0) == 0, f"closed: {ELEM_NAME[e]} visible ({c.get(e)} faces) - mouth not sealed"
    print("[gen] closed seal OK (core/teeth/tongue exposed = 0)")

    opened, dark_o = shifted(cells, dark, -JAW_DROP_VOX)
    reach_o = outside_reachable(opened)
    open_vis = [fc for fc in extract_faces(opened, dark_o, group_aware=False)
                if (fc[0][0] + fc[1][0], fc[0][1] + fc[1][1], fc[0][2] + fc[1][2]) in reach_o]
    write_obj(os.path.join(out_dir, "SM_EnemyVoxel_Chomper_preview_open.obj"), open_vis, "Chomper_open", with_mtl=True)
    co = report_elements("open(visible)", open_vis)
    for e in (E_CORE, E_UTOOTH, E_TONGUE, E_LTOOTH):
        assert co.get(e, 0) > 0, f"open: {ELEM_NAME[e]} not visible"
    print(f"[gen] open exposure OK  (SEAM_Z_CM={seam_z_cm():.1f}, JAW_DROP_CM={JAW_DROP_VOX * VOXEL:.1f})")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join("Saved", "EnemyVoxel"))
