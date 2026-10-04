#!/usr/bin/env python3
"""
gen_tomatoes.py: build-time generator for the tomato images (standard library only; runs in ESP-IDF's Python and on
any Linux host). Owner: ui builder.

Reads assets/tomato_ripe.png and assets/tomato_unripe.png (32 x 32 RGBA, PixelLab, no faces) and writes one C file
with LVGL 9 image descriptors (ARGB8888):
  ui_tomato_ripe, ui_tomato_unripe   the sprites as they are
  ui_tomato_pale                     the ripe sprite with every pixel 55% of the way to white (upcoming sessions)
  ui_tomato_ripe_64                  the ripe sprite at 2x, pixel for pixel (the alarm screens and the splash)
  ui_tomato_ripen[24]                the ripening frames: color breaks at the blossom end and spreads to the stem
                                     through yellow and orange (the mock-up's buildRipenFrames(), docs/mockup.html)

Usage: gen_tomatoes.py <ripe.png> <unripe.png> <out.c>
       gen_tomatoes.py --png <ripe.png> <unripe.png> <dir>   (writes the frames as PNGs, to compare with the mock-up)

The ripening is a line-by-line port of buildRipenFrames() and ripenPixel() in docs/mockup.html (the current version,
with the subtler spread: start capped at .85, SPAN .34, STEPS 5, the mute toward the midpoint). The browser reads the
sprites through a canvas, where a fully transparent pixel reads back as 0, 0, 0, 0, so that's done here too before the
skin mask is taken. Assigning a fraction to the canvas's Uint8ClampedArray rounds half to even, as Python's round()
does. Checked against the 24 frames the mock-up builds in Chromium: identical (test/host/ui/check_ripen.py).
"""
import math
import struct
import sys
import zlib


def read_png_rgba(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', path
    pos, idat, w = 8, b'', 0
    while pos < len(data):
        n, = struct.unpack('>I', data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', body)
            assert depth == 8 and ctype == 6 and interlace == 0, 'expects 8-bit RGBA, not interlaced'
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * 4
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - 4] if i >= 4 else 0
            b = prev[i]
            c = prev[i - 4] if i >= 4 else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return w, h, b''.join(rows)


def pale(rgba):
    """The ripe sprite 55% of the way to white (Math.round, as the mock-up's pale tomato), read through a canvas."""
    rgba = canvas_read(rgba)
    out = bytearray(rgba)
    for i in range(0, len(out), 4):
        for k in range(3):
            out[i + k] = math.floor(rgba[i + k] + (255 - rgba[i + k]) * .55 + .5)   # Math.round
    return bytes(out)


def canvas_read(rgba):
    """What getImageData() returns for a sprite drawn on a canvas: transparent pixels are 0, 0, 0, 0."""
    out = bytearray(rgba)
    for i in range(0, len(out), 4):
        if out[i + 3] == 0:
            out[i:i + 3] = b'\0\0\0'
    return bytes(out)


def rgb_to_hsl(r, g, b):
    r, g, b = r / 255, g / 255, b / 255
    mx, mn = max(r, g, b), min(r, g, b)
    l = (mx + mn) / 2
    if mx == mn:
        return 0.0, 0.0, l
    d = mx - mn
    sat = d / (2 - mx - mn) if l > .5 else d / (mx + mn)
    if mx == r:
        h = (g - b) / d + (6 if g < b else 0)
    elif mx == g:
        h = (b - r) / d + 2
    else:
        h = (r - g) / d + 4
    return h * 60, sat, l


def hsl_to_rgb(h, sat, l):
    h = math.fmod(math.fmod(h, 360) + 360, 360) / 360     # JS: ((h % 360) + 360) % 360 / 360
    if sat == 0:
        return [l * 255, l * 255, l * 255]
    q = l * (1 + sat) if l < .5 else l + sat - l * sat
    pp = 2 * l - q

    def f(t):
        t = math.fmod(t + 1, 1)
        if t < 1 / 6:
            return pp + (q - pp) * 6 * t
        if t < .5:
            return q
        if t < 2 / 3:
            return pp + (q - pp) * (2 / 3 - t) * 6
        return pp
    return [f(h + 1 / 3) * 255, f(h) * 255, f(h - 1 / 3) * 255]


def ripen_pixel(G, R, i, r):
    """One pixel partway between its green and red versions, taking the hue path through yellow and orange."""
    h1, s1, l1 = rgb_to_hsl(G[i], G[i + 1], G[i + 2])
    h2, s2, l2 = rgb_to_hsl(R[i], R[i + 1], R[i + 2])
    if s1 < .08 or s2 < .08:
        return [G[i + k] + (R[i + k] - G[i + k]) * r for k in range(3)]
    if h2 > 180:
        h2 -= 360
    mute = 1 - .25 * math.sin(math.pi * r)
    return hsl_to_rgb(h1 + (h2 - h1) * r, (s1 + (s2 - s1) * r) * mute, l1 + (l2 - l1) * r)


def clamp_u8(v):
    """Uint8ClampedArray assignment: clamp to 0..255, round half to even."""
    if v != v:
        return 0
    return int(min(255, max(0, round(v))))


def ripen_frames(green, red, w, h, frames):
    G, R = canvas_read(green), canvas_read(red)
    skin = []
    min_x, max_x, max_y = w, 0, 0
    for y in range(h):
        for x in range(w):
            i = (y * w + x) * 4
            if abs(G[i] - R[i]) + abs(G[i + 1] - R[i + 1]) + abs(G[i + 2] - R[i + 2]) > 40:
                skin.append((x, y, i))
                min_x, max_x, max_y = min(min_x, x), max(max_x, x), max(max_y, y)
    bx, by = (min_x + max_x) / 2, max_y
    far = max(math.hypot(x - bx, (y - by) * 1.15) for x, y, _ in skin)

    def noise(x, y):
        v = math.sin(x * 12.9898 + y * 78.233) * 43758.5453
        return v - math.floor(v)
    start = []
    for x, y, _ in skin:
        d = math.hypot(x - bx, (y - by) * 1.15) / far
        lobes = math.sin(math.atan2(y - by, x - bx) * 5 + 1.3) * .03 * d
        start.append(min(.85, max(0, .78 * d + lobes + .02 * (noise(x, y) - .5))))
    SPAN, STEPS = .34, 5
    out_frames = []
    for f in range(frames):
        p = f / (frames - 1)
        out = bytearray(G)
        for k, (_, _, i) in enumerate(skin):
            r = (p * (.85 + SPAN) - start[k]) / SPAN
            r = math.floor(max(0, min(1, r)) * STEPS + .5) / STEPS     # JS Math.round: halves go up
            if r == 0:
                continue
            c = [R[i], R[i + 1], R[i + 2]] if r == 1 else ripen_pixel(G, R, i, r)
            out[i], out[i + 1], out[i + 2], out[i + 3] = clamp_u8(c[0]), clamp_u8(c[1]), clamp_u8(c[2]), R[i + 3]
        out_frames.append(bytes(out))
    return out_frames


def scale2(rgba, w, h):
    out = bytearray(w * h * 16)
    for y in range(h * 2):
        for x in range(w * 2):
            j = ((y >> 1) * w + (x >> 1)) * 4
            k = (y * w * 2 + x) * 4
            out[k:k + 4] = rgba[j:j + 4]
    return bytes(out)


def write_png(path, w, h, rgba):
    raw = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)
    with open(path, 'wb') as fh:
        fh.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                 chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def c_image(name, w, h, rgba):
    argb = bytearray()
    for i in range(0, len(rgba), 4):
        r, g, b, a = rgba[i:i + 4]
        argb += bytes((b, g, r, a))  # LVGL ARGB8888 in memory: B, G, R, A
    hexes = ','.join('0x%02x' % v for v in argb)
    return (f'static const uint8_t {name}_map[] = {{{hexes}}};\n'
            f'const lv_image_dsc_t {name} = {{\n'
            f'    .header = {{.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888, .w = {w}, .h = {h}, .stride = {w * 4}}},\n'
            f'    .data_size = sizeof({name}_map),\n'
            f'    .data = {name}_map,\n}};\n')


