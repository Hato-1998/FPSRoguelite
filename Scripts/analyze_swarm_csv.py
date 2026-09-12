# analyze_swarm_csv.py — VAT-1 측정 CSV 분석 (Docs/Review/20260812-plan-vat1-swarm-render-path.md 프로토콜)
# UE CsvProfiler 포맷 실물 대응:
#   - 진짜 헤더는 파일 '끝'(마지막 줄 = [HasHeaderRowAtEnd] 메타데이터, 그 앞 줄 = 전체 컬럼명)
#   - 컬럼은 캡처 중 append-only로 늘어남 → 행마다 열 수가 다름(왼쪽 정렬, 부족분 = 결측)
#   - EVENTS 컬럼은 문자열 — 숫자 변환 금지
# GASM1(Docs/Specs/GASM1_SwarmASCCostMeasurement.md §12-A) 확장:
#   - WANTED 4종 추가 — Excl/TickActors(1차 판정축) · Excl/AbilityTasks(보조) ·
#     PhysicalUsedMB/MemoryFreeMB(참고치 — obj list 는 객체 본체를 안 세므로 1차 판정에는 안 쓴다)
#   - MB 컬럼은 ms 포맷이 아니라 소수 1자리 MB 로 찍는다(MB_LABELS, COUNT_LABELS 와 별도 포맷)
#   - --baseline <csv> 로 기준 캡처를 같은 방식으로 분석해 항목별 절대값 두 줄 + 델타 한 줄을 출력한다
#     (①↔②, ②↔③ 을 손으로 빼지 않기 위함). 지정하지 않으면 기존 출력과 동일(신규 컬럼 4줄만 추가)
#   - 🔁 G2 P3 후속수정: 델타 줄에 노이즈 플로어(baseline 자체의 P95-P50 폭)를 병기한다 — 같은 구성도
#     실행마다 GameThread 가 15% 흔들린다는 실측(Performance.md:93)이 있어 ①↔②의 0.1~0.3ms 델타가 그
#     노이즈와 구분되지 않는다. 델타(avg)가 그 폭보다 작으면 "(~noise)"를 붙인다. §5 판정선은 절대값
#     기준이라 합/불 자체는 안 바뀐다 — 노이즈를 신호로 오독하는 것만 막는 목적(수치는 계속 다 찍는다).
# 사용: python Scripts/analyze_swarm_csv.py <capture.csv> [--skip-seconds 10] [--baseline <baseline.csv>]
import csv, sys, statistics

WANTED = [
    ("FrameTime", "FrameTime"),
    ("GameThread", "GameThreadTime"),
    ("RenderThread", "RenderThreadTime"),
    ("RHIThread", "RHIThreadTime"),
    ("GPUTime", "GPUTime"),
    ("GPU/BasePass", "GPU/BasePass"),
    ("GPU/ShadowDepths", "GPU/ShadowDepths"),
    ("GPU/CustomDepth", "GPU/CustomDepth"),
    ("RHI/DrawCalls", "RHI/DrawCalls"),
    ("RHI/PrimitivesDrawn", "RHI/PrimitivesDrawn"),
    ("Excl/ServerRepActors", "Exclusive/GameThread/ServerReplicateActors"),
    ("Excl/NetworkOutgoing", "Exclusive/GameThread/NetworkOutgoing"),
    ("Excl/TickActors", "Exclusive/GameThread/TickActors"),        # GASM1 §12-A — 1차 판정축(①↔②)
    ("Excl/AbilityTasks", "Exclusive/GameThread/AbilityTasks"),    # GASM1 §12-A — 보조(②↔③)
    ("Repl/ActiveActors",    "Replication/NumberOfActiveActors"),
    ("Repl/FullyDormant",    "Replication/NumberOfFullyDormantActors"),
    ("FPSRMsg/GMSBroadcast", "FPSRMsg/GameThread/GMSBroadcast"),
    ("FPSRMsg/Broadcasts",   "FPSRMsg/Broadcasts"),
    ("FPSRMsg/Dispatches",   "FPSRMsg/Dispatches"),
    ("FPSRMsg/ListenersCopied", "FPSRMsg/ListenersCopied"),
    ("FPSREnemy/ServerAlive", "FPSREnemy/ServerAlive"),
    ("PhysicalUsedMB", "PhysicalUsedMB"),  # GASM1 §12-A — 참고치(워킹셋, 실행간 변동 15% 기록됨: Performance.md:93)
    ("MemoryFreeMB",   "MemoryFreeMB"),    # GASM1 §12-A — 참고치
]

