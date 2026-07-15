#!/usr/bin/env python
"""S4 readability capture checker (Docs/SSOT/Performance.md 5).

Reads a UE CSV profiler capture and reports the 5 readability metrics
(FPSREnemy/ServerAlive|RelevantAlive|VisibleFrustum|VisibleRendered|Near15m)
plus the 3b <= 3a invariant that guards the VisibleRendered source.

Usage:
    python Scripts/s4-check-capture.py                      # newest CSV in Saved/Profiling/CSV
    python Scripts/s4-check-capture.py <path-to.csv>
    python Scripts/s4-check-capture.py <path.csv> --warmup 5 # drop the first 5 seconds of frames

CSV format notes (verified against UE 5.7 CsvProfiler.cpp):
  - Row 1 is the header written at the first flushed row; more series can appear
    later, so data rows may be WIDER than row 1 and a second, complete header row
    is emitted at the end ([HasHeaderRowAtEnd]=1), followed by a metadata row.
    Both non-data rows must be skipped or they poison the columns.
  - A 0-byte file means the capture was never stopped (see the runbook).
"""

import csv
import os
import sys
import glob

NAMES = [
    "FPSREnemy/ServerAlive",
    "FPSREnemy/RelevantAlive",
    "FPSREnemy/VisibleFrustum",
    "FPSREnemy/VisibleRendered",
    "FPSREnemy/Near15m",
]

# Performance.md 5 design targets (guardrails, not go/no-go gates).
# 3 (screen-visible enemies) is judged on VisibleRendered -- the occlusion-aware
# number -- because this map controls readability by breaking sightlines.
TARGETS = {
    "FPSREnemy/RelevantAlive": [("P90", 150)],
    "FPSREnemy/VisibleRendered": [("P50", 40), ("P90", 70)],
    "FPSREnemy/Near15m": [("P90", 25)],
}


def percentile(values, q):
    ordered = sorted(values)
    k = int(round((len(ordered) - 1) * q))
    return ordered[k]


