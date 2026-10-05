# net: Wi-Fi, the API, the Remote and USB

Owner: net builder. The contract is `docs/api.md`; where this file and it disagree, api.md wins.

## Files

| Path | Builds on Linux | What |
|---|---|---|
| `proto/net_api.c` | yes | The one router for HTTP and USB: every endpoint of api.md Appendix A, the Host, rate-limit, token, scope, cookie, Origin, size, Content-Type, JSON nesting and JSON checks, which token may report which call (`403 wrong_client`), the status object and its `rev`/ETag, replies kept within 8 KB, and the USB lines (`hello`, `call`, `status`, `pair`, `request`, `ready`). |
| `proto/net_macs.c` | yes | The Mac table (api.md 5.2): per-Mac state with the token of its latest message, stale messages, call ids, `elapsed_s`, `leaving`, the 90 s time-out, ending calls by token (revoke, Forget all), and the bar's call as their sum. |
| `proto/net_pair.c` | yes | Pairing codes, tokens (SHA-256 hashes only), scopes, back-off, and the saved table. A code on the screen for a device that isn't paired yet holds one of the 10 places (api.md 4.3); `pair/cancel` ends it as a failed pairing; a code typed right, Power off and Restart clear the back-off (a USB pairing doesn't). |
| `proto/net_util.c` | yes | Rate limits, RFC 3339, the Mac's hello time rule, the DNS catch-all's answers and the question's name for the log, the captive-portal check paths, the setup subnet check, the log's per-minute quota, its text escaping and paths without their query, Wi-Fi join errors, the USB line reader, the JSON depth check, and the scan that keeps log output from starting a line with `@tb `. |
| `esp/net_main.c` | no | Life cycle (`net.h`). |
| `esp/net_wifi.c` | no | Station (WPA2/WPA3 Personal, open, WPA2-Enterprise PEAP/TTLS), the TinyBar-Setup access point (scan first, then DHCP, then mode, settings and start, as ESP-IDF's captive_portal example) and join flow, the scan, credentials in NVS `wifi`, a remembered Skip (`wifi/skipped`: the next start stays offline with the radio off; Set up and a working join clear it), mDNS, SNTP, the clock's source, and the setup network's log (up, phones joining, their addresses, leaving). |
| `esp/net_dns.c` | no | The setup network's DNS catch-all: port 53 on every address, answering only the setup subnet while it's up, every query logged after its answer went out (40 a minute). |
| `esp/net_http.c` | no | esp_http_server: `/api/*` (every method) to the router on the app task (`tb_bus_exec`, 900 ms), the gzipped pages with the Host check (421), the captive-portal 302 (check paths and other hosts), which network a request came in on (the socket's own address and the peer's subnet; the peer's alone as a fallback), every setup-network request logged without its query (40 a minute for the checks and the setup page, 40 for the rest), a 3 s deadline per request (a receive override on each socket), and closing rather than draining a body it didn't read. |
| `esp/net_usb.c` | no | The USB Serial/JTAG driver, one non-blocking writer for logs, stdout and protocol lines (no log line starts with the marker), and the line reader. |
| `esp/net_port_esp.c` | no | `net_port.h` on the device: identity, time, randomness, SHA-256, the token table in `nvs_sec/tokens`, the calendar service. |
| `web/remote.html`, `web/setup.html` | — | The Remote and the setup page (ports of the mock-up's), embedded gzipped (24 KB and 5 KB since the pairing round's prompt and Paired devices list; served from flash, never copied to RAM). No external resources. |
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

Run the fake bar by hand with `build-host-net/net/tb_fakebar --port 8080` (add `--setup` for the setup page, `--auth-none` for no pairing) and open `http://127.0.0.1:8080/`. `POST /_sim/usb` with a `@tb` line plays a Mac; `GET /_sim/state` shows the bar's toast, pairing code and its label, and how many devices are paired. `POST /_sim/tap` taps the bar's screen, `/_sim/forget` is Forget all on the bar, `/_sim/restart` is what a restart does to pairing (the code ends, the back-off clears), and `/_sim/connected` puts the bar on Wi-Fi setup's Connected screen (a tap ends it).

## Bring-up checklist (nothing here has run on the board yet)

1. **USB port.** Open the port on a Mac with the app's settings (raw, DTR/RTS untouched): does the bar reset? Does the `ready` line arrive after a restart? Do log lines and `@tb` replies never interleave (send `status` 50 times while logging at Debug)? With no program reading, does the UI stay smooth (the writer must drop logs, not block)? Is the USB serial number the MAC address?
2. **stdout.** LVGL warnings (printf) still appear on the port after `net_usb_init` swaps stdout for the shared writer.
3. **First start.** No saved network: TinyBar-Setup appears (open) about 2 s after the QR code, a phone gets an address in 4.3.2.0/24 with 4.3.2.1 as its gateway and DNS server, and iOS and Android open the setup page on their own (captive portal: a check path or a foreign Host → 302; no option 114). On Android, if the sheet doesn't open by itself (with mobile data on it often doesn't), pull down the notifications and tap "Sign in to Wi-Fi network"; typing `http://4.3.2.1` in a browser works only with mobile data off. Skip (hold, Skip), then Restart: the bar comes back offline, with no TinyBar-Setup network (log `Wi-Fi was skipped: staying offline`); hold, Wi-Fi, Set up brings the QR code back.
   **What the log should show (INFO), and what each step rules out.** Steps 1 and 2 come in this order; from step 3 on, lines from the Wi-Fi events, DNS and HTTP can interleave.
   1. `net.http: listening on port 80, every address` at boot, before any Wi-Fi line (the server starts first). Missing, or `the HTTP server didn't start: ESP_ERR_...`: nothing answers port 80.
   2. `net.wifi: setup begins: scanning, then opening TinyBar-Setup at 4.3.2.1`, `net.wifi: scan: N networks`, `net.wifi: opening the setup network`, `net.wifi: setup network up: "TinyBar-Setup", open, channel 1, address 4.3.2.1 mask 255.255.255.0`, then `net.dns: listening on port 53: every name is 4.3.2.1`. An `ESP_ERR_...` in any `setup network: ...` line (`its address 4.3.2.1: ...; not opening it` keeps the network closed), or `binding port 53 failed`, is the cause. A second `setup begins: TinyBar-Setup is still opening` (or `is already up`) is core asking again, and harmless.
   3. `net.wifi: a phone joined the setup network: <MAC> (1 on it)`, then `net.wifi: the setup network gave <MAC> the address 4.3.2.2`. Joined but no address: DHCP.
   4. `net.dns: 4.3.2.2 asks A connectivitycheck.gstatic.com: answered 4.3.2.1` (Android; iOS asks `captive.apple.com`), usually with `AAAA ...: no address of that type (NODATA)`. Each line is written after the answer went out. No `asks` lines at all: the phone isn't using the bar for DNS (Private DNS set to a provider's name, or a VPN).
   5. `net.http: setup network: GET connectivitycheck.gstatic.com/generate_204 from 4.3.2.2 to 4.3.2.1: 302 to the setup page (a captive-portal check)`, then `GET 4.3.2.1/ ...: the setup page` and `GET 4.3.2.1/api/v1/setup/networks ...: API 200` once the sheet opens. Paths are logged without their query (`/x?...`). The checks and the setup page have their own 40 lines a minute, so other apps' traffic can't crowd them out. DNS lines with no HTTP line after them: the phone dropped its check after the DNS answer. That's what Android does with a private answer when its private-IP rule is on, the **most likely but unconfirmed** cause of 1.0.0's "Connected, no internet" (1.0.1 answers 4.3.2.1 for that reason). `(told by the peer's address)` on these lines means lwIP's local address couldn't be read and the fallback decided.
   6. `net.wifi: a phone left the setup network: <MAC>, reason N`: when and why the phone dropped off.

   **If Android still says "Connected, no internet",** two tests tell the causes apart:
   - **The phone's own verdict:** with USB debugging on, `adb shell dumpsys network_stack` right after joining TinyBar-Setup. Its validation log for that network has either `DNS response to the URL is private IP` (the private-IP rule; it can't appear with 4.3.2.1), or a `PROBE_HTTP http://connectivitycheck.gstatic.com/generate_204 time=...ms ret=302 ...` line (a 302 counts as a portal, so the sheet should be offered), or the probe's failure (`Probe failed with exception ...`: the HTTP check never reached the bar).
   - **The reference:** flash ESP-IDF's stock example (`examples/protocols/http_server/captive_portal`, its own SSID, 192.168.4.1) on the same bar and join it with the same phone. If that fails too, the phone (or its settings: Private DNS, a VPN, a work profile) is the cause, not TinyBar. Flash TinyBar's merged image at 0x0 again afterwards.
