# Font tools

Build steps for the bar's fonts: the **Low Glare layout, option G** in `docs/pixel-fonts.html` (the user's pick, recorded in `docs/decisions.md`):

- **Bitcount Prop Single, round dots:** headlines at 100, 80, 60 and 50 px, and the timer and clock at 120 px.
- **Bitcount, square dots:** the side value at 40 px, and the hold-to-power-off title.
- **Handjet Snap17:** the sub line and all small text at 17 px, and menu tile values at 34 px.

Option G started from option D and moved the sub line from Bitcount's squares to Handjet, so real copy fits.

## `equalize_digits.py`: equal-width digits for Bitcount

### Why

Bitcount Prop Single has proportional digits. Its 1 is two dots narrower than the other digits (500 units against 700). Its tabular figures (the `tnum` feature: `one.tab`, `six.tab` and `seven.tab`) don't fix this, since they're 600 units wide. The firmware also can't use them: lv_font_conv ignores OpenType features, so LVGL only ever gets the default digits.

lv_font_conv does copy the font's GPOS kerning into the LVGL font, and Bitcount kerns some digit pairs, including 1 followed by 7 and 6 followed by 7, each by −1 dot. At the 120 px timer size, this is how the original font places four clock strings:

| | 18:41 | 18:46 | 11:11 | 17:07 |
|---|---|---|---|---|
| Width | 312 px | 336 px | 264 px | 324 px |
| Colon at x | 144 | 144 | 120 | 132 |

Four of the five character slots move as the time ticks.

### What it does

1. If the input is a variable font, pins it to one static instance (`--axes`).
2. Gives 0–9 the widest digit's advance width and centers each digit's outline in it. `--grid` rounds each shift to whole design units (100 = one Bitcount dot), so no dot lands between pixels.
3. Removes every kerning pair with a digit on either side. Kerning between letters is kept.
4. Drops the digits from the `tnum` and `pnum` substitutions, since the digits are tabular now. That keeps a browser's `font-variant-numeric: tabular-nums` from swapping in narrower glyphs.
5. Renames the font, keeping the copyright and license entries, and records the change in the name table's description (name ID 10).

Two options:

- `--colon` gives the colon the digits' side bearings, so there are 2 dots on each side of it in "18:41".
- `--tnum-shapes` uses the font's own tabular glyphs for the digits. For Bitcount, that's the 5-dot-wide 1 with a base. `docs/pixel-fonts.html` sets digits with `tabular-nums`, so this is the 1 shown in option D.

With the recommended options, the same four strings are all 372 px wide, the colon is always at x = 168, and no column moves.

The output is byte-for-byte reproducible from the same input and options. The script keeps the input's modified date unless `SOURCE_DATE_EPOCH` is set.

### Requirements

Python 3 and fontTools (tested with 4.66.1): `pip install fonttools`.

### Source fonts

From the google/fonts repository. These are the files the font study used (SHA-256 checked on 2026-10-04):

| File | URL | SHA-256 |
|---|---|---|
| `BitcountPropSingle[CRSV,ELSH,ELXP,slnt,wght].ttf` | https://github.com/google/fonts/tree/main/ofl/bitcountpropsingle | `e6f39bb1…0c901a8405` |
| `Handjet[ELGR,ELSH,wght].ttf` | https://github.com/google/fonts/tree/main/ofl/handjet | `9262749e…02d41bdf4d` |

### Build the two Bitcount fonts

```sh
VF='BitcountPropSingle[CRSV,ELSH,ELXP,slnt,wght].ttf'

# Round dots: headlines (100, 80, 60 and 50 px) and the timer and clock (120 px)
python3 tools/fonts/equalize_digits.py "$VF" TinyBarBitcount-Round.ttf \
  --family "TinyBar Bitcount Round" --axes wght=400,ELSH=0,CRSV=0,ELXP=0,slnt=0 \
  --grid 100 --colon --tnum-shapes --license tools/fonts/licenses/Bitcount-OFL.txt

# Squares: side value (40 px) and the hold-screen title
python3 tools/fonts/equalize_digits.py "$VF" TinyBarBitcount-Square.ttf \
  --family "TinyBar Bitcount Square" --axes wght=384.88,ELSH=50,CRSV=0,ELXP=0,slnt=0 \
  --grid 100 --colon --tnum-shapes --license tools/fonts/licenses/Bitcount-OFL.txt
```

The axis values are option D's, which option G keeps. `CRSV=0` turns off the cursive a and f, and `ELSH=50` at weight 384.88 gives squares that exactly fill the grid. Every glyph other than the digits and the colon is identical to the static instances the font study rendered.

The script prints each digit's old advance, its shift and its new side bearings. Leave out `--tnum-shapes` for Bitcount's narrow default 1, centered in the same width.

### License (SIL OFL 1.1)

A Modified Version may not use a **Reserved Font Name** (RFN). Neither Bitcount nor Handjet declares one. Both the name tables and the upstream `OFL.txt` files (in `licenses/`) were checked, and their copyright lines have no "with Reserved Font Name" clause.

