# calendar

The secret iCal address, a streaming iCalendar reader with recurrence and time zones, and the sync service that
turns the address into today's and tomorrow's meetings for the app task. Owner: calendar builder.

`src/` is pure C (no ESP-IDF, FreeRTOS or LVGL headers) and is tested on Linux under `test/host/calendar/`;
`esp/cal_sync.c` is the device service around it.

## Files

| File | What |
|---|---|
| `include/cal_url.h`, `src/cal_url.c` | The mock-up's `checkIcal()`: format checks in its order, the normalized `https://` address, the masked form (host, file, last four characters of the token; a file name that is itself the token, long or random-looking, shows as "….ics"), the Google calendar id (the owner's address, for PARTSTAT) |
| `include/cal_tz.h`, `src/cal_tz.c` | Civil dates; POSIX TZ rules (`Mm.w.d`, `Jn`, `n`, times from -167 to 167 h, negative DST like Dublin's); offsets; local to UTC through gaps (moved forward) and overlaps (the earlier instant); local midnights |
| `src/cal_tz_table.inc` | IANA name to POSIX rule, 597 names (links included), about 12 KB. **Generated** by `tools/gen_tz_table.py` from the `tzdata` Python package (2026e) |
| `include/cal_rrule.h`, `src/cal_rrule.c` | RRULE: DAILY, WEEKLY, MONTHLY, YEARLY; INTERVAL, COUNT, UNTIL (date, local, UTC), WKST, BYDAY (with ordinals), BYMONTHDAY, BYMONTH, BYSETPOS. Expansion in local civil time, with old periods skipped arithmetically and a budget of days examined (100,000 an event, 2,000,000 a feed), so a hostile feed's CPU time is bounded; a tick hook (`cal_rrule_expand_ex`, `cal_feed_set_tick`) lets the device yield from inside the walk |
| `include/cal_ics.h`, `src/cal_ics.c` | The streaming reader (`cal_feed_new/write/finish`): unfolding, parameters, escapes, VEVENT, VTIMEZONE (Outlook's Windows zone names), EXDATE, RECURRENCE-ID overrides in any order, what counts, about 28 KB of state and nothing else |
| `include/cal_today.h`, `src/cal_today.c` | The sync window (local midnight today to local midnight after tomorrow), now / next / left today in an explicit zone, trimming to `TB_MEETINGS_MAX` |
| `include/cal_store.h`, `src/cal_store.c` | The packed saved copy of the meetings for NVS (typically a few hundred bytes, at most 4.7 KB) |
| `include/cal_status.h`, `src/cal_status.c` | What `GET /api/v1/calendar` reports, and the check and sync result codes and sentences (matching the Remote's) |
| `include/cal_sync.h`, `esp/cal_sync.c` | The device service: NVS, the HTTPS fetch, the schedule, PUT / remove / Sync now, the bus posts |
| `tools/gen_tz_table.py` | Regenerates the zone table (`--check` tells whether it's current) |

## How a sync runs

1. The `cal_sync` task (core 0, priority 2, 10 KB stack in internal RAM) wakes for a PUT's check, for Sync now, or
   when the 10-minute timer is due, and only while online and once the clock is set (year 2025 or later).
2. `esp_http_client` opens the address over TLS with the certificate bundle. Redirects are followed by hand (at
   most 5, `https://` only). 400, 401, 403, 404 and 410 are `calendar_rejected`; no answer, DNS, TLS, a time-out (20 s
   per step), a 5xx or a cut-off transfer are `calendar_unreachable`.
3. The body goes through `cal_feed_write()` in 2 KB reads. Nothing holds the file: a 9 MB feed with 20,000 past
   events needs the same 28 KB as a small one. A body that isn't iCal stops the fetch after 4 KB (`not_a_calendar`).
   Limits: 16 MB and 3 minutes per fetch (`calendar_unreachable`, "That calendar is too large for TinyBar to read.").
4. `cal_feed_finish()` drops the instances that overrides replace, sorts by start and returns up to 64;
   `cal_today_trim()` keeps 32 (dropping meetings already over first).
5. The app task gets `TB_EV_CAL_MEETINGS` when the list changed (always after a PUT), then `TB_EV_CAL_EVENT`
   (SAVED after a PUT; SYNCED or SYNC_FAILED after Sync now; nothing for background syncs), then `TB_EV_CAL_STATUS`.
   The list is saved in `nvs` / `cal` / `list` only when its contents changed.

Failures keep the last good copy. A background sync that couldn't reach the server retries after 1, 2, 4, 8 minutes,
then every 10; a rejected address or a non-calendar waits the full 10 minutes.

## What counts, and the ids

Timed events that aren't Free (TRANSP:TRANSPARENT), cancelled, or declined by you (the ATTENDEE whose address is the
calendar id in Google's path). All-day events never count. Private and confidential events count, but their title
and location are dropped in the reader, so they're never stored, saved or sent. Titles and locations are cleaned
with `tb_text_clean()`, and characters the fonts can't draw become "?".

A meeting's id is `cal_instance_id(UID, the instance's original start)`, so an instance moved by an override keeps
the id core keys "set aside" on.

## Memory and flash (ESP32-S3, measured with `idf.py size-components`)

- Flash: about 40 KB (28 KB code, 12.6 KB data, mostly the zone table).
- Internal RAM: 652 bytes of static state, plus the 10 KB task stack. The service's 8 KB copy of the last list is in
  PSRAM.
- During a fetch, in PSRAM: the reader (28.3 KB), 64 + 32 meetings (24 KB), the list handed to the app task (8 KB),
  TLS buffers (about 40 KB, set by `sdkconfig.defaults`). The read buffer (2 KB) and the address (1 KB) are internal.

## Tests (host)

`test/host/calendar/` (59 tests, AddressSanitizer and UBSan) with fixtures written by
`test/host/calendar/fixtures/make_fixtures.py`:

- `google_week.ics`: Google's export format for a week, with an EXDATE, an override that comes before its master,
  a cancelled instance, private, Free, all-day and declined events, a meeting across midnight, a London meeting,
  instances moved into and out of the window, a monthly 1TU, a finished COUNT series, a split series, and a fold
  inside a UTF-8 character.
- `google_dst.ics`: the 2026 EU and US changes (local times kept, the doubled 1:30 AM, the missing 2:30 AM, COUNT,
  UTC UNTIL and EXDATE with TZID across the change, INTERVAL=2 with WKST, 2TU, -1FR, the 31st, yearly).
- `outlook.ics`: Exchange's format with LF line ends: Windows zone names defined by VTIMEZONE, a custom zone that
  dropped daylight time, BYSETPOS=-1, a Mozilla-style TZID, an unknown TZID, a floating time.
- Generated in the tests: every chunk size down to one byte, CRLF and LF, 600 KB descriptions and 300 KB
  attachments, a 9 MB feed of history, 100 meetings in the window, random bytes, a sign-in page, every prefix of a
  feed, hostile nesting, and the saved copy's format under corruption.
- The zone code is compared with glibc's own POSIX rules for 17 zones over two years, and every entry of the table is
  parsed and found.
- `test_core_agrees.c` asks core's own `tb_app_current_meeting / next_meeting / meetings_left / left_text` and this
  component's `cal_today_*` about the fixtures' meetings at every minute of the window: they agree.

```sh
cmake -S firmware/test/host -B firmware/build-host-calendar
cmake --build firmware/build-host-calendar -j && ctest --test-dir firmware/build-host-calendar -R calendar --output-on-failure
```

Rebuild the fixtures with `python3 firmware/test/host/calendar/fixtures/make_fixtures.py`, and the zone table with
`pip install tzdata && python3 firmware/components/calendar/tools/gen_tz_table.py`.

## Bring-up checklist (on the board; none of this has run on hardware yet)

1. Paste a real Google secret address on the Remote: the bar toasts "Calendar synced · N meetings left today" and
   the log shows `read ... bytes, ... events ... N meetings; ... bytes of stack left`. Keep at least 2 KB of stack
   left; raise `CAL_TASK_STACK` if not.
2. Time a sync of a large real calendar (several MB): bytes per second and the total time in the log line.
3. Paste a reset (old) secret address: "calendar_rejected" in `GET /api/v1/calendar` `check`, nothing saved.
4. Paste an address whose server is unreachable (a made-up host): `calendar_unreachable`.
5. Paste `https://example.com/calendar.ics` (returns HTML): `not_a_calendar`.
6. Restart with Wi-Fi off: today's meetings still switch In a meeting on and off from the saved copy.
7. Remove the address: "Calendar removed", the saved copy is gone after a restart.
8. Change the time zone in settings: the next sync reads the new local day.
9. Watch the log while syncing: the secret address never appears (esp_http_client's own log is silenced).
10. An iCloud `webcal://` address (it redirects): it syncs.

## Not done, on purpose

- RDATE, RANGE=THISANDFUTURE, BYWEEKNO, BYYEARDAY, BYHOUR and finer rules: calendar apps rarely write them; an
  unsupported RRULE keeps the first instance and is counted in the stats.
- Dates before a zone's last rule change use today's rule (the bar only looks at today and tomorrow).
- The task isn't on the task watchdog: a slow DNS lookup or TLS handshake can block longer than its 10 s, and the
  fetch has its own time limits. It does keep core 0's idle task (which the watchdog watches) running: `cal_sync`
  blocks for a tick after every 50 ms of work, from the fetch loop and, through the reader's tick hook, from inside
  one `cal_feed_write()` and its recurrence walks. The worst feed the budget allows takes about 0.1 s at -O2 on a
  desktop (`test_sec_review.c`), so a few seconds on the S3 at most; to be timed on the board.
