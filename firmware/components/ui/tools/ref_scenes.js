// ref_scenes.js: render the snapshot tool's scenes from docs/mockup.html, at 640 x 172, so the firmware's screens can
// be compared with the mock-up pixel for pixel. Owner: ui builder.
//
//   NODE_PATH=$(npm root -g) node firmware/components/ui/tools/ref_scenes.js <out-dir> [scene ...]
//   python3 firmware/components/ui/tools/compare.py <ref-dir> <snap-dir> <out-dir>   (side by side, with a diff)
//
// Needs Playwright's Chromium. The page gets the real Barlow fonts (firmware/fonts/src) instead of Google Fonts,
// the QR library inlined (QRCODE_JS=<path to qrcode-generator 1.4.4's qrcode.min.js>, else it's fetched from cdnjs
// with curl), a fixed clock (Sunday 2026-10-04, 2:04:00 PM in Los Angeles) and a hook (window.__tb) into the
// mock-up's script, so each scene sets the mock-up's state directly, the way host/ui_scenes.c sets the firmware's.
// Scenes the mock-up can't show (the firmware's proposals: plain Away, the unknown clock) are skipped. The pairing
// round's screens (the pairing screen, the Wi-Fi menu with Devices, Forget all) are drawn from the mock-up's pair and
// tokens. MOCKUP=<path> renders another copy of the mock-up (default docs/mockup.html).
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const REPO = path.join(__dirname, '../../../..');
const FONTS = path.join(REPO, 'firmware/fonts/src');
const FACES = [['Barlow Condensed', 700, 'BarlowCondensed-Bold.ttf'], ['Barlow', 500, 'Barlow-Medium.ttf'],
  ['Barlow', 600, 'Barlow-SemiBold.ttf'], ['Barlow', 700, 'Barlow-Bold.ttf']];

function qrLib() {
  if (process.env.QRCODE_JS) return fs.readFileSync(process.env.QRCODE_JS, 'utf8');
  const cache = path.join(require('os').tmpdir(), 'qrcode-1.4.4.min.js');
  if (!fs.existsSync(cache)) execFileSync('curl', ['-sS', '-o', cache, 'https://cdnjs.cloudflare.com/ajax/libs/qrcode-generator/1.4.4/qrcode.min.js']);
  return fs.readFileSync(cache, 'utf8');
}

const HOOK = `window.__tb = { s, pomo, cal, mac, render, toast, showMenu, showTimerSettings, showWifiMenu, showPowerMenu,
  hideMenu, showHold, phaseLen, withTitle, autoTop, screen, lcd, clearUnderToast,
  resetKey() { lastKey = ''; lastShape = ''; }, set autoShown(v) { autoShown = v; },
  resetToast() { toastHoldUntil = 0; pendingToast = null; },
  get ripenFrames() { return ripenFrames; }, pair, tokens, BAR, showForgetMenu };
  setInterval(loop, 200);`;

