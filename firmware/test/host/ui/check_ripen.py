#!/usr/bin/env python3
"""
check_ripen.py: the tomato images gen_tomatoes.py builds must match the mock-up's pixel for pixel.

fixtures/ripen/ holds the 24 ripening frames and the pale tomato as docs/mockup.html builds them in Chromium
(buildRipenFrames(), read from the page with Playwright on 2026-10-04). This runs the generator's PNG mode and compares
every pixel that is visible in either image. Run by test_ui (test_assets.c), or by hand:

    python3 firmware/test/host/ui/check_ripen.py
Exit status 0 when every image matches.
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
sys.dont_write_bytecode = True     # no __pycache__ next to the generator
sys.path.insert(0, os.path.join(FW, 'components', 'ui', 'tools'))
import gen_tomatoes  # noqa: E402


def main():
    fixtures = os.path.join(HERE, 'fixtures', 'ripen')
    assets = os.path.join(FW, '..', 'assets')
    with tempfile.TemporaryDirectory() as out:
        subprocess.run([sys.executable, os.path.join(FW, 'components', 'ui', 'tools', 'gen_tomatoes.py'), '--png',
                        os.path.join(assets, 'tomato_ripe.png'), os.path.join(assets, 'tomato_unripe.png'), out],
                       check=True)
        names = [f'ripen_{i:02d}.png' for i in range(24)] + ['pale.png']
        bad = 0
        for n in names:
            w, h, want = gen_tomatoes.read_png_rgba(os.path.join(fixtures, n))
            w2, h2, got = gen_tomatoes.read_png_rgba(os.path.join(out, n))
            assert (w, h) == (w2, h2), n
            diff = 0
            for i in range(0, len(want), 4):
                if (want[i + 3] or got[i + 3]) and want[i:i + 4] != got[i:i + 4]:
                    diff += 1
            if diff:
                bad += 1
                print(f'{n}: {diff} pixels differ from the mock-up')
        print(f'check_ripen: {len(names) - bad} of {len(names)} images match the mock-up')
        return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
