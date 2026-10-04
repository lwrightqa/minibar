#!/usr/bin/env python3
"""Give a font's digits 0-9 one common advance width, so a clock or countdown doesn't jitter.

LVGL draws text with each glyph's own advance width plus any kerning, and lv_font_conv ignores
OpenType features, so a font's tabular figures (the `tnum` feature) never reach the device. This
script fixes the digits in the font itself, before conversion:

  1. If the input is a variable font, pins it to one static instance (--axes).
  2. Gives 0-9 the widest digit's advance width and centers each digit's outline in it,
     optionally rounding each shift to a whole number of design-grid units (--grid).
  3. Removes every kerning pair that has a digit on either side (lv_font_conv reads GPOS kerning,
     and Bitcount kerns 1-7 and 6-7, which moves the digit columns even when advances are equal).
  4. Drops the digits from the `tnum` and `pnum` substitutions (they are tabular now), so browsers
     that turn on tabular figures don't swap in the font's narrower .tab glyphs.
  5. Renames the font. The SIL Open Font License forbids a Modified Version from using a
     Reserved Font Name (RFN). The script reads any RFN declared in the name table's copyright and
     license entries and, with --license, in the upstream OFL.txt, and refuses to write a name that
     contains one.

Options: --colon gives the colon the digits' side bearings, so a time is spaced evenly around it;
--tnum-shapes uses the font's own tabular-figure glyphs (for Bitcount, the wider 1 with a base that
docs/pixel-fonts.html shows, since that page sets digits with tabular-nums).

The output is deterministic: the same input and options give a byte-identical file (the head
table's modified date is kept from the input unless SOURCE_DATE_EPOCH is set).

Usage (see README.md):
  python3 equalize_digits.py 'BitcountPropSingle[CRSV,ELSH,ELXP,slnt,wght].ttf' TinyBarBitcount-Round.ttf \
      --family "TinyBar Bitcount Round" --axes wght=400,ELSH=0,CRSV=0,ELXP=0,slnt=0 \
      --grid 100 --colon --tnum-shapes --license tools/fonts/licenses/Bitcount-OFL.txt
"""

import argparse
import copy
import os
import re
import sys

from fontTools.ttLib import TTFont

DIGITS = '0123456789'


# ---------------------------------------------------------------- license / names

def declared_rfns(texts):
    """Reserved Font Names declared as 'with Reserved Font Name(s) X' in copyright statements."""
    names = []
    for text in texts:
        if not text:
            continue
        head = text.split('PREAMBLE')[0]               # the OFL body defines the term; skip it
        head = re.sub(r'\s+', ' ', head)
        for m in re.finditer(r'with\s+Reserved\s+Font\s+Names?\s+(.+?)(?:\.(?:\s|$)|$)', head, re.I):
            rest = m.group(1)
            quoted = re.findall(r'["“‘\']([^"”’\']+)["”’\']', rest)
            parts = quoted or re.split(r',|\band\b', rest)
            names += [p.strip(' .;') for p in parts if p.strip(' .;')]
    return sorted(set(names))


def norm(s):
    return re.sub(r'[\s_\-]+', '', s).lower()


def set_name(font, name_id, value):
    name = font['name']
    name.removeNames(nameID=name_id)
    name.setName(value, name_id, 3, 1, 0x409)          # Windows, Unicode BMP, English (US)


def rename(font, family, note):
    name = font['name']
    ps_family = re.sub(r'[^A-Za-z0-9]', '', family)
    ps = (ps_family + '-Regular')[:63]
    version = font['head'].fontRevision
    set_name(font, 1, family)
    set_name(font, 2, 'Regular')
    set_name(font, 3, f'{version:.3f};TinyBar;{ps}')
    set_name(font, 4, family)
    set_name(font, 6, ps)
    old5 = name.getDebugName(5) or f'Version {version:.3f}'
    set_name(font, 5, old5 + '; TinyBar equal-width digits')
    set_name(font, 10, note)
    for nid in (16, 17, 21, 22, 25):                   # typographic/WWS family, variations PS prefix
        name.removeNames(nameID=nid)
    if 'OS/2' in font:
        font['OS/2'].fsSelection = (font['OS/2'].fsSelection & ~0b1100001) | 0b1000000  # REGULAR
        font['OS/2'].achVendID = 'NONE'                # not the original foundry's release
    font['head'].macStyle = 0
    return ps


# ---------------------------------------------------------------- outlines and metrics

