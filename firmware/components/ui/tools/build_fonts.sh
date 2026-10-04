#!/usr/bin/env bash
# build_fonts.sh: TinyBar's Bold Signal fonts, from the Barlow TTFs to LVGL 9 C files. Owner: ui builder.
#
#   firmware/components/ui/tools/build_fonts.sh
#
# Needs: Python 3 with fonttools and opentype-feature-freezer (pip install fonttools opentype-feature-freezer), and
# Node with npx (lv_font_conv 1.5.3 is fetched by npx). The generated files are committed, so an ordinary
# `idf.py build` needs none of this.
#
# Steps:
#  1. Freeze the tabular figures into the default digits (pyftfeatfreeze -f tnum): lv_font_conv ignores OpenType
#     features, and Barlow's default digits are proportional (Barlow Condensed Bold's 1 is 284 units, every tabular
#     digit 498). The frozen copies get a TinyBar family name and a modification note in their name tables, and keep
#     the Barlow Project's copyright and the OFL (firmware/fonts/licenses/Barlow-OFL.txt). They're written to
#     firmware/fonts/tinybar/. Barlow names no Reserved Font Name, so the new names are a courtesy, not a requirement.
#  2. Check the frozen copies: every digit the same advance and no kerning pair between two digits or a digit and the
#     colon, so a ticking timer, clock or countdown never moves.
#  3. Convert one font per role in ../include/ui_theme.h (ui_font_role_t) with lv_font_conv at 4 bpp, compressed,
#     autohinting off (the glyphs keep the shapes and advances the mock-up's browser draws).
#       - Fonts that draw text from outside (meeting titles, messages, app names, network names, toasts): exactly the
#         characters tb_text_drawable() accepts (components/core/include/tb_text.h): printable ASCII, Latin-1, en and
#         em dash, ellipsis.
#       - The 112, 100 and 78 px headlines carry only their fixed copy's characters: A to Z, 0 to 9, colon, hyphen,
#         space and middle dot. The 46 px value: digits, colon, space, h, m and A to Z (the hold screen's KEEP
#         HOLDING and POWERING OFF). AM/PM fonts: A, M, P. The setup steps: printable ASCII; their bold network
#         name: the letters of TinyBar-Setup.
#     Digits are tabular everywhere except the 100, 78 and 62 px headlines, as in the mock-up's Signal layout CSS
#     (font-variant-numeric: tabular-nums on the screen, proportional-nums on headlines other than the timer and
#     clock): those never tick, and a proportional 1 keeps BACK AT 12:30 at 78 px.
#  4. Print each font's flash size (the glyph bitmaps and tables, from the object file) for fonts/README.md.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UI="$(cd "$HERE/.." && pwd)"
FW="$(cd "$UI/../.." && pwd)"
SRC="$FW/fonts/src"
OUT_TTF="$FW/fonts/tinybar"
OUT_C="$UI/fonts"
LVFC="npx --yes lv_font_conv@1.5.3"
mkdir -p "$OUT_TTF" "$OUT_C"

