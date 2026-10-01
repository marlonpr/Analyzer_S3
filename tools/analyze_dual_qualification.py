#!/usr/bin/env python3
"""Analyze a 30-minute two-device Analyzer v8 qualification capture.

The script intentionally does NOT line-detrend COMMIT/REFRESH errors before
reporting percentiles. It removes only the static median pair offset, then
reports the physical spread around that baseline. Long-term SQW relative drift
is reported separately.
"""

from __future__ import annotations

import argparse
import math
import statistics
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Event:
    ts_us: int
    level: int


def percentile(values: list[float], p: float) -> float:
    if not values:
        raise ValueError("empty values")
    y = sorted(values)
    if len(y) == 1:
        return y[0]
    q = (len(y) - 1) * p
    lo = int(q)
    hi = min(lo + 1, len(y) - 1)
    f = q - lo
    return y[lo] * (1.0 - f) + y[hi] * f


def rms(values: list[float]) -> float:
    return math.sqrt(sum(v * v for v in values) / len(values)) if values else float("nan")


def parse_log(path: Path, wanted_run: int | None, wanted_trial: int | None):
    events: dict[str, list[Event]] = {}
    counts_lines: list[str] = []
    summaries: list[str] = []
    selected: tuple[int, int] | None = None

    for raw in path.read_text(errors="replace").splitlines():
        if "ANZ|EDGE|" in raw:
            line = raw[raw.index("ANZ|EDGE|"):].strip()
            parts = line.split("|")
            if len(parts) < 8:
                continue
            try:
                run = int(parts[2])
                trial = int(parts[3])
                device = parts[5]
                ts = int(parts[6])
                level = int(parts[7])
            except ValueError:
                continue
            if wanted_run is not None and run != wanted_run:
                continue
            if wanted_trial is not None and trial != wanted_trial:
                continue
            if selected is None:
                selected = (run, trial)
            if (run, trial) != selected:
                continue
            events.setdefault(device, []).append(Event(ts, level))
        elif "ANZ|COUNTS|" in raw:
            counts_lines.append(raw[raw.index("ANZ|COUNTS|"):].strip())
        elif "ANZ|SUMMARY|" in raw:
            summaries.append(raw[raw.index("ANZ|SUMMARY|"):].strip())

    return selected, events, summaries, counts_lines


def nearest_pairs(a: list[Event], b: list[Event], max_gap_us: int) -> list[tuple[Event, Event]]:
    pairs: list[tuple[Event, Event]] = []
    i = j = 0
    while i < len(a) and j < len(b):
        d = b[j].ts_us - a[i].ts_us
        if abs(d) <= max_gap_us:
            pairs.append((a[i], b[j]))
            i += 1
            j += 1
        elif b[j].ts_us < a[i].ts_us:
            j += 1
        else:
            i += 1
    return pairs


def report_pair(name: str, a: list[Event], b: list[Event], bias_us: float, max_gap_us: int) -> None:
    pairs = nearest_pairs(a, b, max_gap_us)
    print(f"[{name}]")
    print(f"events_a={len(a)} events_b={len(b)} paired={len(pairs)}")
    if not pairs:
        print("ERROR: no pairs\n")
        return

    gaps = [(eb.ts_us - ea.ts_us) - bias_us for ea, eb in pairs]
    baseline = statistics.median(gaps)
    centered = [g - baseline for g in gaps]
    abs_centered = [abs(v) for v in centered]

    print(f"pair_bias_applied_us={bias_us:.3f}")
    print(f"raw_gap_median_us={baseline:.3f}")
    print(f"centered_median_us={statistics.median(centered):.3f}")
    print(f"centered_p95_abs_us={percentile(abs_centered, 0.95):.3f}")
    print(f"centered_p99_abs_us={percentile(abs_centered, 0.99):.3f}")
    print(f"centered_max_abs_us={max(abs_centered):.3f}")
    for threshold in (1000, 2000, 5000):
        print(f"count_abs_gt_{threshold}us={sum(v > threshold for v in abs_centered)}")
    print(f"first_gap_us={gaps[0]:.3f}")
    print(f"last_gap_us={gaps[-1]:.3f}")
    print(f"gap_change_us={gaps[-1] - gaps[0]:.3f}")
    print()


def rising(events: list[Event]) -> list[int]:
    return [e.ts_us for e in events if e.level == 1]


def period_stats(name: str, ts: list[int]) -> None:
    if len(ts) < 2:
        print(f"{name}: insufficient rising edges")
        return
    periods = [b - a for a, b in zip(ts, ts[1:])]
    mean = statistics.mean(periods)
    residual = [p - mean for p in periods]
    print(
        f"{name}: rising={len(ts)} mean_period_us={mean:.6f} "
        f"period_rms_us={rms(residual):.6f} min_us={min(periods)} max_us={max(periods)}"
    )


def wrap_phase_us(value: float, period_us: float = 1_000_000.0) -> float:
    return ((value + period_us / 2.0) % period_us) - period_us / 2.0


