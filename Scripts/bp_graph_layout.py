# -*- coding: utf-8 -*-
"""블루프린트 그래프 배치 정리 — 노드 좌표만 바꾼다.

로직·배선·핀값은 건드리지 않는다. 변경 API는 ``BlueprintService.set_node_position`` 하나만 쓴다.

엔진이 주는 ``auto_layout_graph`` 는 쓰지 않는다. 실측(2026-07-30) 결과 노드가 많은 그래프에서는
성공을 반환하면서 노드를 하나도 옮기지 않고, 작동하는 그래프에서는 교차를 오히려 늘렸다.
(WBP_RunHUD 48노드 → 0개 이동 / WBP_BossHUDBar 교차 1 → 20)

실행:
    import sys; sys.path.insert(0, "E:/Git_Project/FPSRoguelite/Scripts")
    import bp_graph_layout as L
    L.run(L.TARGETS, apply=False)   # 먼저 비적용으로 수치만 본다
    L.run(L.TARGETS, apply=True)    # 통과한 그래프만 저장
"""

import collections
import math

import unreal

S = unreal.BlueprintService
EAL = unreal.EditorAssetLibrary

TAG = "LAY"

# --- 배치 상수 (Docs/BPGraphLayout_ResumePrompt.md 배치 규칙) ---
COL_GAP = 340.0   # 열 간격 (규칙 1: 320~360)
ROW_MIN = 150.0   # 최소 행 간격 (규칙 1: 140~180)
ROW_PAD = 48.0    # 추정 노드 높이 위에 얹는 세로 여백
BAND_GAP = 320.0  # 서로 연결되지 않은 덩어리 사이 세로 간격
PIN_H = 26.0      # 핀 한 칸 높이 추정
HEAD_H = 34.0     # 노드 머리글 높이 추정
# 엔진이 노드 실제 크기를 주지 않아 제목·핀 이름 길이로 폭을 추정한다(겹침 판정 전용 근사).
CHAR_W = 9.0
PIN_CHAR_W = 7.0
NODE_W_MIN, NODE_W_MAX, NODE_W_PAD = 150.0, 420.0, 60.0

SWEEPS = 10       # 계층 내 순서 barycenter 스윕 횟수
Y_PASSES = 6      # y 정렬 패스 횟수
PURE_PASSES = 6   # 게터 오른쪽 끌어당기기 반복 횟수

# 애님 상태머신. 전이 노드는 엔진이 두 상태를 잇는 선 위에 그리므로 좌표를 건드리지 않는다.
TRANSITION_TYPE = "AnimStateTransitionNode"
ENTRY_TYPE = "AnimStateEntryNode"
SM_COL_GAP = 460.0   # 상태 노드는 넓어서 열 간격을 더 준다
SM_ROW_GAP = 300.0


# ---------------------------------------------------------------- 그래프 읽기

def read_graph(bp, graph):
    """그래프를 좌표·간선·핀 정보로 읽어들인다. 읽기 전용."""
    ns = S.get_nodes_in_graph(bp, graph) or []
    cs = S.get_connections(bp, graph) or []
    if not ns:
        return None

    ids, pos0, title, ntype, height, width = [], {}, {}, {}, {}, {}
    in_pin_index = {}   # (node_id, pin_name) -> 입력핀 순번
    out_pin_index = {}  # (node_id, pin_name) -> 출력핀 순번
    pin_names = {}
    has_exec = {}

    for n in ns:
        k = str(n.node_id)
        ids.append(k)
        pos0[k] = (float(n.pos_x), float(n.pos_y))
        title[k] = str(n.node_title)
        ntype[k] = str(n.node_type)
        ins = outs = 0
        names = []
        exec_seen = False
        in_len = out_len = 0
        for p in n.pins:
            pname = str(p.pin_name)
            names.append(pname)
            if str(p.pin_type) == "exec":
                exec_seen = True
            if bool(p.is_input):
                in_pin_index[(k, pname)] = ins
                ins += 1
                in_len = max(in_len, len(pname))
            else:
                out_pin_index[(k, pname)] = outs
                outs += 1
                out_len = max(out_len, len(pname))
        pin_names[k] = tuple(sorted(names))
        has_exec[k] = exec_seen
        if ntype[k] == KNOT_TYPE:
            height[k] = width[k] = 24.0          # reroute는 점으로 그려진다
        else:
            height[k] = HEAD_H + PIN_H * max(1, max(ins, outs))
            title_w = CHAR_W * max(len(ln) for ln in title[k].split("\n"))
            pin_w = PIN_CHAR_W * (in_len + out_len)
            width[k] = min(NODE_W_MAX, max(NODE_W_MIN, max(title_w, pin_w) + NODE_W_PAD))

    edges = []
    for c in cs:
        edges.append((str(c.source_node_id), str(c.source_pin_name),
                      str(c.target_node_id), str(c.target_pin_name)))

    known = set(ids)
    edges = [e for e in edges if e[0] in known and e[2] in known]

    # 실행 핀이 하나도 없는 노드 = 게터/순수 노드. 소비 노드 바로 왼쪽에 붙일 대상(규칙 3·4).
    pure = set(k for k in ids if not has_exec[k])

    return {
        "bp": bp, "graph": graph, "ids": ids, "pos0": pos0, "edges": edges,
        "title": title, "type": ntype, "height": height, "width": width,
        "pure": pure, "in_pin_index": in_pin_index, "out_pin_index": out_pin_index,
        "pin_names": pin_names,
    }


def endpoints(g, pos, edge):
    """선이 실제로 그려지는 두 점 — 출발 노드의 **오른쪽 가장자리 출력핀**에서
    도착 노드의 **왼쪽 가장자리 입력핀**으로 간다. 원점끼리 이으면 판정이 어긋난다."""
    s, sp, t, tp = edge
    sx, sy = pos[s]
    tx, ty = pos[t]
    if g["type"].get(s) == KNOT_TYPE:
        a = (sx + g["width"][s], sy + g["height"][s] / 2.0)
    else:
        a = (sx + g["width"][s], sy + HEAD_H + PIN_H * (g["out_pin_index"].get((s, sp), 0) + 0.5))
    if g["type"].get(t) == KNOT_TYPE:
        b = (tx, ty + g["height"][t] / 2.0)
    else:
        b = (tx, ty + HEAD_H + PIN_H * (g["in_pin_index"].get((t, tp), 0) + 0.5))
    return a, b


