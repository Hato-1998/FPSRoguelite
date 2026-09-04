# gen_rifle_hardsurface.py — 하드서피스 라이플 절차적 생성
#   사용자 결정 2026-09-03: 복셀 폐기(너무 블럭). 무기는 절차적 하드서피스로 간다.
#   유지되는 결정 = +X 정면 · 머티리얼 슬롯(텍스처 0장) · 파츠 분리 · 파일럿.
#   폐기 = 복셀 칸 2.5cm(격자 없음). 그 자리에 모따기 폭 · 튜브 각수 · 슬랫 수가 들어간다.
#
# 단위 cm · +X 정면(총구) · +Z 상방 · Y 는 폭(중앙 0).
# 전장 95 · 높이 ~31 · 폭 ~10 은 현행 Synty 라이플 실측 기준선을 그대로 쓴다
# (손 IK·조준 정렬이 여기 걸려 있다 — Docs/RifleHardSurface_ResumePrompt.md §2-1).
#
# 🔴 CSG(불리언) 를 쓰지 않는다. 전부 **더하는** 프리미티브다 —
#    통기구·레일·홈은 구멍을 파는 게 아니라 슬랫 사이 간격으로 낸다. 절차적 생성이 깨지지 않는 유일한 길.
#
# 파츠 8종 = 몸통·개머리판·그립·탄창·핸드가드·총열 + 조준경 2종(1x 레드닷 / 2x).
#   조준경 2종은 **같은 소켓**에 붙는 교체 파츠다 — 모듈러/진화 시스템이 그대로 쓴다.
#
# 사용: python Scripts/gen_rifle_hardsurface.py [출력폴더=Saved/RifleHardSurface]
import os, sys, json, math

BEVEL = 0.3     # cm — 전 파츠 공통. 일관된 모따기가 "같은 공장 물건"으로 읽히게 하는 핵심
TUBE_SIDES = 12
VENTS = 6

SLOTS = ("Body", "Barrel", "Grip", "Emissive", "Reticle")
# Reticle 이 Emissive 와 갈린 이유: 레퍼런스의 조준점(빨강)과 프레임 액센트(청록)가
# 서로 다른 색이다. 한 슬롯이면 UE 에서 색을 따로 못 준다. §4-1 색 배정은 사용자 판정.


# ---------------------------------------------------------------------------
# 메시 버퍼
# ---------------------------------------------------------------------------
class Mesh:
    def __init__(self):
        self.v = []                  # [(x,y,z)]
        self.tris = []               # [(i0,i1,i2, slot)]

    def add_v(self, p):
        self.v.append(p)
        return len(self.v) - 1

    def _n(self, a, b, c):
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = self.v[a], self.v[b], self.v[c]
        ux, uy, uz = bx - ax, by - ay, bz - az
        wx, wy, wz = cx - ax, cy - ay, cz - az
        return (uy * wz - uz * wy, uz * wx - ux * wz, ux * wy - uy * wx)

    def tri(self, a, b, c, slot, ref):
        """ref = 이 면이 등져야 하는 내부 기준점. 법선이 안쪽을 보면 뒤집는다.
        와인딩을 손으로 맞추다 틀리는 것보다 이 방식이 안전하다."""
        nx, ny, nz = self._n(a, b, c)
        mx = (self.v[a][0] + self.v[b][0] + self.v[c][0]) / 3.0 - ref[0]
        my = (self.v[a][1] + self.v[b][1] + self.v[c][1]) / 3.0 - ref[1]
        mz = (self.v[a][2] + self.v[b][2] + self.v[c][2]) / 3.0 - ref[2]
        if nx * mx + ny * my + nz * mz < 0:
            a, b, c = a, c, b
        self.tris.append((a, b, c, slot))

    def quad(self, a, b, c, d, slot, ref):
        self.tri(a, b, c, slot, ref)
        self.tri(a, c, d, slot, ref)


