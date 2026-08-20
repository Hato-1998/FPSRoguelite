# analyze_swarm_csv.py — VAT-1 측정 CSV 분석 (Docs/Review/20260812-plan-vat1-swarm-render-path.md 프로토콜)
# UE CsvProfiler 포맷 실물 대응:
#   - 진짜 헤더는 파일 '끝'(마지막 줄 = [HasHeaderRowAtEnd] 메타데이터, 그 앞 줄 = 전체 컬럼명)
#   - 컬럼은 캡처 중 append-only로 늘어남 → 행마다 열 수가 다름(왼쪽 정렬, 부족분 = 결측)
#   - EVENTS 컬럼은 문자열 — 숫자 변환 금지
# 사용: python Scripts/analyze_swarm_csv.py <capture.csv> [--skip-seconds 10]
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
    ("Repl/ActiveActors",    "Replication/NumberOfActiveActors"),
    ("Repl/FullyDormant",    "Replication/NumberOfFullyDormantActors"),
    ("FPSRMsg/GMSBroadcast", "FPSRMsg/GameThread/GMSBroadcast"),
    ("FPSRMsg/Broadcasts",   "FPSRMsg/Broadcasts"),
    ("FPSRMsg/Dispatches",   "FPSRMsg/Dispatches"),
    ("FPSRMsg/ListenersCopied", "FPSRMsg/ListenersCopied"),
    ("FPSREnemy/ServerAlive", "FPSREnemy/ServerAlive"),
]

# 카운트성 컬럼(정수 avg/P50/max 포맷) — 기존 RHI/* 2종 포함, §5-C(4) 신규 9종 중 카운트성 6종.
# 시간성(Excl/*·FPSRMsg/GMSBroadcast)은 이 집합에 넣지 않는다 — 기존 ms 포맷(avg/P50/P95) 유지.
COUNT_LABELS = {
    "RHI/DrawCalls", "RHI/PrimitivesDrawn",
    "Repl/ActiveActors", "Repl/FullyDormant",
    "FPSRMsg/Broadcasts", "FPSRMsg/Dispatches", "FPSRMsg/ListenersCopied",
    "FPSREnemy/ServerAlive",
}

def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    path = sys.argv[1]
    skip_s = 10.0
    if "--skip-seconds" in sys.argv:
        skip_s = float(sys.argv[sys.argv.index("--skip-seconds") + 1])
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
    print(f"file: {path}")
    total_span = sum(get(r, 'FrameTime') for r in frames) / 1000.0
    win_span = sum(get(r, 'FrameTime') for r in win) / 1000.0
    print(f"frames total={len(frames)} ({total_span:.1f}s) | window={len(win)} ({win_span:.1f}s, skip {skip_s}s)")

    for label, _ in WANTED:
        vals = [v for r in win if (v := get(r, label)) is not None]
        if not vals:
            print(f"{label:20s} (no data)")
            continue
        s = sorted(vals)
        p50 = s[int(len(s) * 0.50)]
        p95 = s[min(len(s) - 1, int(len(s) * 0.95))]
        avg = statistics.mean(vals)
        if label == "FrameTime":
            print(f"{label:20s} avg={avg:7.2f}ms (~{1000.0/avg:.0f}fps)  P50={p50:7.2f}  P95={p95:7.2f}")
        elif label in COUNT_LABELS:
            print(f"{label:20s} avg={avg:10.0f}   P50={p50:10.0f}  max={max(vals):10.0f}")
        else:
            print(f"{label:20s} avg={avg:7.2f}ms            P50={p50:7.2f}  P95={p95:7.2f}")

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
