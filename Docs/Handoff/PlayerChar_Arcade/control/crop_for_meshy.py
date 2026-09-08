# §7 crop - turn the three adopted SD plates into Meshy multi-view inputs.
#
# Two things this has to get right, and both are easy to get wrong by hand:
#  1. ONE scale for all three sheets. Multi-view fusion assumes the views agree;
#     our plates differ by 1.85% in figure height (generation noise, not design),
#     so each is normalised to the same figure height on the same canvas.
#  2. The subject is off-white on a light-grey background. That is poor contrast
#     for any automatic background removal, so the primary output is RGBA with a
#     real alpha cut. A flattened dark-grey version ships alongside as a fallback
#     for tools that reject alpha.
#
#   python crop_for_meshy.py
#
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SD   = os.path.join(ROOT, 'sd_out')

PLATES = [
    ('front', 'v19_fullclosed_w045_seed1234567_112834.png'),
    ('side',  'v26_side_goggles_w045_seed1234567_120613.png'),
    ('back',  'v28_back_hem_w045_seed1234567_120930.png'),
]
CANVAS   = 1024
FIG_FRAC = 0.88            # §7 precedent: figure height = 88% of the canvas
FALLBACK_BG = (46, 52, 64)  # dark neutral: the costume is near-white, so a light
                            # background would give the silhouette no contrast


def alpha_cut(im, tol=26):
    """Alpha from a flood fill of the background, not a global colour match.

    A global match would also punch holes inside the figure wherever a pixel
    happens to land on the background colour. Filling inward from the border
    only removes background that is actually connected to the border.
    """
    a = np.asarray(im.convert('RGB'), dtype=np.int16)
    bg = np.median(np.concatenate([a[:4].reshape(-1, 3), a[-4:].reshape(-1, 3),
                                   a[:, :4].reshape(-1, 3), a[:, -4:].reshape(-1, 3)]), axis=0)
    dist = np.abs(a - bg).max(axis=2)

    # .copy() is load-bearing: in Pillow 12.2 ImageDraw.floodfill on an image
    # straight out of Image.fromarray is a SILENT no-op - no exception, mask
    # unchanged, and the crop then "succeeds" with the whole frame opaque.
    # Verified against a control (fresh Image.new fills, fromarray does not).
    seed = Image.fromarray(np.where(dist < tol, 255, 0).astype(np.uint8), 'L').copy()
    h, w = dist.shape
    for xy in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)):
        if seed.getpixel(xy) == 255:
            ImageDraw.floodfill(seed, xy, 128, thresh=0)
    outside = np.asarray(seed) == 128
    # ...and assert it, so the same silent failure cannot come back unnoticed.
    if outside.mean() < 0.30:
        raise RuntimeError('background flood fill covered only %.1f%% - the key '
                           'failed, refusing to emit a full-frame cutout' % (outside.mean() * 100))

    # Erode 1px before feathering: the outermost rim is anti-aliased blend with
    # the background, so keeping it would fringe the figure with light grey when
    # composited onto anything darker.
    mask = Image.fromarray(np.where(outside, 0, 255).astype(np.uint8), 'L')
    mask = mask.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.GaussianBlur(0.7))
    out = im.convert('RGBA')
    out.putalpha(mask)
    return out


def figure_box(rgba):
    a = np.asarray(rgba)[:, :, 3]
    ys, xs = np.nonzero(a > 24)
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1


def body_centre_x(rgba):
    """Horizontal centre from pixel mass, not from the bounding box.

    In the profile plate one hand juts out to the side; a bbox centre would shove
    the whole body off-axis to compensate for it.
    """
    a = np.asarray(rgba)[:, :, 3] > 24
    cols = a.sum(axis=0).astype(np.float64)
    return float((np.arange(len(cols)) * cols).sum() / cols.sum())


def main():
    cut = []
    for name, f in PLATES:
        im = Image.open(os.path.join(SD, f))
        rgba = alpha_cut(im)
        x0, y0, x1, y1 = figure_box(rgba)
        cut.append((name, f, rgba, (x0, y0, x1, y1), body_centre_x(rgba)))

    target_h = CANVAS * FIG_FRAC
    print('%-6s %-46s %7s %7s %7s' % ('view', 'source', 'fig_h', 'scale', 'out_h'))
    for name, f, rgba, (x0, y0, x1, y1), cx in cut:
        fig_h = y1 - y0
        sc = target_h / fig_h
        new = (max(1, round(rgba.width * sc)), max(1, round(rgba.height * sc)))
        big = rgba.resize(new, Image.LANCZOS)

        # place: figure vertically centred, body mass horizontally centred
        top = (y0 * sc)
        canvas = Image.new('RGBA', (CANVAS, CANVAS), (0, 0, 0, 0))
        ox = round(CANVAS / 2 - cx * sc)
        oy = round((CANVAS - target_h) / 2 - top)
        canvas.paste(big, (ox, oy), big)

        out_a = os.path.join(ROOT, 'PlayerCharArcade_%s.png' % name)
        canvas.save(out_a)

        flat = Image.new('RGB', (CANVAS, CANVAS), FALLBACK_BG)
        flat.paste(canvas, (0, 0), canvas)
        flat.save(os.path.join(ROOT, 'PlayerCharArcade_%s_ongrey.png' % name))

        a = np.asarray(canvas)[:, :, 3]
        ys = np.nonzero(a.max(axis=1) > 24)[0]
        print('%-6s %-46s %7d %7.3f %7d' % (name, f, fig_h, sc, ys.max() - ys.min() + 1))


if __name__ == '__main__':
    main()