# ---------------------------------------------------------------------------
# 프리미티브 1 — 모따기 프리즘. 어느 축으로든 뻗고, 양 끝 단면이 달라도 된다(테이퍼).
#   두 끝 단면의 중심을 어긋나게 주면 **기울어진 기둥**이 된다 — 조준경 프레임이 그렇게 만들어진다.
#   삼각 44개/개.
# ---------------------------------------------------------------------------
_AXIS_MAP = {                          # 로컬 (a, p, q) -> 월드 (x, y, z)
    "x": lambda a, p, q: (a, p, q),    # rect = (y0,y1,z0,z1)
    "y": lambda a, p, q: (q, a, p),    # rect = (z0,z1,x0,x1)
    "z": lambda a, p, q: (p, q, a),    # rect = (x0,x1,y0,y1)
}


def _norm_rect(r):
    """좌우 대칭 파츠를 sy=±1 로 찍으면 (−1.5, −2.5) 처럼 범위가 뒤집힌다.
    뒤집힌 채 두면 모따기가 안쪽이 아니라 바깥으로 나간다 → 여기서 항상 정렬."""
    return (min(r[0], r[1]), max(r[0], r[1]), min(r[2], r[3]), max(r[2], r[3]))


def prism(m, axis, a0, rect0, a1, rect1, slot, b=BEVEL):
    mp = _AXIS_MAP[axis]
    if a0 > a1:
        a0, a1, rect0, rect1 = a1, a0, rect1, rect0
    R = (_norm_rect(rect0), _norm_rect(rect1))
    A = (a0, a1)
    # 모따기가 단면보다 크면 면이 뒤집히므로 가장 얇은 치수의 1/3 로 제한
    thin = min(a1 - a0, *(R[i][1] - R[i][0] for i in (0, 1)), *(R[i][3] - R[i][2] for i in (0, 1)))
    b = max(0.0, min(b, thin / 3.0))
    ends = [mp(A[i], (R[i][0] + R[i][1]) / 2.0, (R[i][2] + R[i][3]) / 2.0) for i in (0, 1)]
    ref = tuple((ends[0][k] + ends[1][k]) / 2.0 for k in range(3))
    vA, vP, vQ = {}, {}, {}
    for i in (0, 1):
        sa = -1 if i == 0 else 1
        for j in (0, 1):
            sp = -1 if j == 0 else 1
            for k in (0, 1):
                sq = -1 if k == 0 else 1
                ai = A[i]
                pj = R[i][0] if j == 0 else R[i][1]
                qk = R[i][2] if k == 0 else R[i][3]
                vA[i, j, k] = m.add_v(mp(ai,          pj - sp * b, qk - sq * b))
                vP[i, j, k] = m.add_v(mp(ai - sa * b, pj,          qk - sq * b))
                vQ[i, j, k] = m.add_v(mp(ai - sa * b, pj - sp * b, qk))
    for i in (0, 1):                                            # 6 면
        m.quad(vA[i, 0, 0], vA[i, 1, 0], vA[i, 1, 1], vA[i, 0, 1], slot, ref)
    for j in (0, 1):
        m.quad(vP[0, j, 0], vP[1, j, 0], vP[1, j, 1], vP[0, j, 1], slot, ref)
    for k in (0, 1):
        m.quad(vQ[0, 0, k], vQ[1, 0, k], vQ[1, 1, k], vQ[0, 1, k], slot, ref)
    for j in (0, 1):                                            # 12 모따기
        for k in (0, 1):
            m.quad(vP[0, j, k], vP[1, j, k], vQ[1, j, k], vQ[0, j, k], slot, ref)
    for i in (0, 1):
        for k in (0, 1):
            m.quad(vA[i, 0, k], vA[i, 1, k], vQ[i, 1, k], vQ[i, 0, k], slot, ref)
        for j in (0, 1):
            m.quad(vA[i, j, 0], vA[i, j, 1], vP[i, j, 1], vP[i, j, 0], slot, ref)
    for i in (0, 1):                                            # 8 코너
        for j in (0, 1):
            for k in (0, 1):
                m.tri(vA[i, j, k], vP[i, j, k], vQ[i, j, k], slot, ref)