def shift_glyph(font, gname, dx):
    glyf = font['glyf']
    g = glyf[gname]
    if dx == 0 or g.numberOfContours == 0:
        return
    if g.isComposite():
        for c in g.components:
            c.x += dx
    else:
        g.coordinates.translate((dx, 0))
    g.recalcBounds(glyf)
    # Composites elsewhere that reuse this glyph keep their old placement.
    for other in font.getGlyphOrder():
        og = glyf[other]
        if other != gname and og.isComposite():
            touched = False
            for c in og.components:
                if c.glyphName == gname:
                    c.x -= dx
                    touched = True
            if touched:
                og.recalcBounds(glyf)


def equalize(font, digit_glyphs, grid):
    glyf, hmtx = font['glyf'], font['hmtx']
    rows = []
    for gname in digit_glyphs:
        glyf[gname].recalcBounds(glyf)
    target = max(hmtx[g][0] for g in digit_glyphs)
    for gname in digit_glyphs:
        g = glyf[gname]
        adv, _ = hmtx[gname]
        x0, x1 = g.xMin, g.xMax
        exact = (target - (x1 - x0)) / 2 - x0           # shift that centers the ink in `target`
        dx = int((exact / grid) + 0.5 if exact >= 0 else (exact / grid) - 0.5) * grid
        shift_glyph(font, gname, dx)
        g = glyf[gname]
        hmtx[gname] = (target, g.xMin)
        rows.append((gname, adv, x0, x1, dx, g.xMin, target - g.xMax))
    return target, rows


def space_colon(font, target, digit_glyphs, grid):
    """Center the colon with the same side bearing the widest digits now have."""
    glyf, hmtx = font['glyf'], font['hmtx']
    cname = font.getBestCmap().get(ord(':'))
    if not cname or glyf[cname].numberOfContours == 0:
        return None
    widest = max(glyf[d].xMax - glyf[d].xMin for d in digit_glyphs)
    sb = int((target - widest) / 2 / grid + 0.5) * grid
    g = glyf[cname]
    g.recalcBounds(glyf)
    adv, x0, x1 = hmtx[cname][0], g.xMin, g.xMax
    dx = sb - x0
    shift_glyph(font, cname, dx)
    g = glyf[cname]
    hmtx[cname] = (g.xMax + sb, g.xMin)
    return (cname, adv, x0, x1, dx, g.xMin, sb)


def use_tnum_shapes(font):
    """Point the cmap's digits at the glyphs the font's tnum feature would substitute."""
    alt = {}
    if 'GSUB' in font:
        gsub = font['GSUB'].table
        for fr in gsub.FeatureList.FeatureRecord:
            if fr.FeatureTag != 'tnum':
                continue
            for li in fr.Feature.LookupListIndex:
                lk = gsub.LookupList.Lookup[li]
                for st in lk.SubTable:
                    ltype = lk.LookupType
                    if ltype == 7:
                        ltype, st = st.ExtensionLookupType, st.ExtSubTable
                    if ltype == 1:
                        alt.update(st.mapping)
    done = []
    for table in font['cmap'].tables:
        if not table.isUnicode():
            continue
        for d in DIGITS:
            g = table.cmap.get(ord(d))
            if g in alt:
                table.cmap[ord(d)] = alt[g]
                if f'{g}->{alt[g]}' not in done:
                    done.append(f'{g}->{alt[g]}')
    return done


# ---------------------------------------------------------------- kerning and features

def _zero_value(v):
    if v is None:
        return None
    v = copy.deepcopy(v)
    for k in list(vars(v)):
        setattr(v, k, None if k.endswith('Device') else 0)
    return v


def _nonzero(v):
    return v is not None and any(isinstance(x, int) and x for x in vars(v).values())