# ---------- 1 and 2: freeze tnum, rename, check ----------
freeze() {   # <source.ttf> <out.ttf> <family> <style>
    local src="$1" out="$2" family="$3" style="$4"
    pyftfeatfreeze -f tnum "$SRC/$src" "$OUT_TTF/$out.tmp" >/dev/null
    python3 - "$OUT_TTF/$out.tmp" "$OUT_TTF/$out" "$family" "$style" "$src" <<'PY'
import sys
from fontTools.ttLib import TTFont
src, out, family, style, orig = sys.argv[1:6]
f = TTFont(src)
name = f['name']
full = f'{family} {style}'
ps = (family + '-' + style).replace(' ', '')
old_version = name.getDebugName(5) or ''
for rec in list(name.names):
    if rec.nameID in (1, 2, 3, 4, 6, 16, 17, 21, 22):
        name.removeNames(nameID=rec.nameID)
note = (f'Modified for TinyBar from Barlow ({orig}, {old_version}): the tabular figures (OpenType tnum) are frozen '
        'as the default digits with pyftfeatfreeze, and the family is renamed. Licensed under the SIL Open Font '
        'License 1.1, like the original.')
for nid, val in ((1, family), (2, style), (3, f'TinyBar: {ps}; {old_version}'), (4, full), (6, ps), (10, note)):
    name.setName(val, nid, 3, 1, 0x409)
    name.setName(val, nid, 1, 0, 0)
f.save(out)

# The check: every digit one advance, no digit-digit or digit-colon kerning.
cmap = f.getBestCmap()
hmtx = f['hmtx']
digits = [cmap[ord(c)] for c in '0123456789']
widths = {hmtx[g][0] for g in digits}
assert len(widths) == 1, f'{out}: digits not equal width after freezing: {widths}'
pairs = set()
if 'GPOS' in f:
    gpos = f['GPOS'].table
    targets = set(digits) | {cmap[ord(':')]}
    for lookup in gpos.LookupList.Lookup:
        subtables = lookup.SubTable
        kind = lookup.LookupType
        if kind == 9:
            kind = subtables[0].ExtensionLookupType if subtables else 0
            subtables = [x.ExtSubTable for x in subtables]
        if kind != 2:
            continue
        for st in subtables:
                if st.Format == 1:
                    for i, first in enumerate(st.Coverage.glyphs):
                        if first not in targets:
                            continue
                        for pvr in st.PairSet[i].PairValueRecord:
                            v = pvr.Value1
                            if pvr.SecondGlyph in targets and v is not None and getattr(v, 'XAdvance', 0):
                                pairs.add((first, pvr.SecondGlyph))
                elif st.Format == 2:
                    cls1 = st.ClassDef1.classDefs
                    cls2 = st.ClassDef2.classDefs
                    for first in st.Coverage.glyphs:
                        if first not in targets:
                            continue
                        c1 = cls1.get(first, 0)
                        for second in targets:
                            v = st.Class1Record[c1].Class2Record[cls2.get(second, 0)].Value1
                            if v is not None and getattr(v, 'XAdvance', 0):
                                pairs.add((first, second))
assert not pairs, f'{out}: kerning between digits or the colon: {sorted(pairs)[:8]}'
print(f'  {out.split("/")[-1]}: digits {widths.pop()} units, colon {hmtx[cmap[ord(":")]][0]}, no digit kerning')
PY
    rm -f "$OUT_TTF/$out.tmp"
}

echo "Freezing tabular figures into $OUT_TTF"
freeze BarlowCondensed-Bold.ttf TinyBarCondensed-Bold.ttf "TinyBar Condensed" "Bold"
freeze Barlow-Medium.ttf TinyBarText-Medium.ttf "TinyBar Text" "Medium"
freeze Barlow-SemiBold.ttf TinyBarText-SemiBold.ttf "TinyBar Text" "SemiBold"
freeze Barlow-Bold.ttf TinyBarText-Bold.ttf "TinyBar Text" "Bold"

# ---------- 3: convert ----------
FULL=(-r 0x20-0x7E -r 0xA0-0xFF -r 0x2013-0x2014 -r 0x2026)   # tb_text_drawable()
ASCII=(-r 0x20-0x7E)
HEAD=(-r 0x41-0x5A -r 0x30-0x3A -r 0x2D -r 0x20 -r 0xB7)       # A-Z 0-9 : - space ·
VALUE=(-r 0x30-0x3A -r 0x20 -r 0x41-0x5A --symbols hm)           # durations, times, KEEP HOLDING
AMPM=(--symbols AMP)

PROP="$SRC/BarlowCondensed-Bold.ttf"            # proportional digits: the 100, 78 and 62 px headlines
TAB="$OUT_TTF/TinyBarCondensed-Bold.ttf"
MED="$OUT_TTF/TinyBarText-Medium.ttf"
SEMI="$OUT_TTF/TinyBarText-SemiBold.ttf"
BOLD="$OUT_TTF/TinyBarText-Bold.ttf"