def box(m, x0, x1, y0, y1, z0, z1, slot, b=BEVEL):
    prism(m, "x", x0, (y0, y1, z0, z1), x1, (y0, y1, z0, z1), slot, b)


# ---------------------------------------------------------------------------
# 프리미티브 2 — N각 튜브 (X축). 끝단 림도 모따기해서 자른 단면이 안 보이게.
# ---------------------------------------------------------------------------
def tube(m, x0, x1, cy, cz, r, slot, sides=TUBE_SIDES, b=BEVEL):
    ref = ((x0 + x1) / 2.0, cy, cz)
    b = min(b, (x1 - x0) / 3.0)

    def ring(x, rad):
        return [m.add_v((x, cy + rad * math.cos(2.0 * math.pi * s / sides),
                            cz + rad * math.sin(2.0 * math.pi * s / sides))) for s in range(sides)]
    r_in = max(r - b, r * 0.5)
    cap0, mid0 = ring(x0, r_in), ring(x0 + b, r)
    mid1, cap1 = ring(x1 - b, r), ring(x1, r_in)
    for s in range(sides):
        t = (s + 1) % sides
        m.quad(mid0[s], mid0[t], mid1[t], mid1[s], slot, ref)   # 몸통
        m.quad(cap0[s], cap0[t], mid0[t], mid0[s], slot, ref)   # 뒤 림
        m.quad(mid1[s], mid1[t], cap1[t], cap1[s], slot, ref)   # 앞 림
    for (cap, x) in ((cap0, x0), (cap1, x1)):
        c = m.add_v((x, cy, cz))
        for s in range(sides):
            m.tri(c, cap[s], cap[(s + 1) % sides], slot, ref)


# ---------------------------------------------------------------------------
# 프리미티브 3 — 슬랫 배열. 통기구·피카티니 레일·리코일패드 홈을 전부 이걸로 낸다.
#   구멍을 파는 게 아니라 **사이를 비우는** 것이라 CSG 가 필요 없다.
# ---------------------------------------------------------------------------
def slats(m, a0, a1, n, duty, rect, slot, axis="x"):
    pitch = (a1 - a0) / float(n)
    for i in range(n):
        s0 = a0 + i * pitch + pitch * (1.0 - duty) * 0.5
        prism(m, axis, s0, rect, s0 + pitch * duty, rect, slot)


# ---------------------------------------------------------------------------
# 파츠 — 각 함수가 자기 파츠를 **공유 무기 공간**(0=개머리판 뒤, 총구=+X)에 그린다.
# mount = 이 파츠가 몸통 소켓에 붙는 지점. 내보낼 때 이 점을 원점으로 옮긴다.
# ---------------------------------------------------------------------------
BORE = 20.0     # 총열 축 높이


def p_stock(m):
    box(m, 0.0, 2.2, -4.5, 4.5, 13.0, 27.0, "Grip")                       # 리코일 패드
    slats(m, 0.15, 2.05, 3, 0.45, (-4.6, 4.6, 14.0, 26.0), "Grip")        # 패드 홈
    prism(m, "x", 2.2, (-2.4, 2.4, 22.0, 27.0), 24.0, (-2.2, 2.2, 23.0, 26.5), "Grip")  # 치크레스트
    box(m, 3.0, 22.0, -2.0, 2.0, 14.0, 16.5, "Grip")                      # 아랫대
    box(m, 2.2, 6.0, -1.8, 1.8, 16.5, 22.5, "Grip")                       # 뒤 연결
    tube(m, 19.0, 26.0, 0.0, 21.0, 2.5, "Grip")                           # 버퍼 튜브
    box(m, 20.0, 25.0, -3.2, 3.2, 15.5, 25.0, "Grip")                     # 리시버 결합부
    for sy in (-1, 1):                                                     # 멜빵 고리(후방)
        box(m, 4.5, 6.5, sy * 1.8, sy * 2.6, 15.2, 17.2, "Grip")