def strip_digit_kerning(font, digits):
    removed = 0
    if 'GPOS' in font:
        lookups = font['GPOS'].table.LookupList.Lookup
        for lk in lookups:
            for i, st in enumerate(lk.SubTable):
                ltype = lk.LookupType
                if ltype == 9:
                    ltype, st = st.ExtensionLookupType, st.ExtSubTable
                if ltype != 2:
                    continue
                if st.Format == 1:
                    keep_cov, keep_sets = [], []
                    for g, ps in zip(st.Coverage.glyphs, st.PairSet):
                        recs = ps.PairValueRecord
                        if g in digits:
                            removed += sum(_nonzero(r.Value1) or _nonzero(getattr(r, 'Value2', None)) for r in recs)
                            continue
                        kept = [r for r in recs if r.SecondGlyph not in digits]
                        removed += sum(_nonzero(r.Value1) or _nonzero(getattr(r, 'Value2', None))
                                       for r in recs if r.SecondGlyph in digits)
                        if kept:
                            ps.PairValueRecord, ps.PairValueCount = kept, len(kept)
                            keep_cov.append(g)
                            keep_sets.append(ps)
                    st.Coverage.glyphs, st.PairSet, st.PairSetCount = keep_cov, keep_sets, len(keep_sets)
                elif st.Format == 2:
                    c1, c2 = st.ClassDef1.classDefs, st.ClassDef2.classDefs
                    for g in st.Coverage.glyphs:           # digit as the first glyph
                        if g in digits:
                            rec = st.Class1Record[c1.get(g, 0)]
                            removed += sum(_nonzero(r.Value1) or _nonzero(getattr(r, 'Value2', None))
                                           for r in rec.Class2Record)
                    st.Coverage.glyphs = [g for g in st.Coverage.glyphs if g not in digits]
                    # digit as the second glyph: move the digits into a new class whose values are all 0
                    for d in digits:
                        for rec in st.Class1Record:
                            r = rec.Class2Record[c2.get(d, 0)]
                            removed += _nonzero(r.Value1) or _nonzero(getattr(r, 'Value2', None))
                    new = st.Class2Count
                    for rec in st.Class1Record:
                        z = copy.deepcopy(rec.Class2Record[0])
                        z.Value1 = _zero_value(getattr(z, 'Value1', None))
                        if hasattr(z, 'Value2'):
                            z.Value2 = _zero_value(z.Value2)
                        rec.Class2Record.append(z)
                    st.Class2Count = new + 1
                    for d in digits:
                        c2[d] = new
    if 'kern' in font:
        for st in font['kern'].kernTables:
            if hasattr(st, 'kernTable'):
                before = len(st.kernTable)
                st.kernTable = {k: v for k, v in st.kernTable.items() if k[0] not in digits and k[1] not in digits}
                removed += before - len(st.kernTable)
    return removed


def drop_numeric_alternates(font, digits, tags=('tnum', 'pnum')):
    dropped = []
    if 'GSUB' not in font:
        return dropped
    gsub = font['GSUB'].table
    users = {}
    for fr in gsub.FeatureList.FeatureRecord:
        for li in fr.Feature.LookupListIndex:
            users.setdefault(li, set()).add(fr.FeatureTag)
    for li, tagset in users.items():
        if not tagset & set(tags):
            continue
        if tagset - set(tags):
            print(f'  note: lookup {li} is shared by {sorted(tagset)}; left alone', file=sys.stderr)
            continue
        lk = gsub.LookupList.Lookup[li]
        for st in lk.SubTable:
            ltype = lk.LookupType
            if ltype == 7:
                ltype, st = st.ExtensionLookupType, st.ExtSubTable
            if ltype == 1:
                for d in digits:
                    if d in st.mapping:
                        dropped.append(f'{"/".join(sorted(tagset))}: {d}->{st.mapping.pop(d)}')
    return dropped


# ---------------------------------------------------------------- main

