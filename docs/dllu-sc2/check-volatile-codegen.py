#!/usr/bin/env python3
"""Read a paused volatile-codegen-probe's JIT; run with sudo --pid PID.
Requires FEX_MAXINST=1 FEX_MULTIBLOCK=0 FEX_BLOCKJITNAMING=1.
No process memory is modified.
"""
import argparse,json,struct
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--pid',type=int,required=True)
p.add_argument('--expect',choices=['mixed','ordered','relaxed'],required=True)
a=p.parse_args();pid=a.pid
lines=Path(f'/tmp/perf-{pid}.map').read_text().splitlines()
result={}
with open(f'/proc/{pid}/mem','rb',buffering=0) as mem:
 for rva in [0x1470,0x1473,0x147c,0x1480,0x1490,0x1493]:
  hit=next(x for x in lines if f'.exe+0x{rva:x} ' in x)
  address,size,_=hit.split(None,2);mem.seek(int(address,16));code=mem.read(int(size,16))
  words=struct.unpack('<'+'I'*(len(code)//4),code[:len(code)//4*4])
  # RCpc loads, release stores, and release stores with signed immediate.
  ordered=sum((x&0x3ffffc00) in [0x38bfc000,0x08dffc00,0x089ffc00] or
              (x&0x3fe00c00)==0x19000000 for x in words)
  result[f'{rva:x}']=ordered
expected={f'{rva:x}':int(a.expect=='ordered' or (a.expect=='mixed' and rva in [0x147c,0x1490,0x1493]))
          for rva in [0x1470,0x1473,0x147c,0x1480,0x1490,0x1493]}
print(json.dumps({'pid':pid,'expected':expected,'actual':result,'pass':result==expected},indent=2))
raise SystemExit(result!=expected)