def p_body(m):
    box(m, 22.0, 52.0, -4.6, 4.6, 15.0, 25.5, "Body")                     # 본체
    box(m, 26.0, 50.0, -1.7, 1.7, 25.5, 26.3, "Body")                     # 레일 베이스
    slats(m, 26.0, 50.0, 12, 0.55, (-1.7, 1.7, 26.3, 27.2), "Body")       # 피카티니 레일
    for sy in (-1, 1):                                                     # 측면 인셋 패널
        box(m, 27.0, 44.0, sy * 4.4, sy * 5.0, 17.5, 23.0, "Body")
    box(m, 40.0, 47.5, 4.5, 5.2, 20.0, 24.2, "Body")                      # 탄피 배출구 림(우)
    box(m, 44.0, 50.5, 0.0, 1.3, 24.8, 26.0, "Body")                      # 장전손잡이
    box(m, 49.0, 51.5, 0.0, 2.6, 24.6, 26.2, "Body")                      #   손잡이 노브
    box(m, 33.5, 35.8, -5.1, -4.5, 16.2, 17.6, "Body")                    # 조정간(좌)
    box(m, 36.2, 37.6, 4.5, 5.1, 15.6, 16.8, "Body")                      # 탄창 멈치(우)
    box(m, 35.6, 37.2, -3.4, 3.4, 11.4, 15.0, "Grip")                     # 트리거가드 앞
    box(m, 30.4, 37.2, -3.4, 3.4, 11.4, 12.6, "Grip")                     #   아래
    box(m, 29.4, 30.8, -3.4, 3.4, 11.4, 15.0, "Grip")                     #   뒤
    box(m, 32.4, 33.8, -1.2, 1.2, 12.6, 15.2, "Grip")                     # 트리거
    box(m, 50.2, 52.6, -1.4, 1.4, 25.6, 27.2, "Emissive")                 # 형광 액센트(소면적, §4-2)


def p_grip(m):
    prism(m, "x", 25.6, (-2.2, 2.2, 1.8, 6.2), 29.6, (-2.4, 2.4, 4.0, 9.2), "Grip")
    prism(m, "x", 28.8, (-2.4, 2.4, 6.0, 11.2), 33.8, (-2.4, 2.4, 10.0, 15.4), "Grip")
    box(m, 25.2, 30.0, -2.3, 2.3, 1.2, 2.6, "Grip")                       # 바닥 캡


def p_mag(m):
    prism(m, "x", 36.6, (-2.0, 2.0, 3.4, 8.6), 38.6, (-2.0, 2.0, 5.2, 15.4), "Grip")
    box(m, 37.0, 46.2, -1.9, 1.9, 5.6, 15.4, "Grip")
    box(m, 36.4, 46.6, -2.2, 2.2, 3.0, 4.4, "Grip")                       # 플로어플레이트
    box(m, 37.4, 45.8, -0.7, 0.7, 4.4, 5.5, "Emissive")                   # 잔탄 슬릿


def p_handguard(m):
    box(m, 50.0, 72.0, -4.2, 4.2, 15.4, 17.6, "Body")                     # 하부 프레임
    box(m, 50.0, 72.0, -4.2, 4.2, 23.4, 25.4, "Body")                     # 상부 프레임
    for sy in (-1, 1):                                                     # 측면 통기 슬랫
        slats(m, 50.6, 71.4, VENTS, 0.62, (sy * 3.4, sy * 4.3, 17.6, 23.4), "Body")
    slats(m, 51.0, 68.0, 8, 0.55, (-1.7, 1.7, 25.4, 26.3), "Body")        # 상부 피카티니
    box(m, 65.0, 70.0, -2.7, 2.7, 21.5, 25.6, "Body")                     # 가스블록
    box(m, 66.6, 68.2, -1.1, 1.1, 25.6, 28.4, "Body")                     # 전방 가늠쇠
    for sy in (-1, 1):                                                     # 멜빵 고리(전방)
        box(m, 53.0, 55.0, sy * 4.2, sy * 4.9, 15.6, 17.4, "Body")


