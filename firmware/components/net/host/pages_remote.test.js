/*
 * pages_remote.test.js: the Remote page against the fake bar (the real router and core), in headless Chromium. Owner: net builder
 * (the pairing parts: lead developer, firmware alignment with the mock-up's pairing round).
 *   NODE_PATH=$(npm root -g) node components/net/host/pages_remote.test.js
 *
 * The pairing prompt as the mock-up draws it: every refusal in the mock-up's words, each clearing once its cause is
 * over (a code gone, the back-off over, setup finished, a place free); Pair this device dimmed with aria-disabled while
 * busy or waiting, keeping focus; Cancel taking the code off the bar (pair/cancel); a code that goes from the bar
 * noticed within about 2 s; the error-colored focus ring. Then the Remote itself, the Paired devices list and its
 * foot, the Connect your Mac line, the hidden-character wording, and the ways back to the prompt (Forget all on the
 * bar, Remove this device, removed on another phone).
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
const MAC = '6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60';
const ch = n => String.fromCharCode(n);
const ERR_RING = 'rgb(208, 27, 58)';       // --s-busy, #D01B3A

(async () => {
  const port = 8091, base = `http://127.0.0.1:${port}`;
  const fb = spawn(FB, ['--port', String(port)], { stdio: ['ignore', 'inherit', 'inherit'] });
  await sleep(500);
  const sim = async (p, body) => (await fetch(base + p, { method: body !== undefined ? 'POST' : 'GET', body })).text();
  const state = async () => JSON.parse(await sim('/_sim/state'));
  let usbId = 100;
  // A request over USB (the Mac app, or anything on the cable): the reply's JSON
  const usb = async (method, p, body) => {
    const line = await sim('/_sim/usb', '@tb ' + JSON.stringify({ cmd: 'request', id: ++usbId, method, path: p, body: body || null }));
    return JSON.parse(line.slice(4));
  };
  const b = await chromium.launch();
  const ctx = await b.newContext({ viewport: { width: 390, height: 844 } });
  const p = await ctx.newPage();
  const errors = [];
  const watch = pg => {
    pg.on('pageerror', e => errors.push(e.message));
    pg.on('console', m => { if (m.type() === 'error' && !m.text().startsWith('Failed to load resource')) errors.push(m.text()); });
  };
  watch(p);
  const text = async sel => (await p.textContent(sel)).trim();
  const vis = sel => p.isVisible(sel);
  const active = () => p.evaluate(() => document.activeElement && document.activeElement.id);
  const noScroll = async what => {
    const sw = await p.evaluate(() => document.scrollingElement.scrollWidth);
    check(sw <= 390, `no horizontal scroll at 390 px, ${what} (${sw})`);
  };

  // ---------- the prompt before pairing ----------
  await p.goto(base + '/');
  await sleep(800);
  check(await vis('#pairView'), 'the pairing prompt shows on 401');
  check(!(await vis('#remoteMain')), 'the controls are hidden until this device is paired');
  check((await text('#phoneAddr')).startsWith('MiniBar 2A1C'), 'the header names the bar');
  check(await text('#pairIntro') === 'MiniBar 2A1C shows a 6-digit code on its screen. Type it here, and this device becomes its Remote.', 'intro names the bar');
  check(!(await vis('#pairNote')), 'no note on a first visit');
  check(!(await vis('#pairName')), 'no name field: the Remote names itself after the kind of phone');
  await noScroll('the prompt');

  // in_setup: the bar is on Wi-Fi setup's Connected screen (back on the office Wi-Fi, so it answers)
  await sim('/_sim/connected', '');
  await p.click('#pairAsk');
  await sleep(300);
  check(await text('#pairAskErr') === 'MiniBar is setting up Wi-Fi. Finish setup on the bar, then try again.', 'in_setup: ' + await text('#pairAskErr'));
  check(await p.getAttribute('#pairAsk', 'aria-disabled') === 'false', 'in_setup leaves the button as it is');
  check(!(await state()).pairing, 'no code during setup');
  await sim('/_sim/tap', '');               // the tap ends the Connected screen: setup is over
  await sleep(2500);
  check(!(await vis('#pairAskErr')), 'the setting-up message goes once setup is over');

  // busy: another device's code is on the bar (the Mac, over the cable)
  let mac = await usb('POST', '/api/v1/pair/start', { kind: 'mac', scope: 'call', client: MAC });
  check(mac.http_status === 202, 'the Mac asks for a code');
  await p.click('#pairAsk');
  await sleep(300);
  check(await text('#pairAskErr') === 'Another device is pairing with this MiniBar. Try again in 2 minutes.', 'busy: ' + await text('#pairAskErr'));
  check(await p.getAttribute('#pairAsk', 'aria-disabled') === 'true', 'Pair this device is dimmed (aria-disabled) while busy');
  check(await active() === 'pairAsk', 'and keeps focus');
  // (Playwright's isDisabled() counts aria-disabled, so ask the element itself)
  check(await p.evaluate(() => !document.getElementById('pairAsk').disabled && document.getElementById('pairAsk').tabIndex === 0), 'it stays focusable (not disabled)');
  await p.focus('#pairAsk');                // pressing again (Enter: Playwright won't click an aria-disabled button)
  await p.keyboard.press('Enter');          // only repeats the wait
  await sleep(300);
  check((await state()).who === 'Mac', 'pressing it again asks nothing of the bar');
  check((await p.textContent('#phoneSay')).startsWith('Another device is pairing'), 'it says the wait again');
  await sleep(1000);
  mac = await usb('POST', '/api/v1/pair/cancel', { pairing_id: mac.pairing_id });
  check(mac.http_status === 200 && mac.ok === true, 'the Mac cancels its code (pair/cancel): ' + JSON.stringify(mac));
  check((await state()).toast === 'Pairing canceled', 'the bar says Pairing canceled');
  await sleep(2500);
  check(!(await vis('#pairAskErr')), 'the busy message goes once the code is gone');
  check(await p.getAttribute('#pairAsk', 'aria-disabled') === 'false', 'Pair this device is back');

  // token_limit: 10 devices paired (over USB, as the Mac app does)
  for (let i = 0; i < 10; i++) await sim('/_sim/usb', `@tb {"cmd":"pair","id":${i + 1},"client":"client-${String(i).padStart(4, '0')}"}`);
  check((await state()).paired === 10, '10 devices paired');
  await p.click('#pairAsk');
  await sleep(300);
  check(await text('#pairAskErr') === 'MiniBar 2A1C already has 10 paired devices. Remove one on a paired device, or forget them all on MiniBar: hold its screen, then Wi-Fi, then Devices.', 'token_limit: ' + await text('#pairAskErr'));
  const list = await usb('GET', '/api/v1/clients');
  await usb('DELETE', `/api/v1/clients/${list.clients[0].token_id}`);
  await sleep(2500);
  check(!(await vis('#pairAskErr')), 'the 10-device message goes once a place is free');
  // Forget all on the bar; the Mac pairs over USB again (it has no code screen)
  await sim('/_sim/forget', '');
  await sim('/_sim/usb', `@tb {"cmd":"pair","id":50,"client":"${MAC}"}`);

  // a code for this device; Cancel takes it off the bar
  await p.click('#pairAsk');
  await sleep(300);
  let s = await state();
  check(s.pairing && s.code.length === 6, 'the bar shows a 6-digit code');
  check(s.who === 'Phone', 'no kind of phone in the browser: the bar says Phone: ' + s.who);
  check(await vis('#pairCode') && !(await vis('#pairStart')), 'the code step shows');
  check(/^The code works for 2 minutes · (2:00|1:59) left\.$/.test(await text('#pairLine')), 'the line counts the code down: ' + await text('#pairLine'));
  check(await active() === 'pairInput', 'the field has focus');
  check(await p.isDisabled('#pairGo'), 'Pair waits for six digits');
  await sleep(1100);
  await p.click('#pairCancel');
  await sleep(400);
  s = await state();
  check(!s.pairing && s.toast === 'Pairing canceled', 'Cancel takes the code off the bar: ' + s.toast);
  check(await vis('#pairStart') && await active() === 'pairAsk', 'back to the start, focus on Pair this device');
  check(await p.textContent('#phoneSay') === 'Pairing canceled. The code is gone from MiniBar.', 'and says so');

  // two failed pairings in a row (the Mac's cancel and this one): the back-off
  await p.click('#pairAsk');
  await sleep(300);
  check(await text('#pairAskErr') === 'Too many tries. You can try again in 30 seconds.', 'back-off: ' + await text('#pairAskErr'));
  check(await p.getAttribute('#pairAsk', 'aria-disabled') === 'true' && await active() === 'pairAsk', 'dimmed while waiting, focus kept');
  await sleep(2000);
  check(/^Too many tries\. You can try again in 2[78] seconds\.$/.test(await text('#pairAskErr')), 'the wait counts down: ' + await text('#pairAskErr'));
  await sim('/_sim/restart', '');           // a restart clears the back-off
  await sleep(2500);
  check(!(await vis('#pairAskErr')) && await p.getAttribute('#pairAsk', 'aria-disabled') === 'false', 'the wait message goes once the back-off is over');

  // a wrong code, then the code canceled on the bar while the phone waits
  await p.click('#pairAsk');
  await sleep(300);
  s = await state();
  await sleep(1000);
  await p.fill('#pairInput', s.code === '000000' ? '111111' : '000000');     // pairs at the sixth digit
  await sleep(500);
  check(await text('#pairErr') === "That code didn't match. 2 tries left.", 'wrong code: ' + await text('#pairErr'));
  check(await p.getAttribute('#pairInput', 'aria-invalid') === 'true', 'the field is marked invalid');
  check(await active() === 'pairInput', 'and keeps focus');
  check(await p.evaluate(() => getComputedStyle(document.getElementById('pairInput')).outlineColor) === ERR_RING, 'the focus ring takes the error color');
  check(await p.evaluate(() => getComputedStyle(document.getElementById('pairInput')).borderTopColor) === ERR_RING, 'and so does the border');
  await sim('/_sim/tap', '');               // someone taps the bar: canceled there
  await sleep(2500);
  check(await text('#pairErr') === 'That code has expired or was canceled on MiniBar. Show a new code to try again.', 'gone: ' + await text('#pairErr'));
  check(await p.isDisabled('#pairInput') && await vis('#pairNew') && !(await vis('#pairGo')), 'the field is disabled and Show a new code offered');
  check(await active() === 'pairNew', 'focus moves to Show a new code');

  // the next device's code within the poll: this device's code is canceled on the bar and the Mac asks right away, so
  // info still says "showing"; pairing_seq tells the prompt the code isn't its own (the mock-up's pair.id === phone.pid)
  await sim('/_sim/restart', '');           // the tap above was one failed pairing; a second in a row would lock pair/start
  await p.click('#pairNew');
  await sleep(300);
  s = await state();
  check(s.pairing && s.who === 'Phone', 'a new code for this device');
  await sleep(1100);
  await sim('/_sim/tap', '');               // canceled on the bar...
  mac = await usb('POST', '/api/v1/pair/start', { kind: 'mac', scope: 'call', client: MAC });
  check(mac.http_status === 202 && (await state()).who === 'Mac', '...and the Mac asks within the poll: the bar shows the Mac\'s code');
  await sleep(2500);
  check(await text('#pairErr') === 'That code has expired or was canceled on MiniBar. Show a new code to try again.', 'the prompt still notices within about 2 s: ' + await text('#pairErr'));
  check(await p.isDisabled('#pairInput') && await vis('#pairNew'), 'the field is disabled and Show a new code offered');
  await usb('POST', '/api/v1/pair/cancel', { pairing_id: mac.pairing_id });
  await sim('/_sim/restart', '');           // the two cancels would lock pair/start: a restart clears that
  await sleep(2500);

  // a reload while this device's own code is on the bar: the prompt comes back to its field, with the same code and
  // countdown, and Cancel still takes the code off the bar (decisions.md, Pairing)
  await p.click('#pairNew');
  await sleep(300);
  s = await state();
  check(s.pairing && s.who === 'Phone', 'a code for this device before the reload');
  await sleep(1500);
  await p.reload();
  await sleep(900);
  check(await vis('#pairCode') && !(await vis('#pairStart')), 'after a reload the prompt is back at its field');
  check(/^The code works for 2 minutes · 1:5\d left\.$/.test(await text('#pairLine')), 'with the same countdown: ' + await text('#pairLine'));
  check((await state()).code === s.code && (await state()).who === 'Phone', 'the bar still shows this device\'s code');
  await p.click('#pairCancel');
  await sleep(400);
  s = await state();
  check(!s.pairing && s.toast === 'Pairing canceled', 'Cancel after the reload still takes the code off the bar: ' + s.toast);
  check(await p.textContent('#phoneSay') === 'Pairing canceled. The code is gone from MiniBar.', 'and says so');
  await sim('/_sim/restart', '');
  await sleep(1100);
  // a reload when the code on the bar is another device's: the prompt starts over, and Pair this device says busy
  mac = await usb('POST', '/api/v1/pair/start', { kind: 'mac', scope: 'call', client: MAC });
  await p.reload();
  await sleep(900);
  check(await vis('#pairStart') && !(await vis('#pairCode')), 'a reload under another device\'s code starts over');
  await p.click('#pairAsk');
  await sleep(300);
  check((await text('#pairAskErr')).startsWith('Another device is pairing with this MiniBar.'), 'and the Mac\'s code is reported as busy, not taken for this device\'s');
  await sleep(1000);
  await usb('POST', '/api/v1/pair/cancel', { pairing_id: mac.pairing_id });
  await sim('/_sim/restart', '');
  await sleep(2500);
  // a reload after this device's code is gone from the bar: the prompt starts over
  await p.click('#pairAsk');
  await sleep(300);
  await sleep(1100);
  await sim('/_sim/tap', '');
  await p.reload();
  await sleep(900);
  check(await vis('#pairStart') && !(await vis('#pairCode')), 'a reload after the code is gone starts over');
  check(await p.evaluate(() => { try { return sessionStorage.getItem('tb_pair'); } catch (e) { return 'x'; } }) === null, 'and the stale record is dropped');
  await sim('/_sim/restart', '');
  await sleep(1100);

  // the right code: paired
  await p.click('#pairAsk');
  await sleep(300);
  s = await state();
  await sleep(1000);
  await p.fill('#pairInput', s.code.slice(0, 3) + ' ' + s.code.slice(3));
  await sleep(900);
  check(await vis('#remoteMain') && !(await vis('#pairView')), 'the Remote shows once paired');
  check(await text('#pairedMsg') === "Paired. This device is MiniBar 2A1C's Remote now.", 'Paired line: ' + await text('#pairedMsg'));
  s = await state();
  check(s.toast === 'Paired · Phone', 'the bar toasts Paired · Phone: ' + s.toast);
  await sleep(500);

  // ---------- the Remote ----------
  // Paired devices, and the Mac line for a Mac paired over USB but not connected
  const rows = await p.$$eval('#devList .dev', els => els.map(e => e.innerText.replace(/\s+/g, ' ').trim()));
  check(rows.length === 2, 'two paired devices: ' + rows.length);
  check(/^Phone This device Remove Remote · Full control Paired today, \d+:\d\d [AP]M · used just now$/.test(rows[0]), 'this device first: ' + rows[0]);
  check(/^Mac Remove Mac app · Calls only Paired over USB today, \d+:\d\d [AP]M · used only over USB so far$/.test(rows[1]), 'the Mac: ' + rows[1]);
  check(await text('#devFoot') === "2 of 10. To forget them all at once, hold the bar's screen, then tap Wi-Fi › Devices. Over USB, your Mac needs no pairing.", 'foot: ' + await text('#devFoot'));
  check(await text('#macLine') === 'Not connected right now · paired over USB', 'Mac line before it connects: ' + await text('#macLine'));
  check((await text('#macPairNote')) === 'To pair your Mac, plug this MiniBar into it once. Or, in the MiniBar menu on your Mac, choose Connect…, then Pair Over Wi-Fi. This MiniBar shows the code.', 'Connect your Mac copy');
  await p.click('.sbtn[data-id="busy"]');
  await sleep(300);
  check(!(await vis('#pairedMsg')), 'the Paired line goes at the first action');
  check(await p.getAttribute('.sbtn[data-id="busy"]', 'aria-pressed') === 'true', 'Busy pressed');
  s = await state();
  check(s.idx === 1, 'bar shows Busy');
  // a choice made while a code covers the bar is selected at once (applied underneath)
  mac = await usb('POST', '/api/v1/pair/start', { kind: 'mac', scope: 'call', client: MAC });
  await p.click('.sbtn[data-id="away"]');
  await sleep(300);
  check(await p.getAttribute('.sbtn[data-id="away"]', 'aria-pressed') === 'true', 'Away selected at once under a code');
  check((await state()).pairing, 'the code stays up');
  await sleep(1000);
  await usb('POST', '/api/v1/pair/cancel', { pairing_id: mac.pairing_id });
  await sim('/_sim/restart', '');
  await p.click('.sbtn[data-id="busy"]');
  // Pomodoro
  await p.click('#pStart');
  await sleep(300);
  check((await text('#pRead')) === 'Focus 1 of 4 · running', 'pRead running: ' + await text('#pRead'));
  check((await text('#pStart')) === 'Pause', 'Start became Pause');
  const t1 = await text('#pTime');
  await sleep(2200);
  const t2 = await text('#pTime');
  check(t1 !== t2 && t2 < t1, `countdown moves (${t1} → ${t2})`);
  check(await p.$$eval('#pTomatoes .tomato', e => e.length) === 4, 'four tomatoes');
  await p.click('#pStart');
  await sleep(300);
  check((await text('#pRead')) === 'Focus 1 of 4 · paused', 'paused');
  check((await text('#pStart')) === 'Resume', 'Resume');
  await p.click('.seg[data-k="focus_min"] button[data-v="50"]');
  await sleep(400);
  check(await p.getAttribute('.seg[data-k="focus_min"] button[data-v="50"]', 'aria-pressed') === 'true', 'Focus 50 pressed');
  check((await text('#pTime')) === '50:00', 'focus restarted at 50:00: ' + await text('#pTime'));
  check(await p.isDisabled('#tickSeg button[data-v="medium"]'), 'tick volume disabled while ticking is off');
  await p.check('#pTick');
  await sleep(400);
  check(!(await p.isDisabled('#tickSeg button[data-v="medium"]')), 'tick volume enabled');
  await p.click('#tickSeg button[data-v="medium"]');
  await sleep(400);
  check(await p.getAttribute('#tickSeg button[data-v="medium"]', 'aria-pressed') === 'true', 'Medium pressed');
  s = await state();
  check(s.toast.includes('Ticking on'), 'bar toasts ticking: ' + s.toast);
  await p.click('#pStop');
  await sleep(300);
  check((await text('#pRead')) === 'Focus 1 of 4 · ready', 'stopped: ready');

  // Jira issue count (decisions.md "Jira issue count (2026-10-07)"): set up, test, save, states, remove; the token never comes back
  check(await vis('#jiraSec') && (await text('#jiraBadge')) === 'Not set up', 'the Jira section shows, not set up');
  check(!(await vis('#jiraRemove')), 'no Remove before it is set up');
  await noScroll('with the Jira section');
  const JTOK = 'abcDEF123_token-xyz';
  await p.click('#jiraSave');
  await sleep(300);
  check((await text('#jiraMsg')).startsWith('Use your site address, like https://yourteam.atlassian.net.'), 'an empty Save names the site: ' + await text('#jiraMsg'));
  check(await p.getAttribute('#jiraSite', 'aria-invalid') === 'true', 'and marks the field');
  await p.fill('#jiraSite', 'https://example.atlassian.net/');
  await p.fill('#jiraEmail', 'you@example.com');
  await p.fill('#jiraTok', JTOK);
  await p.fill('#jiraFilter', 'https://example.atlassian.net/issues/?filter=10042');
  await p.click('#jiraTest');
  await sleep(200);
  check((await text('#jiraMsg')) === 'Asking Jira…', 'Test says it is asking: ' + await text('#jiraMsg'));
  await sleep(1800);
  check((await text('#jiraMsg')) === 'It works. "Open bugs" has 12 issues right now.', 'Test result: ' + await text('#jiraMsg'));
  check((await text('#jiraBadge')) === 'Not set up', 'the test saved nothing');
  for (const [mode, words] of [['token', "Jira didn't accept that email and token. Check them, or create a new token."],
    ['nofilter', "Jira has no filter with that ID, or this account can't see it."],
    ['down', "Couldn't reach Jira. Check the site address, and that this MiniBar is online."]]) {
    await sim('/_sim/jira', mode);
    await p.click('#jiraTest');
    await sleep(1800);
    check((await text('#jiraMsg')) === words, `Test (${mode}): ` + await text('#jiraMsg'));
  }
  await sim('/_sim/jira', 'ok 12');
  await p.fill('#jiraLimit', '10');
  await p.click('#jiraSave');
  await sleep(300);
  check((await text('#jiraMsg')).startsWith('Saved. The Jira screen is now in the swipe order. Token saved.'), 'Save: ' + await text('#jiraMsg'));
  s = await state();
  check(s.toast === 'Jira added \u00b7 Open bugs' || s.pending === 'Jira added \u00b7 Open bugs', 'the bar toasts it: ' + s.toast);
  check((await p.inputValue('#jiraTok')) === '' && (await p.getAttribute('#jiraTok', 'placeholder')) === 'Token saved (paste to replace)', 'the token field is empty and says it is saved');
  // the Remote reads the bar again within its 2 s poll
  const badge = async want => { try { await p.waitForFunction(w => document.getElementById('jiraBadge').textContent === w, want, { timeout: 7000 }); } catch (e) { /* the check below says what it was */ } return text('#jiraBadge'); };
  check((await badge('Showing 12')) === 'Showing 12', 'the badge shows the count: ' + await text('#jiraBadge'));
  check((await text('#jiraLine')) === 'example.atlassian.net · filter 10042 (Open bugs) · y\u2022\u2022\u2022@example.com · token saved · alert above 10', 'the line: ' + await text('#jiraLine'));
  check(await vis('#jiraRemove') && (await text('#jiraSave')) === 'Save changes', 'Remove and Save changes appear');
  check(!(await p.evaluate(t => document.documentElement.outerHTML.includes(t) || document.body.innerText.includes(t) || JSON.stringify(Array.from(document.querySelectorAll('input')).map(i => i.value)).includes(t), JTOK)), 'the token is nowhere on the page');
  check(!JSON.stringify(await usb('GET', '/api/v1/jira')).includes(JTOK), 'and the API never returns it');
  // the screen is in the swipe order now: taps reach it (from Away, where a tap goes on to the next screen)
  await usb('POST', '/api/v1/status', { status: 'away' });
  let reached = false;
  for (let i = 0; i < 9 && !reached; i++) { await sim('/_sim/tap', ''); reached = (await state()).idx === 7; }
  check(reached, 'a tap on the bar reaches the Jira screen');
  await sim('/_sim/jira', 'token');
  check((await badge('Token rejected')) === 'Token rejected', 'badge when the token is rejected: ' + await text('#jiraBadge'));
  await sim('/_sim/jira', 'down');
  check((await badge("Can't reach Jira")) === "Can't reach Jira", 'badge when it cannot be reached: ' + await text('#jiraBadge'));
  await p.click('#jiraRemove');
  check((await text('#jiraRemove')) === 'Tap again to remove', 'Remove asks first');
  await p.click('#jiraRemove');
  await sleep(500);
  check((await text('#jiraBadge')) === 'Not set up' && !(await vis('#jiraRemove')), 'Remove puts it back to not set up');
  check((await text('#jiraMsg')).startsWith('Removed.'), 'Remove says so: ' + await text('#jiraMsg'));
  check((await usb('GET', '/api/v1/jira')).jira.configured === false, 'nothing is saved on the bar');
  check((await state()).idx !== 7, 'the bar left the Jira screen');
  await noScroll('after Jira');

  // Display: Time format (decisions.md "Time format (2026-10-07)"): the page's own times follow the bar's setting
  check(await vis('#timeSeg') && (await text('#lDisplay')) === 'Display', 'the Display section has the Time format choice');
  check(await p.getAttribute('#timeSeg button[data-v="12h"]', 'aria-pressed') === 'true', 'Time format starts at 12-hour');
  await noScroll('with the Display section');
  await p.click('#timeSeg button[data-v="24h"]');
  await sleep(900);
  check(await p.getAttribute('#timeSeg button[data-v="24h"]', 'aria-pressed') === 'true', '24-hour pressed');
  check(await p.getAttribute('#timeSeg button[data-v="12h"]', 'aria-pressed') === 'false', '12-hour released');
  s = await state();
  check(s.toast === 'Time format \u00b7 24-hour', 'the bar toasts the change: ' + s.toast);
  const rows24 = await p.$$eval('#devList .dev', els => els.map(e => e.innerText.replace(/\s+/g, ' ').trim()));
  check(/Paired today, \d\d:\d\d \u00b7 used just now$/.test(rows24[0]), 'the page writes its times in 24-hour too: ' + rows24[0]);
  check(((await usb('GET', '/api/v1/settings')).settings || {}).display.time_format === '24h', 'settings say 24h');
  check((await usb('GET', '/api/v1/info')).time_format === '24h', 'info says 24h');
  await p.click('#timeSeg button[data-v="12h"]');
  await sleep(900);
  check(await p.getAttribute('#timeSeg button[data-v="12h"]', 'aria-pressed') === 'true', 'back to 12-hour');
  const rows12 = await p.$$eval('#devList .dev', els => els.map(e => e.innerText.replace(/\s+/g, ' ').trim()));
  check(/Paired today, \d+:\d\d [AP]M \u00b7 used just now$/.test(rows12[0]), 'and its times are 12-hour again: ' + rows12[0]);

  // Display: Meeting chime (decisions.md "Meeting-start sound (2026-10-07)"): on by default, a toast on the bar, no sound
  check(await p.isChecked('#meetChime'), 'Meeting chime starts on');
  check((await text('#chimeLine')).startsWith('A soft chime and one flash when a calendar meeting starts.'), 'its note: ' + await text('#chimeLine'));
  await noScroll('with the Meeting chime switch');
  await p.uncheck('#meetChime');
  await sleep(700);
  check(!(await p.isChecked('#meetChime')), 'unchecked');
  s = await state();
  check(s.toast === 'Meeting chime off', 'the bar toasts it: ' + s.toast);
  check(((await usb('GET', '/api/v1/settings')).settings || {}).sound.meeting_chime === false, 'settings say false');
  await p.check('#meetChime');
  await sleep(700);
  check(await p.isChecked('#meetChime'), 'checked again');
  s = await state();
  check(s.toast === 'Meeting chime on', 'and toasts that: ' + s.toast);
  check(((await usb('GET', '/api/v1/settings')).settings || {}).sound.meeting_chime === true, 'settings say true');

  // Message: characters the bar can't draw are named as you type, and Show waits until they're gone
  s = await state();
  const idxBefore = s.idx;
  await p.fill('#msgInput', 'Pizza 🍕 time');
  await sleep(100);
  check((await text('#msgErr')) === "MiniBar can't show 🍕. Remove it to show this message.", 'unsupported char named while typing: ' + await text('#msgErr'));
  check(await p.isDisabled('#msgShow'), 'Show disabled while it has one');
  check(await p.evaluate(() => { const i = document.getElementById('msgInput'); i.focus(); return getComputedStyle(i).outlineColor; }) === ERR_RING, 'the message field\'s focus ring takes the error color');
  await p.fill('#msgInput', 'Hi 👋🏽 🇺🇸 ✓');
  await sleep(100);
  check((await text('#msgErr')) === "MiniBar can't show 👋🏽, 🇺🇸 or ✓. Remove them to show this message.", 'several named: ' + await text('#msgErr'));
  await p.press('#msgInput', 'Enter');
  await sleep(300);
  s = await state();
  check(s.idx === idxBefore, 'nothing sent while the field has them');
  // characters nobody sees (api.md 2.3): mapped, or named by where they are
  await p.fill('#msgInput', 'Busy' + ch(0xFFF9) + ' now');
  await sleep(100);
  check((await text('#msgErr')) === 'MiniBar can\'t show a hidden character after "Busy". Remove it to show this message.', 'a hidden character, named by where it is: ' + await text('#msgErr'));
  await p.fill('#msgInput', 'Busy' + ch(0xFFF9) + ' now' + ch(0xFFF9));
  await sleep(100);
  check((await text('#msgErr')) === 'MiniBar can\'t show 2 hidden characters, the first after "Busy". Remove them to show this message.', 'two hidden: ' + await text('#msgErr'));
  await p.fill('#msgInput', ch(0xFFF9) + 'Hi');
  await sleep(100);
  check((await text('#msgErr')) === "MiniBar can't show a hidden character at the start. Remove it to show this message.", 'hidden at the start: ' + await text('#msgErr'));
  for (const [t, what] of [['Back at 3:00' + ch(0x202F) + 'PM', 'narrow no-break space'], ['Busy' + ch(0x200B) + ' now', 'zero-width space'],
    [ch(0xFEFF) + 'Busy', 'byte-order mark'], ['5' + ch(0x2009) + 'min', 'thin space'], ['Done ✓' + ch(0xFE0E), 'a variation selector after a character it names']]) {
    await p.fill('#msgInput', t);
    await sleep(80);
    const err = await vis('#msgErr') ? await text('#msgErr') : '';
    // the mock-up names what you see as one character, as typed: the check mark with its selector
    check(what.startsWith('a variation') ? err === "MiniBar can't show ✓" + ch(0xFE0E) + '. Remove it to show this message.' : !err, `${what}: ${err || 'fine'}`);
  }
  await p.fill('#msgInput', 'Cafe' + ch(0x301) + ' at noon');
  await sleep(100);
  check(!(await vis('#msgErr')) && !(await p.isDisabled('#msgShow')), 'a decomposed é is fine');
  await p.click('#msgForm button');
  await sleep(400);
  const shown = await p.evaluate(async () => (await (await fetch('/api/v1/status', { cache: 'no-store' })).json()).message.text);
  check(shown === 'Caf' + ch(0xE9) + ' at noon', 'and the bar shows it composed: ' + shown);
  await p.fill('#msgInput', 'Don’t interrupt – deadline · café…');
  await sleep(100);
  check(!(await vis('#msgErr')) && !(await p.isDisabled('#msgShow')), 'curly quote, dash, Latin-1 and ellipsis are fine');
  await p.fill('#msgInput', 'Don’t interrupt – deadline');
  await p.click('#msgForm button');
  await sleep(300);
  check(await p.getAttribute('.sbtn[data-id="message"]', 'aria-pressed') === 'true', 'Message pressed');
  check(!(await vis('#msgErr')), 'no error');

  // A Mac on USB
  let line = await sim('/_sim/usb', `@tb {"cmd":"hello","id":1,"client":"${MAC}","api":"1.0"}`);
  check(line.startsWith('@tb {"id":1,"ok":true'), 'USB hello');
  check(JSON.parse(line.slice(4)).paired === 2, 'hello carries how many are paired');
  line = await sim('/_sim/usb', `@tb {"cmd":"call","id":2,"client":"${MAC}","session":"s","seq":1,"active":true,"app":"Slack","call_id":1,"elapsed_s":0}`);
  check(line.includes('"showing":"call"'), 'USB call shows');
  await sleep(2300);
  check(await vis('#autoBanner'), 'banner shows');
  check((await text('#abTitle')) === 'On a call · Slack · 1 min', 'banner title: ' + await text('#abTitle'));
  check((await text('#abSub')) === 'From your Mac. Showing on the bar until the call ends.', 'banner sub');
  check((await text('#autoNow')) === 'Showing On a call · from your Mac', 'autoNow');
  check((await text('#macLine')) === 'Connected over USB · on a call (Slack) · heard from it just now', 'macLine: ' + await text('#macLine'));
  await p.click('#abBtn');
  await sleep(300);
  check((await text('#abTitle')) === 'On a call · set aside on the bar', 'set aside title');
  check(await vis('#autoShowAgain'), 'Show again button');
  check((await text('#autoNow')) === 'Showing your status · you set the call aside', 'autoNow aside');
  await p.click('#autoShowAgain');
  await sleep(300);
  check((await text('#abBtn')) === 'Set aside', 'shown again');
  await p.uncheck('#macOn');
  await sleep(500);
  check((await text('#macLine')).startsWith('Off. The bar ignores calls from your Mac.'), 'mac off line');
  check(!(await vis('#autoBanner')), 'no banner with Calls from your Mac off');
  await p.check('#macOn');
  await sleep(500);
  await sim('/_sim/usb', `@tb {"cmd":"call","id":3,"client":"${MAC}","session":"s","seq":2,"active":false}`);
  // Calendars (decisions.md, Multiple calendars): one row per calendar, up to three
  const URL1 = 'https://calendar.google.com/calendar/ical/you%40example.com/private-8c1d5e2a9b7f40c3a6e1d2b3c4f53f2a/basic.ics';
  const URL2 = 'https://calendar.example.com/ical/home/private-0a1b2c3d4e5f60718293a4b5c6d7e8f9/basic.ics';
  const URL3 = 'https://cal.example.com/feeds/8c1d5e2a9b7f40c3a6e1d2b3c4f53f2a.ics';
  const calRows = () => p.locator('#calList .cal-row');
  const rowText = async i => ((await calRows().nth(i).textContent()) || '').replace(/\s+/g, ' ').trim();
  check((await text('#calBadge')) === 'Not set up', 'calendar not set up');
  check(await p.isDisabled('#calOn'), 'calendar switch disabled');
  check(await vis('#calForm') && !(await vis('#calAdd')), 'with none saved the form is the way in');
  check((await p.getAttribute('#calName', 'placeholder')) === 'Calendar 1' && (await p.getAttribute('#calLabel', 'placeholder')) === 'C1', 'placeholders are the defaults');
  await p.fill('#calInput', 'http://calendar.google.com/x/basic.ics');
  await p.click('#calSave');
  check((await text('#calMsg')).includes('starts with http://'), 'http refused on the phone');
  await p.fill('#calInput', URL1);
  await p.click('#calSave');
  await sleep(200);
  check((await text('#calMsg')).includes('Checking the address'), 'checking message');
  await sleep(2600);
  check((await text('#calMsg')).startsWith('Saved Calendar 1. 3 meetings left today.'), 'saved: ' + await text('#calMsg'));
  await sleep(2200);
  check((await text('#calBadge')) === 'Connected', 'badge Connected');
  check((await calRows().count()) === 1, 'one row');
  let r0 = await rowText(0);
  check(r0.includes('Calendar 1') && r0.includes('C1') && r0.includes('Address saved') && r0.includes('Synced just now · 3 meetings left today'), 'row: ' + r0);
  check(!r0.includes('calendar.google.com') && !r0.includes('ending'), 'a saved address shows only "Address saved"');
  check((await text('#calLine')).startsWith('1 of 3 calendars · 3 meetings left today'), 'cal line: ' + await text('#calLine'));
  check(await p.isChecked('#calOn'), 'Calendar meetings on after a first address');
  check(await vis('#calAdd') && !(await vis('#calAddNote')), 'Add a calendar is there with room');
  await p.check('#calTitles');
  await sleep(400);
  s = await state();
  check(s.toast.includes('Meeting titles on'), 'titles toast: ' + s.toast);
  await p.click('#calSync');
  await sleep(2500);
  check((await text('#calMsg')).startsWith('Calendar synced.'), 'synced: ' + await text('#calMsg'));
  await sim('/_sim/meetings', '-5 60');
  await sleep(2300);
  check((await text('#abTitle')).startsWith('In a meeting until'), 'meeting banner: ' + await text('#abTitle'));
  check((await text('#abSub')) === 'Design review · from your calendar', 'meeting sub with title: ' + await text('#abSub'));
  await calRows().nth(0).locator('[data-act="rm"]').click();
  check((await rowText(0)).includes('Remove Calendar 1? The bar stops showing its meetings and Next up.'), 'remove asks first, in the row');
  await calRows().nth(0).locator('[data-act="rm-no"]').click();
  check((await text('#calMsg')) === 'Kept Calendar 1.', 'Keep it');
  await calRows().nth(0).locator('[data-act="rm"]').click();
  await calRows().nth(0).locator('[data-act="rm-yes"]').click();
  await sleep(500);
  s = await state();
  check(s.toast.includes('Calendar 1 removed'), 'the bar says which calendar went: ' + s.toast);
  await sleep(1800);
  check((await text('#calMsg')) === 'Removed Calendar 1.', 'removed: ' + await text('#calMsg'));
  check((await text('#calBadge')) === 'Not set up', 'not set up again');
  check(!(await vis('#autoBanner')), 'meeting gone');
  // a feed whose file name is its private token: nothing of the address shows anywhere (security review, api.md 11.1)
  await p.fill('#calInput', URL3);
  await p.click('#calSave');
  await sleep(2800);
  check((await text('#calMsg')).startsWith('Saved Calendar 1.'), 'token-named feed saved: ' + await text('#calMsg'));
  await sleep(2200);
  check(!(await p.content()).includes('8c1d5e2a9b7f'), 'the token is nowhere on the page');
  // a second and a third calendar
  await p.click('#calAdd');
  check((await p.inputValue('#calName')) === 'Calendar 2' && (await p.inputValue('#calLabel')) === 'C2', 'the form starts with the next free name and tag');
  check((await p.getAttribute('#calInput', 'type')) === 'password', 'the address field starts masked');
  await p.fill('#calName', 'calendar 1');
  await p.fill('#calInput', URL2);
  await p.click('#calSave');
  check((await text('#calMsg')).startsWith('Another calendar already uses that name or tag'), 'duplicate name refused on the phone: ' + await text('#calMsg'));
  await p.fill('#calName', 'Home');
  await p.fill('#calLabel', 'hm');
  await p.click('#calSave');
  await sleep(2800);
  check((await text('#calMsg')).startsWith('Saved Home.'), 'second saved: ' + await text('#calMsg'));
  await sleep(2200);
  check((await calRows().count()) === 2, 'two rows');
  check((await text('#calLine')).startsWith('2 of 3 calendars'), 'two of three: ' + await text('#calLine'));
  await p.click('#calAdd');
  await p.fill('#calInput', 'https://cal.example.com/x/third-private-0a1b2c3d/basic.ics');
  await p.click('#calSave');
  await sleep(2800);
  await sleep(2200);
  check((await calRows().count()) === 3, 'three rows');
  check(await p.isDisabled('#calAdd') && await vis('#calAddNote'), 'at three, Add is off and says why');
  check((await text('#calAddNote')) === 'You can add up to 3 calendars. Remove one to add another.', 'limit note');
  // edit: a new name and tag with the address left empty keeps the saved one
  await calRows().nth(1).locator('[data-act="edit"]').click();
  check((await text('#calFormTitle')) === 'Edit Home' && (await p.inputValue('#calName')) === 'Home' && (await p.inputValue('#calInput')) === '', 'edit form');
  await p.fill('#calName', 'Family');
  await p.fill('#calLabel', 'fam');
  await p.click('#calSave');
  await sleep(600);
  check((await text('#calMsg')) === 'Saved Family.', 'renamed: ' + await text('#calMsg'));
  check((await rowText(1)).includes('Family') && (await rowText(1)).includes('FAM'), 'row renamed: ' + await rowText(1));
  // one that can't sync: its row says why in the one sentence, the badge counts, the others carry on
  await sim('/_sim/calfail', '2 1');
  await sleep(2600);
  check((await text('#calBadge')) === '1 can\'t sync', 'badge: ' + await text('#calBadge'));
  const bad = await rowText(1);
  check(bad.includes("Can't sync") && bad.includes("Couldn't reach this calendar. Last synced") && bad.includes('Its meetings are left out of the bar until it works.'), 'failing row: ' + bad);
  check(await calRows().nth(1).evaluate(e => e.classList.contains('err')), 'the failing row is marked');
  check(!(await rowText(0)).includes("Couldn't reach"), 'the others are fine');
  await sim('/_sim/calfail', '3 1');
  await sim('/_sim/calfail', '1 1');
  await sleep(2600);
  check((await text('#calBadge')) === "Can't sync", 'all failing: ' + await text('#calBadge'));
  check((await text('#calLine')).includes('none can be reached'), 'line: ' + await text('#calLine'));
  await sim('/_sim/calfail', '1 0');
  await sim('/_sim/calfail', '2 0');
  await sim('/_sim/calfail', '3 0');
  await noScroll('three calendars');
  await p.screenshot({ path: path.join(OUT, 'remote_390_three.png'), fullPage: true });
  // remove two of the three, asking each time
  for (const k of [2, 1]) {
    await calRows().nth(k).locator('[data-act="rm"]').click();
    await calRows().nth(k).locator('[data-act="rm-yes"]').click();
    await sleep(2400);
  }
  check((await calRows().count()) === 1, 'back to one row');
  check((await text('#calLine')).startsWith('1 of 3 calendars'), 'line with one: ' + await text('#calLine'));
  await sleep(500);
  await noScroll('the Remote');
  await p.screenshot({ path: path.join(OUT, 'remote_390.png'), fullPage: true });
  await p.emulateMedia({ colorScheme: 'dark' });
  await p.screenshot({ path: path.join(OUT, 'remote_390_dark.png'), fullPage: true });
  await p.emulateMedia({ colorScheme: 'light' });

  // ---------- Paired devices: Remove asks first, in place ----------
  await p.click('#devList [aria-label="Remove Mac"]');
  await sleep(200);
  check((await text('#devList .setup-msg p')) === 'Remove Mac? It stops working over Wi-Fi until it pairs again. USB still works.', 'remove asks first: ' + await text('#devList .setup-msg p'));
  check(await p.evaluate(() => document.activeElement && document.activeElement.textContent) === 'Keep it', 'focus on Keep it');
  await p.click('#devList [data-no]');
  await sleep(200);
  check(await p.evaluate(() => document.activeElement && document.activeElement.getAttribute('aria-label')) === 'Remove Mac', 'Keep it: nothing removed, focus back on Remove');
  await p.click('#devList [aria-label="Remove Mac"]');
  await p.click('#devList [data-yes]');
  await sleep(500);
  s = await state();
  check(s.toast === 'Removed Mac', 'the bar says Removed Mac: ' + s.toast);
  check(await p.$$eval('#devList .dev', e => e.length) === 1, 'one device left');
  check((await text('#devFoot')).startsWith('1 of 10.'), 'foot counts it');
  check(await text('#macLine') === 'Connected over USB · heard from it just now', 'removing the Mac leaves USB working: ' + await text('#macLine'));

  // ---------- the ways back to the prompt ----------
  // Forget all on the bar: "MiniBar forgot this device."
  await sim('/_sim/forget', '');
  await sleep(2500);
  check(await vis('#pairView') && !(await vis('#remoteMain')), 'Forget all on the bar: back to the prompt');
  check(await text('#pairNote') === 'MiniBar forgot this device. Pair it again to use the Remote.', 'note: ' + await text('#pairNote'));
  await noScroll('the prompt with a note');
  // pair again, then Remove this device
  const pairNow = async pg => {
    await pg.click('#pairAsk');
    await sleep(300);
    const st = await state();
    await sleep(1000);
    await pg.fill('#pairInput', st.code);
    await sleep(900);
  };
  await pairNow(p);
  check(await vis('#remoteMain'), 'paired again');
  await sleep(500);
  await p.click('#devList [aria-label="Remove this device"]');
  await sleep(200);
  check((await text('#devList .setup-msg p')) === "Remove this device? The Remote signs out here, and you'll need a code from MiniBar to use it again.", 'remove this device asks first');
  await p.click('#devList [data-yes]');
  await sleep(500);
  check(await vis('#pairView'), 'Remove this device signs the Remote out');
  check(await text('#pairNote') === 'This device is signed out. Pair it again to use the Remote.', 'note: ' + await text('#pairNote'));
  check(await active() === 'pairAsk', 'focus on Pair this device');
  s = await state();
  check(s.toast === 'Removed Phone', 'the bar says Removed Phone: ' + s.toast);
  // removed on another phone: "This device isn't paired with MiniBar 2A1C anymore."
  await pairNow(p);
  const q = await (await b.newContext({ viewport: { width: 390, height: 844 } })).newPage();
  watch(q);
  await q.goto(base + '/');
  await sleep(800);
  await sleep(300);
  await pairNow(q);
  check(await q.isVisible('#remoteMain'), 'a second phone pairs');
  await sleep(600);
  const other = await q.$$eval('#devList .dev', els => els.length);
  check(other === 2, 'it lists both phones: ' + other);
  await q.click('#devList [aria-label="Remove Phone"]');
  await q.click('#devList [data-yes]');
  await sleep(2600);
  check(await vis('#pairView'), 'the first phone, removed on the other, goes back to the prompt');
  check(await text('#pairNote') === "This device isn't paired with MiniBar 2A1C anymore. Pair it again to use the Remote.", 'note: ' + await text('#pairNote'));

  // the bar stops answering while the phone waits on its code: "didn't answer"
  await sim('/_sim/restart', '');
  await sleep(1100);
  await p.click('#pairAsk');
  await sleep(400);
  const code = (await state()).code;
  fb.kill();
  await sleep(300);
  await p.fill('#pairInput', code);
  await sleep(800);
  check(await text('#pairErr') === "MiniBar 2A1C didn't answer. Make sure it's on, then try again.", 'no answer: ' + await text('#pairErr'));
  check(await p.inputValue('#pairInput') !== '', 'the digits stay, to try again');

  check(errors.length === 0, 'no page errors: ' + errors.join(' | '));
  await b.close();
  console.log(fails ? `${fails} FAILED` : 'ALL OK');
  process.exit(fails ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
