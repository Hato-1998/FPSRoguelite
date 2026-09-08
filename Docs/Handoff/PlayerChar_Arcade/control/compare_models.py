# Contact sheet - ENE turnaround across four SD checkpoints, one row each,
# columns = front / side / back.
#
# The only thing this sheet has to get right is scale discipline: proportions
# (head-to-body ratio, limb thickness, silhouette volume) are only comparable
# ACROSS ROWS if every figure is drawn at the same figure height on the same
# cell size. If we let each plate keep its own native scale, a checkpoint that
# just draws everything bigger would look "different" for a reason that has
# nothing to do with proportions. So every figure - regardless of which row it
# lands in - gets rescaled to one fixed target height and bottom-aligned on a
# shared baseline; a leggier build then shows up as a lower waistline at the
# SAME total height, not as a bigger picture.
#
# crop_for_meshy.py already solved the hard part of getting there: cutting the
# off-white subject off its flat grey background (naive thresholding fails on
# that contrast) and locating the figure's true bounding box + pixel-mass
# centre. Reuse those helpers directly rather than re-deriving a second
# segmentation path that could quietly drift from the one already in use.
#
#   python compare_models.py
#
import glob
import os

from PIL import Image, ImageDraw, ImageFont

import crop_for_meshy as cfm

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SD   = os.path.join(ROOT, 'sd_out')
OUT  = os.path.join(ROOT, 'ENE_modelcmp.png')

VIEWS = ['front', 'side', 'back']

# Row order, top to bottom. Patterns are globbed under sd_out/, never a fixed
# timestamp - the modelcmp/ rows are still being generated in parallel while
# this sheet gets built (and rebuilt), so the exact filenames are a moving
# target. Even the already-"adopted" row is globbed on everything but its
# descriptive slug, for the same reason: don't hardcode a seed/timestamp that
# a future re-run could change.
ROWS = [
    ('Illustrious-XL-v1.1 (adopted)', {
        'front': 'v19_fullclosed_w045_seed*.png',
        'side':  'v26_side_goggles_w045_seed*.png',
        'back':  'v28_back_hem_w045_seed*.png',
    }),
    ('NoobAI-XL-v1.1-eps', {
        'front': 'modelcmp/noobai_front_*.png',
        'side':  'modelcmp/noobai_side_*.png',
        'back':  'modelcmp/noobai_back_*.png',
    }),
    ('animagine-xl-4.0', {
        'front': 'modelcmp/animagine_front_*.png',
        'side':  'modelcmp/animagine_side_*.png',
        'back':  'modelcmp/animagine_back_*.png',
    }),
    ('sd_xl_base_1.0', {
        'front': 'modelcmp/sdxlbase_front_*.png',
        'side':  'modelcmp/sdxlbase_side_*.png',
        'back':  'modelcmp/sdxlbase_back_*.png',
    }),
]

# --- layout -------------------------------------------------------------
# Cell size is a fixed pixel budget, NOT derived from any source image, so
# a cell that is still waiting on a generation occupies exactly the same
# rectangle as one that already has a plate in it.
CELL_W, CELL_H = 640, 860
GUTTER_W = 340   # left column: checkpoint name
TITLE_H  = 64
HEADER_H = 100
SHEET_W = GUTTER_W + CELL_W * len(VIEWS)
SHEET_H = TITLE_H + HEADER_H + CELL_H * len(ROWS)   # 2260 x 3604 - under the 4000px cap

# Figure height as a fraction of one cell: the fixed yardstick every plate is
# rescaled to (see module docstring for why this - not native scale - is the
# whole point of the sheet).
FIG_FRAC = 0.82
BASELINE_FRAC = 0.94   # sole line: same fraction down every cell, every row

BG = cfm.FALLBACK_BG   # reuse the one canonical "dark neutral" - the costume
                        # is near-white, so a light backdrop would flatten the
                        # silhouette contrast (crop_for_meshy hit this first).
GRID_LINE    = (70, 78, 94)
BASELINE_CLR = (94, 103, 122)
TEXT_MAIN    = (228, 230, 235)
MISSING_CLR  = (196, 96, 96)

# Real TrueType if we can find one - PIL's built-in bitmap font is legible at
# maybe 10px and this canvas is thousands of pixels on a side.
FONT_CANDIDATES = [
    r'C:\Windows\Fonts\segoeui.ttf',
    r'C:\Windows\Fonts\arial.ttf',
]


def load_font(size):
    for path in FONT_CANDIDATES:
        if os.path.isfile(path):
            try:
                return ImageFont.truetype(path, size)
            except OSError:
                continue
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()


def draw_centered(draw, cx, cy, text, font, fill):
    """Centre text on (cx, cy). textbbox (not textsize, removed in modern
    Pillow) can report a non-zero origin depending on the font's side
    bearings, so the offset has to come from the bbox, not just its size."""
    x0, y0, x1, y1 = draw.textbbox((0, 0), text, font=font)
    draw.text((cx - (x1 - x0) / 2 - x0, cy - (y1 - y0) / 2 - y0), text, font=font, fill=fill)


def wrap_to_width(draw, text, font, max_w):
    """Greedy word-wrap so long checkpoint names fit the label gutter
    without a font-size guessing game."""
    words = text.split(' ')
    lines, cur = [], ''
    for w in words:
        trial = w if not cur else cur + ' ' + w
        if draw.textbbox((0, 0), trial, font=font)[2] <= max_w or not cur:
            cur = trial
        else:
            lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    return lines


def resolve(pattern):
    """Glob under sd_out/; newest mtime wins on a multi-match. None (not an
    exception) on zero matches - the caller turns that into a 'missing'
    placeholder, because sibling checkpoints in this same sheet are still
    mid-generation as this runs."""
    hits = glob.glob(os.path.join(SD, pattern))
    if not hits:
        return None
    return max(hits, key=os.path.getmtime)


