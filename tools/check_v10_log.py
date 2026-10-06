#!/usr/bin/env python3
"""
Offline check for Analyzer_v10 raw EDGE records.

Implements the same design:
1) qualify same-channel 1 Hz trains,
2) associate same-level qualified edges within 5 ms,
3) gate only the first 3 boundaries of each train at 100 us.
"""

import argparse
import re
from pathlib import Path

EDGE_RE = re.compile(
    r"ANZ\|EDGE\|(?P<run>\d+)\|(?P<trial>\d+)\|(?P<ch>[123])\|"
    r"(?P<name>ESP0[123]_COMMIT)\|(?P<t>\d+)\|(?P<level>[01])"
)

NAMES = ("ESP01_COMMIT", "ESP02_COMMIT", "ESP03_COMMIT")

def qualify(raw, train_min, train_max):
    out = {name: [] for name in NAMES}

    for name in NAMES:
        prev = None
        prev_emitted = False

        for edge in raw[name]:
            if prev is None:
                prev = edge
                prev_emitted = False
                continue

            dt = edge[0] - prev[0]
            good = (
                train_min <= dt <= train_max
                and edge[1] != prev[1]
            )

            current_emitted = False

            if good:
                if not prev_emitted:
                    out[name].append(prev)
                out[name].append(edge)
                current_emitted = True

            prev = edge
            prev_emitted = current_emitted

    return out

def associate(qualified, window_us):
    all_edges = []

    for idx, name in enumerate(NAMES):
        for t, level in qualified[name]:
            all_edges.append((t, level, idx))

    all_edges.sort()

    groups = []

    for t, level, dev in all_edges:
        best = None
        best_dist = None

        for g in groups:
            if g["complete"] or g["level"] != level or dev in g["ts"]:
                continue

            dist = abs(t - g["anchor"])
            if dist > window_us:
                continue

            vals = list(g["ts"].values()) + [t]
            if max(vals) - min(vals) > window_us:
                continue

            if best_dist is None or dist < best_dist:
                best = g
                best_dist = dist

        if best is None:
            best = {
                "anchor": t,
                "level": level,
                "ts": {},
                "complete": False,
            }
            groups.append(best)

        best["ts"][dev] = t

        if len(best["ts"]) == 3:
            best["complete"] = True

    return [g for g in groups if g["complete"]]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--train-min-us", type=int, default=995000)
    ap.add_argument("--train-max-us", type=int, default=1005000)
    ap.add_argument("--associate-us", type=int, default=5000)
    ap.add_argument("--new-train-gap-us", type=int, default=1500000)
    ap.add_argument("--start-boundaries", type=int, default=3)
    ap.add_argument("--limit-us", type=int, default=100)
    args = ap.parse_args()

    raw = {name: [] for name in NAMES}

    for m in EDGE_RE.finditer(Path(args.log).read_text(errors="replace")):
        raw[m.group("name")].append(
            (int(m.group("t")), int(m.group("level")))
        )

    q = qualify(raw, args.train_min_us, args.train_max_us)
    groups = associate(q, args.associate_us)
    groups.sort(key=lambda g: min(g["ts"].values()))

    train = 0
    boundary = 0
    last = None
    overall_pass = True

    print("train,boundary,ESP01,ESP02,ESP03,range_us,gated,result")

    for g in groups:
        ts = [g["ts"][0], g["ts"][1], g["ts"][2]]
        group_time = min(ts)

        if last is None or group_time - last > args.new_train_gap_us:
            train += 1
            boundary = 0

        span = max(ts) - min(ts)
        gated = boundary < args.start_boundaries
        passed = (not gated) or span <= args.limit_us

        if not passed:
            overall_pass = False

        print(
            f"{train},{boundary},{ts[0]},{ts[1]},{ts[2]},"
            f"{span},{1 if gated else 0},"
            f"{'PASS' if gated and passed else 'TRIP' if gated else 'INFO'}"
        )

        last = group_time
        boundary += 1

    print()
    print("Qualified counts:",
          ", ".join(f"{k}={len(v)}" for k, v in q.items()))
    print("Detected trains:", train)
    print("OVERALL:", "PASS" if overall_pass else "FAIL")

if __name__ == "__main__":
    main()
