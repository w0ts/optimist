# Linear transient model of the CLK and DO paths: MNA, trapezoid. All units ns, pF, mH? -> V, kohm-free: R ohm, C nF-free below.
import numpy as np
class Ckt:
    def __init__(s): s.n=1; s.R=[];s.C=[];s.L=[]; s.src=[]; s.names={'0':0}
    def node(s,nm=None):
        i=s.n; s.n+=1
        if nm: s.names[nm]=i
        return i
    def r(s,a,b,v): s.R.append((a,b,v))
    def c(s,a,v): s.C.append((a,v))
    def l(s,a,b,v): s.L.append((a,b,v))
    def line(s,a,mm,Z,tdmm,step=4.0):
        k=max(1,int(mm/step+.5)); td=mm*tdmm/1000/k; L=Z*td; C=td/Z*1000
        for i in range(k):
            b=s.node(); s.l(a,b,L); s.c(b,C); a=b
        return a
def sim(ck,drv,src_node,tend,dt=0.01):
    # unknowns: node volts 1..n-1, L currents, and one source current
    N=ck.n-1; M=len(ck.L); U=N+M+1
    G=np.zeros((U,U)); Cm=np.zeros((U,U))
    for a,b,v in ck.R:
        g=1/v
        for p,q,sg in ((a,a,1),(b,b,1),(a,b,-1),(b,a,-1)):
            if p and q: G[p-1,q-1]+=sg*g
    for a,v in ck.C: Cm[a-1,a-1]+=v*1e-3     # pF -> in units of ns/ohm
    for k,(a,b,v) in enumerate(ck.L):
        j=N+k
        if a: G[a-1,j]+=1; G[j,a-1]-=1
        if b: G[b-1,j]-=1; G[j,b-1]+=1
        Cm[j,j]=v
    j=U-1; G[src_node-1,j]+=1; G[j,src_node-1]+=1
    A=Cm/dt+G/2; B=Cm/dt-G/2
    Ai=np.linalg.inv(A); P=Ai@B
    x=np.zeros(U); T=int(tend/dt); out=np.zeros((T,N+1))
    bv=np.zeros(U); u0=drv(0.0)
    for i in range(T):
        u1=drv((i+1)*dt); bv[j]=(u0+u1)/2; x=P@x+Ai@bv; u0=u1; out[i,1:]=x[:N]
    return out
def cross(y,th,dt,rising=True):
    idx=np.argmax(y>=th) if rising else np.argmax(y<=th)
    return idx*dt
VDD=3.3
def clk(Rs,Rser,behind,through_to_B,B_to_J3,ffc=80,Zf=80,card=8.0,esd=2.5,tr=1.0,lcd_flex=60,Cpad=5,Cj=0.8,Ltd=(100,5.3),stub=0,brd_fm1=40,lcd_in=4,show=False):
    ck=Ckt(); s=ck.node('s'); p=ck.node('pad'); ck.r(s,p,Rs); ck.c(p,Cpad)
    a=ck.line(p,brd_fm1,70,5.8); ck.c(a,Cj)
    a=ck.line(a,ffc,Zf,4.5); ck.c(a,Cj)
    B=ck.line(a,through_to_B,*Ltd)
    b2=ck.line(B,B_to_J3,*Ltd); ck.c(b2,Cj); e=ck.line(b2,lcd_flex,Zf,4.5); ck.c(e,lcd_in); ck.names['lcd']=e
    t=ck.line(B,stub,*Ltd) if stub else B
    r=ck.node(); ck.r(t,r,Rser); ck.c(r,.3)
    cd=ck.line(r,behind,*Ltd); ck.c(cd,card+esd+1.0); ck.names['card']=cd
    drv=lambda t: VDD*min(1,max(0,(t-2)/tr))
    y=sim(ck,drv,s,60)
    dt=.01; v=y[:,cd]; 
    t50=cross(v,VDD*.5,dt)-2-tr/2
    tih=cross(v,VDD*.625,dt)-2-tr/2
    return t50,tih,v.max()/VDD,y,ck
