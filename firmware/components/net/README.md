# net: Wi-Fi, the API, the Remote and USB

Owner: net builder. The contract is `docs/api.md`; where this file and it disagree, api.md wins.

## Files

| Path | Builds on Linux | What |
|---|---|---|
| `proto/net_api.c` | yes | The one router for HTTP and USB: every endpoint of api.md Appendix A, the Host, rate-limit, token, scope, cookie, Origin, size, Content-Type and JSON checks, the status object and its `rev`/ETag, and the USB lines (`hello`, `call`, `status`, `pair`, `request`, `ready`). |
| `proto/net_macs.c` | yes | The Mac table (api.md 5.2): per-Mac state, stale messages, call ids, `elapsed_s`, `leaving`, the 90 s time-out, and the bar's call as their sum. |
| `proto/net_pair.c` | yes | Pairing codes, tokens (SHA-256 hashes only), scopes, back-off, and the saved table. |
| `proto/net_util.c` | yes | Rate limits, RFC 3339, the Mac's hello time rule, the DNS catch-all's answers, Wi-Fi join errors, the USB line reader. |
| `esp/net_main.c` | no | Life cycle (`net.h`). |
| `esp/net_wifi.c` | no | Station (WPA2/WPA3 Personal, open, WPA2-Enterprise PEAP/TTLS), the TinyBar-Setup access point and join flow, the scan, credentials in NVS `wifi`, a remembered Skip (`wifi/skipped`: the next start stays offline with the radio off; Set up and a working join clear it), mDNS, SNTP, the clock's source. |
| `esp/net_dns.c` | no | The setup network's DNS catch-all. |
| `esp/net_http.c` | no | esp_http_server: `/api/*` to the router on the app task (`tb_bus_exec`, 900 ms), the gzipped pages, the captive-portal 302. |
| `esp/net_usb.c` | no | The USB Serial/JTAG driver, one non-blocking writer for logs, stdout and protocol lines, and the line reader. |
| `esp/net_port_esp.c` | no | `net_port.h` on the device: identity, time, randomness, SHA-256, the token table in `nvs_sec/tokens`, the calendar service. |
| `web/remote.html`, `web/setup.html` | — | The Remote and the setup page (ports of the mock-up's), embedded gzipped (17 KB and 4 KB). No external resources. |
| `host/fakebar.c` | yes | A bar on Linux (the real router and core, simulated Wi-Fi and calendar) for testing the pages in a browser. Not part of the firmware. |
| `host/pages_*.test.js` | — | Playwright checks of both pages against the fake bar. |

## Tasks and threads

- The router (`net_api_handle`, `net_api_usb_line`, `net_api_tick`) runs only on the app task. HTTP and USB reach it with `tb_bus_exec`; if the app task can't start the job within 900 ms the client gets `503 busy` (USB: the same error line with the request's `id`).
- Wi-Fi and IP events run in the default event loop; slow work (saving credentials, mDNS, SNTP, handing the setup page's calendar address to `cal_sync_put`, scans) runs on the `net` worker task (4 KB, internal RAM, because it writes NVS).
- `usb_rx` (4 KB) reads lines; `dns` (4 KB) runs only while the setup network is up; `httpd` (6 KB). Their stacks are in PSRAM (they never write flash: the router runs on the app task), which spares 14 KB of internal RAM.
- The router's set-up (`net_api_init`: the token table, the paired count) runs only on the app task; `net_init()` waits for it rather than running it on the main task.

## Testing

```sh
cmake -S firmware/test/host -B firmware/build-host-net
cmake --build firmware/build-host-net -j && ctest --test-dir firmware/build-host-net -R net --output-on-failure
cmake --build firmware/build-host-net --target tb_fakebar
NODE_PATH=$(npm root -g) node firmware/components/net/host/pages_remote.test.js
NODE_PATH=$(npm root -g) node firmware/components/net/host/pages_setup.test.js
```

Run the fake bar by hand with `build-host-net/net/tb_fakebar --port 8080` (add `--setup` for the setup page, `--auth-none` for no pairing) and open `http://127.0.0.1:8080/`. `POST /_sim/usb` with a `@tb` line plays a Mac; `GET /_sim/state` shows the bar's toast and pairing code.

## Bring-up checklist (nothing here has run on the board yet)

1. **USB port.** Open the port on a Mac with the app's settings (raw, DTR/RTS untouched): does the bar reset? Does the `ready` line arrive after a restart? Do log lines and `@tb` replies never interleave (send `status` 50 times while logging at Debug)? With no program reading, does the UI stay smooth (the writer must drop logs, not block)? Is the USB serial number the MAC address?
2. **stdout.** LVGL warnings (printf) still appear on the port after `net_usb_init` swaps stdout for the shared writer.
3. **First start.** No saved network: TinyBar-Setup appears (open), a phone gets 192.168.4.1 as DNS, iOS and Android open the setup page on their own (captive portal: foreign Host → 302, RFC 8910 option 114). Skip (hold, Skip), then Restart: the bar comes back offline, with no TinyBar-Setup network (log `Wi-Fi was skipped: staying offline`); hold, Wi-Fi, Set up brings the QR code back.
4. **Scan.** The list fills within a few seconds, strongest first, without the phone dropping off the setup network during scans.
5. **Joins.** WPA2 Personal, WPA3 Personal (SAE), open, and a work login (PEAP/MSCHAPv2 and TTLS) each work; a wrong password says Wrong password within about 10 s; a network out of range says No signal or Network not found; a network that hands out no address says No IP address after 15 s. Note whether the phone loses the setup network when the bar moves to the office channel, and whether the page still sees "connected" in the 15 s the setup network lingers.
6. **Reconnect.** Turn the office access point off and on: the Wi-Fi icon goes crossed out and comes back, with back-off 1, 2, 5, 10, 30 s.
7. **mDNS.** `dns-sd -B _tinybar._tcp` shows the bar with the TXT record of api.md 3; a second bar becomes `tinybar-2.local` and reports it as `host`; renaming the bar in settings changes the instance name.
8. **SNTP.** The clock is set within a minute of joining; `time_source` says `ntp`; the RTC is written (main does that on `TB_EV_TIME_SET`). On a network that blocks public NTP, see open question 3.
9. **The Mac's clock.** With Wi-Fi skipped and the RTC cleared, a `hello` with `time` sets the clock; `time_source` says `mac`.
10. **HTTP.** Seven sockets: open eight connections and check the oldest is closed. A 3 KB body gets 413 without stalling. 2 KB of headers (a phone's) works.
11. **Pairing.** A Wi-Fi code pairing from a phone; USB `pair` with Wi-Fi skipped (the RNG then uses `bootloader_random_enable`); tokens survive a restart (`nvs_sec/tokens`).
12. **Memory.** Free internal heap after Wi-Fi, mDNS, the HTTP server and a calendar fetch; stack high-water marks of `httpd`, `usb_rx`, `net`, `dns`.

## Open questions (for the lead and the user)

1. **Work login certificates.** The bar doesn't check the RADIUS server's certificate for PEAP/TTLS, because office servers mostly use a private CA the bar can't know. Anyone imitating the office network could then capture the MSCHAPv2 exchange. Options: accept that and say so in the setup page, check against the certificate bundle (fails on private CAs), or let IT upload a CA.
2. **Stored secrets.** The Wi-Fi password, a work login's username and password, and the calendar address are in plain NVS (`wifi`, `nvs_sec`), unencrypted until NVS encryption is agreed (ARCHITECTURE.md "Secrets"). Anyone with a cable can read them over the USB-C port (esptool resets the chip into download mode through the port itself); see decisions.md for the choices.
3. **sdkconfig (lead):** `CONFIG_LWIP_DHCP_GET_NTP_SRV=y` and `CONFIG_LWIP_SNTP_MAX_SERVERS=3`, so an office's own NTP server (DHCP option 42) is used next to `pool.ntp.org` and `time.google.com`. The code uses them when set.
4. **API gaps (api.md):** status has no list of skipped rounds, so the Remote shows every earlier round of the set ripe; the bar's own tomato row can show a skipped one pale.
5. **Hidden networks.** The setup page lists only networks it can see (as the mock-up does); the API already accepts a hidden network's name.
