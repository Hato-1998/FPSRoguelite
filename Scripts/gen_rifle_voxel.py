# gen_rifle_voxel.py — 복셀 라이플 절차적 생성 (보드: 아케이드 룩 프로토 / 라이플 복셀)
# 파츠별 OBJ 를 생성한다. 규격 = Docs/RifleVoxel_ResumePrompt.md §2 (전부 확정됨).
#
#   격자 2.5cm · +X 정면(총구) · +Z 상방 · 단위 cm
#   전장 38칸(95cm) × 높이 13칸(32.5cm) × 폭 4칸(10cm)
#
# 파츠 7종(§2-4 파츠 분리). 각 파츠는 **자기 마운트 지점이 원점**이 되도록 이동해 내보낸다 —
# 몸통 소켓에 붙으면 제자리에 오도록. (공유 원점 + identity 부착은 조립이 안 된다:
# 메모리 synty-modular-parts-shared-origin 이 실증한 함정.)
#
# 머티리얼 슬롯(§2-3 B안, 텍스처 없음) = Body / Barrel / Grip / Emissive.
# 색은 여기서 정하지 않는다 — 슬롯만 나누고 실제 색은 UE 절차적 MI 가 낸다(§3-5, §4-1 대역).
#
# 사용: python Scripts/gen_rifle_voxel.py [출력폴더=Saved/RifleVoxel]
# 임포트는 헤드리스 정식 에디터로(§5-1). 라이브 에디터 파이썬 임포트 = 데드락.
import os, sys, json

CELL = 2.5  # cm

# ---------------------------------------------------------------------------
# 파츠 정의 — 공유 무기 공간의 셀 박스 목록. (x0,x1, y0,y1, z0,z1) 는 **양끝 포함**.
# X: 0 = 개머리판 뒤끝, 37 = 총구 끝.  Z: 0 = 탄창 바닥, 8 = 총열 축, 12 = 조준경 위.
# Y: 0..3 (폭 4칸). 총열류는 Y1..2 로 얇게.
#
# 비율 기준선 = 현행 Synty 라이플 실측(§2-1): 총열 20칸 · 핸드가드 12 · 개머리판 10 ·
# 그립 5 · 조준경 4 · 탄창 4. 아래 배치는 그 비율을 따른다.
# ---------------------------------------------------------------------------
PARTS = {
    # 개머리판 — 버트플레이트 + 위아래 두 갈래(가운데를 비워 실루엣에 구멍을 낸다)
    "Stock": {
        "mount": (9, 1.5, 7),
        "slot_default": "Grip",
        "boxes": [
            (0, 1, 0, 3, 4, 9, "Grip"),   # 버트플레이트
            (2, 9, 1, 2, 7, 8, "Grip"),   # 윗대
            (2, 8, 1, 2, 4, 4, "Grip"),   # 아랫대
            (2, 3, 1, 2, 5, 6, "Grip"),   # 뒤 연결
        ],
    },
    # 몸통(리시버) — 유채색 본체. 트리거/트리거가드를 흡수(§2-1: 트리거는 1칸 이하)
    "Body": {
        "mount": (0, 0, 0),               # 몸통이 기준 — 이동 없음
        "slot_default": "Body",
        "boxes": [
            (8, 19, 0, 3, 6, 9, "Body"),      # 리시버 본체
            (8, 18, 1, 2, 10, 10, "Body"),    # 상부 레일
            (13, 14, 0, 3, 5, 5, "Grip"),     # 트리거가드 앞
            (12, 12, 1, 2, 4, 5, "Grip"),     # 트리거(1칸)
            (19, 19, 1, 2, 10, 10, "Emissive"),  # 작은 형광 액센트 — §4-2 소면적만
        ],
    },
    # 그립 — 어두운 총몸. 뒤로 살짝 누인 형태를 계단으로
    "Grip": {
        "mount": (10, 1.5, 6),
        "slot_default": "Grip",
        "boxes": [
            (10, 12, 1, 2, 4, 5, "Grip"),
            (9, 11, 1, 2, 2, 3, "Grip"),
            (9, 10, 1, 2, 1, 1, "Grip"),
        ],
    },
    # 탄창 — 그립 앞, 아래로
    "Mag": {
        "mount": (14, 1.5, 6),
        "slot_default": "Grip",
        "boxes": [
            (14, 16, 1, 2, 2, 5, "Grip"),
            (14, 16, 1, 2, 1, 1, "Emissive"),  # 잔탄 표시 슬릿
        ],
    },
    # 핸드가드 — 리시버 앞. 위아래 슬롯을 비워 통기구 인상
    "Handguard": {
        "mount": (19, 1.5, 7),
        "slot_default": "Body",
        "boxes": [
            (19, 27, 0, 3, 7, 9, "Body"),
            (20, 26, 0, 0, 6, 6, "Body"),
            (20, 26, 3, 3, 6, 6, "Body"),
            (21, 21, 1, 2, 10, 10, "Body"),   # 앞 가늠쇠 받침
        ],
    },
    # 총열 + 소음기 — 어두운 금속
    "Barrel": {
        "mount": (27, 1.5, 8),
        "slot_default": "Barrel",
        "boxes": [
            (27, 32, 1, 2, 7, 8, "Barrel"),   # 총열
            (33, 37, 0, 3, 7, 9, "Barrel"),   # 소음기(굵게)
        ],
        # 총구 소켓은 이 파트에 있다(§3-4 — 몸통 아님)
        "sockets": {"SOCKET_Muzzle": (38, 1.5, 8)},
    },
    # 조준경 — 레드닷. 렌즈만 발광
    "Reddot": {
        "mount": (15, 1.5, 11),
        "slot_default": "Body",
        "boxes": [
            (14, 17, 1, 2, 11, 11, "Body"),   # 마운트 베이스
            (14, 14, 1, 2, 12, 12, "Body"),   # 뒤 프레임
            (17, 17, 1, 2, 12, 12, "Body"),   # 앞 프레임
            (15, 16, 1, 2, 12, 12, "Emissive"),  # 렌즈/레티클
        ],
        # ADS 조준 소켓은 이 파트에 있다(§3-4)
        "sockets": {"SOCKET_Aim": (15.5, 1.5, 12)},
    },
}