def layout_view(g):
    """배치 대상만 남긴 축약 뷰.

    - 상태머신: 상태 노드만 옮기고 전이 노드는 뺀다(엔진이 선 위에 그린다).
    - reroute(knot): 계층 계산에서 빼고 간선을 관통해 접는다. 배치가 끝난 뒤
      ``reposition_knots`` 가 빈 차선으로 다시 놓는다. 손으로 놓은 reroute도 같은 규칙을 탄다.
    """
    if not any(g["type"][k] == TRANSITION_TYPE for k in g["ids"]):
        knots = set(k for k in g["ids"] if g["type"][k] == KNOT_TYPE)
        if not knots:
            return g
        v = dict(g)
        v["ids"] = [k for k in g["ids"] if k not in knots]
        v["edges"] = _fold_knots(g)
        v["pure"] = set(k for k in g["pure"] if k not in knots)
        return v

    trans = set(k for k in g["ids"] if g["type"][k] == TRANSITION_TYPE)
    states = [k for k in g["ids"] if k not in trans]

    # 전이 노드를 지나 상태끼리 직접 잇는 간선으로 접는다.
    tin, tout = {}, {}
    direct = []
    for s, _sp, t, _tp in g["edges"]:
        if t in trans and s not in trans:
            tin.setdefault(t, s)
        elif s in trans and t not in trans:
            tout.setdefault(s, t)
        elif s not in trans and t not in trans:
            direct.append((s, t))          # 진입 노드 → 첫 상태
    edges = []
    for tr in trans:
        a, b = tin.get(tr), tout.get(tr)
        if a and b:
            edges.append((a, "Out", b, "In"))
    for a, b in direct:
        edges.append((a, "Out", b, "In"))

    v = dict(g)
    v["ids"] = states
    v["edges"] = edges
    v["pure"] = set()
    v["height"] = dict((k, SM_ROW_GAP - ROW_PAD) for k in states)
    v["width"] = dict((k, SM_COL_GAP * 0.6) for k in states)
    v["is_state_machine"] = True
    return v


KNOT_TYPE = "K2Node_Knot"
KNOT_IN, KNOT_OUT = "InputPin", "OutputPin"
AUTO_TAG = "auto-reroute"


def _var_name(g, k):
    """변수 게터가 읽는 변수 이름. 게터가 아니면 None."""
    if g["type"].get(k) != "K2Node_VariableGet":
        return None
    d = S.get_node_details(g["bp"], g["graph"], k)
    return str(d.variable_name) if d else None


def _fold_knots(g):
    """knot을 관통해 (진짜 출발핀 → 도착핀)만 남긴 간선 목록."""
    knots = set(k for k in g["ids"] if g["type"].get(k) == KNOT_TYPE)
    if not knots:
        return list(g["edges"])
    src_of = {}
    for s, sp, t, _tp in g["edges"]:
        if t in knots:
            src_of[t] = (s, sp)
    out = []
    for s, sp, t, tp in g["edges"]:
        if t in knots:
            continue                      # 사슬 중간 — 도착 쪽에서 따라간다
        seen = set()
        while s in knots and s not in seen:
            seen.add(s)
            s, sp = src_of.get(s, (s, sp))
        if s in knots:
            continue                      # 출처가 끊긴 knot — 접을 수 없다
        out.append((s, sp, t, tp))
    return out


def logical_fingerprint(g):
    """게터 복제·knot 삽입에도 변하지 않아야 하는 '논리적 연결' 지문.

    - knot은 관통해서 접는다(순수 통과 노드라 의미가 없다).
    - 변수 게터는 노드 정체 대신 **읽는 변수 이름**으로 본다(복제해도 같은 것을 읽으므로).
    - 그 외 노드는 node_id로 본다(이 변환들은 그런 노드를 만들거나 지우지 않는다).
    """
    edges = _fold_knots(g)
    inbound = collections.Counter()
    for _s, _sp, t, _tp in g["edges"]:
        inbound[t] += 1
    out = []
    for s, sp, t, tp in edges:
        vn = _var_name(g, s)
        # 입력이 연결된 게터는 출처가 달라질 수 있어 변수명만으로 동일시하지 않는다
        src = ("VAR", vn, sp) if (vn is not None and inbound[s] == 0) else ("NODE", s, sp)
        out.append((src, t, tp))
    return tuple(sorted(out))


def fingerprint(g):
    """배선 지문. id 기반과 id 무관(제목 기반) 양쪽을 만든다."""
    by_id = tuple(sorted((k, g["title"][k], g["type"][k], g["pin_names"][k]) for k in g["ids"]))
    ed_id = tuple(sorted(g["edges"]))
    by_name = tuple(sorted((g["title"][k], g["type"][k], g["pin_names"][k]) for k in g["ids"]))
    ed_name = tuple(sorted((g["title"][s], sp, g["title"][t], tp) for s, sp, t, tp in g["edges"]))
    return by_id, ed_id, by_name, ed_name


# ------------------------------------------------------------------ 측정 지표

def _crosses(a, b, c, d):
    def side(p, q, r):
        v = (q[1] - p[1]) * (r[0] - q[0]) - (q[0] - p[0]) * (r[1] - q[1])
        return 0 if abs(v) < 1e-9 else (1 if v > 0 else 2)
    o1, o2, o3, o4 = side(a, b, c), side(a, b, d), side(c, d, a), side(c, d, b)
    return o1 != o2 and o3 != o4


def _seg_hits_box(a, b, x, y, w, h):
    for i in range(1, 32):
        f = i / 32.0
        if x <= a[0] + (b[0] - a[0]) * f <= x + w and y <= a[1] + (b[1] - a[1]) * f <= y + h:
            return True
    return False


