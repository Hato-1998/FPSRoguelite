# analyze_swarm_csv.py — VAT-1 측정 CSV 분석 (Docs/Review/20260812-plan-vat1-swarm-render-path.md 프로토콜)
# 사용: python Scripts/analyze_swarm_csv.py Packaged/Measurements/B_300/capture.csv [--skip-seconds 10]
import csv, sys, statistics

def main():
    path = sys.argv[1]
    skip_s = 10.0
    if "--skip-seconds" in sys.argv:
        skip_s = float(sys.argv[sys.argv.index("--skip-seconds") + 1])
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        rows = list(csv.reader(f))
    header = rows[0]
    # CSV 프로파일러 마지막 줄은 메타데이터일 수 있음 — 숫자 아닌 행 제거
    data = []
    for r in rows[1:]:
        if len(r) != len(header):
            continue
        try:
            data.append([float(x) if x else 0.0 for x in r])
        except ValueError:
            continue

    def col(*needles, required=False):
        for i, h in enumerate(header):
            hl = h.lower()
            if all(n.lower() in hl for n in needles):
                return i
        if required:
            raise SystemExit(f"column not found: {needles}\nheader sample: {header[:40]}")
        return None

    i_ft = col("frametime", required=True)          # ms
    # 워밍업 절삭: 누적 프레임타임으로 skip_s 초 이후부터
    t, start = 0.0, 0
    for k, r in enumerate(data):
        t += r[i_ft] / 1000.0
        if t >= skip_s:
            start = k
            break
    win = data[start:]
    ft = [r[i_ft] for r in win]
    ft_sorted = sorted(ft)
    def pct(p):
        return ft_sorted[min(len(ft_sorted) - 1, int(len(ft_sorted) * p))]

    print(f"file: {path}")
    print(f"frames total={len(data)} window={len(win)} (skip {skip_s}s) span={sum(ft)/1000.0:.1f}s")
    print(f"FrameTime avg={statistics.mean(ft):.2f}ms (≈{1000.0/statistics.mean(ft):.1f}fps)  "
          f"P50={pct(0.50):.2f}  P95={pct(0.95):.2f}  P99={pct(0.99):.2f}ms")
    for label, needles in [
        ("GameThread", ("gamethreadtime",)), ("RenderThread", ("renderthreadtime",)),
        ("GPU", ("gputime",)), ("RHIThread", ("rhithreadtime",)),
    ]:
        i = col(*needles)
        if i is not None:
            v = [r[i] for r in win]
            print(f"{label:12s} avg={statistics.mean(v):.2f}ms  P95={sorted(v)[int(len(v)*0.95)]:.2f}ms")
    # 드로우콜 + GPU 패스 분해(있으면)
    for label, needles in [
        ("DrawCalls", ("drawcalls",)), ("Basepass", ("basepass",)),
        ("ShadowDepths", ("shadowdepth",)), ("CustomDepth", ("customdepth",)),
        ("PrePass", ("prepass",)), ("Translucency", ("translucen",)),
    ]:
        i = col(*needles)
        if i is not None:
            v = [r[i] for r in win]
            print(f"{label:12s} avg={statistics.mean(v):.2f}  max={max(v):.2f}  ({header[i]})")
    # 스웜 렌더 서브예산 근사 = Basepass+ShadowDepths+CustomDepth (GPU 열이 ms일 때)
    parts = []
    for needles in [("basepass",), ("shadowdepth",), ("customdepth",)]:
        i = col(*needles)
        if i is not None and "gpu" in header[i].lower():
            parts.append(statistics.mean([r[i] for r in win]))
    if parts:
        print(f"[서브예산 근사] GPU Basepass+Shadow+CustomDepth = {sum(parts):.2f}ms (예산 4ms@300)")

if __name__ == "__main__":
    main()
