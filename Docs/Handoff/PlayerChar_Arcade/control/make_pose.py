# Build an OpenPose (COCO-18) control image straight from the ENE spec numbers.
# Spec source: Docs/PlayerCharacter_ResumePrompt.md 2-1..2-3
#   total 161.25cm = 43 layers, 5 heads tall, hip->sole = 53.5% of height,
#   shoulder width 30cm, skull width 26.25cm, arm (shoulder->fingertip) 60cm.
# Rendering follows the canonical ControlNet annotator (limb order + colors),
# otherwise the model will not read it as a pose.
import sys, math
from PIL import Image, ImageDraw

W, H = 832, 1216
TOP, SOLE = 60, 1140          # figure occupies this vertical span
FIG = SOLE - TOP              # 1080 px  == 161.25 cm
CX = W // 2
def cm(v): return v / 161.25 * FIG   # cm -> px

HEAD = FIG / 5.0                      # 5 heads tall  -> 216 px
CHIN = TOP + HEAD
NECK = TOP + HEAD * 1.11              # short neck (spec: 1 layer)
SHLD = NECK + cm(4)
HIP   = SOLE - 0.535 * FIG            # hip->sole 53.5%
KNEE  = HIP + (SOLE - HIP) * 0.50
ANKLE = SOLE - cm(15)                 # boot height 15cm

hw  = cm(30) / 2                      # half shoulder width
hip_w = cm(22) / 2
ear = cm(26.25) / 2 * 0.75

# A-pose: arms 45 degrees down-and-out
UA, FA = cm(26), cm(22)
d = math.sqrt(0.5)
ELx, ELy = hw + UA * d, SHLD + UA * d
WRx, WRy = ELx + FA * d, ELy + FA * d

# COCO-18: 0 nose 1 neck 2 Rsho 3 Relb 4 Rwri 5 Lsho 6 Lelb 7 Lwri
#          8 Rhip 9 Rkne 10 Rank 11 Lhip 12 Lkne 13 Lank 14 Reye 15 Leye 16 Rear 17 Lear
P = [
 (CX,            TOP + HEAD * 0.72),          # nose (low on a big head)
 (CX,            NECK),
 (CX - hw,       SHLD), (CX - ELx, ELy), (CX - WRx, WRy),
 (CX + hw,       SHLD), (CX + ELx, ELy), (CX + WRx, WRy),
 (CX - hip_w,    HIP),  (CX - hip_w, KNEE), (CX - hip_w, ANKLE),
 (CX + hip_w,    HIP),  (CX + hip_w, KNEE), (CX + hip_w, ANKLE),
 (CX - cm(4),    TOP + HEAD * 0.60), (CX + cm(4), TOP + HEAD * 0.60),
 (CX - ear,      TOP + HEAD * 0.62), (CX + ear,   TOP + HEAD * 0.62),
]

LIMBS = [(1,2),(1,5),(2,3),(3,4),(5,6),(6,7),(1,8),(8,9),(9,10),
         (1,11),(11,12),(12,13),(1,0),(0,14),(14,16),(0,15),(15,17)]
COLORS = [(255,0,0),(255,85,0),(255,170,0),(255,255,0),(170,255,0),(85,255,0),
          (0,255,0),(0,255,85),(0,255,170),(0,255,255),(0,170,255),(0,85,255),
          (0,0,255),(85,0,255),(170,0,255),(255,0,255),(255,0,170),(255,0,85)]

img = Image.new('RGB', (W, H), (0, 0, 0))
dr = ImageDraw.Draw(img)
for i, (a, b) in enumerate(LIMBS):                 # limbs first, then joints
    dr.line([P[a], P[b]], fill=COLORS[i % len(COLORS)], width=10)
for i, pt in enumerate(P):
    r = 5
    dr.ellipse([pt[0]-r, pt[1]-r, pt[0]+r, pt[1]+r], fill=COLORS[i % len(COLORS)])

out = sys.argv[1] if len(sys.argv) > 1 else 'pose_ene_front.png'
img.save(out)
print('saved   :', out)
print('heads   : %.2f  (target 5.00)' % (FIG / HEAD))
print('leg pct : %.1f%% (target 53.5)' % ((SOLE - HIP) / FIG * 100))
print('shoulder: %.0f px = %.1f cm' % (hw * 2, hw * 2 / FIG * 161.25))
