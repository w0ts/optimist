import re,sys,math,collections
# per-net routed copper by layer, width set, vias; from .kicad_pcb text
import os
base=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','')
for d in ['archive-v6-tested','../sd-board']:
    s=open(base+d+'/sd-board.kicad_pcb').read()
    names={m[0]:m[1] for m in re.findall(r'\(net (\d+) "([^"]*)"\)',s)}
    L=collections.defaultdict(lambda:collections.Counter()); W=collections.defaultdict(set); V=collections.Counter()
    for m in re.finditer(r'\(segment\s+\(start ([\d.\-]+) ([\d.\-]+)\)\s+\(end ([\d.\-]+) ([\d.\-]+)\)\s+\(width ([\d.]+)\)\s+\(layer "([^"]+)"\)\s+(?:\(locked [a-z]+\)\s+)?\(net (?:"([^"]*)"|(\d+))\)',s):
        x0,y0,x1,y1,w,lay,nn,ni=m.groups()
        n=nn if nn else names.get(ni,ni)
        l=math.hypot(float(x1)-float(x0),float(y1)-float(y0)); L[n][lay]+=l; W[n].add(w)
    for m in re.finditer(r'\(via\b.*?\(net (?:"([^"]*)"|(\d+))\)',s,re.S):
        nn,ni=m.groups(); V[nn if nn else names.get(ni,ni)]+=1
    print('==',d, re.search(r'\(thickness ([\d.]+)\)',s).group(1))
    for n in sorted(L):
        if n=='' : continue
        print(f"  {n:12s} F {L[n]['F.Cu']:6.1f} B {L[n]['B.Cu']:6.1f} tot {sum(L[n].values()):6.1f} vias {V[n]:2d} w {sorted(W[n])}")
    print('  GND vias',V.get('GND',0))