The outputs are still renamed ("TinyBar Bitcount Round" and "TinyBar Bitcount Square") so they can't be mistaken for the original. The script reads any RFN in the name table, and in `--license` when given, and refuses a family name that contains one.

When the fonts ship, in the firmware source or a release, ship `licenses/Bitcount-OFL.txt` and `licenses/Handjet-OFL.txt` with them.

## Handjet Snap17: Handjet sharp at 17 px

Handjet's digits are already equal (3,360 units) with no digit kerning, so it doesn't need `equalize_digits.py`. Running the script on it would also re-center its 4 and 7.

It does need `snap17.py`. As served, Handjet is 33% soft at 17 px on the device (see the notes below), because many elements sit on half-element steps. `snap17.py` moves every element onto whole elements (480 units, one pixel at 17 px):

- If all of a glyph's elements on an axis are on odd half-steps, the whole glyph shifts half an element, which keeps its shape exactly.
- If only some are, each odd element moves half a step toward the glyph's center, which keeps symmetric glyphs symmetric.
- Advances are rounded up to whole elements, as LVGL rounds them at 17 px.

The result is 0.3% soft at 17 px (only the grave accent), and it stays sharp at 34 px.

```sh
# 1. A static solid-square instance (Google's defaults: wght 400, solid squares, element grid 1)
fonttools varLib.instancer 'Handjet[ELGR,ELSH,wght].ttf' wght=400 ELSH=2 ELGR=1 --static -o HandjetSolid-400.ttf

# 2. Snap it to whole pixels at 17 px, renamed so it isn't mistaken for Handjet
python3 tools/fonts/snap17.py HandjetSolid-400.ttf TinyBarHandjet-Snap17.ttf "TinyBar Handjet Snap17"
```

The mock-up (`docs/mockup.html`) and the font study (`docs/pixel-fonts.html`) embed a woff2 subset of the snapped font, covering ASCII and Latin-1, as "Handjet Snap17".

Some notes on the axes:

- **Element size:** Handjet's element is 480 units, or 1/17 em. At 17 px each element is one pixel, at 34 px two.
- **Half-element offsets:** many glyphs (most diagonals, plus I, J, a, f and t) place elements at half-element (240-unit) offsets. At 17 px those fall between pixels.
- **Element shape:** the round-dot shape (`ELSH=8`, Circle) can't show as a round dot at small-text sizes. At 17 px it renders exactly like the solid shape, and at 34 px it's a 2 × 2 block at 91% brightness. Use the solid shape (`ELSH=2`).
- **Weight:** heavier weights (700, 900) grow the element off the pixel grid at 17 px, so they're softer (62% partly lit pixels).

## Converting for LVGL 9

Use lv_font_conv 1.5.3, at whole-number sizes only, with `--autohint-off` (the autohinter moves Bitcount's crossbars by half a pixel). It reads GPOS kerning, so keep the fonts above as built.

```sh
FULL=0x20-0x7E,0xB0,0xB7,0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2022,0x2026
npx lv_font_conv@1.5.3 --font TinyBarBitcount-Round.ttf --size 100 --bpp 4 --autohint-off \
  -r $FULL --format lvgl --lv-include lvgl.h -o tb_bitcount_round_100.c
```

The table below shows what each font measured on 2026-10-04. "Partly lit" is the share of lit pixels that are only partly lit at 4 bpp (0% means 1 bpp is lossless). "Flash" counts the C arrays: plain, and with lv_font_conv's default RLE compression in brackets. Compression needs `LV_USE_FONT_COMPRESSED 1` in `lv_conf.h`, and it has no effect at 1 bpp.

These measurements were taken for option D's sizes. Option G's other sizes (Bitcount round at 80, 60 and 50 px, the square side value at 40 px, and Handjet Snap17) haven't been converted and measured yet.

| Role | Font | Size | Glyphs | Partly lit | bpp | Flash |
|---|---|---|---|---|---|---|
| Busy, On a call | Round | 100 px | 105 (full) | 22.7% | 4 | 117.1 KB (51.6 KB) |
| Timer, clock | Round | 120 px | 12 (0–9 : space) | 22.6% | 4 | 21.6 KB (8.8 KB) |
| Side value (option D size; G uses 40 px) | Square | 50 px | 105 | 0% | 1 | 9.5 KB |
| Sub line (option D only; G uses Handjet) | Square | 20 px | 105 | 0% | 1 | 3.4 KB |
| Small text, before snapping | Handjet solid | 17 px | 105 | 33.0% (all at half) | 4 | 3.4 KB (2.5 KB) |
| Small text and sub line (option G) | Handjet Snap17 | 17 px | 105 | 0.3% (grave accent only) | not measured | not measured |

The round dots need anti-aliasing. At 2 bpp they're close to 4 bpp (59.7 KB plain for the 100 px headline, 29.7 KB compressed). At 1 bpp they turn into blocky octagons.
