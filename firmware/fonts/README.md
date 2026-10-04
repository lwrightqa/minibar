# Fonts (owner: ui builder)

TinyBar's screen look, Bold Signal, uses the Barlow superfamily: **Barlow Condensed Bold** for headlines and values,
**Barlow** Medium, SemiBold and Bold for all other text.

| Folder | What |
|---|---|
| `src/` | The original TTFs from Google Fonts (Barlow 1.408): `BarlowCondensed-Bold.ttf`, `Barlow-Medium.ttf`, `Barlow-SemiBold.ttf`, `Barlow-Bold.ttf`. |
| `tinybar/` | Modified copies made by `components/ui/tools/build_fonts.sh`: the tabular figures frozen as the default digits (`pyftfeatfreeze -f tnum`), renamed **TinyBar Condensed** and **TinyBar Text**, with a note in each name table (name ID 10) saying what changed. |
| `licenses/` | `Barlow-OFL.txt`, the SIL Open Font License 1.1 that covers the originals, the modified copies and the converted C files in `components/ui/fonts/`. |

The firmware doesn't read these files at run time: the build script converts them into LVGL C files
(`components/ui/fonts/`, see its README for each font's role, size, characters and flash use).

## Licensing notes

- Barlow is Copyright 2017 The Barlow Project Authors (https://github.com/jpt/barlow), licensed under the OFL 1.1. Its
  copyright notice names no Reserved Font Name, so modified versions may keep the name; the TinyBar copies are
  renamed anyway, so a modified font is never mistaken for the original.
- The OFL lets the fonts be bundled with software, including firmware images, as long as the license travels with
  them: keep `licenses/Barlow-OFL.txt` with any distribution of the source, and mention Barlow and the OFL in the
  product's documentation or about page if the firmware image is distributed on its own.
- The fonts are not sold by themselves.