4. **Scan.** The list fills within a few seconds of the page opening, strongest first. It comes from the scan just before the setup network opened; while a phone is on the setup network the bar scans again only if that list is empty (at most every 30 s, log `scanning again`), since a scan takes the radio off the network's channel. The phone must not drop off the setup network while the page is open.
5. **Joins.** WPA2 Personal, WPA3 Personal (SAE), open, and a work login (PEAP/MSCHAPv2 and TTLS) each work; a wrong password says Wrong password within about 10 s; a network out of range says No signal or Network not found; a network that hands out no address says No IP address after 15 s. Note whether the phone loses the setup network when the bar moves to the office channel, and whether the page still sees "connected" in the 15 s the setup network lingers.
6. **Reconnect.** Turn the office access point off and on: the Wi-Fi icon goes crossed out and comes back, with back-off 1, 2, 5, 10, 30 s.
7. **mDNS.** `dns-sd -B _tinybar._tcp` shows the bar with the TXT record of api.md 3; a second bar becomes `tinybar-2.local` and reports it as `host`; renaming the bar in settings changes the instance name.
8. **SNTP.** The clock is set within a minute of joining; `time_source` says `ntp`; the RTC is written (main does that on `TB_EV_TIME_SET`). On a network that blocks public NTP, see open question 3.
9. **The Mac's clock.** With Wi-Fi skipped and the RTC cleared, a `hello` with `time` sets the clock; `time_source` says `mac`.
10. **HTTP.** Seven sockets: open eight connections and check the oldest is closed. A 3 KB body gets 413 with `Connection: close`, and the socket closes without the rest being read (send a 1 MB body: the bar answers at once). 2 KB of headers (a phone's) works. A client that sends one header byte a second, or a body slower than the 3 s deadline, is closed after 3 s while another phone's Remote keeps working. `OPTIONS /api/v1/status` gets the JSON 405 with `Allow: GET, POST`. `curl -H 'Host: evil.example' http://<ip>/` gets 421; on TinyBar-Setup the same gets the 302.
11. **Pairing.** A Wi-Fi code pairing from a phone; USB `pair` with Wi-Fi skipped (the RNG then uses `bootloader_random_enable`); tokens survive a restart (`nvs_sec/tokens`).
12. **Memory.** Free internal heap after Wi-Fi, mDNS, the HTTP server and a calendar fetch; stack high-water marks of `httpd`, `usb_rx`, `net`, `dns`.
13. **Log escaping.** Now that `setup/wifi` refuses a name with control characters, no known input reaches the log with a line break in it, so the escaping is a second line of defense and the host test (`test_sec_review.c`) covers it. On the board, check that it leaves ordinary log lines as they were, and that `@tb` replies still arrive whole while logging at Debug (item 1).

## Open questions (for the lead and the user)

1. **Work login certificates.** The bar doesn't check the RADIUS server's certificate for PEAP/TTLS, because office servers mostly use a private CA the bar can't know. Anyone imitating the office network could then capture the MSCHAPv2 exchange. Options: accept that and say so in the setup page, check against the certificate bundle (fails on private CAs), or let IT upload a CA.
2. **Stored secrets.** The Wi-Fi password, a work login's username and password, and the calendar address are in plain NVS (`wifi`, `nvs_sec`), unencrypted until NVS encryption is agreed (ARCHITECTURE.md "Secrets"). Anyone with a cable can read them over the USB-C port (esptool resets the chip into download mode through the port itself); see decisions.md for the choices.
3. **sdkconfig (lead):** `CONFIG_LWIP_DHCP_GET_NTP_SRV=y` and `CONFIG_LWIP_SNTP_MAX_SERVERS=3`, so an office's own NTP server (DHCP option 42) is used next to `pool.ntp.org` and `time.google.com`. The code uses them when set.
4. **API gaps (api.md):** status has no list of skipped rounds, so the Remote shows every earlier round of the set ripe; the bar's own tomato row can show a skipped one pale.
5. **Hidden networks.** The setup page lists only networks it can see (as the mock-up does); the API already accepts a hidden network's name.