# 카운트성 컬럼(정수 avg/P50/max 포맷) — 기존 RHI/* 2종 포함, §5-C(4) 신규 9종 중 카운트성 6종.
# 시간성(Excl/*·FPSRMsg/GMSBroadcast)은 이 집합에 넣지 않는다 — 기존 ms 포맷(avg/P50/P95) 유지.
COUNT_LABELS = {
    "RHI/DrawCalls", "RHI/PrimitivesDrawn",
    "Repl/ActiveActors", "Repl/FullyDormant",
    "FPSRMsg/Broadcasts", "FPSRMsg/Dispatches", "FPSRMsg/ListenersCopied",
    "FPSREnemy/ServerAlive",
}

# 메모리(MB) 컬럼 — COUNT_LABELS 와 같은 avg/P50/max 축이지만 정수 카운트가 아니라 소수 1자리 MB 값이라
# 포맷을 분리한다(COUNT_LABELS 의 .0f 로 찍으면 수백 MB 단위가 정수로 뭉개진다). GASM1 §12-A.
MB_LABELS = {"PhysicalUsedMB", "MemoryFreeMB"}


def load_window(path, skip_s):
    """CSV 한 개를 읽어 (전체 프레임, 워밍업 절삭 윈도우, get(row,label) 클로저)를 반환한다."""
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        rows = list(csv.reader(f))
    if rows and rows[-1] and rows[-1][0].startswith("[HasHeaderRowAtEnd]"):
        header = rows[-2]
        data_rows = rows[1:-2]  # 첫 줄 = 초기 헤더(불완전), 끝 2줄 = 전체 헤더+메타데이터
    else:
        header = rows[0]
        data_rows = rows[1:]
    idx = {}
    for label, name in WANTED:
        for i, h in enumerate(header):
            if h == name:
                idx[label] = i
                break

    def get(row, label):
        i = idx.get(label)
        if i is None or i >= len(row) or row[i] == "":
            return None
        try:
            return float(row[i])
        except ValueError:
            return None

    frames = [r for r in data_rows if get(r, "FrameTime") is not None]
    # 워밍업 절삭(누적 프레임타임 기준)
    t, start = 0.0, len(frames)
    for k, r in enumerate(frames):
        t += get(r, "FrameTime") / 1000.0
        if t >= skip_s:
            start = k
            break
    win = frames[start:] if start < len(frames) else frames
    return frames, win, get


def print_span(path, frames, win, get, skip_s, tag=""):
    total_span = sum(get(r, "FrameTime") for r in frames) / 1000.0
    win_span = sum(get(r, "FrameTime") for r in win) / 1000.0
    print(f"{tag}{path}")
    print(f"frames total={len(frames)} ({total_span:.1f}s) | window={len(win)} ({win_span:.1f}s, skip {skip_s}s)")


def compute_stats(win, get, label):
    vals = [v for r in win if (v := get(r, label)) is not None]
    if not vals:
        return None
    s = sorted(vals)
    p50 = s[int(len(s) * 0.50)]
    p95 = s[min(len(s) - 1, int(len(s) * 0.95))]
    avg = statistics.mean(vals)
    return {"avg": avg, "p50": p50, "p95": p95, "max": max(vals)}


def format_stats(label, st, tag=""):
    """단일 캡처의 한 줄. tag="" 면 --baseline 없는 기존 출력과 바이트 단위로 동일하다."""
    prefix = f"{label:20s}" + (f" {tag}" if tag else "")
    if label == "FrameTime":
        return f"{prefix} avg={st['avg']:7.2f}ms (~{1000.0/st['avg']:.0f}fps)  P50={st['p50']:7.2f}  P95={st['p95']:7.2f}"
    elif label in COUNT_LABELS:
        return f"{prefix} avg={st['avg']:10.0f}   P50={st['p50']:10.0f}  max={st['max']:10.0f}"
    elif label in MB_LABELS:
        return f"{prefix} avg={st['avg']:10.1f}MB  P50={st['p50']:10.1f}  max={st['max']:10.1f}"
    else:
        return f"{prefix} avg={st['avg']:7.2f}ms            P50={st['p50']:7.2f}  P95={st['p95']:7.2f}"


def noise_floor(base):
    """baseline 자체의 P95-P50 폭 — 같은 구성을 반복 실행해 산포를 직접 재지 않고도(이 스크립트는 캡처
    2개만 받는다) '델타가 신호인지 노이즈인지'의 하한을 즉시 추정하기 위한 대리 지표(§12-A G2 P3).
    ⚠️ 이건 캡처 '내부'(프레임 간) 산포이고, Performance.md:93 이 기록한 15%는 캡처 '사이'(실행 간) 변동이라
    엄밀히는 다른 양이다 — 둘 다 못 재는 반복실행 세트 없이 즉시 쓸 수 있는 근사치로, 정밀한 하한이 아니라
    "이 정도는 흔들려도 이상하지 않다"는 눈대중 기준이다."""
    return base["p95"] - base["p50"]


