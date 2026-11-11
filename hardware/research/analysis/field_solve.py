# 2D electrostatic solve: capacitance per mm and Z0 of the board traces and the FFC
import numpy as np
E0=8.854e-3 # pF/mm
c=299.79    # mm/ns
def solve(nx,ny,er,cond,target,grounds,dx,it=None):
    """cond: int map (0 free, k>0 conductor k). returns charge per mm (pF/mm) on each conductor with target at 1 V"""
    V=np.zeros((ny,nx)); fix=cond>0
    V[cond==target]=1.0
    # edge permittivities between nodes
    ex=(er[:,1:]+er[:,:-1])/2; ey=(er[1:,:]+er[:-1,:])/2
    Y,X=np.mgrid[0:ny,0:nx]; red=((X+Y)%2==0)&~fix; blk=((X+Y)%2==1)&~fix
    red[0,:]=red[-1,:]=False; red[:,0]=red[:,-1]=False; blk[0,:]=blk[-1,:]=False; blk[:,0]=blk[:,-1]=False
    w=1.9 if it is None else 1.9
    D=np.zeros((ny,nx)); D[:,1:]+=ex; D[:,:-1]+=ex; D[1:,:]+=ey; D[:-1,:]+=ey
    for n in range(it or 6000):
        for m in (red,blk):
            S=np.zeros((ny,nx))
            S[:,1:]+=ex*V[:,:-1]; S[:,:-1]+=ex*V[:,1:]; S[1:,:]+=ey*V[:-1,:]; S[:-1,:]+=ey*V[1:,:]
            V[m]+=w*(S[m]/D[m]-V[m])
    # charge on target = sum of flux out of its nodes
    Q=0.0
    t=cond==target
    Fx=ex*(V[:,1:]-V[:,:-1]); Fy=ey*(V[1:,:]-V[:-1,:])
    Q=(Fx*(t[:,:-1]&~t[:,1:])).sum()-(Fx*(t[:,1:]&~t[:,:-1])).sum()+(Fy*(t[:-1,:]&~t[1:,:])).sum()-(Fy*(t[1:,:]&~t[:-1,:])).sum()
    return -Q*E0
def pcb(h,w=0.2,gap=0.25,pour=True,t=0.035,er_fr4=4.5,neigh=None):
    d=0.01; W=int(5/d); H=int((h+2.5)/d)
    cond=np.zeros((H,W),int); er=np.ones((H,W))
    y0=int(1.0/d)                    # top of ground plane
    cond[:y0,:]=2
    ys=y0+int(h/d)
    er[y0:ys,:]=er_fr4
    tc=max(int(t/d),2)
    cx=W//2; hw=int(w/2/d)
    cond[ys:ys+tc,cx-hw:cx+hw]=1
    if pour:
        g=int(gap/d)
        cond[ys:ys+tc,:cx-hw-g]=2; cond[ys:ys+tc,cx+hw+g:]=2
    cond[-1,:]=0
    def q(er): return solve(W,H,er,cond,1,[2],d,it=2500)
    C=q(er); C0=q(np.ones_like(er))
    Z=1/(c*np.sqrt(C*C0))*1e3*1e3/1e3 # ohm: 1/(c*sqrt(C C0)) with C in pF/mm, c mm/ns => ns/pF=1000 ohm
    return C,Z,np.sqrt(C/C0)/c*1000
for name,args in [('1.6 mm, 0.2 w, pour gap 0.25',(1.51,0.2,0.25,True)),('1.6 mm, 0.2 w, no top pour',(1.51,0.2,0.25,False)),('1.0 mm, 0.2 w, pour gap 0.25',(0.91,0.2,0.25,True)),('1.0 mm, 0.2 w, no top pour',(0.91,0.2,0.25,False)),('1.6 mm 0.4 w, gap 0.2',(1.51,0.4,0.2,True)),('4-layer 0.1 mm prepreg 0.15 w',(0.1,0.15,0.2,True)),('1.0 mm, 0.5 w, gap 0.2',(0.91,0.5,0.2,True))]:
    C,Z,td=pcb(*args); print(f"{name:34s} C {C:.3f} pF/mm Z0 {Z:5.1f} ohm  delay {td:.2f} ps/mm")
# FFC 10 lines, 0.5 pitch, 0.3 wide 0.05 thick, PET er 3.2 0.06 each side
d=0.01; W=int(9/d); H=int(6/d)
base=np.zeros((H,W),int); er=np.ones((H,W))
ym=H//2; tc=5; yt=ym-3
er[yt-6:yt+tc+6,int(1.75/d):int(1.75/d)+int(6/d)]=3.2
xs=[int((2.0+0.5*i)/d) for i in range(10)]
def line(i,val):
    pass
hw=15
role={1:'GND',2:'DC',3:'CS',4:'CLK',5:'MOSI',6:'x',7:'3V3',8:'RST',9:'BL',10:'GND'}
def ffc(grounds,target=4,metal=None):
    cond=np.zeros((H,W),int)
    for i in range(1,11):
        k=2 if i in grounds else 0
        if i==target:k=1
        if k: cond[yt:yt+tc,xs[i-1]-hw:xs[i-1]+hw]=k
    # undriven lines: treat as dielectric-free floats (ignored)
    if metal is not None:
        cond[yt+int(metal/d):,:]=2
    C=solve(W,H,er,cond,1,None,d,it=2500); C0=solve(W,H,np.ones_like(er),cond,1,None,d,it=2500)
    return C,1/(c*np.sqrt(C*C0))*1e3,np.sqrt(C/C0)/c*1000
for nm,g,m in [('all lines driven (AC ground)',{1,2,3,5,6,7,8,9,10},None),('only GND+3V3 return',{1,7,10},None),('all driven, lying on ground 0.5 mm',{1,2,3,5,6,7,8,9,10},0.5)]:
    C,Z,td=ffc(g,4,m); print(f"FFC CLK {nm:36s} C {C:.3f} pF/mm Z0 {Z:5.1f} delay {td:.2f} ps/mm")