def metrics(g, pos):
    live = [e for e in g["edges"] if e[0] != e[2] and e[0] in pos and e[2] in pos]
    segs = [endpoints(g, pos, e) for e in live]
    lens = [math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in segs]

    cross = 0
    for i in range(len(segs)):
        a, b = segs[i]
        for j in range(i + 1, len(segs)):
            c, d = segs[j]
            if a in (c, d) or b in (c, d):
                continue          # 같은 노드에서 갈라지는 선은 교차로 세지 않는다
            if _crosses(a, b, c, d):
                cross += 1

    back = sum(1 for a, b in segs if b[0] <= a[0])

    ks = g["ids"]
    ov = 0
    for i in range(len(ks)):
        xi, yi = pos[ks[i]]
        hi, wi = g["height"][ks[i]], g["width"][ks[i]]
        for j in range(i + 1, len(ks)):
            xj, yj = pos[ks[j]]
            hj, wj = g["height"][ks[j]], g["width"][ks[j]]
            if (xi < xj + wj and xj < xi + wi) and (yi < yj + hj and yj < yi + hi):
                ov += 1

    # 선이 다른 노드의 상자를 지나가는 수 — 2단계(reroute)의 주 지표
    over = 0
    for e, (a, b) in zip(live, segs):
        for k in ks:
            if k in (e[0], e[2]):
                continue
            if _seg_hits_box(a, b, pos[k][0], pos[k][1], g["width"][k], g["height"][k]):
                over += 1
                break

    return {
        "cross": cross, "back": back, "overlap": ov, "over": over,
        "avglen": (sum(lens) / len(lens)) if lens else 0.0,
        "maxlen": max(lens) if lens else 0.0,
        "edges": len(segs), "nodes": len(ks),
    }


# -------------------------------------------------------------------- 레이아웃

def _weak_components(ids, succ, pred):
    seen, comps = set(), []
    for start in ids:
        if start in seen:
            continue
        stack, comp = [start], []
        seen.add(start)
        while stack:
            n = stack.pop()
            comp.append(n)
            for m in succ[n] | pred[n]:
                if m not in seen:
                    seen.add(m)
                    stack.append(m)
        comps.append(comp)
    return comps


def _break_cycles(comp, succ, pred):
    """DFS로 역방향 간선을 찾는다. 간선 자체는 지우지 않고 계층 계산에서만 뺀다."""
    cs = set(comp)
    WHITE, GRAY, BLACK = 0, 1, 2
    color = dict((n, WHITE) for n in comp)
    back = set()

    def visit(root):
        stack = [(root, sorted(succ[root] & cs), 0)]
        color[root] = GRAY
        while stack:
            n, kids, i = stack[-1]
            if i < len(kids):
                stack[-1] = (n, kids, i + 1)
                v = kids[i]
                c = color.get(v, BLACK)
                if c == GRAY:
                    back.add((n, v))
                elif c == WHITE:
                    color[v] = GRAY
                    stack.append((v, sorted(succ[v] & cs), 0))
            else:
                color[n] = BLACK
                stack.pop()

    roots = sorted(n for n in comp if not (pred[n] & cs))
    for n in roots + list(comp):
        if color[n] == WHITE:
            visit(n)
    return back


def _ranks(comp, dsucc, dpred, pure):
    indeg = dict((n, len(dpred[n])) for n in comp)
    q = collections.deque(sorted(n for n in comp if indeg[n] == 0))
    topo = []
    while q:
        n = q.popleft()
        topo.append(n)
        for v in sorted(dsucc[n]):
            indeg[v] -= 1
            if indeg[v] == 0:
                q.append(v)
    for n in comp:                      # 위상정렬에서 빠진 노드가 있으면 뒤에 붙인다
        if n not in topo:
            topo.append(n)

    rank = {}
    for n in topo:
        rank[n] = max([rank[u] + 1 for u in dpred[n] if u in rank] or [0])

    # 규칙 3·4 — 게터/순수 노드를 맨 왼쪽이 아니라 소비 노드 바로 왼쪽으로 끌어당긴다.
    for _ in range(PURE_PASSES):
        changed = False
        for n in sorted(comp, key=lambda x: -rank[x]):
            if n not in pure or not dsucc[n]:
                continue
            lo = max([rank[u] for u in dpred[n]] or [-1]) + 1
            hi = min(rank[v] for v in dsucc[n]) - 1
            want = hi if hi >= lo else lo
            if want != rank[n]:
                rank[n] = want
                changed = True
        if not changed:
            break

    lo = min(rank.values())
    return dict((n, rank[n] - lo) for n in comp)


def _median(vals, fallback):
    if not vals:
        return fallback
    vals = sorted(vals)
    m = len(vals) // 2
    return vals[m] if len(vals) % 2 else (vals[m - 1] + vals[m]) / 2.0


def _order_layers(layers, dsucc, dpred, g):
    """계층 내 순서를 barycenter 스윕으로 정하고, 마지막에 입력핀 순서를 강제한다(규칙 5)."""
    rs = sorted(layers)
    for it in range(SWEEPS):
        idx = {}
        for r in rs:
            for i, n in enumerate(layers[r]):
                idx[n] = i
        seq = rs[1:] if it % 2 == 0 else list(reversed(rs[:-1]))
        for r in seq:
            nbr = dpred if it % 2 == 0 else dsucc
            keyed = [(_median([idx[m] for m in nbr[n] if m in idx], idx[n]), idx[n], n)
                     for n in layers[r]]
            keyed.sort()
            layers[r] = [n for _k, _i, n in keyed]
            for i, n in enumerate(layers[r]):
                idx[n] = i

    # 규칙 5: 여러 입력을 받는 노드(분기·블렌드)의 선행 노드들이 같은 계층에 있으면
    #         위→아래 순서를 입력핀 순서와 맞춘다.
    tgt_pin = {}
    for s, _sp, t, tp in g["edges"]:
        if s != t:
            tgt_pin.setdefault((t, s), g["in_pin_index"].get((t, tp), 99))
    for r in rs:
        order = set(layers[r])
        place = dict((n, i) for i, n in enumerate(layers[r]))
        for t in set(g["ids"]):
            group = [n for n in layers[r] if (t, n) in tgt_pin]
            if len(group) < 2:
                continue
            slots = sorted(place[n] for n in group)
            for n, slot in zip(sorted(group, key=lambda m: tgt_pin[(t, m)]), slots):
                place[n] = slot
        layers[r] = [n for n, _i in sorted(place.items(), key=lambda kv: kv[1]) if n in order]
    return layers


def _place_column(order, desired, gaps):
    ys = list(desired)
    for i in range(1, len(ys)):
        ys[i] = max(ys[i], ys[i - 1] + gaps[i - 1])
    for i in range(len(ys) - 2, -1, -1):
        ys[i] = min(ys[i], ys[i + 1] - gaps[i])
    for i in range(1, len(ys)):
        ys[i] = max(ys[i], ys[i - 1] + gaps[i - 1])
    return ys


