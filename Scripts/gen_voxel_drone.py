# gen_voxel_drone.py — 복셀 드론 적 절차 생성 (쩝쩝이 대체 · Docs/DroneEnemy_ResumePrompt.md §3·§4)
#
# 키아트(Docs/Concept/ArcadePixel_KeyArt_UserRef_2026-09-06_v2_HUD.png, 색 정답 = v3a 의 빨간 라이트 드론)의 쿼드 드론을
# ArtDirection §A 색 규칙으로 **번역**한 복셀 메시. 쩝쩝이(gen_voxel_chomper.py) 파이프라인을 복제 — 면 추출·OBJ·요소ID·
# 플러드필 단언은 같고, **메시·요소ID 계약·WPO 동작**만 다르다.
#
# 단일 소스: 이 파일의 층(layer) 기술이 정본이다. 2D 3뷰 스프라이트(DRONE_TOP/FRONT/SIDE)는 여기서 **파생**해
# Scripts/gen_concept_sheet_arcade_pixel.mjs 에 붙여 넣는다(--sprites 로 출력) — 3D 드론은 2D 맵 3장으로 유일하게 복원되지 않아
# 실행 문서 §3 의 "mjs 가 정본" 방향을 뒤집었다(불일치를 만들지 않는 유일한 길).
#
# ── 요소 ID 계약 (정본 = 이 주석, M_FPSREnemyVoxelDrone 의 LUT 순서와 같다) ──────────────────
#   0  몸통 윗면(+Z 면)            BODY   #3A2748     6  로터 날개 ×4              ROTOR  #B34A70  ← WPO 회전 (몸통과 확실히 갈리는 밝은 자주)
#   1  몸통 옆·밑면(어두운 면)     BODY   #2A1E36     7  밑면 라이트 ×4(이미시브)  LIGHT  #FF3B4E
#   2  코어 프레임(눈 테두리)      BODY   #1A1024     8  꼬리 핀                   BODY   #6E2E44
#   3  코어(이미시브) = **약점**   CORE   #FF6B2C     9~11 예비(엘리트 장식·2번 코어)   (§B-5 읽힘점 = 텔레그래프 색)
#   4  팔 ×4                       BODY   #4A2E58
#   5  로터 허브 ×4                HUB    #2A1E36
#   요소 0/1 은 같은 셀의 **면 방향**으로 갈린다(+Z 면 = 0, 나머지 = 1) — 셀 요소가 아니라 면 요소.
#   ROTOR(6) 정점의 UV = (6 + fx, k + fy): 타일 정수부 = 요소 6 / 허브 번호 k(0..3), **소수부 = 허브 중심으로부터의 오프셋**
#   fx = 0.5 + dx/ROTOR_UV_SPAN, fy = 0.5 + dy/ROTOR_UV_SPAN (dx,dy 는 cm, SPAN = 60cm → 소수부 [0.1,0.9]).
#   머티리얼은 위치 변환 없이 d = (frac(UV)-0.5)·SPAN 으로 오프셋을 얻어 R(θ)·d − d 를 로컬 오프셋으로 낸다(θ = Time·Rate·2π + k·90°).
#   2026-09-07 실사고: WorldPos→Local 변환으로 오프셋을 구하던 1차 버전은 정점별 로컬 위치가 상수로 들어와 십자 4개가 통째로
#   드론 중심을 도는 모양이 났다(사용자 관찰 "전체가 제자리에서 도는데 로터는 안 돈다"). 위치를 안 쓰는 지금 방식이 정답.
#   **정점 위치로 그룹을 가르지 않는다** — 그룹·오프셋 전부 UV 에서 나온다.
#   휴식 변위 = 회전각 0 일 때 0 (C0-at-entry) — 회전은 연속이라 진입 팝이 없다.
#
# 격자 7.5cm(ArtDirection §B-3 적 클래스). 발자국 16×16 칸 = 120cm(로터 포함), 높이 6층 = 45cm. 몸통 8×8×5 · 허브 3×3 · 로터 십자 5×5. 원점 = 발자국 중심·높이 중심.
# 층(z, 바닥 0): 0~4 몸통(라이트는 허브 밑 1층) · 2~4 허브 · 2~3 팔 · 5 로터 날개. 로터는 몸통·허브보다 한 층 위라 어떤 회전각에서도 같은 층의
# 다른 셀과 겹치지 않는다(단언이 0°·45°·90° 에서 검사).
# ⚠️ OBJ 임포터 Y 반전(Troubleshooting D12): 이 메시는 좌우 대칭 — 무영향.
# 사용: python Scripts/gen_voxel_drone.py [출력폴더=Saved/EnemyVoxel] [--sprites]
import math, os, sys
from collections import deque

