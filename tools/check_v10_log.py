#!/usr/bin/env python3
"""Replay raw V10 captures with the same retrospective grouping as firmware."""
import argparse
from pathlib import Path
from analyzer_v10 import load_capture

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('log',type=Path)
    ap.add_argument('--train-min-us',type=int,default=995000)
    ap.add_argument('--train-max-us',type=int,default=1005000)
    ap.add_argument('--associate-us',type=int,default=5000)
    ap.add_argument('--new-train-gap-us',type=int,default=1500000)
    ap.add_argument('--start-boundaries',type=int,default=3)
    ap.add_argument('--limit-us',type=int,default=100)
    ap.add_argument('--expect-start-ranges',help='Comma-separated exact boundary-0 ranges, e.g. 46,48,68')
    args=ap.parse_args()
    result=load_capture(args.log,train_min=args.train_min_us,train_max=args.train_max_us,
        associate_us=args.associate_us,new_train_gap_us=args.new_train_gap_us)
    print('run,trial,train,boundary,ch1_us,ch2_us,ch3_us,range_us,gated,result')
    for g in result.groups:
        gated=g.boundary<args.start_boundaries
        verdict=('PASS' if g.span_us<=args.limit_us else 'TRIP') if gated else 'INFO'
        print(f'{g.run},{g.trial},{g.train},{g.boundary},{g.timestamps[0]},{g.timestamps[1]},'
              f'{g.timestamps[2]},{g.span_us},{int(gated)},{verdict}')
    starts=[]
    for i,(key,groups) in enumerate(result.trains().items(),1):
        starts.append(groups[0].span_us)
        print(f'TRAIN{i}_START_RANGE_US={groups[0].span_us}')
        print(f'TRAIN{i}_START_GATE_WORST_US={max(g.span_us for g in groups[:args.start_boundaries])}')
    passed=result.passes(args.limit_us,args.start_boundaries)
    if args.expect_start_ranges:
        expected=[int(x) for x in args.expect_start_ranges.split(',')]
        matched=starts==expected
        print(f'EXPECTED_START_RANGES_MATCH={"PASS" if matched else "FAIL"}')
        passed=passed and matched
    print(f'REPLAY_SOURCE={result.source}')
    print(f'DETECTED_TRAINS={len(result.trains())}')
    print(f'INCOMPLETE_GROUPS={len(result.missing)}')
    print(f'DROPPED={result.dropped}')
    for error in result.errors: print(f'ERROR={error}')
    print(f'OVERALL={"PASS" if passed else "FAIL"}')
    return 0 if passed else 2

if __name__=='__main__': raise SystemExit(main())
