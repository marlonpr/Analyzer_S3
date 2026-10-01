#!/usr/bin/env python3
"""Estimate Analyzer v8 fixed channel dispatch skew from a fan-out pulse capture.

Feed the SAME digital pulse into all six analyzer inputs, BEGIN a short trial,
and collect at least ~30 transitions. The script groups nearly simultaneous
edges and reports median channel delay relative to ESP01_SQW plus the three
pair-bias values used by analyze_dual_qualification.py.
"""

from __future__ import annotations
import argparse
import statistics
from pathlib import Path

LABELS = [
    "ESP01_SQW", "ESP01_COMMIT", "ESP01_REFRESH",
    "ESP02_SQW", "ESP02_COMMIT", "ESP02_REFRESH",
]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("log", type=Path)
    ap.add_argument("--burst-window-us", type=int, default=100)
    args = ap.parse_args()

    edges = []
    for raw in args.log.read_text(errors="replace").splitlines():
        if "ANZ|EDGE|" not in raw:
            continue
        line = raw[raw.index("ANZ|EDGE|"):]
        p = line.split("|")
        if len(p) < 8:
            continue
        try:
            edges.append((int(p[6]), p[5], int(p[7])))
        except ValueError:
            pass
    edges.sort()
    if not edges:
        print("No edges found")
        return 2

    bursts = []
    cur = [edges[0]]
    for e in edges[1:]:
        if e[0] - cur[0][0] <= args.burst_window_us:
            cur.append(e)
        else:
            bursts.append(cur)
            cur = [e]
    bursts.append(cur)

    offsets = {name: [] for name in LABELS}
    complete = 0
    for burst in bursts:
        by_name = {}
        for ts, name, level in burst:
            if name in LABELS and name not in by_name:
                by_name[name] = (ts, level)
        if any(name not in by_name for name in LABELS):
            continue
        levels = {by_name[name][1] for name in LABELS}
        if len(levels) != 1:
            continue
        complete += 1
        ref = by_name["ESP01_SQW"][0]
        for name in LABELS:
            offsets[name].append(by_name[name][0] - ref)

    if complete == 0:
        print("No complete six-channel simultaneous bursts found")
        return 2

    med = {name: statistics.median(v) for name, v in offsets.items()}
    print(f"complete_bursts={complete}")
    for name in LABELS:
        print(f"{name}_offset_from_ESP01_SQW_us={med[name]:.3f}")

    print()
    print("Pair-bias arguments for qualification analyzer:")
    print(f"--sqw-pair-bias-us {med['ESP02_SQW'] - med['ESP01_SQW']:.3f}")
    print(f"--commit-pair-bias-us {med['ESP02_COMMIT'] - med['ESP01_COMMIT']:.3f}")
    print(f"--refresh-pair-bias-us {med['ESP02_REFRESH'] - med['ESP01_REFRESH']:.3f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
