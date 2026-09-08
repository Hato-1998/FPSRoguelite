# Compare upper-body vs lower-body widths - the ratio the eye actually reads.
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB'); W,H = im.size; px = im.load()
bg = px[4,4]
def fig(c): return abs(c[0]-bg[0])+abs(c[1]-bg[1])+abs(c[2]-bg[2]) > 40
ys=[y for y in range(H) if any(fig(px[x,y]) for x in range(0,W,3))]
top,sole=ys[0],ys[-1]; FIG=sole-top; CM=161.25/FIG
def runs_at(f):
    y=int(top+FIG*f); out=[];cur=None
    for x in range(W):
        if fig(px[x,y]): cur = x if cur is None else cur
        elif cur is not None: out.append((cur,x-1)); cur=None
    if cur is not None: out.append((cur,W-1))
    return y,[r for r in out if r[1]-r[0]>3]
print('scale: 1px = %.3f cm | figure %d px' % (CM,FIG))
print()
for label,f in [('chest (tank top)',0.30),('waist',0.38),('hip / shorts',0.46),('upper thigh',0.52)]:
    y,rs=runs_at(f)
    tot=sum((b-a+1) for a,b in rs)*CM
    span=(rs[-1][1]-rs[0][0]+1)*CM if rs else 0
    mid=max(rs,key=lambda r:r[1]-r[0]) if rs else None
    midw=(mid[1]-mid[0]+1)*CM if mid else 0
    print('%-18s y=%4d  runs=%d  widest=%5.1fcm(%.1fvox)  outer span=%5.1fcm' % (label,y,len(rs),midw,midw/3.75,span))
