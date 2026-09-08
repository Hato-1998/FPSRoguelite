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

# taper: thigh 15cm -> calf 11cm. The model drew 6.5/5.8cm from an 11cm capsule,
# so the blockout has to overshoot the spec, not merely match it.
TH, CA = cm(17) / 2, cm(12) / 2
KNEE = HIP + (ANKLE - HIP) * 0.52
JT = NECK + cm(2)                  # jacket top
JB = HIP + cm(30)                  # FULLY closed jacket - hem well past mid-thigh

if VIEW == 'side':
    # ---- profile, facing right (+x = forward) ------------------------------
    # Rebuilt 2026-09-08. The old branch reused the FRONT polygons with narrower
    # widths, so the A-pose arm still swept 45 deg ACROSS the frame. In a real
    # profile that arm is edge-on and hangs nearly straight down; the angled
    # version read as "arm reaching forward" and the model answered with a 3/4
    # view (v8b). The other half of the fix is front/back asymmetry - a profile
    # silhouette that is mirror-symmetric carries no cue for which way the
    # figure faces, so the model falls back to the pose it likes best, 3/4.
    DEP = cm(18.75)                # torso DEPTH is what the width means here
    JD  = DEP / 2 * 1.35           # the oversized jacket is deeper than the torso
    # leg - one column, edge-on
    cap(CX - TH, HIP, CX + TH, KNEE, int(TH), 150)
    cap(CX - CA, KNEE - cm(3), CX + CA, ANKLE, int(CA), 150)
    # boot - 30cm long, toe forward
    bh = cm(15)
    cap(CX - cm(10), SOLE - bh, CX + cm(20), SOLE, int(bh * 0.45), 180)
    # torso
    cap(CX - DEP / 2, NECK, CX + DEP / 2, HIP + cm(4), int(cm(6)), 170)
    # jacket - closed and long. Front placket near-vertical, back hem swings out.
    d.polygon([(CX - JD * 0.95, JT),
               (CX + JD,        JT),
               (CX + JD * 0.90, HIP),
               (CX + JD * 0.80, JB),
               (CX - JD * 1.00, JB),
               (CX - JD * 1.10, HIP)], fill=165)
    # near arm - hangs in front of the torso and drifts forward only slightly.
    # Brighter than the jacket: in profile it is the camera-side limb.
    sx, sy = CX + cm(3), NECK + cm(6)
    ex, ey = sx + cm(4), sy + cm(26) * 0.97
    wx, wy = ex + cm(4), ey + cm(22) * 0.97
    d.line([(sx, sy), (ex, ey)], fill=185, width=int(cm(13)))   # puffy sleeve
    d.line([(ex, ey), (wx, wy)], fill=182, width=int(cm(9)))
    d.ellipse([wx - cm(4.5), wy - cm(4.5), wx + cm(4.5), wy + cm(4.5)], fill=182)
    # head - bob. Nape lobe behind, goggle mass in front: back-heavy hair plus a
    # forward face is the strongest "which way is forward" cue a profile has.
    hx = CX - cm(1.5)
    d.ellipse([hx - HEAD_W / 2, CHIN - cm(15), hx + cm(3), CHIN + cm(5)], fill=200)
    d.ellipse([hx - HEAD_W / 2, TOP, hx + HEAD_W / 2, CHIN], fill=210)
    d.ellipse([hx + cm(7), CHIN - cm(21), hx + cm(23), CHIN - cm(3)], fill=214)
else:
    # ---- front / back ------------------------------------------------------
    # 'back' is the mirror of 'front' and shares this geometry on purpose:
    # identical height and scale across the two sheets is what Meshy needs.
    LEG_XS = [-cm(4.5), cm(4.5)]   # hips must NOT exceed the torso (26cm):
                                   # span = 2*(4.5+8.5) = 26cm exactly
    for lx in LEG_XS:
        x = CX + lx
        cap(x - TH, HIP, x + TH, KNEE, int(TH), 150)
        cap(x - CA, KNEE - cm(3), x + CA, ANKLE, int(CA), 150)
    # boots - oversized, wider than the leg
    bw, bh = cm(17) / 2, cm(15)
    for lx in LEG_XS:
        x = CX + lx
        cap(x - bw, SOLE - bh, x + bw, SOLE, int(bh * 0.45), 175)
    # torso
    tw = cm(26) / 2
    cap(CX - tw, NECK, CX + tw, HIP + cm(4), int(cm(6)), 170)
    # jacket - wide at the shoulders, tapering all the way down. A wide panel
    # that ends abruptly at hip height is what produced the protruding side
    # flaps before; a continuous taper past the hip reads as one closed garment.
    jw = SHO_W / 2 * 1.15
    d.polygon([(CX - jw,        JT),
               (CX + jw,        JT),
               (CX + jw * 0.70, HIP),
               (CX + jw * 0.55, JB),
               (CX - jw * 0.55, JB),
               (CX - jw * 0.70, HIP)], fill=165)
    # arms, A-pose 45 deg
    UA, FA, aw = cm(26), cm(22), cm(9) / 2
    dd = math.sqrt(0.5)
    for s in [-1, 1]:
        sx, sy = CX + s * jw * 0.9, NECK + cm(6)
        ex, ey = sx + s * UA * dd, sy + UA * dd
        wx, wy = ex + s * FA * dd, ey + FA * dd
        d.line([(sx, sy), (ex, ey)], fill=160, width=int(cm(13)))     # puffy sleeve
        d.line([(ex, ey), (wx, wy)], fill=155, width=int(aw * 2))
        d.ellipse([wx - aw, wy - aw, wx + aw, wy + aw], fill=155)
    # head mass LAST and brightest - the item every previous attempt failed
    d.ellipse([CX - HEAD_W / 2, TOP, CX + HEAD_W / 2, CHIN], fill=210)

img = img.filter(ImageFilter.GaussianBlur(6))
out = sys.argv[1] if len(sys.argv) > 1 else ('depth_ene_%s.png' % VIEW)
img.convert('RGB').save(out)
print('view      :', VIEW)
print('saved     :', out)
print('heads     : %.2f' % (FIG / HEAD_H))
if VIEW == 'side':
    print('head/depth: %.2f  (hair mass vs torso depth)' % (HEAD_W / cm(18.75)))
else:
    print('head/shldr: %.2f  (target 1.35)' % (HEAD_W / SHO_W))
print('leg pct   : %.1f%%' % ((SOLE - HIP) / FIG * 100))
