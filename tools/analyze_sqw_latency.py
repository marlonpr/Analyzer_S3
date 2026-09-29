#!/usr/bin/env python3
import argparse, re, statistics
from pathlib import Path

FW_RE = re.compile(r"SQW_TRACE sequence=(\d+) local_us=(\d+) core=(-?\d+) refresh_phase=0x([0-9A-Fa-f]+) frame=(\d+) plane=(\d+) row=(\d+) stage=(\d+) network_phase=(\d+)")
ANZ_RE = re.compile(r"ANZ\|EDGE\|\d+\|\d+\|\d+\|ESP01_SQW\|(\d+)\|(\d+)")

def linfit(xs, ys):
    mx=sum(xs)/len(xs); my=sum(ys)/len(ys)
    sxx=sum((x-mx)**2 for x in xs)
    b=sum((x-mx)*(y-my) for x,y in zip(xs,ys))/sxx
    a=my-b*mx
    r=[y-(a+b*x) for x,y in zip(xs,ys)]
    return a,b,r

def main():
    ap=argparse.ArgumentParser(description='Pair v6.16 firmware SQW timestamps with analyzer physical rising edges.')
    ap.add_argument('firmware_log')
    ap.add_argument('analyzer_log')
    ap.add_argument('--threshold-us',type=float,default=3.0)
    args=ap.parse_args()
    fw=[]
    for line in Path(args.firmware_log).read_text(errors='replace').splitlines():
        m=FW_RE.search(line)
        if m:
            fw.append(dict(seq=int(m[1]), t=int(m[2]), core=int(m[3]), phase=int(m[4],16), frame=int(m[5]), plane=int(m[6]), row=int(m[7]), stage=int(m[8]), net=int(m[9])))
    anz=[]
    for line in Path(args.analyzer_log).read_text(errors='replace').splitlines():
        m=ANZ_RE.search(line)
        if m and int(m[2])==1:
            anz.append(int(m[1]))
    if len(fw)<8 or len(anz)<8:
        raise SystemExit(f'need >=8 edges; firmware={len(fw)} analyzer_rising={len(anz)}')
    # Search integer sequence alignment and choose the overlap with minimum residual RMS.
    best=None
    for off in range(-len(fw)+8, len(anz)-7):
        pairs=[]
        for i,f in enumerate(fw):
            j=i+off
            if 0<=j<len(anz): pairs.append((f,anz[j]))
        if len(pairs)<8: continue
        xs=[p[0]['t'] for p in pairs]; ys=[p[1] for p in pairs]
        a,b,res=linfit(xs,ys)
        rms=(sum(x*x for x in res)/len(res))**0.5
        score=(-len(pairs),rms)
        if best is None or score<best[0]: best=(score,off,pairs,a,b,res)
    _,off,pairs,a,b,res=best
    med=statistics.median(res)
    centered=[x-med for x in res]
    rms=(sum(x*x for x in centered)/len(centered))**0.5
    print(f'alignment_offset={off} pairs={len(pairs)} analyzer_per_firmware={b:.12f}')
    print(f'residual_median_us={med:.3f} centered_rms_us={rms:.3f} min_us={min(centered):.3f} max_us={max(centered):.3f}')
    flags=[]
    for (f,at),e in zip(pairs,centered):
        if abs(e)>args.threshold_us:
            flags.append((f,e,at))
    print(f'excursions_abs_gt_{args.threshold_us:g}us={len(flags)}')
    for f,e,at in flags:
        print(f"seq={f['seq']} residual_us={e:+.3f} fw_local_us={f['t']} analyzer_us={at} core={f['core']} frame={f['frame']} plane={f['plane']} row={f['row']} stage={f['stage']} network_phase={f['net']}")

if __name__=='__main__': main()
