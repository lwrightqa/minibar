# Testing the mock-up

`docs/mockup.html` is written for the claude.ai Artifact viewer, which wraps it in a document skeleton and adds `[hidden]{display:none!important}`. To test it locally in headless Chromium, wrap it the same way.

## Setup in the cloud container

- Playwright is installed globally; run scripts with `NODE_PATH=$(npm root -g) node script.js`.
- Chromium is preinstalled. Don't run `playwright install`.
- Google Fonts and most CDNs are blocked from the container. Inline the QR library yourself (download it once with `curl -sS -o qrcode.min.js https://cdnjs.cloudflare.com/ajax/libs/qrcode-generator/1.4.4/qrcode.min.js`, which is allowed) and expect fallback fonts.

## Minimal harness

```js
const { chromium } = require('playwright');
const fs = require('fs');
(async () => {
  let src = fs.readFileSync('/home/user/tinybar/docs/mockup.html', 'utf8');
  src = src.replace(
    '<script src="https://cdnjs.cloudflare.com/ajax/libs/qrcode-generator/1.4.4/qrcode.min.js"></script>',
    '<script>' + fs.readFileSync('qrcode.min.js', 'utf8') + '</script>');
  const html = '<!doctype html><html><head><meta charset="utf-8">'
    + '<meta name="viewport" content="width=device-width,initial-scale=1">'
    + '<style>[hidden]{display:none!important}</style></head><body>' + src + '</body></html>';
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 1100, height: 900 } });
  const errors = [];
  p.on('pageerror', e => errors.push(e.message));
  await p.setContent(html, { waitUntil: 'domcontentloaded' });
  await p.waitForTimeout(800);            // ripening frames build asynchronously
  // ... drive the page ...
  console.log('errors:', errors);
  await b.close();
})();
```

## Tips

- The action log under the device (`#log`) describes what the last control did, which makes assertions easy.
- `#hint` shows the controls for the current state.
- Pointer gestures on `#screen`: `mouse.move` to a point inside it, `mouse.down`, wait (about 700 ms for a hold), `mouse.up`. Call `locator('#screen').scrollIntoViewIfNeeded()` first; clicking buttons lower on the page scrolls the screen out of view.
- The PWR side button (`#btnPwr`) uses pointer down/up: hold 3 s to power off.
- The demo speed (`#speedSeg button[data-v="60"]`) runs timers at 60×, so a 25-minute focus session ends in about 25 s.
- Write scratch scripts and screenshots in your scratchpad directory, not in the repo.