def do(Rcard,R1,behind,stub,through_to_J2,J2_to_J3,ffc=80,Zf=80,tr=2.0,card_c=6,esd=2.5,lcd_flex=60,Cpad=5,brd_fm1=40,Cj=0.8,lcd_in=4):
    ck=Ckt(); s=ck.node(); c=ck.node(); ck.r(s,c,Rcard); ck.c(c,card_c+esd)
    a=ck.line(c,behind,100,5.3); r=ck.node(); ck.r(a,r,R1); ck.c(r,.3)
    B=ck.line(r,stub,100,5.3) if stub else r
    t=ck.line(B,J2_to_J3,100,5.3); ck.c(t,Cj); e=ck.line(t,lcd_flex,Zf,4.5); ck.c(e,lcd_in)
    f=ck.line(B,through_to_J2,100,5.3); ck.c(f,Cj); f=ck.line(f,ffc,Zf,4.5); ck.c(f,Cj); f=ck.line(f,brd_fm1,70,5.8); ck.c(f,Cpad)
    drv=lambda t: VDD*min(1,max(0,(t-2)/tr))
    y=sim(ck,drv,s,60)
    v=y[:,f]
    return cross(v,VDD*.5,.01)-2-tr/2, cross(v,VDD*.7,.01)-2-tr/2, v.max()/VDD
V={ # name: Rclk,Rdo, clk(behind,through_to_B,B_to_J3,stub) do(behind,stub,toJ2,toJ3)
 'v6 510/220':(510,220,(46.6,8.9,8.3,0),(5.3,50.2,8.6,8.6)),
 'v6 340/150':(340,150,(46.6,8.9,8.3,0),(5.3,50.2,8.6,8.6)),
 'v6 245/150':(245,150,(46.6,8.9,8.3,0),(5.3,50.2,8.6,8.6)),
 'short 330/150':(330,150,(9.1,10,9.4,3.9),(13.3,1.0,10,9.5)),
 'long 100/47':(100,47,(46.6,8.9,8.3,0),(5.3,50.2,8.6,8.6)),
 'short 100/47':(100,47,(9.1,10,9.4,3.9),(13.3,1.0,10,9.5)),
}
print('variant             Rpad  T_clk50 T_do50 sum  | T_clk(Vih) T_do(0.7) sum | overshoot card, PC8')
for nm,(rc,rd,cv,dv) in V.items():
    for Rs in (35,):
        a,b,o,_,_=clk(Rs,rc,cv[0],cv[1],cv[2],stub=cv[3]); d1,d2,o2=do(40,rd,*dv)
        print(f'{nm:18s} {Rs:4d} {a:7.2f} {d1:7.2f} {a+d1:6.2f} | {b:7.2f} {d2:7.2f} {b+d2:6.2f} | {o:.2f} {o2:.2f}')
print('--- SCK drive (v6 510/220)')
for Rs in (100,60,35,20):
    a,b,o,_,_=clk(Rs,510,46.6,8.9,8.3); print(Rs,round(a,2),round(b,2))
print('--- ribbon length (v6 340/150, Rpad 35)')
for f in (40,60,80,100):
    a,b,o,_,_=clk(35,340,46.6,8.9,8.3,ffc=f); d1,d2,o2=do(40,150,5.3,50.2,8.6,8.6,ffc=f); print(f,round(a,2),round(d1,2),round(a+d1,2))
print('--- ribbon Z (v6 340/150)')
for z in (80,120,160):
    a,b,o,_,_=clk(35,340,46.6,8.9,8.3,Zf=z); d1,d2,o2=do(40,150,5.3,50.2,8.6,8.6,Zf=z); print(z,round(a,2),round(d1,2),round(a+d1,2))
print('--- ESD off / low cap; card C (v6 340/150)')
for e,cd in ((2.5,8),(3.5,10),(0.5,8),(0,8),(2.5,5)):
    a,b,o,_,_=clk(35,340,46.6,8.9,8.3,esd=e,card=cd); print(e,cd,round(a,2),round(b,2))
print('--- bus load: LCD flex + input (v6 340/150), T_do')
for lf,li in ((60,4),(100,6),(60,10)):
    print(lf,li,[round(x,2) for x in do(40,150,5.3,50.2,8.6,8.6,lcd_flex=lf,lcd_in=li)[:2]])
print('--- ringing at the card, low R (edges 1 ns pad, ) and pad HD')
for R in (510,340,100,47,22,0.1):
    for tr in (1.0,):
        a,b,o,y,ck=clk(20,R,9.1,10,9.4,stub=3.9,tr=tr); v=y[:,ck.names['card']]/VDD
        # undershoot after first crossing to 1
        i=np.argmax(v>=.95); print(f'R {R:5} ov {v.max():.2f} min after {v[i:].min():.2f}')