# 몸통에 만들 소켓(파츠 마운트 + 양손). 값은 공유 무기 공간의 셀 좌표.
BODY_SOCKETS = {
    "SOCKET_Mount_Stock_0":     PARTS["Stock"]["mount"],
    "SOCKET_Mount_Grip_0":      PARTS["Grip"]["mount"],
    "SOCKET_Mount_Mag_0":       PARTS["Mag"]["mount"],
    "SOCKET_Mount_Handguard_0": PARTS["Handguard"]["mount"],
    "SOCKET_Mount_Barrel_0":    PARTS["Barrel"]["mount"],
    "SOCKET_Mount_Reddot_0":    PARTS["Reddot"]["mount"],
    "SOCKET_RightHand":         (11, 1.5, 4),   # 그립을 쥐는 손
    "SOCKET_LeftHand":          (23, 1.5, 6),   # 핸드가드를 받치는 손
}


def fill(boxes):
    """셀 박스 목록 -> {(x,y,z): slot}"""
    cells = {}
    for (x0, x1, y0, y1, z0, z1, slot) in boxes:
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                for z in range(z0, z1 + 1):
                    cells[(x, y, z)] = slot
    return cells


# 셀 기준 6면. (법선, 그 면을 이루는 4꼭짓점 오프셋)
FACES = [
    ((1, 0, 0),  [(1, 0, 0), (1, 1, 0), (1, 1, 1), (1, 0, 1)]),
    ((-1, 0, 0), [(0, 0, 0), (0, 0, 1), (0, 1, 1), (0, 1, 0)]),
    ((0, 1, 0),  [(0, 1, 0), (0, 1, 1), (1, 1, 1), (1, 1, 0)]),
    ((0, -1, 0), [(0, 0, 0), (1, 0, 0), (1, 0, 1), (0, 0, 1)]),
    ((0, 0, 1),  [(0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)]),
    ((0, 0, -1), [(0, 0, 0), (0, 1, 0), (1, 1, 0), (1, 0, 0)]),
]


def write_obj(path, cells, origin_cell, name):
    """겉면만 내보낸다(이웃이 채워진 면은 생략) — 내부면이 남으면 삼각형이 배로 는다."""
    ox, oy, oz = origin_cell
    verts, vmap, normals, nmap = [], {}, [], {}
    groups = {}          # slot -> [(vi,ni) x4, ...]
    for (c, slot) in sorted(cells.items()):
        cx, cy, cz = c
        for (n, corners) in FACES:
            if (cx + n[0], cy + n[1], cz + n[2]) in cells:
                continue  # 이웃이 있으면 그 면은 안 보인다
            if n not in nmap:
                nmap[n] = len(normals) + 1
                normals.append(n)
            quad = []
            for (dx, dy, dz) in corners:
                key = (cx + dx, cy + dy, cz + dz)
                if key not in vmap:
                    vmap[key] = len(verts) + 1
                    verts.append(((key[0] - ox) * CELL,
                                  (key[1] - oy) * CELL,
                                  (key[2] - oz) * CELL))
                quad.append((vmap[key], nmap[n]))
            groups.setdefault(slot, []).append(quad)

    with open(path, "w", encoding="utf-8") as f:
        f.write("# %s — gen_rifle_voxel.py (cell %.2fcm, +X forward, cm)\n" % (name, CELL))
        f.write("o %s\n" % name)
        for v in verts:
            f.write("v %.4f %.4f %.4f\n" % v)
        for n in normals:
            f.write("vn %d %d %d\n" % n)
        for slot in sorted(groups):
            f.write("usemtl %s\n" % slot)          # 머티리얼 슬롯 = UE 섹션이 된다
            for quad in groups[slot]:
                # 사각면을 삼각 2장으로 (OBJ 사각면도 되지만 임포터별 편차를 피한다)
                a, b, c2, d = quad
                f.write("f %d//%d %d//%d %d//%d\n" % (a[0], a[1], b[0], b[1], c2[0], c2[1]))
                f.write("f %d//%d %d//%d %d//%d\n" % (a[0], a[1], c2[0], c2[1], d[0], d[1]))
    return len(verts), sum(len(q) * 2 for q in groups.values()), sorted(groups)