_INIT_KEYS = (
    lambda g, n: (g["pos0"][n][1], g["pos0"][n][0], n),   # 원래 y 순 (저작 의도 보존)
    lambda g, n: (g["pos0"][n][0], g["pos0"][n][1], n),   # 원래 x 순
    lambda g, n: (-g["pos0"][n][1], g["pos0"][n][0], n),  # 원래 y 역순
    lambda g, n: (n,),                                     # id 순 (중립)
)


def _layout_component(comp, dsucc, dpred, g, variant=0):
    col_gap = SM_COL_GAP if g.get("is_state_machine") else COL_GAP
    rank = _ranks(comp, dsucc, dpred, g["pure"])
    layers = collections.defaultdict(list)
    for n in comp:
        layers[rank[n]].append(n)
    init = _INIT_KEYS[variant % len(_INIT_KEYS)]
    for r in layers:
        layers[r].sort(key=lambda n: init(g, n))

    layers = _order_layers(layers, dsucc, dpred, g)

    gap_of = lambda n: max(g["height"][n] + ROW_PAD, ROW_MIN)

    y = {}
    for r in sorted(layers):
        cur = 0.0
        for n in layers[r]:
            y[n] = cur
            cur += gap_of(n)

    # 연결된 이웃 쪽으로 끌어당겨 선을 곧게 편다.
    for p in range(Y_PASSES):
        rs = sorted(layers)
        seq = rs if p % 2 == 0 else list(reversed(rs))
        nbr = dpred if p % 2 == 0 else dsucc
        for r in seq:
            order = layers[r]
            desired = [_median([y[m] for m in nbr[n] if m in y], y[n]) for n in order]
            gaps = [gap_of(n) for n in order]
            for n, ny in zip(order, _place_column(order, desired, gaps)):
                y[n] = ny

    # 규칙 3·4 — 게터를 소비 노드의 해당 입력핀 높이에 맞춘다.
    snap = {}
    for n in comp:
        if n not in g["pure"]:
            continue
        tgts = []
        for s, _sp, t, tp in g["edges"]:
            if s == n and t in y and t != n:
                tgts.append(y[t] + HEAD_H + PIN_H * g["in_pin_index"].get((t, tp), 0))
        if tgts:
            snap[n] = sum(tgts) / float(len(tgts))
    if snap:
        for n, v in snap.items():
            y[n] = v
        for r in sorted(layers):
            order = sorted(layers[r], key=lambda n: (y[n], n))
            layers[r] = order
            gaps = [gap_of(n) for n in order]
            for n, ny in zip(order, _place_column(order, [y[n] for n in order], gaps)):
                y[n] = ny

    # 열 x는 그 열에서 가장 넓은 노드에 맞춰 누적한다(폭 넓은 노드가 옆 열을 침범하지 않게).
    xs, cur = {}, 0.0
    for r in sorted(layers):
        xs[r] = cur
        cur += max([g["width"][n] for n in layers[r]] or [0.0]) + col_gap * 0.45
    pos = dict((n, (xs[rank[n]], y[n])) for n in comp)
    ys = [y[n] for n in comp]
    span = (max(ys) + max(g["height"][n] for n in comp)) - min(ys)
    base = min(ys)
    return dict((n, (x, yy - base)) for n, (x, yy) in pos.items()), span


