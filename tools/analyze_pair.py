#!/usr/bin/env python3
"""Analyze ESP01/ESP02 ANZ|EDGE records from a long presentation capture."""

from __future__ import annotations

import argparse
import statistics
from pathlib import Path


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("log", type=Path, help="serial log containing ANZ|EDGE records")
    p.add_argument("--run", type=int, default=None)
    p.add_argument("--trial", type=int, default=None)
    p.add_argument("--a", default="ESP01")
    p.add_argument("--b", default="ESP02")
    return p.parse_args()


def percentile(values: list[float], p: float) -> float:
    if not values:
        raise ValueError("empty values")
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    x = (len(ordered) - 1) * p
    lo = int(x)
    hi = min(lo + 1, len(ordered) - 1)
    frac = x - lo
    return ordered[lo] * (1.0 - frac) + ordered[hi] * frac


def main() -> int:
    args = parse_args()
    events: dict[tuple[int, int, str], list[int]] = {}
    counts_lines: list[str] = []

    for raw in args.log.read_text(errors="replace").splitlines():
        line = raw.strip()
        if "ANZ|EDGE|" in line:
            line = line[line.index("ANZ|EDGE|"):]
            parts = line.split("|")
            if len(parts) != 7:
                continue
            _, kind, run_s, trial_s, _channel_s, device, ts_s = parts
            if kind != "EDGE":
                continue
            try:
                run = int(run_s)
                trial = int(trial_s)
                ts = int(ts_s)
            except ValueError:
                continue
            if args.run is not None and run != args.run:
                continue
            if args.trial is not None and trial != args.trial:
                continue
            events.setdefault((run, trial, device), []).append(ts)
        elif "ANZ|COUNTS|" in line:
            counts_lines.append(line[line.index("ANZ|COUNTS|"):])

    trials = sorted({(run, trial) for run, trial, device in events if device in (args.a, args.b)})
    if not trials:
        print("No matching ANZ|EDGE records found.")
        return 2

    for run, trial in trials:
        a = events.get((run, trial, args.a), [])
        b = events.get((run, trial, args.b), [])
        n = min(len(a), len(b))
        print(f"run={run} trial={trial} {args.a}={len(a)} {args.b}={len(b)} paired={n}")
        if len(a) != len(b):
            print("WARNING: channel edge counts differ; do not trust index pairing until the missing edge is resolved.")
        if n == 0:
            continue

        spread = [b[i] - a[i] for i in range(n)]
        abs_spread = [abs(x) for x in spread]
        print(f"first_spread_us={spread[0]}")
        print(f"last_spread_us={spread[-1]}")
        print(f"spread_change_us={spread[-1] - spread[0]}")
        print(f"min_spread_us={min(spread)}")
        print(f"max_spread_us={max(spread)}")
        print(f"median_spread_us={statistics.median(spread):.3f}")
        print(f"p95_abs_spread_us={percentile(abs_spread, 0.95):.3f}")
        print()

    if counts_lines:
        print("Integrity records:")
        for line in counts_lines:
            print(line)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