VOXEL = 7.5
HALF = 8                     # 발자국 반폭(칸) — 셀 인덱스 -8..7
BODY = (-4, 3)               # 몸통 ix/iy 범위(포함) 8칸 — 키아트처럼 스크린 달린 몸통이 주인공(6칸은 로터에 묻혔다)
BODY_Z = (0, 4)              # 5층 — 밑면이 0층까지 내려와 전체 높이 6층(45cm)
ARM_Z = (2, 3)
HUB_LO, HUB_HI = 4, 6        # 허브 3×3 칸 범위(절댓값 축), 중심 5.5
HUB_Z = (2, 4)
ROTOR_Z = 5
ROTOR_R = 2                  # 날개 반경(칸) — 십자 5×5 (발자국 16 유지: 5.5+2 = 7.5)
HUB_CM = (HUB_LO + 1.5) * VOXEL   # 허브 중심 = (±41.25, ±41.25) cm — 머티리얼 파라미터 HubOffsetCm 과 같은 값
N_LAYERS = 6
ROTOR_RATE_TPS = 3.0         # 머티리얼 기본 회전 속도(turn/s)
ROTOR_UV_SPAN_CM = 60.0      # 로터 UV 소수부 인코딩 스팬(±30cm) — 날개 반경 2칸+1 = 22.5cm 가 안에 든다

E_TOP, E_SIDE, E_FRAME, E_CORE, E_ARM, E_HUB, E_ROTOR, E_LIGHT, E_FIN, E_R9, E_R10, E_R11 = range(12)
N_ELEM = 12
ELEM_NAME = ["BodyTop", "BodySide", "CoreFrame", "Core", "Arm", "Hub", "Rotor", "Light", "Fin", "Rsv9", "Rsv10", "Rsv11"]


def hexc(h):
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (1, 3, 5))


PREVIEW_KD = {
    E_TOP: hexc("#3A2748"), E_SIDE: hexc("#2A1E36"), E_FRAME: hexc("#1A1024"), E_CORE: hexc("#FF6B2C"),
    E_ARM: hexc("#4A2E58"), E_HUB: hexc("#2A1E36"), E_ROTOR: hexc("#B34A70"), E_LIGHT: hexc("#FF3B4E"),
    E_FIN: hexc("#6E2E44"), E_R9: hexc("#3A2748"), E_R10: hexc("#3A2748"), E_R11: hexc("#3A2748"),
}
EMISSIVE = {E_CORE, E_LIGHT}


def cc(i):
    return i + 0.5


# ── 셀 구성 ──────────────────────────────────────────────────────────────────
# cells[(ix,iy,iz)] = (elem, hub)  hub = 로터/허브/라이트의 허브 번호 0..3, 그 외 -1
def build_cells():
    cells = {}

    def put(ix, iy, iz, e, hub=-1):
        cells[(ix, iy, iz)] = (e, hub)

    # 몸통 8×8×5 (윗면/옆면 색은 면 추출에서 가른다 → 셀 요소는 E_SIDE)
    for ix in range(BODY[0], BODY[1] + 1):
        for iy in range(BODY[0], BODY[1] + 1):
            for iz in range(BODY_Z[0], BODY_Z[1] + 1):
                put(ix, iy, iz, E_SIDE)
    # 정면(+X) 스크린: 앞 열(ix=2) 4×4 프레임 + 2×2 코어
    for iy in range(-2, 2):
        for iz in range(1, 5):
            put(BODY[1], iy, iz, E_CORE if (-1 <= iy <= 0 and 2 <= iz <= 3) else E_FRAME)
    # 팔·허브·로터·라이트 ×4 (X자: 허브 중심 (±5.5, ±5.5))
    for k in range(4):
        sx = 1 if (k & 1) == 0 else -1
        sy = 1 if (k & 2) == 0 else -1
        # 팔: 몸통 모서리(3,3)를 감싸는 L 두 칸 (3,4)·(4,3) — 몸통·허브 둘 다에 면으로 닿는다
        for (ax, ay) in ((BODY[1], HUB_LO), (HUB_LO, BODY[1])):
            for iz in range(ARM_Z[0], ARM_Z[1] + 1):
                put(sx * ax if sx > 0 else -ax - 1, sy * ay if sy > 0 else -ay - 1, iz, E_ARM, k)
        # 허브 3×3 × 3층
        for ax in range(HUB_LO, HUB_HI + 1):
            for ay in range(HUB_LO, HUB_HI + 1):
                for iz in range(HUB_Z[0], HUB_Z[1] + 1):
                    put(ax if sx > 0 else -ax - 1, ay if sy > 0 else -ay - 1, iz, E_HUB, k)
        # 로터 십자(반경 3) — 허브 위 한 층
        hc = HUB_LO + 1
        hx, hy = (hc if sx > 0 else -hc - 1), (hc if sy > 0 else -hc - 1)   # 허브 중심 셀
        for d in range(-ROTOR_R, ROTOR_R + 1):
            put(hx + d, hy, ROTOR_Z, E_ROTOR, k)
            put(hx, hy + d, ROTOR_Z, E_ROTOR, k)
        # 라이트: 허브 중심 셀 바로 아래
        put(hx, hy, HUB_Z[0] - 1, E_LIGHT, k)
    # 꼬리 핀: 뒤(-X) 몸통 밖 세로 판 2칸 폭
    for iy in (-1, 0):
        for iz in (3, 4):
            put(BODY[0] - 1, iy, iz, E_FIN)
    return cells


