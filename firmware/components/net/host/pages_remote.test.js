/*
 * pages_remote.test.js: the Remote page against the fake bar (the real router and core), in headless Chromium. Owner: net builder.
 *   NODE_PATH=$(npm root -g) node components/net/host/pages_remote.test.js
 */
const { chromium } = require('playwright');
const { spawn } = require('child_process');
const path = require('path'), os = require('os');
// The fake bar (host/fakebar.c), built with the host tests: cmake --build build-host-<name> --target tb_fakebar
const FB = process.env.FAKEBAR || path.join(__dirname, '../../../build-host-net/net/tb_fakebar');
const OUT = process.env.OUT_DIR || os.tmpdir();
const sleep = ms => new Promise(r => setTimeout(r, ms));
let fails = 0;
const check = (c, what) => { if (!c) { fails++; console.log('FAIL', what); } else console.log('ok  ', what); };

(async () => {
  const port = 8091, base = `http://127.0.0.1:${port}`;
  const fb = spawn(FB, ['--port', String(port)], { stdio: ['ignore', 'inherit', 'inherit'] });
  await sleep(500);
  const sim = async (path, body) => (await fetch(base + path, { method: body ? 'POST' : 'GET', body })).text();
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 390, height: 844 } });
  const errors = [];
  p.on('pageerror', e => errors.push(e.message));
  p.on('console', m => { if (m.type() === 'error' && !m.text().startsWith('Failed to load resource')) errors.push(m.text()); });
  await p.goto(base + '/');
  await sleep(800);
  check(await p.isVisible('#pairCard'), 'pairing card shows on 401');
  check(!(await p.isVisible('#remoteMain')), 'main hidden before pairing');
  check((await p.textContent('#phoneAddr')).includes('TinyBar 2A1C'), 'header shows the bar name');
  await p.fill('#pairName', 'iPhone');
  await p.click('#pairStart');
  await sleep(300);
  check(await p.isVisible('#pairCodeBox'), 'code box shows');
  let s = JSON.parse(await sim('/_sim/state'));
  check(s.pairing && s.code.length === 6, 'bar shows a 6-digit code');
  await p.fill('#pairCode', '000000' === s.code ? '111111' : '000000');
  await sleep(1100);
  await p.click('#pairGo');
  await sleep(300);
  check((await p.textContent('#pairMsg')).includes("doesn't match. 2 tries left"), 'wrong code message: ' + await p.textContent('#pairMsg'));
  await sleep(1100);
  await p.fill('#pairCode', s.code.slice(0, 3) + ' ' + s.code.slice(3));
  await p.click('#pairGo');
  await sleep(800);
  check(await p.isVisible('#remoteMain'), 'main shows after pairing');
  s = JSON.parse(await sim('/_sim/state'));
  check(s.toast.includes('Paired · iPhone'), 'bar toasts Paired · iPhone: ' + s.toast);
  await sleep(500);
  check((await p.textContent('#devList')).includes('iPhone (this phone)'), 'paired devices list shows this phone');

  // Status buttons
  await p.click('.sbtn[data-id="busy"]');
  await sleep(300);
  check(await p.getAttribute('.sbtn[data-id="busy"]', 'aria-pressed') === 'true', 'Busy pressed');
  s = JSON.parse(await sim('/_sim/state'));
  check(s.idx === 1, 'bar shows Busy');
  // Pomodoro
  await p.click('#pStart');
  await sleep(300);
  check((await p.textContent('#pRead')) === 'Focus 1 of 4 · running', 'pRead running: ' + await p.textContent('#pRead'));
  check((await p.textContent('#pStart')) === 'Pause', 'Start became Pause');
  const t1 = await p.textContent('#pTime');
  await sleep(2200);
  const t2 = await p.textContent('#pTime');
  check(t1 !== t2 && t2 < t1, `countdown moves (${t1} → ${t2})`);
  check(await p.$$eval('#pTomatoes .tomato', e => e.length) === 4, 'four tomatoes');
  await p.click('#pStart');
  await sleep(300);
  check((await p.textContent('#pRead')) === 'Focus 1 of 4 · paused', 'paused');
  check((await p.textContent('#pStart')) === 'Resume', 'Resume');
  // Settings segments and checks
  await p.click('.seg[data-k="focus_min"] button[data-v="50"]');
  await sleep(400);
  check(await p.getAttribute('.seg[data-k="focus_min"] button[data-v="50"]', 'aria-pressed') === 'true', 'Focus 50 pressed');
  check((await p.textContent('#pTime')) === '50:00', 'focus restarted at 50:00: ' + await p.textContent('#pTime'));
  check(await p.isDisabled('#tickSeg button[data-v="medium"]'), 'tick volume disabled while ticking is off');
  await p.check('#pTick');
  await sleep(400);
  check(!(await p.isDisabled('#tickSeg button[data-v="medium"]')), 'tick volume enabled');
  await p.click('#tickSeg button[data-v="medium"]');
  await sleep(400);
  check(await p.getAttribute('#tickSeg button[data-v="medium"]', 'aria-pressed') === 'true', 'Medium pressed');
  s = JSON.parse(await sim('/_sim/state'));
  check(s.toast.includes('Ticking on'), 'bar toasts ticking: ' + s.toast);
  await p.click('#pStop');
  await sleep(300);
  check((await p.textContent('#pRead')) === 'Focus 1 of 4 · ready', 'stopped: ready');
  // Message
  await p.fill('#msgInput', 'Pizza 🍕 time');
  await p.click('#msgForm button');
  await sleep(300);
  check((await p.textContent('#msgErr')).includes("can't show this character: 🍕"), 'unsupported char message: ' + await p.textContent('#msgErr'));
  await p.fill('#msgInput', 'Don’t interrupt – deadline');
  await p.click('#msgForm button');
  await sleep(300);
  check(await p.getAttribute('.sbtn[data-id="message"]', 'aria-pressed') === 'true', 'Message pressed');
  check(!(await p.isVisible('#msgErr')), 'no error');
  // A Mac on USB
  let line = await sim('/_sim/usb', '@tb {"cmd":"hello","id":1,"client":"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60","api":"1.0"}');
  check(line.startsWith('@tb {"id":1,"ok":true'), 'USB hello');
  line = await sim('/_sim/usb', '@tb {"cmd":"call","id":2,"client":"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60","session":"s","seq":1,"active":true,"app":"Slack","call_id":1,"elapsed_s":0}');
  check(line.includes('"showing":"call"'), 'USB call shows');
  await sleep(2300);
  check(await p.isVisible('#autoBanner'), 'banner shows');
  check((await p.textContent('#abTitle')) === 'On a call · Slack · 1 min', 'banner title: ' + await p.textContent('#abTitle'));
  check((await p.textContent('#abSub')) === 'From your Mac. Showing on the bar until the call ends.', 'banner sub');
  check((await p.textContent('#autoNow')) === 'Showing On a call · from your Mac', 'autoNow');
  check((await p.textContent('#macLine')) === 'Connected over USB · on a call (Slack) · heard from it just now', 'macLine: ' + await p.textContent('#macLine'));
  await p.click('#abBtn');
  await sleep(300);
  check((await p.textContent('#abTitle')) === 'On a call · set aside on the bar', 'set aside title');
  check(await p.isVisible('#autoShowAgain'), 'Show again button');
  check((await p.textContent('#autoNow')) === 'Showing your status · you set the call aside', 'autoNow aside');
  await p.click('#autoShowAgain');
  await sleep(300);
  check((await p.textContent('#abBtn')) === 'Set aside', 'shown again');
  await p.uncheck('#macOn');
  await sleep(500);
  check((await p.textContent('#macLine')).startsWith('Off. The bar ignores calls from your Mac.'), 'mac off line');
  check(!(await p.isVisible('#autoBanner')), 'no banner with Calls from your Mac off');
  await p.check('#macOn');
  await sleep(500);
  await sim('/_sim/usb', '@tb {"cmd":"call","id":3,"client":"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60","session":"s","seq":2,"active":false}');
  // Calendar
  check((await p.textContent('#calBadge')) === 'Not set up', 'calendar not set up');
  check(await p.isDisabled('#calOn'), 'calendar switch disabled');
  await p.fill('#calInput', 'http://calendar.google.com/x/basic.ics');
  await p.click('#calSave');
  check((await p.textContent('#calMsg')).includes("starts with http://"), 'http refused on the phone');
  await p.fill('#calInput', 'https://calendar.google.com/calendar/ical/you%40example.com/private-8c1d5e2a9b7f40c3a6e1d2b3c4f53f2a/basic.ics');
  await p.click('#calSave');
  await sleep(200);
  check((await p.textContent('#calMsg')).includes('Checking the address'), 'checking message');
  await sleep(2600);
  check((await p.textContent('#calMsg')).startsWith('Saved. 3 meetings left today.'), 'saved: ' + await p.textContent('#calMsg'));
  await sleep(2200);
  check((await p.textContent('#calBadge')) === 'Connected', 'badge Connected');
  check((await p.textContent('#calMask')) === 'calendar.google.com/…/basic.ics · ending 3f2a', 'mask: ' + await p.textContent('#calMask'));
  check((await p.textContent('#calLine')).startsWith('Synced just now · 3 meetings left today'), 'cal line: ' + await p.textContent('#calLine'));
  check(await p.isChecked('#calOn'), 'Calendar meetings on after a first address');
  await p.check('#calTitles');
  await sleep(400);
  s = JSON.parse(await sim('/_sim/state'));
  check(s.toast.includes('Meeting titles on'), 'titles toast: ' + s.toast);
  await p.click('#calSync');
  await sleep(2500);
  check((await p.textContent('#calMsg')).startsWith('Calendar synced.'), 'synced: ' + await p.textContent('#calMsg'));
  // a meeting now
  await sim('/_sim/meetings', '-5 60');
  await sleep(2300);
  check((await p.textContent('#abTitle')).startsWith('In a meeting until'), 'meeting banner: ' + await p.textContent('#abTitle'));
  check((await p.textContent('#abSub')) === 'Design review · from your calendar', 'meeting sub with title: ' + await p.textContent('#abSub'));
  await p.click('#calRemove');
  check(await p.isVisible('#calConfirm'), 'remove asks first');
  await p.click('#calRemoveYes');
  await sleep(2300);
  check((await p.textContent('#calMsg')) === 'Calendar removed.', 'removed');
  check((await p.textContent('#calBadge')) === 'Not set up', 'not set up again');
  check(!(await p.isVisible('#autoBanner')), 'meeting gone');
  // layout
  const sw = await p.evaluate(() => document.scrollingElement.scrollWidth);
  check(sw <= 390, 'no horizontal scroll at 390 px (' + sw + ')');
  await p.screenshot({ path: path.join(OUT, 'remote_390.png'), fullPage: true });
  await p.emulateMedia({ colorScheme: 'dark' });
  await p.screenshot({ path: path.join(OUT, 'remote_390_dark.png'), fullPage: true });
  // forget this phone
  await p.click('#devList button');
  await sleep(400);
  check(await p.isVisible('#pairCard'), 'forget this phone shows pairing again');
  check(errors.length === 0, 'no page errors: ' + errors.join(' | '));
  await b.close();
  fb.kill();
  console.log(fails ? `${fails} FAILED` : 'ALL OK');
  process.exit(fails ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
