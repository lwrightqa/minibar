#!/usr/bin/env python3
"""
check_fonts.py: the converted fonts keep their contract. Owner: ui builder.

    python3 check_fonts.py <drawable.txt>

drawable.txt lists, one hex code point per line, every character tb_text_drawable() accepts (test_assets.c writes it
from core, so the check follows tb_text.h). For components/ui/fonts/ui_font_*.c (lv_font_conv output):
  - the fonts that draw text from outside have exactly those characters;
  - the 112, 100 and 78 px headlines, the 46 px value, the AM/PM fonts and the setup steps have their fixed sets;
  - in every font with tabular digits, the ten digits have one advance, and no kerning pair joins two digits or a
    digit and the colon (a ticking timer, clock or countdown never moves);
  - the 100, 78 and 62 px headlines keep Barlow's proportional digits (the 1 is narrower), as the mock-up draws them.
Exit status 0 when all hold.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.normpath(os.path.join(HERE, '..', '..', '..', 'components', 'ui', 'fonts'))

HEAD = set(range(0x41, 0x5B)) | set(range(0x30, 0x3B)) | {0x2D, 0x20, 0xB7}
EXPECT = {
    'ui_font_head_112': ('set', HEAD), 'ui_font_head_100': ('set', HEAD), 'ui_font_head_78': ('set', HEAD),
    'ui_font_head_62': ('full', None), 'ui_font_value_28': ('full', None), 'ui_font_sub_19': ('full', None),
    'ui_font_kick_15': ('full', None), 'ui_font_sys_15': ('full', None), 'ui_font_foot_14': ('full', None),
    'ui_font_label_12': ('full', None),
    'ui_font_value_46': ('set', set(range(0x30, 0x3B)) | {0x20} | set(range(0x41, 0x5B)) | {ord('h'), ord('m')}),
    'ui_font_ampm_36': ('set', {ord(c) for c in 'AMP'}), 'ui_font_ampm_17': ('set', {ord(c) for c in 'AMP'}),
    'ui_font_step_16': ('set', set(range(0x20, 0x7F))), 'ui_font_step_16b': ('set', {ord(c) for c in 'MiniBar-Setup'}),
    # Low Glare Pixel's 17 px Handjet (Snap17): the same drawable set as the Bold Signal fonts, one copy for the pixel theme.
    'ui_font_pixel_snap_17': ('full', None),
}
PROPORTIONAL = {'ui_font_head_100', 'ui_font_head_78', 'ui_font_head_62'}


def ints(text):
    return [int(v, 0) for v in re.findall(r'-?(?:0x[0-9a-fA-F]+|\d+)', text)]


def array(src, name):
    m = re.search(name + r'\[\]\s*=\s*\{(.*?)\};', src, re.S)
    return ints(re.sub(r'/\*.*?\*/', '', m.group(1), flags=re.S)) if m else None


def check(name, src, full):
    errs = []
    cps = [int(h, 16) for h in re.findall(r'/\* U\+([0-9A-F]+) ', src)]
    kind, want = EXPECT[name]
    want = full if kind == 'full' else want
    got = set(cps)
    if got != want:
        errs.append(f'{name}: missing {sorted(hex(c) for c in want - got)[:8]}, extra {sorted(hex(c) for c in got - want)[:8]}')
    gid = {cp: i + 1 for i, cp in enumerate(cps)}       # glyph id 0 is reserved
    adv = [int(v) for v in re.findall(r'\.adv_w = (\d+)', src)]
    digits = [ord(c) for c in '0123456789']
    if all(d in gid for d in digits):
        widths = {adv[gid[d]] for d in digits}
        if name in PROPORTIONAL:
            if len(widths) == 1:
                errs.append(f'{name}: digits are tabular; the mock-up draws these headlines with proportional digits')
        else:
            if len(widths) != 1:
                errs.append(f'{name}: digits have different advances {sorted(widths)}')
            targets = {gid[d] for d in digits} | ({gid[0x3A]} if 0x3A in gid else set())
            left, right, vals = array(src, 'kern_left_class_mapping'), array(src, 'kern_right_class_mapping'), array(src, 'kern_class_values')
            if left is not None:
                rc = int(re.search(r'\.right_class_cnt\s*=\s*(\d+)', src).group(1))
                for a in targets:
                    for b in targets:
                        la, rb = left[a], right[b]
                        if la and rb and vals[(la - 1) * rc + (rb - 1)]:
                            errs.append(f'{name}: kerning between glyphs {a} and {b}')
            pairs = array(src, 'kern_pair_glyph_ids')
            if pairs is not None:
                pv = array(src, 'kern_pair_values')
                for i in range(0, len(pairs), 2):
                    if pairs[i] in targets and pairs[i + 1] in targets and pv[i // 2]:
                        errs.append(f'{name}: kerning pair {pairs[i]}, {pairs[i + 1]}')
    return errs


def main():
    full = {int(line, 16) for line in open(sys.argv[1]) if line.strip()}
    errs = []
    seen = set()
    for f in sorted(os.listdir(FONTS)):
        if not f.endswith('.c'):
            continue
        name = f[:-2]
        seen.add(name)
        if name not in EXPECT:
            errs.append(f'{name}: not in the font contract (check_fonts.py EXPECT)')
            continue
        errs += check(name, open(os.path.join(FONTS, f)).read(), full)
    for name in set(EXPECT) - seen:
        errs.append(f'{name}: missing (run components/ui/tools/build_fonts.sh)')
    for e in errs:
        print(e)
    print(f'check_fonts: {len(seen)} fonts, {len(errs)} problems')
    return 1 if errs else 0


if __name__ == '__main__':
    sys.exit(main())
