# Generated fonts (owner: ui builder)

Bold Signal's fonts as LVGL 9 C files, one per role in `../include/ui_theme.h` (`ui_font_role_t`), made by
`../tools/build_fonts.sh` from the Barlow TTFs in `firmware/fonts/src/`. They're committed, so a plain `idf.py build`
needs neither Node nor Python packages. Rebuild them only when a role, a size or the character set changes:

```sh
pip install fonttools opentype-feature-freezer
LVGL_DIR=$PWD/firmware/managed_components/lvgl__lvgl firmware/components/ui/tools/build_fonts.sh
```

## How they're made

1. **Tabular digits frozen in.** lv_font_conv ignores OpenType features and Barlow's default digits are
   proportional, so `pyftfeatfreeze -f tnum` makes the tabular figures the default ones. The frozen copies are
   renamed **TinyBar Condensed** and **TinyBar Text**, with a modification note in their name tables
   (`firmware/fonts/tinybar/`). The script checks them: every digit one advance (498 units in TinyBar Condensed Bold,
   533, 538 and 544 in TinyBar Text Medium, SemiBold and Bold) and no kerning between digits or a digit and the colon.
   The family and file names keep the TinyBar prefix from before the project's rename to MiniBar: they're binary
   files nobody sees on the bar, and these C files are built from them, so they're left byte for byte as they are.
2. **Converted** with lv_font_conv 1.5.3 at 4 bpp, compressed (its default), autohinting off, so the glyphs keep the
   shapes and advances the mock-up's browser draws.
3. **Tracking baked in.** Bold Signal letter-spaces the kicker by 1.5 px and the labels, chips and tile labels by
   1.2 px. LVGL's `text_letter_space` is whole pixels only, so the script adds the tracking to every glyph's advance
   (they're in 1/16 px) in `ui_font_kick_15.c` and `ui_font_label_12.c`.
4. **Checked** by the host tests (`test/host/ui/check_fonts.py`): each font has exactly its character set, the
   tabular fonts have one digit advance and no digit kerning, and the 100, 78 and 62 px headlines keep proportional
   digits.

Digits are tabular everywhere except the 100, 78 and 62 px headlines, as in the mock-up's Signal layout CSS: those
never tick, and a proportional 1 keeps BACK AT 12:30 at 78 px.

## The fonts

The full set is exactly what `tb_text_drawable()` accepts (`components/core/include/tb_text.h`): printable ASCII,
Latin-1, en and em dash, ellipsis. Sizes are the bytes each adds to the image (`.rodata`, compiled for the host; the
ESP32-S3's are the same within a few bytes).

| File | Role | Font | Size | Characters | Bytes |
|---|---|---|---|---|---|
| `ui_font_head_112.c` | timer, clock, BUSY, the pairing code | TinyBar Condensed Bold (tabular) | 112 px | A-Z 0-9 : - space · | 19,760 |
| `ui_font_head_100.c` | AVAILABLE, ON A CALL | Barlow Condensed Bold | 100 px | A-Z 0-9 : - space · | 17,117 |
| `ui_font_head_78.c` | IN A MEETING, BACK AT 2:30, the splash | Barlow Condensed Bold | 78 px | A-Z 0-9 : - space · | 12,373 |
| `ui_font_head_62.c` | titles, messages, BREAK TIME, setup titles, headlines too wide for 78 | Barlow Condensed Bold | 62 px | full | 44,677 |
| `ui_font_value_46.c` | info column value, the hold screen's title | TinyBar Condensed Bold | 46 px | 0-9 : space h m A-Z | 6,714 |
| `ui_font_value_28.c` | word values ("Rest of day"), tile values | TinyBar Condensed Bold | 28 px | full | 18,711 |
| `ui_font_ampm_36.c` | AM/PM after the clock (.32 em of 112) | TinyBar Condensed Bold | 36 px | A M P | 616 |
| `ui_font_ampm_17.c` | AM/PM after a side value | TinyBar Condensed Bold | 17 px | A M P | 401 |
| `ui_font_sub_19.c` | the sub line | TinyBar Text Medium | 19 px | full | 13,467 |
| `ui_font_step_16.c` | the setup screen's steps | TinyBar Text Medium | 16 px | printable ASCII | 5,888 |
| `ui_font_step_16b.c` | MiniBar-Setup in the first step | TinyBar Text Bold | 16 px | the letters of MiniBar-Setup | 876 |
| `ui_font_kick_15.c` | the kicker (+1.5 px tracking) | TinyBar Text Bold | 15 px | full | 11,347 |
| `ui_font_sys_15.c` | status row time, the pill, toasts | TinyBar Text SemiBold | 15 px | full | 11,248 |
| `ui_font_foot_14.c` | info column foot, tile feet, setup foot, hold line | TinyBar Text Medium | 14 px | full | 10,715 |
| `ui_font_label_12.c` | labels, source chips, tile labels (+1.2 px tracking) | TinyBar Text Bold | 12 px | full | 9,873 |
| | | | | **total** | **183,783** |

About 180 KB, 1.1% of the 16 MB flash (3% of a 6 MB app slot). Everything at the full character set would add about
60 KB; the tomato images add 124 KB more (`ui_tomatoes.c`, generated at build time).

## Licenses

Barlow is © 2017 The Barlow Project Authors, under the SIL Open Font License 1.1
(`firmware/fonts/licenses/Barlow-OFL.txt`); it declares no Reserved Font Name. These C files are converted subsets of
Barlow and of the frozen copies named TinyBar Condensed and TinyBar Text, under the same license.
