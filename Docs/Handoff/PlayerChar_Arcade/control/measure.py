# Measure limb/neck widths of a generated sheet and convert to ENE voxel units.
# Spec: total height 161.25 cm = 43 layers of 3.75 cm. Limb minimum = 2 voxels (7.5 cm).
import sys
from PIL import Image

path = sys.argv[1]
im = Image.open(path).convert('RGB')
W, H = im.size
px = im.load()

bg = px[4, 4]                                   # flat backdrop corner
def is_fig(c):
    return abs(c[0]-bg[0]) + abs(c[1]-bg[1]) + abs(c[2]-bg[2]) > 40

rows = []
for y in range(H):
    xs = [x for x in range(0, W, 2) if is_fig(px[x, y])]
    rows.append((min(xs), max(xs), len(xs)*2) if xs else None)

ys = [y for y, r in enumerate(rows) if r]
top, sole = ys[0], ys[-1]
FIG = sole - top
CM = 161.25 / FIG                               # px -> cm
print('figure : y %d..%d  = %d px  (= 161.25 cm assumed)' % (top, sole, FIG))
print('scale  : 1 px = %.3f cm   |  1 voxel(3.75cm) = %.1f px' % (CM, 3.75 / CM))
print()

def span_at(frac, label, pick='widest'):
    """measure the horizontal run(s) at a given height fraction"""
    y = int(top + FIG * frac)
    runs, cur = [], None
    for x in range(W):
        if is_fig(px[x, y]):
            cur = x if cur is None else cur
        elif cur is not None:
            runs.append((cur, x - 1)); cur = None
    if cur is not None: runs.append((cur, W - 1))
    runs = [r for r in runs if r[1] - r[0] > 2]
    if not runs:
        print('%-22s (nothing at y=%d)' % (label, y)); return
    if pick == 'widest':
        r = max(runs, key=lambda a: a[1] - a[0]); sel = [r]
    else:
        sel = runs
    for r in sel:
        w = (r[1] - r[0] + 1) * CM
        vox = w / 3.75
        flag = 'OK ' if vox >= 2.0 else ('THIN' if vox >= 1.4 else 'FAIL')
        print('%-22s %6.1f cm  = %4.1f voxel   %s' % (label, w, vox, flag))

print('part                    width        voxels   verdict   (min 2.0)')
print('-' * 64)
span_at(0.205, 'neck')
span_at(0.60,  'thigh (each)',  'all')
span_at(0.75,  'calf (each)',   'all')
span_at(0.90,  'ankle (each)',  'all')
print()
print('reference from spec 2-2:')
print('  bare arm 7.5cm = 2.0 vox (hard floor) | calf 7.5cm = 2.0 | thigh 15cm = 4.0')
print('  neck height 3.75cm = 1 layer')
