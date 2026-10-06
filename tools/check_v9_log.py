#!/usr/bin/env python3
import argparse
import re
from pathlib import Path

EDGE_RE = re.compile(
    r"ANZ\|EDGE\|(?P<run>\d+)\|(?P<trial>\d+)\|(?P<ch>\d+)\|"
    r"(?P<name>ESP0[123]_COMMIT)\|(?P<t>\d+)\|(?P<level>[01])"
)

NAMES = ("ESP01_COMMIT", "ESP02_COMMIT", "ESP03_COMMIT")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--limit-us", type=int, default=100)
    args = ap.parse_args()

    text = Path(args.log).read_text(errors="replace")
    edges = {name: [] for name in NAMES}

    for m in EDGE_RE.finditer(text):
        name = m.group("name")
        edges[name].append((int(m.group("t")), int(m.group("level"))))

    counts = {name: len(v) for name, v in edges.items()}
    print("Counts:", ", ".join(f"{k}={v}" for k, v in counts.items()))

    if min(counts.values()) == 0:
        raise SystemExit("FAIL: at least one COMMIT channel has zero edges")

    if len(set(counts.values())) != 1:
        raise SystemExit("FAIL: COMMIT counts do not match")

    worst = -1
    worst_i = -1
    trips = 0
    level_mismatches = 0

    print("edge,ESP01_us,ESP02_us,ESP03_us,range_us,result")

    for i in range(counts[NAMES[0]]):
        row = [edges[name][i] for name in NAMES]
        ts = [x[0] for x in row]
        levels = [x[1] for x in row]

        span = max(ts) - min(ts)
        level_ok = len(set(levels)) == 1
        passed = span <= args.limit_us and level_ok

        if span > worst:
            worst = span
            worst_i = i + 1

        if not level_ok:
            level_mismatches += 1

        if not passed:
            trips += 1

        print(
            f"{i+1},{ts[0]},{ts[1]},{ts[2]},{span},"
            f"{'PASS' if passed else 'TRIP'}"
        )

    print()
    print(f"Worst range : {worst} us")
    print(f"Worst edge  : {worst_i}")
    print(f"Limit       : {args.limit_us} us")
    print(f"Trips       : {trips}")
    print(f"Level errors: {level_mismatches}")
    print(f"OVERALL     : {'PASS' if trips == 0 else 'FAIL'}")

if __name__ == "__main__":
    main()