# ── 단언 ─────────────────────────────────────────────────────────────────────
def hub_center(k):
    return (HUB_CM / VOXEL if (k & 1) == 0 else -HUB_CM / VOXEL,
            HUB_CM / VOXEL if (k & 2) == 0 else -HUB_CM / VOXEL)


def assert_rotor_clearance(cells):
    """로터 셀을 허브 중심 둘레로 0°/45°/90° 돌렸을 때 같은 층의 비-자기 셀과 중심 거리 < 1 이 없어야 한다."""
    rotors = [(k, cc(ix), cc(iy)) for (ix, iy, iz), (e, k) in cells.items() if e == E_ROTOR]
    others = [(cc(ix), cc(iy), k2) for (ix, iy, iz), (e, k2) in cells.items() if iz == ROTOR_Z and e != E_ROTOR]
    for deg in (0, 45, 90):
        a = math.radians(deg)
        pts = []
        for k, x, y in rotors:
            cx, cy = hub_center(k)
            dx, dy = x - cx, y - cy
            pts.append((k, cx + dx * math.cos(a) - dy * math.sin(a), cy + dx * math.sin(a) + dy * math.cos(a)))
        for i, (k, x, y) in enumerate(pts):
            for (ox, oy, k2) in others:
                assert math.hypot(x - ox, y - oy) >= 1.0, f"rotor {k} hits non-rotor cell at {deg}°"
            for j, (k2, x2, y2) in enumerate(pts):
                if k2 != k:
                    assert math.hypot(x - x2, y - y2) >= 1.0, f"rotor {k} hits rotor {k2} at {deg}°"
    print("[gen] rotor clearance OK (0/45/90 deg)")


def assert_symmetry(cells):
    asym = [k for k, v in cells.items() if (cells.get((k[0], -1 - k[1], k[2])) or (None,))[0] != v[0]]
    assert not asym, f"left/right asymmetry: {asym[:5]}"
    print("[gen] symmetry OK")


# ── 면 추출 ─────────────────────────────────────────────────────────────────
DIRS = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]


def extract_faces(cells):
    faces = []
    for (ix, iy, iz), (e, hub) in cells.items():
        for d in DIRS:
            n = (ix + d[0], iy + d[1], iz + d[2])
            if n in cells:
                continue
            elem = e
            if e == E_SIDE and d == (0, 0, 1):
                elem = E_TOP
            faces.append(((ix, iy, iz), d, elem, hub))
    return faces


def quad_corners(cell, d):
    ix, iy, iz = cell
    V = VOXEL
    x0, y0, z0 = ix * V, iy * V, iz * V - N_LAYERS * V / 2.0
    x1, y1, z1 = x0 + V, y0 + V, z0 + V
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


def write_obj(path, faces, name, with_mtl=False):
    lines = [f"# FPSR voxel enemy mesh: {name} (generated by gen_voxel_drone.py)",
             f"# ElementId = floor(UV.u); rotor hub = floor(UV.v) for element 6; rotor frac(UV) = 0.5 + offset/SPAN. HUB_CM={HUB_CM:.2f} ROTOR_RATE_TPS={ROTOR_RATE_TPS} ROTOR_UV_SPAN_CM={ROTOR_UV_SPAN_CM}"]
    if with_mtl:
        lines.append(f"mtllib {os.path.basename(path)[:-4]}.mtl")
    lines.append(f"o {name}")
    v, vt, vn, f = [], [], [], []
    cur = None
    for (cell, d, elem, hub) in (sorted(faces, key=lambda t: t[2]) if with_mtl else faces):
        corners = quad_corners(cell, d)
        assert check_winding(corners, d), (cell, d)
        bv, bt, bn = len(v), len(vt), len(vn)
        v.extend(corners)
        if elem == E_ROTOR and hub >= 0:
            # 로터: 코너마다 허브 중심 오프셋을 소수부에 싣는다(정점 위치 불필요). 코너 순서 = quad_corners 와 동일
            cx, cy = hub_center(hub)
            uvs = []
            for (px, py, pz) in corners:
                dx, dy = px - cx * VOXEL, py - cy * VOXEL
                fx, fy = 0.5 + dx / ROTOR_UV_SPAN_CM, 0.5 + dy / ROTOR_UV_SPAN_CM
                assert 0.02 < fx < 0.98 and 0.02 < fy < 0.98, (cell, dx, dy)
                uvs.append((elem + fx, hub + fy))
            vt.extend(uvs)
        else:
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
                if elem in EMISSIVE:
                    mh.write(f"Ke {kd[0] * 8:.2f} {kd[1] * 8:.2f} {kd[2] * 8:.2f}\n")
                mh.write("\n")
    xs = [p[0] for p in v]; ys = [p[1] for p in v]; zs = [p[2] for p in v]
    print(f"[gen] {path}  quads={len(faces)} tris={len(faces) * 2}  "
          f"bounds X[{min(xs):.1f},{max(xs):.1f}] Y[{min(ys):.1f},{max(ys):.1f}] Z[{min(zs):.1f},{max(zs):.1f}]")


