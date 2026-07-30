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
                outs += 1
                out_len = max(out_len, len(pname))
        pin_names[k] = tuple(sorted(names))
        has_exec[k] = exec_seen
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
        "pure": pure, "in_pin_index": in_pin_index, "pin_names": pin_names,
    }


def layout_view(g):
    """배치 대상만 남긴 축약 뷰. 상태머신이면 상태 노드만 옮기고 전이 노드는 뺀다."""
    if not any(g["type"][k] == TRANSITION_TYPE for k in g["ids"]):
        return g

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


def metrics(g, pos):
    segs = [(pos[s], pos[t]) for s, _sp, t, _tp in g["edges"] if s != t]
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

    return {
        "cross": cross, "back": back, "overlap": ov,
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