conv() {   # <name> <size> <ttf> <range args...>
    local name="$1" size="$2" ttf="$3"; shift 3
    $LVFC --bpp 4 --size "$size" --format lvgl --lv-include lvgl.h \
        --font "$ttf" --autohint-off "$@" --lv-font-name "$name" -o "$OUT_C/$name.c" >/dev/null
    # lv_font_conv writes its full command line (with this machine's paths) into the header; keep it portable.
    sed -i -e "s#$FW/##g" -e 's#npx --yes ##' "$OUT_C/$name.c"
    echo "  $name.c"
}

# Letter-spacing baked into a font's advances: Bold Signal tracks the kicker by 1.5 px (.1em at 15 px) and the labels,
# chips and tile labels by 1.2 px (.1em at 12 px), but LVGL's text_letter_space is whole pixels only. Advances in an
# lv_font_conv font are in 1/16 px, so the tracking goes into every glyph's adv_w (CSS letter-spacing also follows
# every character, the last one included). LVGL still rounds each glyph's advance to a whole pixel when it draws.
track() {   # <name> <px>
    python3 - "$OUT_C/$1.c" "$2" <<'PY'
import re, sys
path, px = sys.argv[1], float(sys.argv[2])
add = round(px * 16)
src = open(path).read()
n = 0
def bump(m):
    global n
    n += 1
    return f'.adv_w = {int(m.group(1)) + add}' if n > 1 else m.group(0)   # glyph id 0 is reserved
src = re.sub(r'\.adv_w = (\d+)', bump, src)
src = src.replace(' * Opts: ', f' * Tracking: +{px} px baked into every advance (tools/build_fonts.sh)\n * Opts: ', 1)
open(path, 'w').write(src)
PY
}

echo "Converting into $OUT_C"
rm -f "$OUT_C"/ui_font_*.c
conv ui_font_head_112   112 "$TAB"  "${HEAD[@]}"
conv ui_font_head_100   100 "$PROP" "${HEAD[@]}"
conv ui_font_head_78     78 "$PROP" "${HEAD[@]}"
conv ui_font_head_62     62 "$PROP" "${FULL[@]}"
conv ui_font_value_46    46 "$TAB"  "${VALUE[@]}"
conv ui_font_value_28    28 "$TAB"  "${FULL[@]}"
conv ui_font_ampm_36     36 "$TAB"  "${AMPM[@]}"
conv ui_font_ampm_17     17 "$TAB"  "${AMPM[@]}"
conv ui_font_sub_19      19 "$MED"  "${FULL[@]}"
conv ui_font_step_16     16 "$MED"  "${ASCII[@]}"
conv ui_font_step_16b    16 "$BOLD" --symbols "TinyBar-Setup"
conv ui_font_kick_15     15 "$BOLD" "${FULL[@]}"
conv ui_font_sys_15      15 "$SEMI" "${FULL[@]}"
conv ui_font_foot_14     14 "$MED"  "${FULL[@]}"
conv ui_font_label_12    12 "$BOLD" "${FULL[@]}"
track ui_font_kick_15 1.5
track ui_font_label_12 1.2

# ---------- 4: measure ----------
if command -v gcc >/dev/null && [ -n "${LVGL_DIR:-}" ]; then
    echo "Flash per font (bytes of .rodata, compiled for the host; the device's are within a few bytes):"
    tmp="$(mktemp -d)"
    total=0
    for c in "$OUT_C"/ui_font_*.c; do
        gcc -c -Os -I"$UI/host" -I"$LVGL_DIR" -I"$LVGL_DIR/.." -DLV_CONF_INCLUDE_SIMPLE \
            "$c" -o "$tmp/f.o"
        n=$(size -A "$tmp/f.o" | awk '/\.rodata|\.data\.rel\.ro|\.data/ {s += $2} END {print s}')
        total=$((total + n))
        printf '  %-22s %7d\n' "$(basename "$c" .c)" "$n"
    done
    printf '  %-22s %7d\n' total "$total"
    rm -rf "$tmp"
fi