def main():
    if sys.argv[1] == '--png':
        ripe_path, unripe_path, out_dir = sys.argv[2:5]
        w, h, red = read_png_rgba(ripe_path)
        _, _, green = read_png_rgba(unripe_path)
        for f, rgba in enumerate(ripen_frames(green, red, w, h, 24)):
            write_png(f'{out_dir}/ripen_{f:02d}.png', w, h, rgba)
        write_png(f'{out_dir}/pale.png', w, h, pale(red))
        return
    ripe_path, unripe_path, out_path = sys.argv[1:4]
    w, h, red = read_png_rgba(ripe_path)
    w2, h2, green = read_png_rgba(unripe_path)
    assert (w, h) == (w2, h2)
    frames = 24
    parts = ['/* Generated by components/ui/tools/gen_tomatoes.py. Do not edit. */\n#include "ui_assets.h"\n\n',
             c_image('ui_tomato_ripe', w, h, red), c_image('ui_tomato_unripe', w, h, green),
             c_image('ui_tomato_pale', w, h, pale(red)), c_image('ui_tomato_ripe_64', w * 2, h * 2, scale2(red, w, h))]
    for f, rgba in enumerate(ripen_frames(green, red, w, h, frames)):
        parts.append(c_image(f'ui_tomato_ripen_{f}', w, h, rgba))
    parts.append('static const lv_image_dsc_t *const ripen[] = {' +
                 ', '.join(f'&ui_tomato_ripen_{f}' for f in range(frames)) + '};\n\n')
    parts.append('const lv_image_dsc_t *ui_asset_tomato_ripe(void) { return &ui_tomato_ripe; }\n'
                 'const lv_image_dsc_t *ui_asset_tomato_unripe(void) { return &ui_tomato_unripe; }\n'
                 'const lv_image_dsc_t *ui_asset_tomato_pale(void) { return &ui_tomato_pale; }\n'
                 'const lv_image_dsc_t *ui_asset_tomato_ripe_64(void) { return &ui_tomato_ripe_64; }\n'
                 'const lv_image_dsc_t *ui_asset_tomato_ripening(int f)\n{\n'
                 f'    if (f < 0) f = 0;\n    if (f > {frames - 1}) f = {frames - 1};\n    return ripen[f];\n}}\n')
    with open(out_path, 'w') as fh:
        fh.write(''.join(parts))


if __name__ == '__main__':
    main()
