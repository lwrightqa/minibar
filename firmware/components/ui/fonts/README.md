# Generated fonts (owner: ui builder)

The converted Barlow fonts land here as `*.c` files (LVGL 9 format, from `lv_font_conv`), and are committed so a
plain `idf.py build` needs neither Node nor Python packages. They're built by `../tools/build_fonts.sh` from the TTFs
in `firmware/fonts/src/`:

1. Freeze the tabular figures into the default digits: `pyftfeatfreeze -f tnum` (pip `opentype-feature-freezer`),
   and give the copy a TinyBar family name and a modification note, keeping the Barlow Project's copyright and the OFL
   (`firmware/fonts/licenses/Barlow-OFL.txt`). Check: every digit the same advance, no digit kerning pairs.
2. Convert with `npx lv_font_conv --bpp 4 --format lvgl --lv-include lvgl.h`, one file per role in
   `../include/ui_theme.h` (`ui_font_role_t`), with the character set `tb_text_drawable()` defines
   (`components/core/include/tb_text.h`) for every font that draws user text, and the short fixed set
   (A-Z, 0-9, colon, space, middle dot) for the 112, 100 and 78 px headlines.
3. Record each file's flash size in this README.

`../src/ui_fonts.c` maps each role to its font.