def p_barrel(m):
    tube(m, 70.0, 84.5, 0.0, BORE, 2.2, "Barrel")                         # 총열
    tube(m, 82.0, 95.0, 0.0, BORE, 3.4, "Barrel")                         # 소음기
    for i in range(5):                                                     # 소음기 방열 링
        x = 84.0 + i * 2.1
        tube(m, x, x + 1.0, 0.0, BORE, 3.75, "Barrel", b=0.12)
    tube(m, 94.6, 95.6, 0.0, BORE, 3.0, "Barrel", b=0.12)                 # 총구 캡


# --- 조준경 ---------------------------------------------------------------
#   레퍼런스 2·3: 프레임 두 기둥이 **좌우로** 벌어져 서고 가운데는 뚫려 있다.
#   1x = 조준점 하나. 2x = 같은 언어로 더 높고 안쪽 모서리가 발광, 가운데 표식은 눈금.
def _sight_base(m, x0, x1):
    box(m, x0, x1, -1.8, 1.8, 27.2, 28.5, "Body")                         # 마운트 베이스
    box(m, x0 + 0.6, x1 - 0.6, -2.4, 2.4, 28.5, 29.2, "Body")             # 상판


def p_sight_red(m):
    _sight_base(m, 38.0, 46.4)
    for sy in (-1, 1):                                                     # 바깥으로 벌어진 기둥
        prism(m, "z", 29.2, (40.4, 42.6, sy * 1.5, sy * 2.5),
                      34.6, (40.8, 42.4, sy * 2.9, sy * 3.9), "Body")
        prism(m, "z", 34.6, (40.8, 42.4, sy * 2.9, sy * 3.9),
                      35.6, (41.0, 42.2, sy * 3.1, sy * 3.7), "Reticle")   # 기둥 끝 발광
    box(m, 41.2, 42.0, -0.4, 0.4, 31.6, 32.4, "Reticle")                  # ● 조준점 하나


def p_sight_2x(m):
    _sight_base(m, 37.0, 47.6)
    for sy in (-1, 1):
        prism(m, "z", 29.2, (40.0, 43.0, sy * 1.6, sy * 2.8),
                      37.2, (40.6, 42.6, sy * 3.2, sy * 4.4), "Body")
        prism(m, "z", 29.6, (40.9, 42.3, sy * 1.5, sy * 1.9),
                      36.8, (41.1, 42.1, sy * 3.1, sy * 3.5), "Emissive")  # 안쪽 모서리 발광
    box(m, 41.2, 42.0, -0.25, 0.25, 32.4, 34.2, "Reticle")                # │ 세로 눈금
    box(m, 41.2, 42.0, -0.9, 0.9, 33.4, 33.9, "Reticle")                  # ─ 가로 눈금
    box(m, 39.6, 43.6, -1.5, 1.5, 29.2, 30.0, "Emissive")                 # 하부 라이트바


PARTS = {
    "Stock":     {"fn": p_stock,     "mount": (24.0, 0.0, 20.0)},
    # 몸통 원점 = **그립 마운트 지점**. Synty 몸통이 그렇다(SOCKET_Mount_Grip_0 = 본 원점 (0,0,0), 2026-09-03 실측)
    # — 캐릭터의 SOCKET_Weapon 이 그 관례로 튜닝돼 있어서, 원점을 개머리판 끝(0,0,0)에 두면
    # 총 전체가 ~30cm 앞으로 밀려 붙는다. 몸통 소켓 좌표는 이 점 기준으로 다시 잰다(manifest).
    "Body":      {"fn": p_body,      "mount": (30.0, 0.0, 15.5)},
    "Grip":      {"fn": p_grip,      "mount": (30.0, 0.0, 15.5)},
    "Mag":       {"fn": p_mag,       "mount": (38.0, 0.0, 15.5)},
    "Handguard": {"fn": p_handguard, "mount": (50.0, 0.0, 20.0)},
    "Barrel":    {"fn": p_barrel,    "mount": (70.0, 0.0, BORE),
                  "sockets": {"SOCKET_Muzzle": (96.0, 0.0, BORE)}},
    # 조준경 2종은 **같은 소켓**에 붙는 교체 파츠 — 모듈러/진화가 그대로 쓴다.
    "SightRed":  {"fn": p_sight_red, "mount": (38.0, 0.0, 27.2),
                  "sockets": {"SOCKET_Aim": (41.6, 0.0, 32.0)}},
    "Sight2x":   {"fn": p_sight_2x,  "mount": (38.0, 0.0, 27.2),
                  "sockets": {"SOCKET_Aim": (41.6, 0.0, 33.6)}},
}
ALT_PARTS = ("Sight2x",)      # 조립 미리보기·전장 계산에서 제외(같은 소켓을 두 번 채우면 겹친다)

