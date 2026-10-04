# Handjet for 17 px: every element of every glyph moved onto whole elements (480 units = 1 px at 17 px), so
# lv_font_conv draws it with no soft pixels. Handjet places elements on half-element steps (240 units) for smoother
# diagonals and centered stems; at 17 px those land on half pixels. Rules, per glyph and per axis:
#  - every element on an odd half-step: shift the whole glyph half an element (keeps the shape exactly);
#  - mixed: each odd element moves half a step toward the glyph's center (keeps symmetric glyphs symmetric).
# Advances are rounded up to whole elements, as LVGL rounds them at 17 px. Output: a static TTF (wght 400,
# element shape 2 = solid squares, element grid 1). Usage: python3 snap17.py IN.ttf OUT.ttf [family name]
import sys
from fontTools.ttLib import TTFont
src, dst = sys.argv[1], sys.argv[2]
fam = sys.argv[3] if len(sys.argv) > 3 else None
H, E = 240, 480
f = TTFont(src); glyf = f['glyf']; hm = f['hmtx']
moved = {}
def snap_axis(vals, lo, hi_excl, prefer):
    """vals: element positions (multiples of 240). Returns new positions on the 480 grid."""
    odd = [v % E != 0 for v in vals]
    if not any(odd): return vals
    if all(odd):
        return [v + prefer for v in vals]
    c2 = lo + hi_excl  # twice the ink center
    out = []
    for v in vals:
        if v % E == 0: out.append(v); continue
        # element spans v..v+480, its center doubled is 2v+480
        out.append(v + H if 2 * v + E < c2 else v - H if 2 * v + E > c2 else v - H)
    return out
for gn in f.getGlyphOrder():
    g = glyf[gn]
    if not g.isComposite(): continue
    comps = g.components
    if any(c.x % H or c.y % H for c in comps): continue
    xs = [c.x for c in comps]; ys = [c.y for c in comps]
    adv, lsb = hm[gn]
    adv2 = -(-adv // E) * E
    lo, hi = min(xs), max(xs) + E
    # whole-glyph shift direction for x: keep side bearings balanced
    best = None
    for s in (-H, H):
        l2, r2 = lo + s, adv2 - (hi + s)
        score = (abs(l2 - r2), -min(l2, r2))
        if best is None or score < best[0]: best = (score, s)
    nx = snap_axis(xs, lo, hi, best[1])
    ny = snap_axis(ys, min(ys), max(ys) + E, -H)
    if nx != xs or ny != ys or adv2 != adv: moved[gn] = (xs != nx, ys != ny, adv2 != adv)
    seen = set(); keep = []
    for c, x, y in zip(comps, nx, ny):
        if (c.glyphName, x, y) in seen: continue
        seen.add((c.glyphName, x, y)); c.x, c.y = x, y; keep.append(c)
    g.components = keep
    g.recalcBounds(glyf)
    hm[gn] = (adv2, g.xMin if hasattr(g, 'xMin') else lsb)
# Kerning, if any pair were off the grid, would also need rounding; Handjet's are all whole elements.
if fam:
    nm = f['name']
    for rec in list(nm.names):
        if rec.nameID in (1, 4, 16): rec.string = fam if rec.nameID != 4 else fam + ' Regular'
        if rec.nameID == 6: rec.string = fam.replace(' ', '') + '-Regular'
f.save(dst)
print('glyphs changed:', len(moved))
