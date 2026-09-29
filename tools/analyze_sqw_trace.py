#!/usr/bin/env python3
"""Analyze v6.15 firmware SQW timestamps and optionally physical SQW analyzer edges."""
import argparse,re,statistics,math
FW=re.compile(r"SQW_TRACE edge=(\d+) isr_local_us=(-?\d+) delta_us=(-?\d+) delta_minus_1s_us=(-?\d+) isr_core=(-?\d+) task_receive_us=(-?\d+) isr_to_task_us=(-?\d+)")
AN=re.compile(r"ANZ\|EDGE\|\d+\|\d+\|\d+\|([^|]+)\|(\d+)\|(\d+)")
def pct(xs,p):
    y=sorted(xs); q=(len(y)-1)*p; i=int(q); j=min(i+1,len(y)-1); f=q-i; return y[i]*(1-f)+y[j]*f

def affine_resid(x,y):
    n=len(x); xm=sum(x)/n; ym=sum(y)/n; den=sum((v-xm)**2 for v in x); b=sum((a-xm)*(c-ym) for a,c in zip(x,y))/den if den else 1.0; a=ym-b*xm; return a,b,[c-(a+b*v) for v,c in zip(x,y)]

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('firmware_log'); ap.add_argument('--analyzer'); ap.add_argument('--device',default='ESP01_SQW'); a=ap.parse_args()
    txt=open(a.firmware_log,errors='replace').read(); fw=[]
    for m in FW.finditer(txt):
        edge,isr,delta,err,core,task,lat=map(int,m.groups()); fw.append((edge,isr,delta,err,core,task,lat))
    if not fw: raise SystemExit('No SQW_TRACE records found')
    errs=[r[3] for r in fw if r[2]!=0]; lats=[r[6] for r in fw]; cores={c:sum(1 for r in fw if r[4]==c) for c in sorted(set(r[4] for r in fw))}
    print(f"firmware_edges={len(fw)} cores={cores}")
    if errs: print(f"firmware_interval_error_us mean={statistics.mean(errs):.3f} rms={(sum(v*v for v in errs)/len(errs))**0.5:.3f} p95_abs={pct([abs(v) for v in errs],.95):.3f} max_abs={max(abs(v) for v in errs)}")
    print(f"isr_to_task_us median={statistics.median(lats):.1f} p95={pct(lats,.95):.1f} max={max(lats)}")
    if not a.analyzer: return
    atxt=open(a.analyzer,errors='replace').read(); physical=[]
    for m in AN.finditer(atxt):
        dev,ts,level=m.groups()
        if dev==a.device and int(level)==1: physical.append(int(ts))
    print(f"physical_rising_edges={len(physical)}")
    if len(physical)<5 or len(fw)<5: return
    fwt=[r[1] for r in fw]
    best=None
    # Trace starts near START, analyzer can contain a few earlier SQW rises. Search index alignment.
    for poff in range(max(0,len(physical)-len(fwt)-5), min(len(physical),8)):
        n=min(len(fwt),len(physical)-poff)
        if n<5: continue
        _,b,res=affine_resid(fwt[:n],physical[poff:poff+n])
        rms=(sum(v*v for v in res)/n)**0.5
        if best is None or rms<best[0]: best=(rms,poff,n,b,res)
    if best:
        rms,poff,n,b,res=best
        print(f"physical_vs_isr alignment_physical_offset={poff} n={n} fitted_scale={b:.12f} residual_rms_us={rms:.3f} p95_abs_us={pct([abs(v) for v in res],.95):.3f} max_abs_us={max(abs(v) for v in res):.3f}")
        print("Interpretation: sub-us residual => physical edge and firmware ISR timestamp move together; larger firmware-only residual => GPIO ISR service latency/core masking.")
if __name__=='__main__': main()
