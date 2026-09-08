# Silhouette proportions of a generated plate - the number 0-A argues about (heads).
#
# measure.py samples widths at FIXED height fractions, so it points at the wrong
# body part the moment the silhouette changes (its own header says so). That is
# fatal for a cross-checkpoint A/B, where the silhouette is exactly what moves.
# Head height is found FROM the silhouette instead: the neck pinch = the narrowest
# outer span in the upper band. Same code therefore means the same body part on a
# leggy plate and a chibi one.
#
# CALIBRATION WARNING - read before quoting a number. The pinch sits at the top of
# the COLLAR, not at the chin, and the head mass it measures includes hair volume
# and headphones. So "heads" here is systematically MORE generous (smaller number)
# than the 턱~정수리 definition in spec 2-1. Use it to compare plates against each
# other, which is what it is calibrated for; do not overwrite the spec table with it.
# Run with --debug to get an overlay showing exactly where the cuts landed.
#
# Two metrics were tried and REMOVED because a control run proved they measured
# something other than their label (v19 vs noobai, 2026-09-08):
#   - "leg %" via the crotch split -> tracks how far apart the legs are drawn, not
#     leg length. noobai draws its legs touching, so it scored 21% against v19's 46%
#     while visibly having the LONGER legs.
#   - "head vs shoulders" against the widest row -> the widest row is the A-pose arm
#     span, not the shoulders, so both plates scored ~0.35 regardless of head size.
#
#   python measure_proportions.py <png> [<png> ...] [--debug]
#
import sys, os
from PIL import Image, ImageDraw

def load(path):
    im = Image.open(path).convert('RGB')
    W, H = im.size
    px = im.load()
    bg = px[4, 4]                               # flat backdrop corner, as in measure.py
    def is_fig(c):
        return abs(c[0]-bg[0]) + abs(c[1]-bg[1]) + abs(c[2]-bg[2]) > 40
    rows = []
    for y in range(H):
        runs, cur = [], None
        for x in range(W):
            if is_fig(px[x, y]):
                cur = x if cur is None else cur
            elif cur is not None:
                runs.append((cur, x-1)); cur = None
        if cur is not None:
            runs.append((cur, W-1))
        rows.append([r for r in runs if r[1]-r[0] > 3])   # ignore lineart specks
    return im, rows

def span(runs):
    return 0 if not runs else runs[-1][1] - runs[0][0] + 1

def measure(path, debug=False):
    im, rows = load(path)
    ys = [y for y, r in enumerate(rows) if r]
    if not ys:
        print('%-46s (no figure found)' % os.path.basename(path)); return
    top, sole = ys[0], ys[-1]
    FIG = sole - top

    # Above 8% we are still inside the head mass; below 32% the shoulders have
    # already flared. Headphones widen the head, which only deepens the pinch.
    lo, hi = top + int(FIG*0.08), top + int(FIG*0.32)
    neck_y = min(range(lo, hi), key=lambda y: span(rows[y]) or 10**6)
    head_h = neck_y - top
    heads  = FIG / head_h if head_h else 0

    head_w  = max(span(rows[y]) for y in range(top, neck_y + 1))
    # Shoulder line: just under the pinch, before the arms swing out. In an A-pose
    # this still catches some upper arm, so it is an upper bound on shoulder width -
    # which is the conservative direction for the "head wider than shoulders" test.
    sh_y    = min(sole, neck_y + int(FIG*0.045))
    shoulder = span(rows[sh_y])

    print('%-46s' % os.path.basename(path))
    print('   figure %4dpx  head %3dpx  ->  %5.2f heads   (spec 2-3 target 5.375; see calibration note)'
          % (FIG, head_h, heads))
    print('   head %4dpx  vs shoulder line %4dpx  =  %.2f   (spec 2-2 wants > 1.0)'
          % (head_w, shoulder, head_w / shoulder if shoulder else 0))

    if debug:
        d = ImageDraw.Draw(im)
        for y, col, lab in ((top, (255, 0, 0), 'top'), (neck_y, (255, 0, 255), 'neck pinch'),
                            (sh_y, (0, 160, 255), 'shoulder'), (sole, (255, 0, 0), 'sole')):
            d.line([(0, y), (im.size[0], y)], fill=col, width=3)
            d.text((6, max(0, y-16)), lab, fill=col)
        out = os.path.join(os.path.dirname(path) or '.', '_dbg_' + os.path.basename(path))
        im.save(out); print('   debug overlay ->', out)

dbg = '--debug' in sys.argv
for p in [a for a in sys.argv[1:] if not a.startswith('--')]:
    measure(p, dbg)