def unwrap_phase(values: list[float], period_us: float = 1_000_000.0) -> list[float]:
    if not values:
        return []
    out = [values[0]]
    for raw in values[1:]:
        candidate = raw
        while candidate - out[-1] > period_us / 2.0:
            candidate -= period_us
        while candidate - out[-1] < -period_us / 2.0:
            candidate += period_us
        out.append(candidate)
    return out


def linear_fit(x: list[float], y: list[float]) -> tuple[float, float, list[float]]:
    xm = statistics.mean(x)
    ym = statistics.mean(y)
    den = sum((v - xm) ** 2 for v in x)
    slope = sum((a - xm) * (b - ym) for a, b in zip(x, y)) / den if den else 0.0
    intercept = ym - slope * xm
    residuals = [yy - (intercept + slope * xx) for xx, yy in zip(x, y)]
    return intercept, slope, residuals


def robust_linear_fit(x: list[float], y: list[float]) -> tuple[float, float, list[float], int]:
    idx = list(range(len(x)))
    for _ in range(3):
        xx = [x[i] for i in idx]
        yy = [y[i] for i in idx]
        intercept, slope, residuals = linear_fit(xx, yy)
        med = statistics.median(residuals)
        abs_dev = [abs(r - med) for r in residuals]
        mad = statistics.median(abs_dev)
        limit = max(10.0, 6.0 * 1.4826 * mad)
        new_idx = [i for i, r in zip(idx, residuals) if abs(r - med) <= limit]
        if len(new_idx) == len(idx) or len(new_idx) < max(10, len(x) // 2):
            break
        idx = new_idx
    xx = [x[i] for i in idx]
    yy = [y[i] for i in idx]
    intercept, slope, residuals = linear_fit(xx, yy)
    return intercept, slope, residuals, len(idx)


def report_sqw(a_events: list[Event], b_events: list[Event], pair_bias_us: float) -> None:
    a = rising(a_events)
    b = rising(b_events)
    print("[SQW]")
    period_stats("ESP01", a)
    period_stats("ESP02", b)
    n = min(len(a), len(b))
    print(f"paired_rising={n} (index paired; phase reported modulo 1 second)")
    if n < 3:
        print("ERROR: insufficient SQW pairs\n")
        return

    wrapped = [wrap_phase_us((b[i] - a[i]) - pair_bias_us) for i in range(n)]
    phase = unwrap_phase(wrapped)
    x = [(a[i] - a[0]) / 1_000_000.0 for i in range(n)]  # seconds
    _, slope_us_per_s, residuals, kept = robust_linear_fit(x, phase)
    relative_ppm = slope_us_per_s  # 1 us/s == 1 ppm

    print(f"pair_bias_applied_us={pair_bias_us:.3f}")
    print(f"initial_phase_mod_1s_us={wrapped[0]:.3f}")
    print(f"final_phase_mod_1s_us={wrapped[-1]:.3f}")
    print(f"unwrapped_phase_change_us={phase[-1] - phase[0]:.3f}")
    print(f"robust_relative_drift_ppm={relative_ppm:.6f}")
    print(f"fit_points={kept}/{n}")
    print(f"fit_residual_rms_us={rms(residuals):.3f}")
    print()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("log", type=Path)
    ap.add_argument("--run", type=int)
    ap.add_argument("--trial", type=int)
    ap.add_argument("--commit-pair-bias-us", type=float, default=0.0,
                    help="subtract fixed analyzer ESP02_COMMIT-minus-ESP01_COMMIT skew")
    ap.add_argument("--refresh-pair-bias-us", type=float, default=0.0,
                    help="subtract fixed analyzer ESP02_REFRESH-minus-ESP01_REFRESH skew")
    ap.add_argument("--sqw-pair-bias-us", type=float, default=0.0,
                    help="subtract fixed analyzer ESP02_SQW-minus-ESP01_SQW skew")
    ap.add_argument("--pair-window-us", type=int, default=100_000,
                    help="maximum COMMIT/REFRESH pairing gap; default 100 ms")
    args = ap.parse_args()

    selected, events, summaries, counts = parse_log(args.log, args.run, args.trial)
    if selected is None:
        print("No ANZ|EDGE records found.")
        return 2
    print(f"run={selected[0]} trial={selected[1]}")
    print()

    required = [
        "ESP01_SQW", "ESP01_COMMIT", "ESP01_REFRESH",
        "ESP02_SQW", "ESP02_COMMIT", "ESP02_REFRESH",
    ]
    missing = [name for name in required if name not in events]
    if missing:
        print("WARNING missing channels: " + ", ".join(missing))
        print()

    report_pair(
        "COMMIT ESP02-ESP01",
        events.get("ESP01_COMMIT", []), events.get("ESP02_COMMIT", []),
        args.commit_pair_bias_us, args.pair_window_us,
    )
    report_pair(
        "REFRESH ESP02-ESP01",
        events.get("ESP01_REFRESH", []), events.get("ESP02_REFRESH", []),
        args.refresh_pair_bias_us, args.pair_window_us,
    )
    report_sqw(
        events.get("ESP01_SQW", []), events.get("ESP02_SQW", []),
        args.sqw_pair_bias_us,
    )

    if summaries or counts:
        print("[CAPTURE INTEGRITY]")
        for line in summaries:
            print(line)
        for line in counts:
            print(line)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