BODY_SOCKETS = {
    "SOCKET_Mount_Stock_0":     PARTS["Stock"]["mount"],
    "SOCKET_Mount_Grip_0":      PARTS["Grip"]["mount"],
    "SOCKET_Mount_Mag_0":       PARTS["Mag"]["mount"],
    "SOCKET_Mount_Handguard_0": PARTS["Handguard"]["mount"],
    "SOCKET_Mount_Barrel_0":    PARTS["Barrel"]["mount"],
    "SOCKET_Mount_Reddot_0":    PARTS["SightRed"]["mount"],
    "SOCKET_RightHand":         (31.0, 0.0, 10.0),
    "SOCKET_LeftHand":          (60.0, 0.0, 15.0),
}

# 손 소켓 **회전** (엔진 프레임 P/Y/R, 도). 팔 IK 가 소켓 전체 트랜스폼으로 손바닥 방향을 잡으므로 회전이 없으면
# 손이 엉뚱한 방향을 본다(2026-09-04 PIE 실측). 값 = Synty SK_Wep_Mod_A_Body_01 손 소켓 회전을 본 공간
# (정면 +Z, 위 -Y) → 엔진 프레임(정면 +Y, 위 +Z) 으로 기저 변환(Te = Minv*Ts*M)한 것. 위치는 몸통 형상이
# 달라 위 BODY_SOCKETS 값을 쓴다. 미세 조정은 PIE 판정 후 사용자와 — 이 표만 고치고 소켓 스크립트를 재실행.
BODY_SOCKET_ROTATIONS = {
    "SOCKET_LeftHand":  (0.0, 100.0, 90.0),
    "SOCKET_RightHand": (15.6, -77.5, -86.6),
}


# ---------------------------------------------------------------------------
# 내보내기 프레임 — 저작 공간(+X 정면)을 **엔진 무기 프레임(+Y 정면)** 으로 돌린다.
#   이 프로젝트의 무기 관례는 Synty 팩을 따른다: DA 주석 "this pack's weapon-forward is +Y",
#   SK_Wep_Mod_A_Body_01 바운드 장축 Y(42.5cm), 캐릭터 SOCKET_Weapon(hand_r, P/Y/R 0/75/-17)이
#   그 관례로 튜닝돼 있다(2026-09-04 실측). +X 정면 그대로 붙이면 총이 90° 옆을 본다.
#   → 저작은 +X 로 하고(사람이 읽기 쉽다), OBJ·manifest 만 여기서 +90° yaw 를 건다:
#     author(x, y, z) -> engine(-y, x, z)     [det = +1, 미러 없음]
#   따라서 DA 의 ADSAimRotationOffset(Yaw 90) 은 **그대로 둔다** — 무기 전체가 Synty 와 같은 프레임에 있으므로.
#   (§2-2 의 "0 으로 되돌린다"는 이 실측 전의 오판이었다. PIE §6-2 에서 ADS 가 90° 틀리면 그때 0 으로.)
def to_engine(p):
    """저작 → **엔진에 실제로 있어야 하는** 좌표. 소켓/manifest 는 파이썬이 직접 찍으므로 이걸 쓴다."""
    x, y, z = p
    return (-y, x, z)


