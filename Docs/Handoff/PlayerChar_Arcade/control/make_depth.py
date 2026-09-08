# Depth blockout for ENE - constrains the SILHOUETTE (head size included),
# which OpenPose cannot do: face keypoints are too sparse to beat the model's
# head-to-body prior. Numbers come from Docs/PlayerCharacter_ResumePrompt.md 2-1..2-3.
import sys, math
from PIL import Image, ImageDraw, ImageFilter

VIEW = (sys.argv[2] if len(sys.argv) > 2 else 'front').lower()

W, H = 832, 1216
TOP, SOLE = 70, 1150
FIG = SOLE - TOP
CX = W // 2
def cm(v): return v / 161.25 * FIG

HEAD_H = FIG / 5.0                 # 5 heads tall
HEAD_W = cm(41.25)                 # hair mass 41.25cm  > shoulders 30cm
SHO_W  = cm(30)
CHIN   = TOP + HEAD_H
NECK   = CHIN + cm(3.75)
HIP    = SOLE - 0.535 * FIG
ANKLE  = SOLE - cm(15)

img = Image.new('L', (W, H), 0)
d = ImageDraw.Draw(img)
def cap(x0, y0, x1, y1, r, v):     # rounded capsule
    # radius must stay under half of BOTH sides or PIL raises
    r = int(max(0, min(r, (x1 - x0) / 2 - 1, (y1 - y0) / 2 - 1)))
    d.rounded_rectangle([x0, y0, x1, y1], radius=r, fill=v)

SIDE = (VIEW == 'side')
if SIDE:
    # side view: torso DEPTH becomes the width; head keeps its depth (still big)
    HEAD_W = cm(41.25)
    SHO_W  = cm(18.75)

# legs (leggings) - thin, they are the part that must NOT dominate
lw = cm(11) / 2
LEG_XS = [0] if SIDE else [-cm(6), cm(6)]   # pull legs inward: hips must not exceed shoulders
# taper: thigh 15cm -> calf 11cm. The model drew 6.5/5.8cm from an 11cm capsule,
# so the blockout has to overshoot the spec, not merely match it.
TH, CA = cm(17) / 2, cm(12) / 2
KNEE = HIP + (ANKLE - HIP) * 0.52
for lx in LEG_XS:
    x = CX + lx
    cap(x - TH, HIP, x + TH, KNEE, int(TH), 150)
    cap(x - CA, KNEE - cm(3), x + CA, ANKLE, int(CA), 150)
# boots - oversized, wider than the leg
bw, bh = (cm(30) / 2 if SIDE else cm(17) / 2), cm(15)
for lx in LEG_XS:
    x = CX + lx + (cm(5) if SIDE else 0)   # side: boot toe points forward
    cap(x - bw, SOLE - bh, x + bw, SOLE, int(bh * 0.45), 175)
# torso
tw = cm(26) / 2
cap(CX - tw, NECK, CX + tw, HIP + cm(4), int(cm(6)), 170)
# jacket shoulders (oversized) - still narrower than the head mass
jw = SHO_W / 2 * 1.15
cap(CX - jw, NECK + cm(2), CX + jw, HIP - cm(6), int(cm(8)), 165)
# arms, A-pose 45 deg
UA, FA, aw = cm(26), cm(22), cm(9) / 2
dd = math.sqrt(0.5)
for s in ([1] if SIDE else [-1, 1]):
    sx, sy = CX + s * jw * 0.9, NECK + cm(6)
    ex, ey = sx + s * UA * dd, sy + UA * dd
    wx, wy = ex + s * FA * dd, ey + FA * dd
    d.line([(sx, sy), (ex, ey)], fill=160, width=int(cm(13)))     # puffy sleeve
    d.line([(ex, ey), (wx, wy)], fill=155, width=int(aw * 2))
    d.ellipse([wx - aw, wy - aw, wx + aw, wy + aw], fill=155)
# head mass LAST and brightest - this is the item every previous attempt failed
d.ellipse([CX - HEAD_W / 2, TOP, CX + HEAD_W / 2, CHIN], fill=210)

img = img.filter(ImageFilter.GaussianBlur(6))
out = sys.argv[1] if len(sys.argv) > 1 else ('depth_ene_%s.png' % VIEW)
img.convert('RGB').save(out)
print('view      :', VIEW)
print('saved     :', out)
print('heads     : %.2f' % (FIG / HEAD_H))
print('head/shldr: %.2f  (target 1.35)' % (HEAD_W / SHO_W))
print('leg pct   : %.1f%%' % ((SOLE - HIP) / FIG * 100))