def main():
    args = [a for a in sys.argv[1:]]
    warmup = 0.0
    if "--warmup" in args:
        i = args.index("--warmup")
        warmup = float(args[i + 1])
        del args[i:i + 2]

    if args:
        path = args[0]
    else:
        root = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                            "Saved", "Profiling", "CSV")
        files = sorted(glob.glob(os.path.join(root, "*.csv")), key=os.path.getmtime)
        if not files:
            print("FAIL: no CSV found in %s" % root)
            return 2
        path = files[-1]

    print("file : %s" % path)
    size = os.path.getsize(path)
    print("size : %s bytes" % format(size, ","))
    if size == 0:
        print("\nFAIL: 0 bytes. The capture was started but never finalized --")
        print("      you stopped PIE / closed the editor before 'csvprofile stop'.")
        print("      The file is created at capture START and only written at STOP.")
        return 2

    with open(path, newline="") as handle:
        rows = list(csv.reader(handle))

    header = rows[0]
    missing = [n for n in NAMES if n not in header]
    if missing:
        print("\nFAIL: these columns are absent -- the metrics subsystem never ticked:")
        for n in missing:
            print("      %s" % n)
        print("      Wrong binaries (branch not built?), no local pawn, or CSV_PROFILER_STATS off.")
        return 2

    idx = {n: header.index(n) for n in NAMES}
    body = [r for r in rows[1:] if r and r[0] != "EVENTS" and not r[0].startswith("[")]
    print("frames : %d" % len(body))

    if warmup > 0:
        # Frame time column is emitted per frame; drop the leading `warmup` seconds.
        ft = header.index("FrameTime") if "FrameTime" in header else None
        if ft is None:
            print("warn : no FrameTime column; --warmup ignored")
        else:
            acc, cut = 0.0, 0
            for n, r in enumerate(body):
                if ft < len(r) and r[ft].strip():
                    acc += float(r[ft]) / 1000.0
                if acc >= warmup:
                    cut = n
                    break
            body = body[cut:]
            print("warmup : dropped %d frames (%.1fs)" % (cut, warmup))

    if not body:
        print("\nFAIL: no data rows.")
        return 2

    def column(name):
        i = idx[name]
        return [float(r[i]) for r in body if i < len(r) and r[i].strip() != ""]

    print("")
    print("%-16s %8s %8s %8s %8s   %s" % ("metric", "P50", "P90", "P99", "max", "target"))
    verdict = 0
    for name in NAMES:
        values = column(name)
        if not values:
            print("%-16s  (no samples)" % name.split("/")[1])
            verdict = 2
            continue
        p50, p90 = percentile(values, 0.50), percentile(values, 0.90)
        p99, mx = percentile(values, 0.99), max(values)
        notes = []
        for stat, limit in TARGETS.get(name, []):
            actual = p50 if stat == "P50" else p90
            notes.append("%s<=%d %s" % (stat, limit, "OK" if actual <= limit else "OVER"))
        note = "  ".join(notes)
        print("%-16s %8.0f %8.0f %8.0f %8.0f   %s" % (name.split("/")[1], p50, p90, p99, mx, note))

    # The invariant check. NOTE ON THE PASS CRITERION: a 0% violation rate is NOT achievable and must not be the
    # bar. 3b is a render stamp from up to RenderRecencyFrames ago while 3a is evaluated NOW, so when the camera
    # turns, an enemy that was genuinely on screen 1-3 frames ago but has since left the frustum legitimately
    # counts in 3b and not in 3a. That residue is the metric's physics, not a bug.
    #
    # What separates a real bug from the residue is the EXCESS, not the rate:
    #   - shadow-pass bug (pre-e198f668):  47.9% of frames, excess P90 39, max 59  <- enemies fully behind you
    #   - bounds mismatch (pre-19e1065d):  excess ~1                               <- a 59cm shell at the frustum edge
    #   - transition-frame residue:        excess ~1, only while turning           <- expected, harmless
    # So gate on BOTH: rate < 5% AND max excess <= 2.
    frustum = column("FPSREnemy/VisibleFrustum")
    rendered = column("FPSREnemy/VisibleRendered")
    pairs = min(len(frustum), len(rendered))
    excesses = [rendered[i] - frustum[i] for i in range(pairs) if rendered[i] > frustum[i]]
    bad = len(excesses)
    pct = 100.0 * bad / pairs if pairs else 0.0
    max_excess = max(excesses) if excesses else 0
    print("")
    print("INVARIANT 3b <= 3a : %d/%d frames violate (%.1f%%)" % (bad, pairs, pct))
    if excesses:
        print("  excess on violating frames: P50 %.0f / P90 %.0f / max %d"
              % (percentile(excesses, 0.50), percentile(excesses, 0.90), max_excess))
    if pct >= 5.0 and max_excess > 2:
        print("  -> FAIL. 3b is reading something that is not on-screen visibility.")
        print("     ~47.9% with excess up to 59 = the pre-e198f668 shadow-pass bug (stale binary?).")
        print("     A few % with excess ~1 = the pre-19e1065d bounds mismatch (3a not testing mesh bounds).")
        verdict = 2
    elif pct >= 5.0:
        print("  -> SUSPECT. Excess is small (<=2) but the rate is high: check RenderRecencyFrames, or whether")
        print("     the capture is mostly fast camera turns. Not the shadow/bounds class of bug.")
        verdict = 2
    elif max_excess > 2:
        print("  -> SUSPECT. Rate is low but excess is large: a few frames are badly wrong. Investigate those")
        print("     frames rather than dismissing them -- a real bug can be rare without being small.")
        verdict = 2
    else:
        print("  -> OK. Rate <5% and excess <=2 = transition-frame residue, the expected floor.")
        print("     3b is occlusion-aware and bounded by its frustum upper bound.")

    alive = column("FPSREnemy/ServerAlive")
    if alive and max(alive) == 0:
        print("\nWARN: ServerAlive is 0 for the whole capture -- client-side capture, or no run active.")
    print("")
    return verdict


if __name__ == "__main__":
    sys.exit(main())
