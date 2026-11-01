#!/usr/bin/env python3
"""Inspect a held probe's actual JIT code without modifying process memory."""
import argparse, json, subprocess
from pathlib import Path
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
p=argparse.ArgumentParser()
p.add_argument('pid',type=int);p.add_argument('--expect',choices=['on','off'],required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
exe=Path(__file__).with_name('dereference-prefetch-probe.exe')
nm='/home/dllu/wines/build/toolchains/llvm-mingw-20261006-ucrt-ubuntu-22.04-aarch64/bin/llvm-nm'
names={'selected':1,'selected_negative':1,'selected_odd':1,'selected_jump':1,'immediate':0,'overwritten':0,'skipped':1}
symbols={fields[2]:int(fields[0],16)-0x140000000 for line in subprocess.check_output([nm,str(exe)],text=True).splitlines() if len(fields:=line.split())==3 and fields[2] in names}
entries=Path(f'/tmp/perf-{a.pid}.map').read_text().splitlines()
cs=Cs(CS_ARCH_ARM64,CS_MODE_ARM);cs.skipdata=True
result={}
with open(f'/proc/{a.pid}/mem','rb',buffering=0) as mem:
 for name,rva in symbols.items():
  hits=[line.split(maxsplit=2) for line in entries if f'.exe+0x{rva:x} ' in line or f'JIT_0x{0x140000000+rva:x}_' in line]
  if not hits: raise SystemExit('missing compiled function '+name)
  # First subblock is the entry, where the hint must be. Later subblocks for
  # conditional fallthrough share the root label but cannot contain this hint.
  address,size,_=hits[0];address=int(address,16);size=int(size,16)
  mem.seek(address);code=mem.read(size)
  instructions=list(cs.disasm(code,address))
  hints=[f'{i.mnemonic} {i.op_str}' for i in instructions if i.mnemonic in ['prfm','prfum']]
  ordered=sum((int.from_bytes(code[i:i+4],'little')&0x3ffffc00)==0x38bfc000 for i in range(0,len(code)-3,4))
  expected=names[name] if a.expect=='on' else 0
  result[name]={'hints':hints,'expected_hints':expected,'ordered_loads':ordered,
                'pass':len(hints)==expected and ordered>=1,'host_bytes':code.hex(),
                'asm':[f'{i.address:x}: {i.mnemonic} {i.op_str}' for i in instructions]}
output={'pid':a.pid,'expect':a.expect,'pass':all(x['pass'] for x in result.values()),'functions':result}
a.output.write_text(json.dumps(output,indent=2)+'\n')
print(json.dumps({name:{k:v for k,v in r.items() if k not in ['host_bytes','asm']} for name,r in result.items()},indent=2))
raise SystemExit(not output['pass'])