function page() {
  let src = fs.readFileSync(process.env.MOCKUP || path.join(REPO, 'docs/mockup.html'), 'utf8');
  src = src.replace(/<link rel="stylesheet" href="https:\/\/fonts.googleapis[^>]*>/, '');
  src = src.replace('<script src="https://cdnjs.cloudflare.com/ajax/libs/qrcode-generator/1.4.4/qrcode.min.js"></script>', () => '<script>' + qrLib() + '</script>');
  if (!src.includes('  setInterval(loop, 200);')) throw new Error('the hook point in docs/mockup.html moved');
  src = src.replace('  setInterval(loop, 200);', HOOK);
  const faces = FACES.map(([f, w, file]) => `@font-face{font-family:'${f}';font-weight:${w};src:url(data:font/ttf;base64,${fs.readFileSync(path.join(FONTS, file)).toString('base64')})}`).join('\n');
  // The screen at exactly 640 x 172 CSS pixels, square corners, no transitions (a scene is a still frame).
  const css = '#device{width:640px!important;max-width:640px!important;padding:0!important;border-radius:0!important}'
    + '#screen{border-radius:0!important}*{transition:none!important}';
  return `<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><style>${faces}[hidden]{display:none!important}${css}</style></head><body>${src}</body></html>`;
}

// In-page helpers: the same building blocks as host/ui_scenes.c.
const LIB = () => {
  const t = window.__tb, MIN = 60000;
  const at = m => new Date(Date.now() + m * MIN);
  const ID = { available: 0, busy: 1, meeting: 2, pomodoro: 3, away: 4, message: 5, clock: 6 };
  const SAMPLE = [[56, 45, 'Design review', 'Room 4'], [116, 30, '1:1 with Sam', ''], [206, 60, 'Team retro', 'Room 2']];
  const NOW_MEETING = [[-19, 45, 'Design review', 'Room 4'], [56, 30, '1:1 with Sam', '']];
  const el = id => document.getElementById(id);
  window.__sc = {
    SAMPLE, NOW_MEETING,
    base(st, wifi = true) {
      const { s, pomo, cal, mac } = t;
      t.hideMenu();
      el('toast').hidden = true;
      // A held toast (pairing's endings) would otherwise hold back the next scene's toast: the page clock is fixed.
      t.resetToast();
      el('holdOv').hidden = true;
      el('holdOv').classList.remove('go', 'done');
      el('holdOv').querySelector('.track i').style.width = '';
      el('holdTitle').textContent = 'Keep holding';
      el('holdSub').textContent = 'to power off';
      el('device').classList.remove('flipped');
      t.screen.classList.remove('off', 'dead');
      el('bar').style.visibility = '';
      t.lcd.classList.remove('alert');
      Object.assign(s, { powered: true, booting: false, off: false, flipped: false, ringing: false, idx: ID[st], lastStatus: 1,
        since: at(-12), aside: { call: null, meeting: null }, message: 'On a deadline until 3 PM, message me instead', messageAt: at(0) });
      s.wifi = wifi ? { mode: 'ok', ssid: 'Office-WiFi', ip: '10.0.4.42', error: '' } : { mode: 'setup', ssid: '', ip: '', error: '' };
      Object.assign(pomo, { phase: 'focus', round: 1, remaining: t.phaseLen('focus'), running: false, waiting: false, justEnded: null,
        doneToday: 1, skipped: [], focusedMs: 31 * MIN + 18000, autoPaused: null, heldAlarm: null, auto: false });
      Object.assign(cal, { state: 'none', on: false, titles: false, checking: false, events: [], lastSync: at(-2) });
      Object.assign(mac, { on: true, link: null, call: null, quiet: false, quietMs: 0 });
      t.autoShown = null;
      // No code out, and the mock-up's three sample devices (a Mac over USB, this phone, a script), as it starts.
      Object.assign(t.pair, { code: null, id: null, who: null, left: 0, shown: false, shownAt: 0, tries: 0 });
      if (!window.__tok0) window.__tok0 = t.tokens.slice();
      t.tokens.length = 0;
      window.__tok0.forEach(x => t.tokens.push(x));
      t.BAR.host = 'tinybar.local';
    },
    // Ten devices, used one minute apart in this order, so Forget all names them as host/ui_scenes.c's TEN does.
    ten() {
      const names = ['iPhone', 'Mac', 'Desk script', 'iPad', 'Android phone', 'Windows browser', 'Hallway sign', 'Shortcuts', 'Chromebook', 'Kitchen iPad'];
      t.tokens.length = 0;
      names.forEach((name, i) => t.tokens.push({ id: 'a' + i, name, kind: 'remote', scope: 'full', client: 'c' + i, at: at(-600), via: 'wifi', used: at(-i) }));
    },
    // A code on the bar for who ({ kind, name }), shown 18 s ago: 1:42 left.
    pairing(who) { Object.assign(t.pair, { code: '482913', id: 'x', who, left: 102000, shown: true, shownAt: Date.now(), tries: 3 }); },
    pomo(phase, round, secs, running) { Object.assign(t.pomo, { phase, round, remaining: secs * 1000, running, waiting: false, justEnded: null }); },
    waiting(ended, next, round) { Object.assign(t.pomo, { phase: next, round, remaining: t.phaseLen(next), running: false, waiting: true, justEnded: ended }); },
    calendar(titles, list) {
      Object.assign(t.cal, { state: 'ok', on: true, titles, lastSync: at(-2),
        events: list.map(([m, len, title, loc, priv], i) => ({ id: 100 + i, title, loc, start: at(m), end: at(m + len), priv: !!priv })) });
    },
    call(app) {
      Object.assign(t.mac, { link: 'usb', call: { id: 7, app: app || null, since: at(-12), via: 'usb' } });
      if (t.pomo.running) { t.pomo.running = false; t.pomo.autoPaused = 'call'; }
    },
    meetingTakesOver() { if (t.pomo.running) { t.pomo.running = false; t.pomo.autoPaused = 'meeting'; } },
    asideAll() { const a = t.autoTop(); if (t.mac.call) t.s.aside.call = t.mac.call.id; const m = t.cal.events.find(e => e.start <= new Date() && new Date() < e.end); if (m) t.s.aside.meeting = m.id; return a; },
    // The call or meeting on screen counts as announced, so the loop's syncAuto() adds no toast.
    draw() { t.autoShown = t.autoTop(); t.resetKey(); t.render(); },
    t, el,
  };
};

// Each scene, as host/ui_scenes.c builds it. null: the mock-up can't show it.
const SCENES = {
  available: () => { __sc.base('available'); },
  available_free: () => { __sc.base('available'); __sc.calendar(false, __sc.SAMPLE); },
  available_free_titles: () => { __sc.base('available'); __sc.calendar(true, __sc.SAMPLE); },
  available_rest: () => { __sc.base('available'); __sc.calendar(false, []); },
  busy: () => { __sc.base('busy'); },
  meeting_manual: () => { __sc.base('meeting'); },
  meeting_manual_next: () => { __sc.base('meeting'); __sc.calendar(true, __sc.SAMPLE); },
  away: null,
  away_back: () => { __sc.base('away'); },
  message_short: () => { __sc.base('message'); __sc.t.s.message = 'Out to lunch'; },
  message_long: () => { __sc.base('message'); },
  clock: () => { __sc.base('clock'); __sc.calendar(false, __sc.SAMPLE); },
  clock_titles: () => { __sc.base('clock'); __sc.calendar(true, __sc.SAMPLE); },
  clock_nothing: () => { __sc.base('clock'); __sc.calendar(false, []); },
  clock_notsetup: () => { __sc.base('clock'); },
  clock_caloff: () => { __sc.base('clock'); __sc.calendar(false, __sc.SAMPLE); __sc.t.cal.on = false; },
  clock_offline: () => { __sc.base('clock'); __sc.calendar(false, __sc.SAMPLE); __sc.t.s.wifi.mode = 'offline'; },
  clock_unset: null,
  pomo_ready: () => { __sc.base('pomodoro'); },
  pomo_focus: () => { __sc.base('pomodoro'); __sc.pomo('focus', 2, 18 * 60 + 42, true); },
  pomo_paused: () => { __sc.base('pomodoro'); __sc.pomo('focus', 2, 18 * 60 + 42, false); },
  pomo_short: () => { __sc.base('pomodoro'); __sc.pomo('short', 2, 3 * 60 + 10, true); },
  pomo_long: () => { __sc.base('pomodoro'); __sc.pomo('long', 4, 12 * 60, true); __sc.t.pomo.doneToday = 4; },
  pomo_bigfoot: () => { __sc.base('pomodoro'); __sc.pomo('focus', 2, 18 * 60 + 42, true); Object.assign(__sc.t.pomo, { doneToday: 12, focusedMs: (5 * 60 + 10) * 60000 }); },
  pomo_paused_long: () => { __sc.base('pomodoro'); __sc.pomo('long', 4, 12 * 60, false); __sc.t.pomo.doneToday = 4; },
  pomo_paused_short: () => { __sc.base('pomodoro'); __sc.pomo('short', 2, 3 * 60 + 10, false); },
  pomo_break_time: () => { __sc.base('pomodoro'); __sc.waiting('focus', 'short', 2); __sc.t.pomo.doneToday = 2; },
  pomo_back_to_it: () => { __sc.base('pomodoro'); __sc.waiting('short', 'focus', 3); __sc.t.pomo.doneToday = 2; },
  pill_focus: () => { __sc.base('busy'); __sc.pomo('focus', 2, 18 * 60 + 42, true); },
  pill_paused: () => { __sc.base('available'); __sc.pomo('focus', 2, 18 * 60 + 42, false); },
  pill_done: () => { __sc.base('busy'); __sc.waiting('focus', 'short', 2); },
  pill_short: () => { __sc.base('busy'); __sc.pomo('short', 2, 3 * 60 + 10, true); },
  call: () => { __sc.base('busy'); __sc.call('Slack'); },
  call_noapp: () => { __sc.base('busy'); __sc.call(null); },
  call_longname: () => { __sc.base('busy'); __sc.call('Microsoft Teams (work or school)'); },
  call_paused: () => { __sc.base('pomodoro'); __sc.pomo('focus', 2, 18 * 60 + 42, true); __sc.call('Slack'); },
  call_meeting: () => { __sc.base('busy'); __sc.calendar(false, __sc.NOW_MEETING); __sc.call('Zoom'); },
  call_meeting_titles: () => { __sc.base('busy'); __sc.calendar(true, __sc.NOW_MEETING); __sc.call('Zoom'); },
  meeting_cal: () => { __sc.base('busy'); __sc.calendar(false, __sc.NOW_MEETING); },
  meeting_cal_titles: () => { __sc.base('busy'); __sc.calendar(true, __sc.NOW_MEETING); },
  meeting_cal_private: () => { __sc.base('busy'); __sc.calendar(true, [[-19, 45, 'Doctor', 'Clinic', true], [56, 30, '1:1 with Sam', '']]); },
  meeting_cal_long: () => { __sc.base('busy'); __sc.calendar(true, [[-19, 45, 'Quarterly planning with the platform and infrastructure teams', '']]); },
  aside_call: () => { __sc.base('busy'); __sc.call('Slack'); __sc.asideAll(); },
  aside_meeting_pill: () => { __sc.base('busy'); __sc.pomo('focus', 2, 18 * 60 + 42, true); __sc.calendar(false, __sc.NOW_MEETING); __sc.meetingTakesOver(); __sc.t.mac.link = 'usb'; __sc.asideAll(); },
  mac_icon: () => { __sc.base('busy'); __sc.t.mac.link = 'usb'; },
  setup_qr: () => { __sc.base('clock', false); },
  setup_connecting: () => { __sc.base('clock', false); Object.assign(__sc.t.s.wifi, { mode: 'connecting', ssid: 'Office-WiFi' }); },
  setup_connected: () => { __sc.base('clock', false); Object.assign(__sc.t.s.wifi, { mode: 'connected', ssid: 'Office-WiFi', ip: '10.0.4.42' }); },
  setup_failed: () => { __sc.base('clock', false); Object.assign(__sc.t.s.wifi, { mode: 'failed', ssid: 'Office-WiFi', error: 'Wrong password' }); },
  menu_quick: () => { __sc.base('available'); __sc.calendar(false, __sc.SAMPLE); __sc.draw(); __sc.t.showMenu(); },
  menu_quick_offline: () => { __sc.base('available'); __sc.t.s.wifi.mode = 'offline'; __sc.draw(); __sc.t.showMenu(); },
  menu_quick_nocal: () => { __sc.base('available'); __sc.draw(); __sc.t.showMenu(); },
  menu_quick_syncing: () => { __sc.base('available'); __sc.calendar(false, __sc.SAMPLE); __sc.t.cal.checking = true; __sc.draw(); __sc.t.showMenu(); },
  menu_quick_showagain: () => { __sc.base('busy'); __sc.call('Slack'); __sc.asideAll(); __sc.draw(); __sc.t.showMenu(); },
  menu_timer: () => { __sc.base('pomodoro'); __sc.pomo('focus', 2, 18 * 60 + 42, true); __sc.draw(); __sc.t.showMenu(); },
  menu_timer_ready: () => { __sc.base('pomodoro'); __sc.draw(); __sc.t.showMenu(); },
  menu_timer_showagain: () => { __sc.base('pomodoro'); __sc.pomo('focus', 2, 18 * 60 + 42, true); __sc.call('Slack'); __sc.asideAll(); __sc.draw(); __sc.t.showMenu(); },
  menu_timer_settings: () => { __sc.base('pomodoro'); __sc.pomo('focus', 2, 18 * 60 + 42, true); __sc.draw(); __sc.t.showTimerSettings(); },
  menu_wifi: () => { __sc.base('available'); __sc.calendar(false, __sc.SAMPLE); __sc.draw(); __sc.t.showWifiMenu(); },
  menu_wifi_none: () => { __sc.base('available'); __sc.calendar(false, __sc.SAMPLE); __sc.t.tokens.length = 0; __sc.draw(); __sc.t.showWifiMenu(); },
  menu_wifi_none_renamed: () => { __sc.base('available'); __sc.calendar(false, __sc.SAMPLE); __sc.t.tokens.length = 0; __sc.t.BAR.host = 'tinybar-2.local'; __sc.draw(); __sc.t.showWifiMenu(); },
  menu_wifi_none_longip: () => { __sc.base('available'); __sc.calendar(false, __sc.SAMPLE); __sc.t.tokens.length = 0; __sc.t.BAR.host = 'tinybar-2.local'; __sc.t.s.wifi.ip = '192.168.100.200'; __sc.draw(); __sc.t.showWifiMenu(); },
  menu_wifi_full: () => { __sc.base('available'); __sc.calendar(false, __sc.SAMPLE); __sc.ten(); __sc.draw(); __sc.t.showWifiMenu(); },
  menu_wifi_offline: () => { __sc.base('available'); __sc.t.s.wifi.mode = 'offline'; __sc.t.tokens.length = 0; __sc.draw(); __sc.t.showWifiMenu(); },
  menu_forget: () => { __sc.base('available'); __sc.calendar(false, __sc.SAMPLE); __sc.draw(); __sc.t.showForgetMenu(); },
  menu_forget_full: () => { __sc.base('available'); __sc.calendar(false, __sc.SAMPLE); __sc.ten(); __sc.draw(); __sc.t.showForgetMenu(); },
  menu_power: () => { __sc.base('available'); __sc.calendar(false, __sc.SAMPLE); __sc.draw(); __sc.t.showPowerMenu(); },
  menu_setup: () => { __sc.base('clock', false); __sc.draw(); __sc.t.showMenu(); },
  toast_status: () => { __sc.base('available'); __sc.calendar(false, __sc.SAMPLE); __sc.draw(); __sc.t.toast('Meeting set aside · hold to show it again'); },
  toast_clock: () => { __sc.base('clock'); __sc.calendar(false, __sc.SAMPLE); __sc.draw(); __sc.t.toast('Ready'); },
  toast_setup: () => { __sc.base('clock', false); __sc.draw(); __sc.t.toast('Scan the code with your phone'); },
  toast_menu: () => { __sc.base('pomodoro'); __sc.pomo('focus', 2, 18 * 60 + 42, true); __sc.t.pomo.auto = true; __sc.draw(); __sc.t.showTimerSettings(); __sc.t.toast('Auto-start on'); },
  toast_title: () => {
    __sc.base('available'); __sc.calendar(true, [[-1, 45, 'Quarterly planning with the platform team', '']]); __sc.t.autoShown = __sc.t.autoTop();
    __sc.draw(); __sc.t.toast(__sc.t.withTitle(x => `Calendar: ${x} started`, 'Quarterly planning with the platform team'));
  },
  hold_keep: () => { __sc.base('busy'); __sc.draw(); __sc.el('holdOv').hidden = false; __sc.el('holdOv').querySelector('.track i').style.width = '50%'; },
  hold_off: () => {
    __sc.base('busy'); __sc.draw(); __sc.el('holdTitle').textContent = 'Powering off'; __sc.el('holdSub').textContent = 'Press PWR to turn it back on';
    __sc.el('holdOv').classList.add('done'); __sc.el('holdOv').hidden = false;
  },
  flash: () => { __sc.base('pomodoro'); __sc.waiting('focus', 'short', 2); __sc.t.pomo.doneToday = 2; __sc.t.s.ringing = true; __sc.draw(); __sc.t.lcd.classList.add('alert'); },
  splash: () => { __sc.base('clock'); __sc.t.s.booting = true; },
  pairing: () => { __sc.base('busy'); __sc.pairing({ kind: 'mac', name: null }); },
  pairing_phone: () => { __sc.base('busy'); __sc.pairing({ kind: 'remote', name: null }); },
  pairing_named: () => { __sc.base('busy'); __sc.pairing({ kind: 'mac', name: "Alex's MacBook Air" }); },
  pairing_script: () => { __sc.base('busy'); __sc.pairing({ kind: 'automation', name: null }); },
  pairing_late: () => { __sc.base('busy'); __sc.pairing({ kind: 'remote', name: null }); __sc.t.pair.left = 5000; },
  toast_pair_canceled: () => { __sc.base('busy'); __sc.draw(); __sc.t.toast('Pairing canceled', true); },
  toast_paired: () => { __sc.base('busy'); __sc.draw(); __sc.t.toast('Paired · iPhone', true); },
  toast_pair_flip: () => { __sc.base('pomodoro'); __sc.pomo('focus', 1, 25 * 60, true); __sc.draw(); __sc.t.toast('Pairing canceled · Focus started', true); },
  toast_forgot: () => { __sc.base('available'); __sc.calendar(false, __sc.SAMPLE); __sc.t.tokens.length = 0; __sc.draw(); __sc.t.toast('Forgot 3 devices'); },
  dark: () => { __sc.base('busy'); __sc.draw(); __sc.t.s.off = true; __sc.t.screen.classList.add('off'); __sc.el('bar').style.visibility = 'hidden'; },
  flipped: () => { __sc.base('available'); __sc.calendar(false, __sc.SAMPLE); __sc.t.s.flipped = true; __sc.el('device').classList.add('flipped'); __sc.draw(); __sc.t.toast('Meeting set aside · hold to show it again'); },
};
// After the scene: animations stopped at the snapshot tool's moment (the marquee 3 s in, the flash 120 ms in).
const ANIM = { message_long: 3000, flash: 120 };

(async () => {
  const out = process.argv[2] || '.';
  const only = process.argv.slice(3);
  fs.mkdirSync(out, { recursive: true });
  const b = await chromium.launch({ args: ['--font-render-hinting=none', '--disable-lcd-text'] });
  const ctx = await b.newContext({ timezoneId: 'America/Los_Angeles', viewport: { width: 900, height: 600 }, deviceScaleFactor: 1 });
  const p = await ctx.newPage();
  const errs = [];
  p.on('pageerror', e => errs.push(e.message));
  await p.clock.setFixedTime(new Date('2026-10-04T14:04:00-07:00'));
  await p.setContent(page(), { waitUntil: 'load' });
  await p.waitForFunction(() => window.__tb && window.__tb.ripenFrames);
  await p.evaluate(() => document.fonts.ready);
  await p.evaluate(LIB);
  for (const [name, fn] of Object.entries(SCENES)) {
    if (!fn || (only.length && !only.includes(name))) continue;
    await p.evaluate(`(${fn.toString()})(); if (!document.getElementById('menu').hidden || !document.getElementById('toast').hidden || !document.getElementById('holdOv').hidden) { window.__tb.clearUnderToast(); } else { __sc.draw(); }`);
    await p.waitForTimeout(450);     // past the next loop() tick, which redraws once after a reset key
    await p.evaluate(ms => {
      document.getAnimations().forEach(a => { a.pause(); a.currentTime = ms || 0; });
      window.__tb.clearUnderToast();
    }, ANIM[name] || 0);
    const box = await p.locator('#screen').boundingBox();
    await p.screenshot({ path: path.join(out, name + '.png'), clip: { x: Math.round(box.x), y: Math.round(box.y), width: 640, height: 172 } });
    console.log(path.join(out, name + '.png'));
  }
  if (errs.length) console.log('page errors:', errs);
  await b.close();
})();