def load_cell(path):
    """Segment + measure one plate with crop_for_meshy's own helpers.

    Returns None on ANY failure - corrupt/partial file (a parallel generation
    process can still be mid-write on a file our glob just matched), or a
    background the flood-fill can't key on - so one bad plate degrades to a
    placeholder instead of taking the whole sheet down.
    """
    try:
        im = Image.open(path)
        im.load()   # force the read now, so a truncated file raises here
        rgba = cfm.alpha_cut(im)
        box = cfm.figure_box(rgba)
        cx = cfm.body_centre_x(rgba)
        return rgba, box, cx
    except Exception as exc:
        print('  ! failed to segment %s: %r' % (path, exc))
        return None


def place_figure(cell_img, rgba, box, cx):
    """Rescale so THIS plate's own figure-box height becomes the sheet-wide
    target height, then bottom-align the sole on the shared baseline and
    centre on the body's pixel mass (not the bbox centre - a hand jutting out
    in the profile view would otherwise drag the whole figure sideways; this
    is exactly the trap cfm.body_centre_x already avoids)."""
    x0, y0, x1, y1 = box
    fig_h = y1 - y0
    scale = (CELL_H * FIG_FRAC) / fig_h
    new_size = (max(1, round(rgba.width * scale)), max(1, round(rgba.height * scale)))
    big = rgba.resize(new_size, Image.LANCZOS)

    baseline_y = CELL_H * BASELINE_FRAC
    ox = round(CELL_W / 2 - cx * scale)
    oy = round(baseline_y - y1 * scale)
    # paste() clips source + mask to the destination canvas automatically, so
    # an over-wide silhouette (e.g. an arm out in the side view) is cropped at
    # the cell edge instead of bleeding into the next column.
    cell_img.paste(big, (ox, oy), big)


def draw_missing(draw, x0, y0, font):
    draw.rectangle([x0 + 10, y0 + 10, x0 + CELL_W - 10, y0 + CELL_H - 10],
                   outline=MISSING_CLR, width=2)
    draw_centered(draw, x0 + CELL_W / 2, y0 + CELL_H / 2, 'missing', font, MISSING_CLR)


def main():
    sheet = Image.new('RGB', (SHEET_W, SHEET_H), BG)
    draw = ImageDraw.Draw(sheet)

    title_font   = load_font(30)
    header_font  = load_font(30)
    label_font   = load_font(26)
    missing_font = load_font(28)

    draw_centered(draw, SHEET_W / 2, TITLE_H / 2,
                  'ENE turnaround \u2014 SD checkpoint comparison '
                  '(equal figure height, common baseline)',
                  title_font, TEXT_MAIN)

    for c, view in enumerate(VIEWS):
        cx = GUTTER_W + c * CELL_W + CELL_W / 2
        draw_centered(draw, cx, TITLE_H + HEADER_H / 2, view, header_font, TEXT_MAIN)

    print('%-32s %-6s %-46s %s' % ('checkpoint', 'view', 'pattern', 'status'))
    missing = []
    for r, (label, patterns) in enumerate(ROWS):
        row_top = TITLE_H + HEADER_H + r * CELL_H
        baseline_global = row_top + CELL_H * BASELINE_FRAC

        lines = wrap_to_width(draw, label, label_font, GUTTER_W - 24)
        lh = draw.textbbox((0, 0), 'Ag', font=label_font)[3]
        ly = row_top + CELL_H / 2 - lh * len(lines) / 2
        for line in lines:
            draw_centered(draw, GUTTER_W / 2, ly + lh / 2, line, label_font, TEXT_MAIN)
            ly += lh

        # Ground line across this row's three view columns, drawn BEFORE the
        # figures so a standing silhouette naturally overlaps and covers it -
        # it reads as the line the character is standing on, and it is a
        # cheap visual proof that every cell really does share one baseline.
        draw.line([(GUTTER_W, baseline_global), (SHEET_W, baseline_global)],
                  fill=BASELINE_CLR, width=1)

        for c, view in enumerate(VIEWS):
            x0 = GUTTER_W + c * CELL_W
            y0 = row_top
            pattern = patterns[view]
            path = resolve(pattern)
            loaded = load_cell(path) if path else None
            status = (os.path.basename(path) if loaded else
                       'missing (failed to segment)' if path else
                       'missing (no match)')
            print('%-32s %-6s %-46s %s' % (label, view, pattern, status))

            if loaded is None:
                missing.append('%s / %s' % (label, view))
                draw_missing(draw, x0, y0, missing_font)
                continue

            rgba, box, cx_body = loaded
            cell_layer = Image.new('RGBA', (CELL_W, CELL_H), (0, 0, 0, 0))
            place_figure(cell_layer, rgba, box, cx_body)
            sheet.paste(cell_layer, (x0, y0), cell_layer)

        y_bottom = row_top + CELL_H
        draw.line([(0, y_bottom), (SHEET_W, y_bottom)], fill=GRID_LINE, width=1)

    # Column + gutter dividers over the whole grid, drawn last so they stay
    # crisp on top of everything.
    grid_top = TITLE_H + HEADER_H
    for c in range(len(VIEWS) + 1):
        x = GUTTER_W + c * CELL_W
        draw.line([(x, grid_top), (x, SHEET_H)], fill=GRID_LINE, width=1)
    draw.line([(0, grid_top), (SHEET_W, grid_top)], fill=GRID_LINE, width=1)

    sheet.save(OUT)
    print('\nwrote %s  (%dx%d)' % (OUT, SHEET_W, SHEET_H))
    print('missing cells: %s' % (', '.join(missing) if missing else 'none'))


if __name__ == '__main__':
    main()