# 🔴 UE 의 OBJ 임포터는 **Y 를 부호 반전**한다(오른손 OBJ → 왼손 UE, 2026-09-04 실측:
#    엔진 기대 Y −8..22.6 이 −22.6..8.0 으로 들어옴. X·Z 는 그대로). 크기(size)만 보면 절대 안 잡힌다 —
#    반드시 바운드 min/max 로 볼 것(Troubleshooting D12). 그래서 OBJ 정점은 미리 Y 를 뒤집어 쓴다:
#    file = (Xe, −Ye, Ze). 이 사상은 det −1 이라 파일 안에서 와인딩이 뒤집히므로 삼각 인덱스도 함께 뒤집어
#    **파일 자체는 정상 오른손 OBJ(바깥 CCW)** 가 되게 한다. 임포터가 그걸 한 번 더 뒤집어 엔진에서 바르게 선다.
def to_file(p):
    ex, ey, ez = to_engine(p)
    return (ex, -ey, ez)


def write_obj(path, mesh, origin, name):
    ox, oy, oz = origin
    by_slot = {}
    for (a, b, c, slot) in mesh.tris:
        by_slot.setdefault(slot, []).append((a, c, b))      # det −1 사상 → 와인딩 반전으로 바깥면 유지
    with open(path, "w", encoding="utf-8") as f:
        f.write("# %s — gen_rifle_hardsurface.py (cm; file=OBJ right-handed, engine=+Y forward after UE Y-flip; "
                "authored +X; bevel %.2f)\n" % (name, BEVEL))
        f.write("o %s\n" % name)
        for (x, y, z) in mesh.v:
            fx, fy, fz = to_file((x - ox, y - oy, z - oz))
            f.write("v %.4f %.4f %.4f\n" % (fx, fy, fz))
        for slot in sorted(by_slot):
            f.write("usemtl %s\n" % slot)
            for (a, b, c) in by_slot[slot]:
                f.write("f %d %d %d\n" % (a + 1, b + 1, c + 1))
    return len(mesh.v), len(mesh.tris), sorted(by_slot)


SVG_COLORS = {"Body": (74, 91, 140), "Grip": (35, 38, 46), "Barrel": (58, 62, 70),
              "Emissive": (120, 220, 230), "Reticle": (240, 70, 60)}


