# Contact sheet - does SD's hires-fix second pass actually improve LINE AND
# FINISH QUALITY, judged at the size the plate is actually consumed at?
#
# That last clause is the whole design constraint. crop_for_meshy.py
# normalises every adopted plate so the figure is ~900px tall on a 1024x1024
# canvas (cfm.CANVAS * cfm.FIG_FRAC). A base plate's figure is ~1116px tall
# going in - a small downscale to get there. A hires plate's figure is
# ~1674px tall going in - a much bigger downscale, so it carries more real
# detail through that downscale than the raw pixel count of "hires" would
# suggest on its own. Comparing the two at their native resolutions would
# measure the upscale factor, not finish quality - so every column here goes
# through the exact same section-7 yardstick before anything is judged:
#
#   1. Normalise to crop_for_meshy's own CANVAS/FIG_FRAC (~900px figure on a
#      1024 canvas) - imported, not re-derived, so this sheet can't quietly
#      drift from what section 7 actually does to the adopted plates.
#   2. Show a downscaled full-figure thumbnail, for orientation only.
#   3. Show two 1:1 pixel crops - head+headgear and feet+sneakers, the two
#      places linework has the most edges to get right - at NO scale at all.
#      A contact sheet that thumbnails everything would destroy exactly the
#      difference being measured; magnifying would show detail neither
#      section 7 nor the viewer will ever actually see.
#
# Both detail crops are a fixed pixel window, but the window's CENTRE is
# derived per column from that column's own (re-measured, post-normalise)
# figure box, not a hardcoded coordinate - so the crop still lands on the
# head/feet even if some future plate's proportions shift a little.
#
#   python compare_hires.py dial          -> ../ENE_hires_dial.png
#   python compare_hires.py turnaround    -> ../ENE_hires_turnaround.png
#
import os
import sys

from PIL import Image, ImageDraw

import crop_for_meshy as cfm
import compare_models as cmm

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SD   = os.path.join(ROOT, 'sd_out')

# Two named column-sets. Each entry is (label, glob pattern relative to
# sd_out/) - globbed, never a fixed filename: hires/ generations are still
# landing in parallel while this sheet gets (re)built, and cmm.resolve()
# already turns "no match yet" into None rather than an exception.
MODES = {
    # All front view - what the denoise dial itself trades off.
    'dial': [
        ('base (no hires)', 'v19_fullclosed_w045_seed*.png'),
        ('hires d0.25',     'hires/front_d025_*.png'),
        ('hires d0.35',     'hires/front_d035_*.png'),
        ('hires d0.50',     'hires/front_d050_*.png'),
    ],
    # The doc's starting denoise (0.35), all three turnaround views.
    'turnaround': [
        ('front base',  'v19_fullclosed_w045_seed*.png'),
        ('front hires', 'hires/front_d035_*.png'),
        ('side base',   'v26_side_goggles_w045_seed*.png'),
        ('side hires',  'hires/side_d035_*.png'),
        ('back base',   'v28_back_hem_w045_seed*.png'),
        ('back hires',  'hires/back_d035_*.png'),
    ],
}
DEFAULT_MODE = 'dial'

# --- layout ---------------------------------------------------------------
# One column per entry, three stacked bands per column. Every band is the
# same fixed square: the full-figure band gets downscaled INTO that square
# for context, while the two detail bands are windows cut directly out of
# the 1024 canvas at 1:1 - no resize, so the square size IS the crop size.
CELL_W   = 460
CROP     = 420             # fixed pixel size of every band, detail bands
                           # included - this is the 1:1 window.
GUTTER_W = 210             # left gutter: band labels
TITLE_H  = 64
HEADER_H = 100
BAND_LABELS = ['full figure (context only, downscaled)',
               'head + headgear (1:1, no scaling)',
               'feet + sneakers (1:1, no scaling)']

