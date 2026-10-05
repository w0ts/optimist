# SPDX-License-Identifier: GPL-3.0-only
# DX7 engine vs msfa (tools/dx7_ref/compare.sh)
import random, struct, subprocess, sys
import os
R=os.environ.get('DX7_RENDER', 'build/dx7_render')
REF=os.environ.get('DX7_REF', 'build/dx7_ref/ref_render')
VDIR=os.environ.get('DX7_VDIR', 'build/dx7_ref')
OPMAX=[99,99,99,99,99,99,99,99,99,99,99,3,3,7,3,7,99,1,31,99,14]
GLMAX=[99,99,99,99,99,99,99,99,31,7,1,99,99,99,99,1,5,7,48]
def run(cmd):
    b=subprocess.run(cmd,capture_output=True).stdout
    return struct.unpack('<%di'%(len(b)//4), b)
random.seed(int(sys.argv[2]) if len(sys.argv)>2 else 1)
ams_mode=sys.argv[3] if len(sys.argv)>3 else 'noams'
worst=[]
for t in range(int(sys.argv[1])):
    v=[]
    for op in range(6):
        o=[random.randint(0,m) for m in OPMAX]
        o[17]= 1 if random.random()<0.1 else 0
        if ams_mode=='noams': o[14]=0
        v+=o
    g=[random.randint(0,m) for m in GLMAX]
    g[18]=random.randint(12,36)
    v+=g+[65]*10
    open(os.path.join(VDIR, 'fz.bin'),'wb').write(bytes(v))
    note=random.randint(24,96); vel=random.randint(1,127)
    a=run([R,'0',str(note),str(vel),'300','200',os.path.join(VDIR, 'fz.bin')])
    b=run([REF,os.path.join(VDIR, 'fz.bin'),str(note),str(vel),'300','200'])
    d=max(abs(x-y) for x,y in zip(a,b)); peak=max(1,max(abs(x) for x in b))
    worst.append((d/peak,d,t,note,vel))
worst.sort(reverse=True)
print('exact', sum(1 for w in worst if w[1]==0), 'of', len(worst)); print('worst', worst[:5])
