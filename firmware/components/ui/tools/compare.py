#!/usr/bin/env python3
"""
compare.py: the firmware's snapshots next to the mock-up's, scene by scene. Owner: ui builder.

    python3 compare.py <ref-dir> <snap-dir> <out-dir> [scene ...]

For each scene with a PNG in both directories (ref_scenes.js writes the mock-up's, tinybar_snapshot the firmware's),
writes <out-dir>/<scene>.png: the mock-up on top, the firmware in the middle, and where they differ at the bottom
(gray: the same; red: the firmware is darker; green: lighter). A flipped scene's snapshot is the panel's frame, so
it's turned back 180 degrees first. Prints the share of pixels that differ by more than 48 (of 255) in any channel,
worst first. Needs Pillow.
"""
import os
import sys

from PIL import Image, ImageChops, ImageDraw

FLIPPED = {'flipped'}


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
        rows.append((bad / (w * h), name))
    for share, name in sorted(rows, reverse=True):
        print(f'{share * 100:6.2f}%  {name}')


if __name__ == '__main__':
    main()