def compute_layout(g, variant=0):
    succ = collections.defaultdict(set)
    pred = collections.defaultdict(set)
    for s, _sp, t, _tp in g["edges"]:
        if s != t:
            succ[s].add(t)
            pred[t].add(s)

    comps = _weak_components(g["ids"], succ, pred)
    linked = [c for c in comps if len(c) > 1]
    loose = [c[0] for c in comps if len(c) == 1]
    linked.sort(key=len, reverse=True)

    out = {}
    band = 0.0
    for comp in linked:
        cs = set(comp)
        back = _break_cycles(comp, succ, pred)
        dsucc = collections.defaultdict(set)
        dpred = collections.defaultdict(set)
        for u in comp:
            for v in succ[u] & cs:
                if (u, v) in back:
                    continue
                dsucc[u].add(v)
                dpred[v].add(u)
        cpos, span = _layout_component(comp, dsucc, dpred, g, variant)
        for k, (x, yy) in cpos.items():
            out[k] = (x, band + yy)
        band += span + BAND_GAP

    # 어디에도 연결되지 않은 노드는 맨 아래 한 줄로 몰아둔다.
    for i, n in enumerate(sorted(loose, key=lambda m: (g["pos0"][m][1], g["pos0"][m][0]))):
        out[n] = ((i % 4) * COL_GAP, band + (i // 4) * ROW_MIN * 1.6)

    return out


def layout_state_machine(g):
    """상태머신용 방사형 후보. 가장 많은 전이가 걸린 상태를 가운데 두고 나머지를 원주에 돌린다.

    ``best_layout`` 이 계층 배치 후보와 함께 재보고 점수가 낮은 쪽을 고른다.
    "상태 전이는 양방향이라 방사형이 낫다"는 짐작으로 넣었지만, Blu 로코모션 2개에서는
    실측상 계층 배치가 이겼다(방사형 22.7 / 계층 12.8 / 원본 손저작 방사형 17.3 —
    허브를 안 거치는 전이가 스포크를 가로질러 교차가 12개까지 늘어난다).
    상태 수가 많거나 허브가 뚜렷한 다른 상태머신에서는 이길 수 있어 후보로 남겨둔다.
    """
    entry = [k for k in g["ids"] if g["type"][k] == ENTRY_TYPE]
    states = [k for k in g["ids"] if k not in entry]
    if not states:
        return None

    deg = collections.Counter()
    for s, _sp, t, _tp in g["edges"]:
        if s != t:
            deg[s] += 1
            deg[t] += 1
    hub = max(states, key=lambda k: (deg[k], k))
    others = [k for k in states if k != hub]

    cx, cy = g["pos0"][hub]
    base = {}
    for i, k in enumerate(others):
        dx, dy = g["pos0"][k][0] - cx, g["pos0"][k][1] - cy
        base[k] = math.atan2(dy, dx) if (dx or dy) else (2 * math.pi * i / max(1, len(others)))
    others.sort(key=lambda k: (base[k], k))

    step = SM_ROW_GAP + 60.0
    radius = max(520.0, len(others) * step / (2 * math.pi))
    pos = {hub: (0.0, 0.0)}
    for i, k in enumerate(others):
        a = 2 * math.pi * i / len(others) + math.pi / 2   # 첫 상태를 아래쪽에서 시작
        pos[k] = (radius * math.cos(a), radius * math.sin(a))

    # 진입 노드는 원 바깥 왼쪽에 둔다(원주 위 상태와 겹치지 않게).
    for e in entry:
        tgt = [t for s, _sp, t, _tp in g["edges"] if s == e and t in pos]
        _ax, ay = pos[tgt[0]] if tgt else (0.0, 0.0)
        pos[e] = (-(radius + SM_COL_GAP), ay)
    return pos


def best_layout(g):
    """후보를 여러 개 만들어 교차가 가장 적은 것을 고른다."""
    cands = []
    for v in range(len(_INIT_KEYS)):
        try:
            cands.append(compute_layout(g, v))
        except Exception as exc:                       # 한 후보가 실패해도 나머지로 진행
            print("%s  variant %d failed on %s/%s: %s" % (TAG, v, g["bp"], g["graph"], exc))
    if g.get("is_state_machine"):
        radial = layout_state_machine(g)
        if radial:
            cands.insert(0, radial)

    best, best_key, best_m = None, None, None
    for pos in cands:
        for k in g["ids"]:
            pos.setdefault(k, g["pos0"][k])
        m = metrics(g, pos)
        key = (_score(m, g.get("is_state_machine")), m["avglen"])
        if best_key is None or key < best_key:
            best, best_key, best_m = pos, key, m
    return best, best_m


# ------------------------------------------------------------------ 적용/검증

def _relink(bp, graph, edges_at_pin, target, pin, new_source):
    """도착 핀의 링크 하나만 갈아끼운다.

    ``disconnect_pin`` 은 그 핀의 링크를 **전부** 끊는다(실행 입력핀은 여러 개가 들어올 수 있다).
    그래서 원래 걸려 있던 링크를 모두 기억했다가, 바꿀 하나만 빼고 되살린다.
    """
    if not S.disconnect_pin(bp, graph, target, pin):
        return False
    ok = True
    # 바꿀 대상(old)을 제외한 나머지를 원래대로 복구
    old_s, old_sp, new_s, new_sp = new_source
    for s, sp in edges_at_pin:
        if (s, sp) == (old_s, old_sp):
            continue
        ok = S.connect_nodes(bp, graph, s, sp, target, pin) and ok
    ok = S.connect_nodes(bp, graph, new_s, new_sp, target, pin) and ok
    return ok


def _links_into(g, target, pin):
    return [(s, sp) for s, sp, t, tp in g["edges"] if t == target and tp == pin]


def duplicate_getters(bp, graph, g):
    """여러 곳으로 나가는 순수 변수 게터를 소비처마다 하나씩으로 쪼갠다 (질문 ③).

    순수 변수 게터는 UE가 컴파일할 때 어차피 소비처마다 다시 평가하므로, 복제해도
    의미·성능이 동일하다. 첫 소비처는 원본을 그대로 두고 나머지만 새로 만든다.
    """
    outgoing = collections.defaultdict(list)
    for s, sp, t, tp in g["edges"]:
        if g["type"].get(s) == "K2Node_VariableGet" and s != t:
            outgoing[s].append((sp, t, tp))

    made = 0
    for src, cons in sorted(outgoing.items()):
        if len(cons) < 2:
            continue
        vname = _var_name(g, src)
        if not vname:
            continue
        # 입력이 연결된 게터 = 다른 객체의 멤버를 읽는 것. `add_get_variable_node` 는
        # 이 블루프린트 자신의 변수만 만들 수 있어 복제할 수 없다(실측: WBP_RunHUD/MissionWindows).
        if any(t == src for _s, _sp, t, _tp in g["edges"]):
            continue
        for sp, t, tp in sorted(cons)[1:]:
            nid = S.add_get_variable_node(bp, graph, vname, g["pos0"][src][0], g["pos0"][src][1])
            if not nid:
                print("%s   게터 복제 실패: %s %s/%s" % (TAG, vname, bp.split("/")[-1], graph))
                continue
            if _relink(bp, graph, _links_into(g, t, tp), t, tp, (src, sp, nid, sp)):
                made += 1
            else:
                S.delete_node(bp, graph, nid)
    return made


def knot_chains(g):
    """(진짜 출발노드, [knot…], 진짜 도착노드) 목록. 손으로 놓은 것도 함께 잡힌다."""
    knots = set(k for k in g["ids"] if g["type"].get(k) == KNOT_TYPE)
    if not knots:
        return []
    src_of, nxt = {}, collections.defaultdict(list)
    for s, _sp, t, _tp in g["edges"]:
        if t in knots:
            src_of[t] = s
        if s in knots:
            nxt[s].append(t)
    chains = []
    for k in sorted(knots):
        if src_of.get(k) in knots or k not in src_of:
            continue                                   # 사슬의 시작만 잡는다
        chain, cur, guard = [k], k, 0
        while guard < 32:
            guard += 1
            step = [x for x in nxt.get(cur, []) if x in knots]
            if not step:
                break
            cur = step[0]
            chain.append(cur)
        ends = [x for x in nxt.get(chain[-1], []) if x not in knots]
        if ends:
            chains.append((src_of[k], chain, ends[0]))
    return chains


def _collapse_knots(bp, graph, g, victims):
    """지정한 knot들을 걷어내고 관통 연결을 되살린다."""
    victims = set(victims)
    folded = _fold_knots(g)
    for k in victims:
        S.delete_node(bp, graph, k)
    cur = read_graph(bp, graph)
    have = set(cur["edges"])
    for s, sp, t, tp in folded:
        if s in victims or t in victims:
            continue
        if (s, sp, t, tp) not in have:
            S.connect_nodes(bp, graph, s, sp, t, tp)
    return len(victims)


def reposition_knots(bp, graph, g):
    """reroute를 출발·도착 사이의 빈 가로 차선에 다시 놓는다."""
    moved = 0
    for src, chain, dst in knot_chains(g):
        if src not in g["pos0"] or dst not in g["pos0"]:
            continue
        x1 = g["pos0"][src][0] + g["width"][src] + LANE_PAD
        x2 = g["pos0"][dst][0] - LANE_PAD
        if x2 <= x1:
            x1, x2 = sorted((x1, x2))
            x2 = x1 + LANE_PAD
        lane = _find_lane(g, x1, x2, (g["pos0"][src][1] + g["pos0"][dst][1]) / 2.0, skip=(src, dst))
        if lane is None:
            continue
        n = len(chain)
        for i, k in enumerate(chain):
            x = x1 if n == 1 else x1 + (x2 - x1) * i / float(n - 1)
            if S.set_node_position(bp, graph, k, float(x), float(lane)):
                moved += 1
    return moved


LANE_PAD = 70.0     # 차선이 노드 위아래로 띄우는 여백
LANE_HALF = 18.0    # 선이 지나가는 띠의 반두께


def _find_lane(g, x_lo, x_hi, want_y, skip):
    """x 구간 [x_lo, x_hi]를 가로지르는 동안 어떤 노드 상자도 안 건드리는 가로 차선의 y.

    노드 위/아래, 그리고 노드 띠 사이의 틈을 후보로 두고 원래 선 높이에 가장 가까운 것을 고른다.
    없으면 None.
    """
    pos, W, H = g["pos0"], g["width"], g["height"]
    band = [k for k in g["ids"]
            if k not in skip and pos[k][0] < x_hi and pos[k][0] + W[k] > x_lo]
    if not band:
        return want_y
    boxes = sorted((pos[k][1], pos[k][1] + H[k]) for k in band)
    cands = [boxes[0][0] - LANE_PAD]
    for i in range(len(boxes) - 1):
        lo, hi = boxes[i][1], boxes[i + 1][0]
        if hi - lo > 2 * LANE_HALF + 20.0:
            cands.append((lo + hi) / 2.0)
    cands.append(max(b[1] for b in boxes) + LANE_PAD)

    def clear(y):
        return all(not (lo < y + LANE_HALF and y - LANE_HALF < hi) for lo, hi in boxes)

    ok = [y for y in cands if clear(y)]
    return min(ok, key=lambda y: abs(y - want_y)) if ok else None


def insert_reroutes(bp, graph, g, limit_ratio=1.5):
    """노드 위를 지나가는 선에 reroute(knot)를 넣어 돌린다 (질문 ②).

    열을 건너뛰기만 하는 선은 놔두고, **실제로 노드 상자를 지나가는 선**에만 넣는다.
    """
    pos, W = g["pos0"], g["width"]

    def blocked_by(edge):
        a, b = endpoints(g, pos, edge)
        for k in g["ids"]:
            if k in (edge[0], edge[2]):
                continue
            if _seg_hits_box(a, b, pos[k][0], pos[k][1], W[k], g["height"][k]):
                return True
        return False

    # 이미 reroute를 지나는 선은 건드리지 않는다 → 다시 돌려도 knot이 쌓이지 않는다(멱등).
    knots = set(k for k in g["ids"] if g["type"].get(k) == KNOT_TYPE)
    jobs = []
    for s, sp, t, tp in g["edges"]:
        if s in knots or t in knots:
            continue
        if s != t and s in pos and t in pos and blocked_by((s, sp, t, tp)):
            jobs.append((-abs(pos[t][0] - pos[s][0]), s, sp, t, tp))
    jobs.sort()

    cap = int(len(g["ids"]) * limit_ratio)
    made, skipped, nolane = 0, 0, 0
    for _neg, s, sp, t, tp in jobs:
        if made + 2 > cap:
            skipped += 1
            continue
        # 출발 상자 오른쪽 ~ 도착 상자 왼쪽 사이를 가로지를 빈 차선을 찾는다
        x1 = pos[s][0] + W[s] + LANE_PAD
        x2 = pos[t][0] - LANE_PAD
        if x2 - x1 < LANE_PAD:
            nolane += 1
            continue
        lane = _find_lane(g, x1, x2, (pos[s][1] + pos[t][1]) / 2.0, skip=(s, t))
        if lane is None:
            nolane += 1
            continue

        chain = []
        for kx in (x1, x2):
            nid = S.create_node_by_key(bp, graph, "NODE " + KNOT_TYPE, float(kx), float(lane))
            if not nid:
                break
            S.configure_node(bp, graph, nid, "NodeComment", AUTO_TAG)
            chain.append(nid)
        if len(chain) != 2:
            for nid in chain:
                S.delete_node(bp, graph, nid)
            continue

        prev, prev_pin = s, sp
        for nid in chain:
            S.connect_nodes(bp, graph, prev, prev_pin, nid, KNOT_IN)
            prev, prev_pin = nid, KNOT_OUT
        if _relink(bp, graph, _links_into(g, t, tp), t, tp, (s, sp, prev, prev_pin)):
            made += 2
        else:
            for nid in chain:
                S.delete_node(bp, graph, nid)
    if skipped or nolane:
        print("%s   %s/%s: knot 미삽입 — 상한(%d) %d개 / 빈 차선 없음 %d개"
              % (TAG, bp.split("/")[-1], graph, cap, skipped, nolane))
    return made


def _apply(bp, graph, pos):
    ok = True
    for k, (x, y) in pos.items():
        if not S.set_node_position(bp, graph, k, float(x), float(y)):
            ok = False
    return ok


def _score(m, is_sm=False):
    """읽기 어려움 점수. 낮을수록 좋다.

    노드가 서로 겹치면 내용 자체가 가려지므로 선 교차보다 3배 무겁게 본다.
    선이 되짚어 올라가는 것(역방향)은 보기 나쁘지만 내용을 가리진 않아 절반으로 본다.
    상태머신은 전이선 자체가 내용이라 선 길이도 넣는다(100px당 1점).
    """
    s = 3.0 * m["overlap"] + 1.0 * m["cross"] + 0.5 * m["back"]
    return s + (m["avglen"] / 100.0 if is_sm else 0.0)


def process_graph(bp, graph, apply=True, verbose=True):
    """그래프 하나를 정리한다. 배선이 달라지거나 교차가 줄지 않으면 원좌표로 되돌린다."""
    g0 = read_graph(bp, graph)
    if g0 is None:
        return {"bp": bp, "graph": graph, "status": "empty"}
    if not g0["edges"]:
        return {"bp": bp, "graph": graph, "status": "no-edges"}

    fp0 = fingerprint(g0)                  # 지문은 언제나 그래프 전체로 대조한다
    view = layout_view(g0)                 # 배치·측정은 옮길 노드만 놓고 본다
    if not view["edges"]:
        return {"bp": bp, "graph": graph, "status": "no-edges"}

    m0 = metrics(view, view["pos0"])
    new, m1 = best_layout(view)
    if new is None:
        return {"bp": bp, "graph": graph, "status": "layout-failed"}

    rec = {"bp": bp, "graph": graph, "before": m0, "after": m1, "status": "?",
           "is_sm": bool(view.get("is_state_machine"))}
    if rec["is_sm"]:
        rec["graph"] = graph + " (상태머신)"

    is_sm = bool(view.get("is_state_machine"))
    if _score(m1, is_sm) >= _score(m0, is_sm):
        rec["status"] = "keep-original"
        if verbose:
            _log(rec)
        return rec

    if not apply:
        rec["status"] = "would-apply"
        if verbose:
            _log(rec)
        return rec

    _apply(bp, graph, new)

    g1 = read_graph(bp, graph)
    fp1 = fingerprint(g1)
    if fp0 != fp1:
        _apply(bp, graph, g0["pos0"])
        rec["status"] = "REVERTED-wiring-changed"
        rec["detail"] = _fp_diff(fp0, fp1)
        if verbose:
            _log(rec)
        return rec

    rec["status"] = "applied"
    v1 = layout_view(g1)
    rec["after"] = metrics(v1, v1["pos0"])   # 에디터에 실제로 반영된 좌표로 다시 잰다
    if verbose:
        _log(rec)
    return rec


def _score2(m):
    """2단계 판정 점수. 낮을수록 좋다.

    선이 노드를 지나가면 그 노드 이름이 가려지므로 교차보다 2배로 본다.
    노드끼리 겹치는 건 1단계와 같이 3배.
    """
    return 3.0 * m["overlap"] + 2.0 * m["over"] + 1.0 * m["cross"] + 0.5 * m["back"]


def enhance_graph(bp, graph, apply=True, verbose=True, do_getters=True, do_reroutes=True):
    """게터 복제 + reroute 삽입까지 하는 2단계 처리.

    논리적 연결(``logical_fingerprint``)이 달라지면 그 그래프를 통째로 되돌린다.
    좌표만 바꾸는 1단계는 ``process_graph`` 를 쓴다.
    """
    g0 = read_graph(bp, graph)
    if g0 is None or not g0["edges"]:
        return {"bp": bp, "graph": graph, "status": "no-edges"}
    if any(g0["type"].get(k) == TRANSITION_TYPE for k in g0["ids"]):
        return {"bp": bp, "graph": graph, "status": "skip-state-machine"}

    lf0 = logical_fingerprint(g0)
    m0 = metrics(g0, g0["pos0"])
    rec = {"bp": bp, "graph": graph, "before": m0, "status": "?", "getters": 0, "knots": 0}
    if not apply:
        rec["status"] = "dry"
        rec["after"] = m0
        if verbose:
            _log2(rec)
        return rec

    undo_pos = dict(g0["pos0"])
    try:
        # --- ① 게터 복제 + 배치. 나빠지면 통째로 되돌린다 ---
        if do_getters:
            rec["getters"] = duplicate_getters(bp, graph, read_graph(bp, graph))
        cur = read_graph(bp, graph)
        new, _m = best_layout(layout_view(cur))   # reroute는 접어서 계층 계산에서 뺀다
        if new:
            _apply(bp, graph, new)
        reposition_knots(bp, graph, read_graph(bp, graph))

        stage1 = read_graph(bp, graph)
        m1 = metrics(stage1, stage1["pos0"])
        if _score2(m1) > _score2(m0):
            _revert(bp, graph, g0, stage1, undo_pos)
            rec["status"] = "keep-original"
            rec["getters"] = 0
            rec["after"] = metrics(read_graph(bp, graph), undo_pos)
            if verbose:
                _log2(rec)
            return rec

        # --- ② reroute. 나빠지면 방금 넣은 knot만 걷어낸다 ---
        if do_reroutes:
            n = insert_reroutes(bp, graph, stage1)
            post = read_graph(bp, graph)
            m2 = metrics(post, post["pos0"])
            if n and _score2(m2) >= _score2(m1):
                added = [k for k in post["ids"] if k not in stage1["pos0"]]
                _collapse_knots(bp, graph, post, added)
                rec["knots"] = 0
                rec["detail"] = "reroute 되돌림(선위 %d→%d 교차 %d→%d 겹침 %d→%d)" % (
                    m1["over"], m2["over"], m1["cross"], m2["cross"], m1["overlap"], m2["overlap"])
            else:
                rec["knots"] = n

        g1 = read_graph(bp, graph)
        lf1 = logical_fingerprint(g1)
        if lf0 != lf1:
            rec["status"] = "REVERTED-logic-changed"
            rec["detail"] = "논리연결 +%d -%d" % (len(set(lf1) - set(lf0)), len(set(lf0) - set(lf1)))
            _revert(bp, graph, g0, g1, undo_pos)
            rec["after"] = metrics(read_graph(bp, graph), undo_pos)
        else:
            rec["status"] = "enhanced"
            rec["after"] = metrics(g1, g1["pos0"])
    except Exception as exc:
        rec["status"] = "ERROR"
        rec["detail"] = str(exc)[:160]
        rec["after"] = m0
    if verbose:
        _log2(rec)
    return rec


def _revert(bp, graph, g0, g1, undo_pos):
    """추가된 노드를 지우고 원래 연결·좌표로 되돌린다."""
    added = [k for k in g1["ids"] if k not in g0["pos0"]]
    for k in added:
        S.delete_node(bp, graph, k)
    cur = read_graph(bp, graph)
    have = set(cur["edges"])
    for s, sp, t, tp in g0["edges"]:
        if (s, sp, t, tp) not in have:
            S.connect_nodes(bp, graph, s, sp, t, tp)
    for k, (x, y) in undo_pos.items():
        S.set_node_position(bp, graph, k, x, y)


def _log2(rec):
    b, a = rec["before"], rec.get("after", rec["before"])
    print("%s %-24s|%-26s n=%3d->%3d  선위노드 %3d->%3d  교차 %3d->%3d  겹침 %2d->%2d  게터+%d knot+%d  %s%s"
          % (TAG, rec["bp"].split("/")[-1], rec["graph"], b["nodes"], a["nodes"],
             b["over"], a["over"], b["cross"], a["cross"], b["overlap"], a["overlap"],
             rec.get("getters", 0), rec.get("knots", 0), rec["status"],
             (" | " + rec["detail"]) if rec.get("detail") else ""))


def _fp_diff(a, b):
    names = ("nodes-by-id", "edges-by-id", "nodes-by-name", "edges-by-name")
    out = []
    for nm, x, y in zip(names, a, b):
        if x != y:
            sx, sy = set(x), set(y)
            out.append("%s: +%d -%d" % (nm, len(sy - sx), len(sx - sy)))
    return "; ".join(out)


def _log(rec):
    b, a = rec.get("before"), rec.get("after")
    if not b:
        print("%s %-52s %-26s %s" % (TAG, rec["bp"].split("/")[-1], rec["graph"], rec["status"]))
        return
    print("%s %-26s|%-26s n=%3d e=%3d  cross %3d->%3d  back %2d->%2d  ovl %3d->%3d  avg %5.0f->%5.0f  %s%s"
          % (TAG, rec["bp"].split("/")[-1], rec["graph"], b["nodes"], b["edges"],
             b["cross"], a["cross"], b["back"], a["back"], b["overlap"], a["overlap"],
             b["avglen"], a["avglen"], rec["status"],
             (" | " + rec["detail"]) if rec.get("detail") else ""))


# --------------------------------------------------------------------- 대상/실행

def graphs_of(bp, extra=()):
    """정리 대상 그래프 이름 목록. 이름이 겹치는 그래프는 개별 주소 지정이 안 되므로 뺀다."""
    names, seen = [], collections.Counter()
    for cand in ["EventGraph"] + list(extra):
        if cand not in names:
            names.append(cand)
    try:
        for f in (S.list_functions(bp) or []):
            fn = str(f.function_name)
            if fn not in names and fn != "UserConstructionScript":
                names.append(fn)
    except Exception:
        pass
    out = []
    for n in names:
        try:
            if S.get_nodes_in_graph(bp, n):
                out.append(n)
        except Exception:
            pass
    return out


TARGETS = [
    # (에셋 경로, 추가 그래프 이름)
    ("/Game/UI/HUD/WBP_RunHUD", ()),
    ("/Game/UI/HUD/WBP_ThreatIndicator", ()),
    ("/Game/UI/HUD/WBP_HitMarker", ()),
    ("/Game/UI/HUD/WBP_DownedOverlay", ()),
    ("/Game/UI/HUD/WBP_EnemyHealthBar", ()),
    ("/Game/UI/HUD/WBP_BossHUDBar", ()),
    ("/Game/UI/HUD/WBP_BasicCrosshair", ()),
    ("/Game/UI/HUD/WBP_MissionBanner", ()),
    ("/Game/UI/HUD/WBP_DamageNumber", ()),
    ("/Game/UI/Widget/WBP_Lobby", ()),
    ("/Game/UI/Widget/WBP_LoadoutEntry", ()),
    ("/Game/UI/Menu/WBP_Result", ()),
    ("/Game/Character/Boss/WBP_BossHealthBar", ()),
    ("/Game/Character/Player/BP_LobbyDisplayPawn", ()),
    ("/Game/Character/Boss/BP_Boss", ()),
    ("/Game/Character/Enemy/BP_EnemyMeleeBase", ()),
    ("/Game/Actors/BP_Door", ()),
    ("/Game/Cards/Character/GA_Lifesteal", ()),
    ("/Game/Characters/Blu/Anims/ABL_Blu_W2_Rifle",
     ("AnimGraph", "Locomotion", "Ground", "ApplyWeaponAimOffset", "GetWeaponLocomotionPose")),
    ("/Game/Characters/Blu/Anims/ABP_Blu_Body",
     ("AnimGraph", "Locomotion2")),
]


def save_asset(bp):
    """컨테이너 위젯을 안전하게 저장한다.

    ``compile_blueprint`` 는 부르지 않는다. 노드 좌표는 컴파일 결과에 들어가지 않아 재컴파일이
    필요 없고, 자식 WBP를 품은 위젯을 프로그래매틱으로 컴파일하면 재인스턴싱 중 에디터가
    죽은 전례가 있다(2026-06-24 WBP_GameHUD/WBP_RunHUD). 저장도 모달을 띄우는
    ``save_asset`` 대신 ``save_packages`` 를 쓴다.
    """
    obj = EAL.load_asset(bp)
    if obj is None:
        return False
    pkg = obj.get_outermost()
    return bool(unreal.EditorLoadingAndSavingUtils.save_packages([pkg], False))


def run(targets, apply=False, save=True):
    aes = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
    results = []
    for bp, extra in targets:
        if apply:
            obj = EAL.load_asset(bp)
            if obj is not None:
                aes.close_all_editors_for_asset(obj)   # 열린 편집 탭이 덮어쓰지 않게
        recs = [process_graph(bp, gname, apply=apply) for gname in graphs_of(bp, extra)]
        results.extend(recs)
        if apply and save and any(r["status"] == "applied" for r in recs):
            print("%s SAVED %s -> %s" % (TAG, bp, save_asset(bp)))
    _summary(results)
    return results


def _summary(results):
    st = collections.Counter(r["status"] for r in results)
    print("%s ==== 요약 ====" % TAG)
    for k, v in sorted(st.items()):
        print("%s   %-26s %d" % (TAG, k, v))
    done = [r for r in results if r["status"] in ("applied", "would-apply")]
    if done:
        bc = sum(r["before"]["cross"] for r in done)
        ac = sum(r["after"]["cross"] for r in done)
        bo = sum(r["before"]["overlap"] for r in done)
        ao = sum(r["after"]["overlap"] for r in done)
        bb = sum(r["before"]["back"] for r in done)
        ab = sum(r["after"]["back"] for r in done)
        bs = sum(_score(r["before"], r.get("is_sm")) for r in done)
        as_ = sum(_score(r["after"], r.get("is_sm")) for r in done)
        print("%s   교차 합계 %d -> %d / 겹침 %d -> %d / 역방향 %d -> %d / 점수 %.0f -> %.0f (그래프 %d개)"
              % (TAG, bc, ac, bo, ao, bb, ab, bs, as_, len(done)))