def parse_axes(s):
    out = {}
    for part in filter(None, re.split(r'[,\s]+', s or '')):
        k, v = part.split('=')
        out[k.strip()] = float(v)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('input', help='static TTF, or a variable TTF together with --axes')
    ap.add_argument('output')
    ap.add_argument('--family', help='family name of the output (default: input family + " TinyBar")')
    ap.add_argument('--axes', help='pin a variable font first, e.g. wght=400,ELSH=0,CRSV=0,ELXP=0,slnt=0')
    ap.add_argument('--grid', type=int, default=1,
                    help='round each shift to a multiple of this many font units (Bitcount: 100, one dot)')
    ap.add_argument('--license', help="the font's upstream OFL.txt, checked for a Reserved Font Name")
    ap.add_argument('--keep-kerning', action='store_true', help='leave kerning pairs with digits in place')
    ap.add_argument('--colon', action='store_true',
                    help="give the colon the digits' side bearings, so 18:41 is spaced evenly around the colon")
    ap.add_argument('--tnum-shapes', action='store_true',
                    help="map 0-9 to the font's tabular-figure glyphs (its tnum feature) before equalizing")
    a = ap.parse_args(argv)

    font = TTFont(a.input, recalcTimestamp=False)
    src_family = font['name'].getBestFamilyName()
    src_desc = f'{src_family} {font["name"].getDebugName(5) or ""}'.strip()

    if 'fvar' in font:
        axes = parse_axes(a.axes)
        tags = [ax.axisTag for ax in font['fvar'].axes]
        unknown = set(axes) - set(tags)
        if unknown:
            sys.exit(f'error: {a.input} has no axis {sorted(unknown)} (axes: {tags})')
        for ax in font['fvar'].axes:
            if ax.axisTag not in axes:
                print(f'  note: {ax.axisTag} not given, pinned at its default {ax.defaultValue:g}', file=sys.stderr)
                axes[ax.axisTag] = ax.defaultValue
        from fontTools.varLib import instancer
        font = instancer.instantiateVariableFont(font, axes, static=True)
        src_desc += ' at ' + ', '.join(f'{k}={v:g}' for k, v in axes.items())
    elif a.axes:
        sys.exit(f'error: --axes given but {a.input} is not a variable font')
    if 'glyf' not in font:
        sys.exit('error: only TrueType (glyf) fonts are supported')

    # License check: the name table first, then the upstream OFL.txt if given.
    texts = [font['name'].getDebugName(0), font['name'].getDebugName(13)]
    if a.license:
        texts.append(open(a.license, encoding='utf-8').read())
    rfns = declared_rfns(texts)
    family = a.family or f'{src_family} TinyBar'
    ps_family = re.sub(r'[^A-Za-z0-9]', '', family)
    clashes = [r for r in rfns if norm(r) in norm(family) or norm(r) in norm(ps_family)]
    if clashes:
        sys.exit(f'error: the license reserves the font name(s) {clashes}; a modified font may not use them. '
                 f'Choose another --family.')

    cmap = font.getBestCmap()
    missing = [d for d in DIGITS if ord(d) not in cmap]
    if missing:
        sys.exit(f'error: no glyph for {missing}')
    remapped = []
    if a.tnum_shapes:
        remapped = use_tnum_shapes(font)
        cmap = font.getBestCmap()
    digit_glyphs = list(dict.fromkeys(cmap[ord(d)] for d in DIGITS))

    target, rows = equalize(font, digit_glyphs, a.grid)
    colon = space_colon(font, target, digit_glyphs, a.grid) if a.colon else None
    kern_removed = 0 if a.keep_kerning else strip_digit_kerning(font, set(digit_glyphs))
    dropped = drop_numeric_alternates(font, digit_glyphs)
    for t in ('hdmx', 'LTSH', 'VDMX'):                    # per-size device metrics would now be stale
        if t in font:
            del font[t]
    if 'OS/2' in font:
        font['OS/2'].recalcAvgCharWidth(font)

    note = (f'Modified by the TinyBar project from {src_desc}: digits 0-9 share one advance width '
            f'({target} units) with centered outlines'
            + (', using the tabular-figure shapes' if remapped else '')
            + (', the colon has the digits\' side bearings' if colon else '')
            + ('' if a.keep_kerning else ', and kerning pairs with digits are removed')
            + '. Licensed under the SIL Open Font License 1.1.')
    ps = rename(font, family, note)

    if os.environ.get('SOURCE_DATE_EPOCH'):
        from fontTools.misc.timeTools import epoch_diff
        font['head'].modified = int(os.environ['SOURCE_DATE_EPOCH']) - epoch_diff
    font.save(a.output)

    print(f'{a.input} -> {a.output}')
    print(f'  family "{family}" ({ps}); reserved font names declared: {", ".join(rfns) if rfns else "none"}'
          + ('' if a.license else ' (name table only; pass --license to check OFL.txt)'))
    if remapped:
        print(f'  cmap now uses the tnum shapes: {", ".join(remapped)}')
    print(f'  common digit advance: {target} units ({target / font["head"].unitsPerEm:.3f} em)')
    print('  glyph        old adv  ink x0..x1    shift  new lsb  new rsb')
    for gname, adv, x0, x1, dx, lsb, rsb in rows + ([colon] if colon else []):
        flag = '' if abs(lsb - rsb) <= a.grid else '  (off center: grid)'
        print(f'  {gname:12s} {adv:7d}  {x0:5d}..{x1:<5d}  {dx:+6d}  {lsb:7d}  {rsb:7d}{flag}')
    print(f'  kerning pairs with a digit removed: {kern_removed}')
    if dropped:
        print(f'  numeric alternates dropped: {", ".join(dropped)}')


if __name__ == '__main__':
    main()
