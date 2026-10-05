# SPDX-License-Identifier: GPL-3.0-only
# DX7 engine vs msfa (tools/dx7_ref/compare.sh)
import sys, struct, subprocess
import os
R=os.environ.get('DX7_RENDER', 'build/dx7_render')
REF=os.environ.get('DX7_REF', 'build/dx7_ref/ref_render')
VDIR=os.environ.get('DX7_VDIR', 'build/dx7_ref')
def run(cmd):
    b=subprocess.run(cmd,capture_output=True).stdout
    return struct.unpack('<%di'%(len(b)//4), b)
res=[]
for vi in range(int(sys.argv[1])):
    for note,vel in ((36,100),(60,64),(60,127),(84,90)):
        a=run([R,'0',str(note),str(vel),'600','400',os.path.join(VDIR, 'v%02d.bin'%vi)])   # (the DX7 part: OP7 / OP8 off)
        b=run([REF,os.path.join(VDIR, 'v%02d.bin'%vi),str(note),str(vel),'600','400'])
        n=min(len(a),len(b)); diff=[abs(a[i]-b[i]) for i in range(n)]
        peak=max(abs(x) for x in b) or 1
        mx=max(diff); first=next((i for i,d in enumerate(diff) if d),-1)
        res.append((vi,note,vel,mx,mx/peak,first,peak))
        print(vi,note,vel,'maxdiff',mx,'rel %.2e'%(mx/peak),'first',first,'peak',peak)