# --- Blockbench 저작용 .bbmodel ------------------------------------------------
# 사용자가 열어서 직접 다듬는 **출발점**이다. 이 스크립트의 출력 중 유일하게 "사람이 편집할 것".
#
# 단위 계약: **Blockbench 1칸 = 복셀 1칸 = 2.5cm.**
#   Blockbench 기본 스냅이 1칸이라, 이렇게 두면 드래그만 해도 복셀 격자를 벗어날 수 없다.
#   (1칸=1cm 로 두면 2.5 배수로 직접 타이핑해야 해서 사람이 격자를 깨기 쉽다.)
#   → 임포트 때 ×2.5 로 되돌린다. 그 환산이 맞았는지는 바운드 실측으로 검증한다(§5-4).
#
# 축 계약: Blockbench 는 **Y 상방**, 이 프로젝트는 **Z 상방 · +X 정면**.
#   bb(x, y, z) = ue(x, z, y)  ← 여기서 변환해 내보내고, 임포트에서 되돌린다.
BB_SLOT_COLOR = {"Body": 4, "Grip": 7, "Barrel": 6, "Emissive": 3}  # 아웃라이너 마커색


def write_bbmodel(path, parts_cells):
    """파츠별 그룹으로 묶은 Blockbench 모델. 셀 하나 = 큐브 하나 대신, 원본 박스 단위로 낸다
    (셀마다 큐브를 만들면 612개가 되어 편집이 불가능하다)."""
    elements, outliner = [], []
    uid = [0]

    def new_uuid(tag):
        uid[0] += 1
        return "%08x-0000-4000-8000-%012x" % (abs(hash(tag)) & 0xFFFFFFFF, uid[0])

    for pname, spec in PARTS.items():
        kids = []
        for i, (x0, x1, y0, y1, z0, z1, slot) in enumerate(spec["boxes"]):
            u = new_uuid("%s_%d" % (pname, i))
            # 셀 인덱스 -> Blockbench 좌표(Y 상방). to 는 x1+1 (양끝 포함이므로)
            frm = [x0, z0, y0]
            to = [x1 + 1, z1 + 1, y1 + 1]
            faces = {f: {"uv": [0, 0, 16, 16], "texture": None}
                     for f in ("north", "east", "south", "west", "up", "down")}
            elements.append({
                "name": "%s_%s_%d" % (pname, slot, i), "box_uv": False, "type": "cube",
                "uuid": u, "from": frm, "to": to, "origin": [0, 0, 0],
                "color": BB_SLOT_COLOR.get(slot, 0), "faces": faces,
            })
            kids.append(u)
        outliner.append({"name": pname, "uuid": new_uuid("grp_" + pname),
                         "origin": [0, 0, 0], "color": 0, "isOpen": False, "children": kids})

    doc = {
        "meta": {"format_version": "4.5", "model_format": "free", "box_uv": False},
        "name": "RifleVoxel",
        "resolution": {"width": 16, "height": 16},
        "elements": elements,
        "outliner": outliner,
        "textures": [],
    }
    with open(path, "w", encoding="utf-8") as f:
        json.dump(doc, f, ensure_ascii=False, indent=1)
    return len(elements), len(outliner)


# 미리보기 전용 색(§4-1 실제 색이 아니다 — UE 절차적 MI 가 정한다). 슬롯 구분용.
PREVIEW_COLORS = {"Body": "#4A5B8C", "Grip": "#23262E", "Barrel": "#3A3E46", "Emissive": "#C8E85A"}