def format_delta(label, base, cur, tag="[ D  ]"):
    """base->cur 델타 한 줄(§12-A 비교모드) — 절대값 두 줄과 같은 단위·자리수, 부호만 붙는다.
    🔁 노이즈 플로어 병기(G2 P3) — avg 델타의 절대값이 baseline의 P95-P50 폭보다 작으면 "(~noise)"를
    붙인다. 판정선은 절대값 기준이라 합/불 자체는 이걸로 안 바뀐다(§5) — 오독 방지가 목적이다."""
    prefix = f"{label:20s} {tag}"
    noise = noise_floor(base)
    if label == "FrameTime":
        davg = cur["avg"] - base["avg"]
        dfps = (1000.0 / cur["avg"]) - (1000.0 / base["avg"])
        dp50 = cur["p50"] - base["p50"]
        dp95 = cur["p95"] - base["p95"]
        note = f"  noise(P95-P50)≈±{noise:.2f}ms" + ("  (~noise)" if abs(davg) < noise else "")
        return f"{prefix} avg={davg:+7.2f}ms (~{dfps:+.0f}fps)  P50={dp50:+7.2f}  P95={dp95:+7.2f}{note}"
    elif label in COUNT_LABELS:
        davg = cur["avg"] - base["avg"]
        dp50 = cur["p50"] - base["p50"]
        dmax = cur["max"] - base["max"]
        note = f"  noise(P95-P50)≈±{noise:.0f}" + ("  (~noise)" if abs(davg) < noise else "")
        return f"{prefix} avg={davg:+10.0f}   P50={dp50:+10.0f}  max={dmax:+10.0f}{note}"
    elif label in MB_LABELS:
        davg = cur["avg"] - base["avg"]
        dp50 = cur["p50"] - base["p50"]
        dmax = cur["max"] - base["max"]
        note = f"  noise(P95-P50)≈±{noise:.1f}MB" + ("  (~noise)" if abs(davg) < noise else "")
        return f"{prefix} avg={davg:+10.1f}MB  P50={dp50:+10.1f}  max={dmax:+10.1f}{note}"
    else:
        davg = cur["avg"] - base["avg"]
        dp50 = cur["p50"] - base["p50"]
        dp95 = cur["p95"] - base["p95"]
        note = f"  noise(P95-P50)≈±{noise:.2f}ms" + ("  (~noise)" if abs(davg) < noise else "")
        return f"{prefix} avg={davg:+7.2f}ms            P50={dp50:+7.2f}  P95={dp95:+7.2f}{note}"


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    path = sys.argv[1]
    skip_s = 10.0
    if "--skip-seconds" in sys.argv:
        skip_s = float(sys.argv[sys.argv.index("--skip-seconds") + 1])
    baseline_path = None
    if "--baseline" in sys.argv:
        baseline_path = sys.argv[sys.argv.index("--baseline") + 1]

    frames, win, get = load_window(path, skip_s)
    print_span(path, frames, win, get, skip_s, tag="file: ")

    base_win = base_get = None
    if baseline_path:
        base_frames, base_win, base_get = load_window(baseline_path, skip_s)
        print_span(baseline_path, base_frames, base_win, base_get, skip_s, tag="baseline: ")

    for label, _ in WANTED:
        cur_st = compute_stats(win, get, label)
        if not baseline_path:
            # --baseline 없음 = 기존 동작 그대로(신규 WANTED 4줄만 추가로 찍힌다)
            if cur_st is None:
                print(f"{label:20s} (no data)")
            else:
                print(format_stats(label, cur_st))
            continue

        base_st = compute_stats(base_win, base_get, label)
        if base_st is None and cur_st is None:
            print(f"{label:20s} (no data)")
        elif base_st is None:
            print(f"{label:20s} [base] (no data)")
            print(format_stats(label, cur_st, "[cur ]"))
        elif cur_st is None:
            print(format_stats(label, base_st, "[base]"))
            print(f"{label:20s} [cur ] (no data)")
        else:
            print(format_stats(label, base_st, "[base]"))
            print(format_stats(label, cur_st, "[cur ]"))
            print(format_delta(label, base_st, cur_st))

    parts = []
    for label in ("GPU/BasePass", "GPU/ShadowDepths", "GPU/CustomDepth"):
        vals = [v for r in win if (v := get(r, label)) is not None]
        if vals:
            parts.append((label, statistics.mean(vals)))
    if parts:
        total = sum(v for _, v in parts)
        detail = " + ".join(f"{l.split('/')[1]} {v:.2f}" for l, v in parts)
        print(f"[스웜 렌더 서브예산] {detail} = {total:.2f}ms (예산 4ms@300)")


if __name__ == "__main__":
    main()