# Head/feet band fractions, as a fraction of the NORMALISED figure height
# (~901px = cfm.CANVAS * cfm.FIG_FRAC, identical for every column by
# construction - that is the entire point of normalising first). Picked from
# the row-width profile of the three adopted plates: headphone earcups peak
# around the top 10%, chin/neck narrows to its minimum around 18-20%, and
# shoulder/jacket flare starts ramping up around 25-30% - so the top 26%
# holds the whole head+headgear without much jacket collar. Shoe widening
# starts around the bottom 14-16% on the front/back plates (more like 20% on
# the side plate, where the forward foot splays earlier) - the bottom 20%
# leaves a small margin above the ankle cuff on all three views.
HEAD_FRAC = 0.26
FEET_FRAC = 0.20

BG          = cfm.FALLBACK_BG   # dark neutral - the costume is near-white,
                                 # a light backdrop would kill silhouette
                                 # contrast (crop_for_meshy hit this first).
GRID_LINE   = cmm.GRID_LINE
TEXT_MAIN   = cmm.TEXT_MAIN
MISSING_CLR = cmm.MISSING_CLR


def normalize_to_canvas(rgba, box, cx):
    """Rescale + place exactly like crop_for_meshy.main() does for its own
    output plates. CANVAS/FIG_FRAC are read from that module so this sheet
    can never quietly drift onto a second yardstick. That placement lives
    inline in cfm.main() rather than as an importable function, so it is
    reproduced verbatim here instead of re-derived."""
    x0, y0, x1, y1 = box
    fig_h = y1 - y0
    target_h = cfm.CANVAS * cfm.FIG_FRAC
    sc = target_h / fig_h
    new_size = (max(1, round(rgba.width * sc)), max(1, round(rgba.height * sc)))
    big = rgba.resize(new_size, Image.LANCZOS)

    top = y0 * sc
    canvas = Image.new('RGBA', (cfm.CANVAS, cfm.CANVAS), (0, 0, 0, 0))
    ox = round(cfm.CANVAS / 2 - cx * sc)
    oy = round((cfm.CANVAS - target_h) / 2 - top)
    canvas.paste(big, (ox, oy), big)
    return canvas


def crop_fixed(canvas, cx, cy, size):
    """A `size` x `size` window centred at (cx, cy) in canvas space. Clamped
    and padded (transparent, so the sheet's own dark background shows
    through) rather than resized to fit - resizing here would be exactly the
    interpolation this sheet exists to rule out."""
    x0, y0 = round(cx - size / 2), round(cy - size / 2)
    out = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    sx0, sy0 = max(x0, 0), max(y0, 0)
    sx1, sy1 = min(x0 + size, canvas.width), min(y0 + size, canvas.height)
    if sx1 > sx0 and sy1 > sy0:
        out.paste(canvas.crop((sx0, sy0, sx1, sy1)), (sx0 - x0, sy0 - y0))
    return out


def build_bands(path):
    """One source plate -> the three band images for one column, or None on
    any failure (corrupt/partial file - a parallel generation process can
    still be mid-write on something our glob just matched - a background the
    flood-fill can't key on, or a degenerate figure box). The caller turns
    None into a 'missing' placeholder for the whole column, same policy as
    compare_models.py uses per cell."""
    loaded = cmm.load_cell(path)
    if loaded is None:
        return None
    rgba, box, cx = loaded
    try:
        canvas = normalize_to_canvas(rgba, box, cx)
        # Re-measure on the NORMALISED plate rather than trust our own
        # placement arithmetic - this is the "derive from the normalised
        # figure box" the crop rectangles are supposed to come from.
        nx0, ny0, nx1, ny1 = cfm.figure_box(canvas)
        ncx = cfm.body_centre_x(canvas)
        fig_h = ny1 - ny0

        full = canvas.resize((CROP, CROP), Image.LANCZOS)
        head = crop_fixed(canvas, ncx, ny0 + HEAD_FRAC * fig_h / 2, CROP)
        feet = crop_fixed(canvas, ncx, ny1 - FEET_FRAC * fig_h / 2, CROP)
        return full, head, feet
    except Exception as exc:
        print('  ! failed to normalise %s: %r' % (path, exc))
        return None


