/*
 * pages_setup.test.js: the setup page against the fake bar (the real router and core), in headless Chromium. Owner: net builder.
 *   NODE_PATH=$(npm root -g) node components/net/host/pages_setup.test.js
 */
const { chromium } = require('playwright');
const { spawn } = require('child_process');
const path = require('path'), os = require('os');
// The fake bar (host/fakebar.c), built with the host tests: cmake --build build-host-<name> --target tb_fakebar
const FB = process.env.FAKEBAR || path.join(__dirname, '../../../build-host-net/net/tb_fakebar');
const OUT = process.env.OUT_DIR || os.tmpdir();
const sleep = ms => new Promise(r => setTimeout(r, ms));
// Polls for a condition rather than sleeping a fixed time: the fake bar's join takes 2.5 s and the page polls every
// 2 s, so a fixed wait reads the previous message on a loaded machine.
const waitFor = async (fn, ms, step = 100) => { const t0 = Date.now(); let v; while (!(v = await fn()) && Date.now() - t0 < ms) await sleep(step); return v; };
let fails = 0;
const check = (c, what) => { if (!c) { fails++; console.log('FAIL', what); } else console.log('ok  ', what); };
(async () => {
  const port = 8092, base = `http://127.0.0.1:${port}`;
  const fb = spawn(FB, ['--port', String(port), '--setup'], { stdio: ['ignore', 'inherit', 'inherit'] });
  await sleep(500);
  const sim = async path => JSON.parse(await (await fetch(base + path)).text());
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 390, height: 844 } });
  const errors = [];
  p.on('pageerror', e => errors.push(e.message));
  await p.goto(base + '/');
  await sleep(2000);   // past the splash, so the bar takes the join
  check(await p.$$eval('label.net', e => e.length) === 4, 'four networks');
  check((await p.textContent('label.net:nth-of-type(1)')).includes('Office-WiFi'), 'strongest first');
  check((await p.textContent('label.net:last-of-type small')) === 'weak signal', 'weak signal label');
  check(await p.isChecked('input[value="Office-WiFi"]'), 'first network picked');
  await p.check('input[value="Office-Corp"]');
  check(await p.isVisible('#userField'), 'work login asks for a username');
  await p.check('input[value="Office-Guest"]');
  check(await p.isVisible('#guestWarn') && !(await p.isVisible('#passField')), 'open network: warning, no password');
  await p.check('input[value="Office-WiFi"]');
  await p.click('#wConnect');
  check((await p.textContent('#setupMsg')) === 'Enter the Wi-Fi password.', 'password needed');
  await p.fill('#wPass', 'short');
  await p.click('#wConnect');
  await sleep(300);
  check((await p.textContent('#setupMsg')) === 'Wi-Fi passwords are 8 to 63 characters, or 64 hex digits.', 'short password refused: ' + await p.textContent('#setupMsg'));
  await p.fill('#wCal', 'https://calendar.google.com/calendar/ical/x/public/basic.ics');
  await p.click('#wConnect');
  check((await p.textContent('#wCalErr')).includes("public address"), 'public calendar address flagged at the field');
  await p.fill('#wCal', '');
  await p.fill('#wPass', 'wrong-password');
  await p.click('#wConnect');
  await sleep(300);
  check((await p.textContent('#setupMsg')).startsWith('MiniBar is connecting to Office-WiFi.'), 'connecting message');
  check(await p.isDisabled('#wConnect'), 'Connect disabled while connecting');
  let s = await sim('/_sim/state');
  check(s.wifi_mode === 3, 'bar shows Connecting');
  const failedMsg = "Couldn't connect to Office-WiFi: the password was wrong. Check it and try again.";
  check(await waitFor(async () => (await p.textContent('#setupMsg')) === failedMsg, 10000), 'failed: ' + await p.textContent('#setupMsg'));
  s = await sim('/_sim/state');
  check(s.wifi_mode === 5, 'bar shows Couldn\'t connect');
  await p.fill('#wPass', 'correct horse battery staple');
  await p.fill('#wCal', 'webcal://calendar.google.com/calendar/ical/me/private-abcd1234ef/basic.ics');
  await p.click('#wConnect');
  check(await waitFor(async () => (await p.textContent('#setupMsg')).startsWith('Connected to Office-WiFi.'), 10000), 'connected: ' + await p.textContent('#setupMsg'));
  check((await p.inputValue('#wPass')) === '', 'password cleared');
  s = await sim('/_sim/state');
  check(s.wifi_mode === 4, 'bar shows Connected');
  const sw = await p.evaluate(() => document.scrollingElement.scrollWidth);
  check(sw <= 390, 'no horizontal scroll (' + sw + ')');
  await p.screenshot({ path: path.join(OUT, 'setup_390.png'), fullPage: true });
  s = await waitFor(async () => { const x = await sim('/_sim/state'); return x.toast.startsWith('Calendar synced') ? x : null; }, 6000) || await sim('/_sim/state');
  check(s.toast.startsWith('Calendar synced'), 'calendar from setup saved once online: ' + s.toast + ' / ' + s.pending);
  check(errors.length === 0, 'no page errors: ' + errors.join(' | '));
  await b.close();
  fb.kill();
  console.log(fails ? `${fails} FAILED` : 'ALL OK');
  process.exit(fails ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