def write_preview_svg(path, all_cells):
    """직교 2면도(측면 X-Z, 평면 X-Y). 임포트 전에 비율·실루엣을 눈으로 보기 위한 것."""
    S = 14  # 셀당 픽셀
    xs = [c[0] for c in all_cells]; ys = [c[1] for c in all_cells]; zs = [c[2] for c in all_cells]
    x0, x1 = min(xs), max(xs); y0, y1 = min(ys), max(ys); z0, z1 = min(zs), max(zs)
    W = (x1 - x0 + 1) * S; Hs = (z1 - z0 + 1) * S; Ht = (y1 - y0 + 1) * S
    pad, gap = 24, 30
    side, top = {}, {}
    for (c, slot) in all_cells.items():
        # 측면 = Y가 작은(=보는 쪽) 셀 우선 / 평면 = Z가 큰(=위) 셀 우선
        k = (c[0], c[2])
        if k not in side or c[1] < side[k][0]:
            side[k] = (c[1], slot)
        k2 = (c[0], c[1])
        if k2 not in top or c[2] > top[k2][0]:
            top[k2] = (c[2], slot)

    parts = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
             'viewBox="0 0 %d %d">' % (W + pad*2, Hs + Ht + gap + pad*3, W + pad*2, Hs + Ht + gap + pad*3),
             '<rect width="100%%" height="100%%" fill="#0d1014"/>']
    for (x, z), (_, slot) in side.items():
        parts.append('<rect x="%d" y="%d" width="%d" height="%d" fill="%s" stroke="#0d1014" stroke-width="1"/>'
                     % (pad + (x - x0) * S, pad + (z1 - z) * S, S, S, PREVIEW_COLORS.get(slot, "#888")))
    oy = pad + Hs + gap
    for (x, y), (_, slot) in top.items():
        parts.append('<rect x="%d" y="%d" width="%d" height="%d" fill="%s" stroke="#0d1014" stroke-width="1"/>'
                     % (pad + (x - x0) * S, oy + (y - y0) * S, S, S, PREVIEW_COLORS.get(slot, "#888")))
    parts.append('<text x="%d" y="%d" fill="#7d8590" font-family="monospace" font-size="11">'
                 'SIDE  (+X forward)  %d cells = %.1fcm</text>' % (pad, pad - 8, x1 - x0 + 1, (x1 - x0 + 1) * CELL))
    parts.append('<text x="%d" y="%d" fill="#7d8590" font-family="monospace" font-size="11">TOP</text>'
                 % (pad, oy - 8))
    parts.append('</svg>')
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(parts))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join("Saved", "RifleVoxel")
    os.makedirs(out, exist_ok=True)

    manifest = {"cell_cm": CELL, "forward_axis": "+X", "parts": {}, "body_sockets": {}}
    all_cells = {}
    total_cells = total_tris = 0
    lo = [10**9] * 3
    hi = [-10**9] * 3

    for name, spec in PARTS.items():
        cells = fill(spec["boxes"])
        all_cells.update(cells)
        for (x, y, z) in cells:
            lo = [min(lo[0], x), min(lo[1], y), min(lo[2], z)]
            hi = [max(hi[0], x + 1), max(hi[1], y + 1), max(hi[2], z + 1)]
        path = os.path.join(out, "SM_RifleVoxel_%s.obj" % name)
        nv, nt, slots = write_obj(path, cells, spec["mount"], "SM_RifleVoxel_%s" % name)
        total_cells += len(cells)
        total_tris += nt
        manifest["parts"][name] = {
            "file": os.path.basename(path), "cells": len(cells), "tris": nt,
            "slots": slots, "mount_cell": spec["mount"],
            "sockets": {k: [(v[0] - spec["mount"][0]) * CELL,
                            (v[1] - spec["mount"][1]) * CELL,
                            (v[2] - spec["mount"][2]) * CELL]
                        for k, v in spec.get("sockets", {}).items()},
        }
        print("  %-10s cells=%4d  tris=%5d  slots=%s" % (name, len(cells), nt, ",".join(slots)))

    # 몸통 소켓은 몸통 원점(= Body mount, 0,0,0) 기준 cm
    for k, v in BODY_SOCKETS.items():
        manifest["body_sockets"][k] = [v[0] * CELL, v[1] * CELL, v[2] * CELL]

    dims = [(hi[i] - lo[i]) * CELL for i in range(3)]
    manifest["assembled_size_cm"] = {"length_x": dims[0], "width_y": dims[1], "height_z": dims[2]}
    with open(os.path.join(out, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
    write_preview_svg(os.path.join(out, "preview.svg"), all_cells)
    ne, ng = write_bbmodel(os.path.join(out, "RifleVoxel.bbmodel"), PARTS)
    print("bbmodel: 큐브 %d개 / 그룹 %d개 (Blockbench 1칸 = 2.5cm, Y상방)" % (ne, ng))

    print("\n조립 크기: 전장 %.1fcm(%d칸) × 폭 %.1fcm × 높이 %.1fcm"
          % (dims[0], round(dims[0] / CELL), dims[1], dims[2]))
    print("합계: 셀 %d · 삼각 %d · 파츠 %d" % (total_cells, total_tris, len(PARTS)))
    print("출력: %s" % out)


if __name__ == "__main__":
    main()