def write_preview_svg(path, tris_world, yaw=-38.0, pitch=22.0, scale=7.0):
    """3/4 축측투상 + 평면 플랫셰이딩. 모따기가 보이려면 정면도로는 안 된다."""
    a, p = math.radians(yaw), math.radians(pitch)
    ca, sa, cp, sp = math.cos(a), math.sin(a), math.cos(p), math.sin(p)
    L = (0.42, -0.66, 0.62)

    def proj(v):
        x, y, z = v
        x1 = x * ca - y * sa
        y1 = x * sa + y * ca
        return (x1, -(y1 * sp + z * cp), y1 * cp - z * sp)   # u, v, depth

    faces = []
    for (p0, p1, p2, slot) in tris_world:
        ux, uy, uz = (p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2])
        wx, wy, wz = (p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2])
        nx, ny, nz = (uy * wz - uz * wy, uz * wx - ux * wz, ux * wy - uy * wx)
        ln = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
        sh = 0.30 + 0.70 * max(0.0, (nx * L[0] + ny * L[1] + nz * L[2]) / ln)
        pts = [proj(q) for q in (p0, p1, p2)]
        faces.append((sum(q[2] for q in pts) / 3.0, pts, slot, sh))
    faces.sort(key=lambda t: -t[0])

    us = [q[0] for _, pts, _, _ in faces for q in pts]
    vs = [q[1] for _, pts, _, _ in faces for q in pts]
    u0, u1, v0, v1 = min(us), max(us), min(vs), max(vs)
    W, H, pad = (u1 - u0) * scale, (v1 - v0) * scale, 26
    out = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">'
           % (W + pad * 2, H + pad * 2, W + pad * 2, H + pad * 2),
           '<rect width="100%" height="100%" fill="#0d1014"/>']
    for (_, pts, slot, sh) in faces:
        r, g, b = SVG_COLORS.get(slot, (140, 140, 140))
        col = "#%02x%02x%02x" % (min(255, int(r * sh)), min(255, int(g * sh)), min(255, int(b * sh)))
        d = " ".join("%.2f,%.2f" % (pad + (q[0] - u0) * scale, pad + (q[1] - v0) * scale) for q in pts)
        out.append('<polygon points="%s" fill="%s"/>' % (d, col))
    out.append('</svg>')
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join("Saved", "RifleHardSurface")
    os.makedirs(out, exist_ok=True)

    manifest = {"units": "cm", "forward_axis": "+X", "bevel_cm": BEVEL,
                "tube_sides": TUBE_SIDES, "vents": VENTS, "parts": {}, "body_sockets": {},
                "alt_parts": list(ALT_PARTS)}
    world_tris, sight2x_tris, total_v, total_t = [], [], 0, 0
    lo = [1e9] * 3
    hi = [-1e9] * 3

    for name, spec in PARTS.items():
        m = Mesh()
        spec["fn"](m)
        tris = [(m.v[a], m.v[b], m.v[c], slot) for (a, b, c, slot) in m.tris]
        if name in ALT_PARTS:
            sight2x_tris += tris
        else:
            world_tris += tris
            for (x, y, z) in m.v:
                lo = [min(lo[0], x), min(lo[1], y), min(lo[2], z)]
                hi = [max(hi[0], x), max(hi[1], y), max(hi[2], z)]
        path = os.path.join(out, "SM_RifleHS_%s.obj" % name)
        nv, nt, slots = write_obj(path, m, spec["mount"], "SM_RifleHS_%s" % name)
        total_v += nv
        total_t += nt
        mt = spec["mount"]
        manifest["parts"][name] = {
            "file": os.path.basename(path), "verts": nv, "tris": nt, "slots": slots,
            "mount_author_cm": list(mt),
            # 소켓은 파츠 로컬(mount 원점) · **엔진 프레임** — UE 에서 그대로 찍는 값
            "sockets": {k: list(to_engine((v[0] - mt[0], v[1] - mt[1], v[2] - mt[2])))
                        for k, v in spec.get("sockets", {}).items()},
        }
        print("  %-10s verts=%5d  tris=%5d  slots=%s" % (name, nv, nt, ",".join(slots)))

    bm = PARTS["Body"]["mount"]          # 몸통 소켓은 몸통 로컬(= 그립 마운트 기준) · 엔진 프레임 cm
    for k, v in BODY_SOCKETS.items():
        manifest["body_sockets"][k] = list(to_engine((v[0] - bm[0], v[1] - bm[1], v[2] - bm[2])))
    # 회전은 이미 엔진 프레임 P/Y/R 로 적혀 있다 — 변환 없이 그대로 기록(소켓 스크립트가 그대로 찍는다)
    manifest["body_socket_rotations"] = {k: list(v) for k, v in BODY_SOCKET_ROTATIONS.items()}
    manifest["frame"] = "engine: +Y forward, +Z up (authored +X forward, exported with +90deg yaw)"
    manifest["assembled_size_cm"] = {"length_x": hi[0] - lo[0], "width_y": hi[1] - lo[1],
                                     "height_z": hi[2] - lo[2]}
    with open(os.path.join(out, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)

    write_preview_svg(os.path.join(out, "preview.svg"), world_tris)
    # 2x 조준경은 1x 자리에 바꿔 끼운 조립도를 따로 낸다
    sight_red = PARTS["SightRed"]["fn"]
    m_red = Mesh(); sight_red(m_red)
    red_set = {(m_red.v[a], m_red.v[b], m_red.v[c]) for (a, b, c, _) in m_red.tris}
    without_red = [t for t in world_tris if (t[0], t[1], t[2]) not in red_set]
    write_preview_svg(os.path.join(out, "preview_2x.svg"), without_red + sight2x_tris)

    print("\n조립 크기: 전장 %.1fcm · 폭 %.1f · 높이 %.1f" % (hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]))
    print("합계: 정점 %d · 삼각 %d · 파츠 %d (모따기 %.2fcm · 튜브 %d각 · 통기구 %d)"
          % (total_v, total_t, len(PARTS), BEVEL, TUBE_SIDES, VENTS))
    print("출력: %s  (preview.svg = 1x 조립 / preview_2x.svg = 2x 조립)" % out)


if __name__ == "__main__":
    main()