def report_elements(tag, faces):
    counts = {}
    for (_, _, e, _) in faces:
        counts[e] = counts.get(e, 0) + 1
    print(f"[gen] {tag} faces by element: " + ", ".join(f"{ELEM_NAME[e]}={counts.get(e, 0)}" for e in range(N_ELEM)))
    return counts


# ── 2D 스프라이트 파생(컨셉 시트용) ───────────────────────────────────────────
SPRITE_CH = {E_TOP: "B", E_SIDE: "S", E_FRAME: "F", E_CORE: "C", E_ARM: "A", E_HUB: "H", E_ROTOR: "R", E_LIGHT: "L", E_FIN: "N"}


def derive_sprites(cells):
    """평면(위에서, +X = 위) / 정면(+X 에서, 왼쪽 = -Y) / 측면(-Y 에서, 왼쪽 = -X). 가장 가까운 셀의 요소 문자."""
    def sheet(proj, w, h):
        grid = [["." for _ in range(w)] for _ in range(h)]
        best = {}
        for (ix, iy, iz), (e, _) in cells.items():
            col, row, depth = proj(ix, iy, iz)
            key = (col, row)
            if key not in best or depth > best[key][0]:
                best[key] = (depth, e)
        for (col, row), (_, e) in best.items():
            grid[row][col] = SPRITE_CH.get(e, "?")
        return "\n".join("".join(r) for r in grid)
    top = sheet(lambda ix, iy, iz: (iy + HALF, HALF - 1 - ix, iz), 2 * HALF, 2 * HALF)
    front = sheet(lambda ix, iy, iz: (iy + HALF, N_LAYERS - 1 - iz, ix), 2 * HALF, N_LAYERS)
    side = sheet(lambda ix, iy, iz: (ix + HALF, N_LAYERS - 1 - iz, -iy), 2 * HALF, N_LAYERS)
    return top, front, side


def main(argv):
    out_dir = next((a for a in argv if not a.startswith("--")), os.path.join("Saved", "EnemyVoxel"))
    os.makedirs(out_dir, exist_ok=True)
    cells = build_cells()
    assert_symmetry(cells)
    assert_rotor_clearance(cells)
    xs = [k[0] for k in cells]; ys = [k[1] for k in cells]; zs = [k[2] for k in cells]
    assert min(xs) >= -HALF and max(xs) < HALF and min(ys) >= -HALF and max(ys) < HALF and 0 <= min(zs) and max(zs) < N_LAYERS
    print(f"[gen] cells={len(cells)} footprint={(max(xs)-min(xs)+1)}x{(max(ys)-min(ys)+1)} layers={max(zs)+1} "
          f"size={(max(xs)-min(xs)+1)*VOXEL:.0f}x{(max(ys)-min(ys)+1)*VOXEL:.0f}x{(max(zs)+1)*VOXEL:.1f}cm")
    faces = extract_faces(cells)
    write_obj(os.path.join(out_dir, "SM_EnemyVoxel_Drone.obj"), faces, "SM_EnemyVoxel_Drone")
    c = report_elements("UE", faces)
    assert c.get(E_ROTOR, 0) > 0 and c.get(E_CORE, 0) > 0 and c.get(E_LIGHT, 0) == 4 * 5, "요소 누락"
    write_obj(os.path.join(out_dir, "SM_EnemyVoxel_Drone_preview.obj"), faces, "Drone_preview", with_mtl=True)
    if "--sprites" in argv:
        top, front, side = derive_sprites(cells)
        print("DRONE_TOP\n" + top + "\nDRONE_FRONT\n" + front + "\nDRONE_SIDE\n" + side)
        with open(os.path.join(out_dir, "drone_sprites.txt"), "w", encoding="ascii") as fh:
            fh.write("DRONE_TOP\n" + top + "\nDRONE_FRONT\n" + front + "\nDRONE_SIDE\n" + side + "\n")


if __name__ == "__main__":
    main(sys.argv[1:])
