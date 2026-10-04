#!/usr/bin/env python3
"""
compare.py: the firmware's snapshots next to the mock-up's, scene by scene. Owner: ui builder.

    python3 compare.py <ref-dir> <snap-dir> <out-dir> [scene ...]

For each scene with a PNG in both directories (ref_scenes.js writes the mock-up's, tinybar_snapshot the firmware's),
writes <out-dir>/<scene>.png: the mock-up on top, the firmware in the middle, and where they differ at the bottom
(gray: the same; red: the firmware is darker; green: lighter). A flipped scene's snapshot is the panel's frame, so
it's turned back 180 degrees first. Prints the share of pixels that differ by more than 48 (of 255) in any channel,
worst first.

The share alone misses a line drawn a few pixels off (a 4 px drop of the sub line is under 2% of the pixels), so it
also measures each text line's baseline in both (the most common bottom row of strong ink across the band's columns;
descenders are a minority) and lists every line that's 1 px or more apart. The exit status is 1 when any is. Needs
Pillow.
"""
import os
import sys

from PIL import Image, ImageChops, ImageDraw

FLIPPED = {'flipped'}

# The text lines of a screen with a status field (statuses, the Pomodoro, calls, meetings, the pairing and setup
# screens), as (x0, x1, y0, y1) bands on the 640 x 172 screen. On the QR screen the text starts right of the code.
# Menus, the hold screen, the splash, the flash and the dark screen aren't measured this way: the bands would cross
# tile edges and fills rather than one line of text (the share of pixels and the side-by-side sheet cover them).
BANDS = {'kicker': (0, 448, 10, 37), 'head': (0, 448, 38, 133), 'sub': (0, 448, 136, 165),
         'sys': (448, 638, 10, 37), 'label': (448, 638, 78, 96), 'value': (448, 638, 97, 136), 'foot': (448, 638, 137, 162)}
QR_SCENES = {'setup_qr', 'toast_setup'}
NO_BASELINES = {'toast_menu', 'hold_keep', 'hold_off', 'splash', 'dark', 'flash'}


def bands_for(name):
    if name.startswith('menu_') or name in NO_BASELINES:
        return {}
    if name in QR_SCENES:   # the text column right of the code, with its two steps
        return {'kicker': (180, 620, 10, 37), 'title': (180, 620, 38, 90), 'step1': (180, 620, 95, 116),
                'step2': (180, 620, 117, 136), 'foot': (180, 620, 137, 165)}
    return BANDS


def baseline(px, box):
    x0, x1, y0, y1 = box
    hist = {}
    for y in range(y0, y1):
        for x in range(x0, x1):
            hist[px[x, y]] = hist.get(px[x, y], 0) + 1
    bg = max(hist, key=hist.get)
    bottoms = {}
    for x in range(x0, x1):
        b = None
        for y in range(y0, y1):
            p = px[x, y]
            if max(abs(p[i] - bg[i]) for i in range(3)) > 128:
                b = y
        if b is not None:
            bottoms[b] = bottoms.get(b, 0) + 1
    if not bottoms:
        return None
    return max(bottoms, key=bottoms.get) + 1


def main():
    ref_dir, snap_dir, out_dir = sys.argv[1:4]
    only = set(sys.argv[4:])
    os.makedirs(out_dir, exist_ok=True)
    rows = []
    for f in sorted(os.listdir(snap_dir)):
        name = f[:-4]
        if not f.endswith('.png') or (only and name not in only):
            continue
        rp = os.path.join(ref_dir, f)
        if not os.path.exists(rp):
            continue
        ref = Image.open(rp).convert('RGB')
        snap = Image.open(os.path.join(snap_dir, f)).convert('RGB')
        if name in FLIPPED:
            snap = snap.rotate(180)
        diff = ImageChops.difference(ref, snap)
        px = diff.load()
        w, h = ref.size
        bad = sum(1 for y in range(h) for x in range(w) if max(px[x, y]) > 48)
        heat = Image.new('RGB', (w, h))
        hp = heat.load()
        rl, sl = ref.convert('L').load(), snap.convert('L').load()
        for y in range(h):
            for x in range(w):
                d = sl[x, y] - rl[x, y]
                g = 128 + (rl[x, y] - 128) // 4
                hp[x, y] = (g, g, g) if abs(d) <= 24 else ((255, 40, 40) if d < 0 else (40, 220, 80))
        sheet = Image.new('RGB', (w, h * 3 + 48), (255, 255, 255))
        dr = ImageDraw.Draw(sheet)
        y = 0
        for label, im in (('mock-up', ref), ('firmware', snap), ('difference', heat)):
            dr.text((4, y + 2), f'{name}: {label}', fill=(0, 0, 0))
            sheet.paste(im, (0, y + 16))
            y += h + 16
        sheet.save(os.path.join(out_dir, f))
        rp_, sp_ = ref.load(), snap.load()
        off = []
        for band, box in bands_for(name).items():
            rb, sb = baseline(rp_, box), baseline(sp_, box)
            if rb is not None and sb is not None and rb != sb:
                off.append(f'{band} {rb}/{sb}')
        rows.append((bad / (w * h), name, off))
    moved = 0
    for share, name, off in sorted(rows, reverse=True):
        print(f'{share * 100:6.2f}%  {name}' + (f'   baselines off (mock-up/firmware): {", ".join(off)}' if off else ''))
        moved += bool(off)
    print(f'{len(rows)} scenes, {moved} with a text line 1 px or more off its baseline')
    sys.exit(1 if moved else 0)


if __name__ == '__main__':
    main()