def draw_missing(draw, x0, y0, size, font):
    draw.rectangle([x0 + 10, y0 + 10, x0 + size - 10, y0 + size - 10],
                   outline=MISSING_CLR, width=2)
    cmm.draw_centered(draw, x0 + size / 2, y0 + size / 2, 'missing', font, MISSING_CLR)


def main(mode):
    columns = MODES[mode]
    out_path = os.path.join(ROOT, 'ENE_hires_%s.png' % mode)

    n = len(columns)
    sheet_w = GUTTER_W + CELL_W * n
    sheet_h = TITLE_H + HEADER_H + CROP * 3
    inset = (CELL_W - CROP) // 2   # centres every CROP-wide band in its cell

    sheet = Image.new('RGB', (sheet_w, sheet_h), BG)
    draw = ImageDraw.Draw(sheet)

    title_font   = cmm.load_font(28)
    header_font  = cmm.load_font(26)
    label_font   = cmm.load_font(22)
    missing_font = cmm.load_font(26)

    title = ('ENE hires fix — %s (equal figure height, 1:1 detail crops)' %
              ('denoise dial, front view' if mode == 'dial' else 'base vs hires, all views'))
    cmm.draw_centered(draw, sheet_w / 2, TITLE_H / 2, title, title_font, TEXT_MAIN)

    # Band labels down the left gutter.
    for r, band_label in enumerate(BAND_LABELS):
        band_top = TITLE_H + HEADER_H + r * CROP
        lines = cmm.wrap_to_width(draw, band_label, label_font, GUTTER_W - 24)
        lh = draw.textbbox((0, 0), 'Ag', font=label_font)[3]
        ly = band_top + CROP / 2 - lh * len(lines) / 2
        for line in lines:
            cmm.draw_centered(draw, GUTTER_W / 2, ly + lh / 2, line, label_font, TEXT_MAIN)
            ly += lh

    print('%-20s %-34s %s' % ('column', 'pattern', 'status'))
    missing = []
    for c, (label, pattern) in enumerate(columns):
        x0 = GUTTER_W + c * CELL_W
        cmm.draw_centered(draw, x0 + CELL_W / 2, TITLE_H + HEADER_H / 2,
                           label, header_font, TEXT_MAIN)

        path = cmm.resolve(pattern)
        bands = build_bands(path) if path else None
        status = (os.path.basename(path) if bands else
                   'missing (failed to normalise)' if path else
                   'missing (no match)')
        print('%-20s %-34s %s' % (label, pattern, status))

        if bands is None:
            missing.append(label)
            for r in range(3):
                draw_missing(draw, x0 + inset, TITLE_H + HEADER_H + r * CROP, CROP, missing_font)
            continue

        for r, band_img in enumerate(bands):
            y0 = TITLE_H + HEADER_H + r * CROP
            sheet.paste(band_img, (x0 + inset, y0), band_img)

    # Grid lines over the whole sheet, drawn last so they stay crisp on top.
    grid_top = TITLE_H + HEADER_H
    for c in range(n + 1):
        x = GUTTER_W + c * CELL_W
        draw.line([(x, grid_top), (x, sheet_h)], fill=GRID_LINE, width=1)
    for r in range(4):
        y = grid_top + r * CROP
        draw.line([(0, y), (sheet_w, y)], fill=GRID_LINE, width=1)

    sheet.save(out_path)
    print('\nwrote %s  (%dx%d)' % (out_path, sheet_w, sheet_h))
    print('missing columns: %s' % (', '.join(missing) if missing else 'none'))


if __name__ == '__main__':
    mode = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_MODE
    if mode not in MODES:
        print('unknown mode %r - choose one of: %s' % (mode, ', '.join(MODES)))
        sys.exit(1)
    main(mode)
