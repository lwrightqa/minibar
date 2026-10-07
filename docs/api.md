# MiniBar API, version 1

**Status:** draft for review, 2026-10-04. Written by the lead developer for the Mac app job.

This is the **contract** between the bar's firmware and every program that talks to it: the Mac app, the Remote web page the bar serves at `minibar.local`, and automations (scripts, Shortcuts, home automation). The firmware team and the Mac app team both build to this file. When either side needs something different, change this file first, then the code.

- It starts from the mock-up's section "How the Mac app talks to MiniBar" (`docs/mockup.html`, id `apiTitle`), the Remote's "Connect your Mac" card, and the entry "Proposed: the Mac app's API" in `docs/decisions.md`. Where this file differs from them, section 14 lists the change and why, so the mock-up and decisions can be brought in line.
- Items marked **Proposed** need the user's OK, as in `decisions.md`. Everything else is technical design that the team can change through this file.
- The product rules (which source wins, set aside and Show again, the Pomodoro pausing during calls and meetings, no sound during calls and meetings, the 90-second timeout) are decided in `decisions.md` and the mock-up. This file says how the API carries them; it doesn't restate every rule.

## Contents

1. [At a glance](#1-at-a-glance)
2. [Conventions](#2-conventions): versioning, content types, JSON, times, errors, limits
3. [Finding the bar (mDNS)](#3-finding-the-bar-mdns)
4. [Pairing and tokens](#4-pairing-and-tokens)
5. [Calls from the Mac: call state and heartbeat](#5-calls-from-the-mac-call-state-and-heartbeat)
6. [USB serial](#6-usb-serial)
7. [Reading the bar: info and status](#7-reading-the-bar-info-and-status)
8. [Your status, message, set aside and Show again](#8-your-status-message-set-aside-and-show-again)
9. [Pomodoro](#9-pomodoro)
10. [Settings](#10-settings)
11. [Calendar](#11-calendar)
12. [Paired devices](#12-paired-devices)
13. [The Wi-Fi setup network](#13-the-wi-fi-setup-network)
14. [Changes from the mock-up, and open questions](#14-changes-from-the-mock-up-and-open-questions)
15. [Notes for the firmware](#15-notes-for-the-firmware)
16. [Notes for the Mac app](#16-notes-for-the-mac-app)
17. [Download for Mac](#17-download-for-mac): the Mac app's zip from the bar, and `mac_app` in `info` (Proposed)

Appendices: [A, every endpoint](#appendix-a-every-endpoint) · [B, error codes](#appendix-b-error-codes)

---

## 1. At a glance

| Link | Address | Used by | Who may use it |
|---|---|---|---|
| **Wi-Fi** (HTTP) | `http://minibar.local/api/v1/…` (port 80) | The Mac app when the bar isn't plugged into the Mac, the Remote page, automations | Paired clients with a token (section 4) |
| **USB serial** | The bar's USB Serial/JTAG port (VID `0x303A`, PID `0x1001`) | The Mac app when the bar is powered from the Mac | Whatever is plugged in. The cable is the proof, so no pairing |
| **Setup network** | `http://4.3.2.1/api/v1/setup/…` on `MiniBar-Setup` | The Wi-Fi setup page, only while the bar shows its setup screens | Anyone on the setup network (section 13) |

- **One set of messages.** USB carries the same JSON as HTTP, one object per line. The firmware runs one router for both (section 15).
- **What leaves the Mac:** whether you're on a call, and optionally the name of the app using the mic or camera. To keep track of which Mac is which, the app also sends a random install ID (not a hardware ID or the computer's name) with a counter, and over USB the time, so a bar without Wi-Fi still has a clock (sections 5 and 6). **Proposed:** an optional `inputs` field saying whether the mic, the camera or both are in use; the Mac app doesn't send it until the user agrees (section 14).
- **What never leaves the bar:** the calendar's secret address, tokens (except once, to the client that pairs), the Wi-Fi password, meeting titles and locations while Show meeting titles is off, and the title of any event marked Private.

A minimal Wi-Fi session for the Mac app:

```http
POST /api/v1/call HTTP/1.1
Host: minibar.local
Authorization: Bearer tb1_w1rV1lN4jm2ohruSAozMZxVlcceAL7yS8r45__-ref4
Content-Type: application/json

{"client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "session": "q8Zr2Lx0", "seq": 1, "active": true, "app": "Slack", "call_id": 1, "elapsed_s": 0}
```

```http
HTTP/1.1 200 OK
Content-Type: application/json; charset=utf-8

{"ok": true, "device_id": "f412fa3f2a1c", "showing": "call", "screen": "on", "stale": false, "call": {"active": true, "app": "Slack", "inputs": null, "via": "wifi", "since": "2026-10-04T14:12:00-07:00", "aside": false}, "sources": {"calendar": true, "mac": true}, "heartbeat_s": 30, "timeout_s": 90, "time": "2026-10-04T14:12:00-07:00"}
```

The same over USB (the `@tb ` marker is explained in section 6):

```text
mac → bar  @tb {"cmd": "call", "id": 2, "client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "session": "q8Zr2Lx0", "seq": 1, "active": true, "app": "Slack", "call_id": 1, "elapsed_s": 0}
bar → mac  @tb {"id": 2, "ok": true, "device_id": "f412fa3f2a1c", "showing": "call", "screen": "on", "stale": false, "call": {"active": true, "app": "Slack", "inputs": null, "via": "usb", "since": "2026-10-04T14:12:00-07:00", "aside": false}, "sources": {"calendar": true, "mac": true}, "heartbeat_s": 30, "timeout_s": 90, "time": "2026-10-04T14:12:00-07:00"}
```

---

## 2. Conventions

### 2.1 Versioning

- The major version is in the path: every endpoint is under **`/api/v1/`**. Nothing is served at the mock-up's unversioned `/api/call` or `/api/status`.
- The full API version is a string, **`"api": "1.0"`**, in `GET /api/v1/info`, in the USB `hello` reply and `ready` event, and in the mDNS TXT record. The firmware version is separate (`"fw": "1.0.0"`).
- **Minor versions (1.1, 1.2…) only add things:** new endpoints, new optional request fields, new response fields, new error codes, new USB events, and new values in the fields this file marks as *open* (for example `showing`). The bar ignores request fields it doesn't know; clients ignore response fields they don't know, and handle unknown values of open fields as described next to each one.
- **Anything else is a major version:** removing or renaming a field, changing its type or meaning, or making an optional field required. That goes under `/api/v2/`, and the firmware keeps serving `/api/v1/` at least until a Mac app release that uses v2 has shipped.
- Clients say which version they speak in the USB `hello` (`"api": "1.0"`). Over HTTP, clients should send a `User-Agent` such as `MiniBarMac/1.0 (api 1.0)`; it's for the bar's logs only.

### 2.2 Content types and headers

- A request with a body must send **`Content-Type: application/json`** (`; charset=utf-8` is optional; UTF-8 is assumed). Anything else gets `415 unsupported_media_type`. This also keeps other web pages from posting to the bar with a plain HTML form: a form can't send `application/json` without a CORS preflight, and the bar answers no preflight.
- A body is one JSON object, UTF-8, with no byte-order mark. `GET` and `DELETE` have no body.
- Every API response has `Content-Type: application/json; charset=utf-8`, `Cache-Control: no-store` and `X-Content-Type-Options: nosniff`. The bar sends **no CORS headers**, so pages on other sites can't read its answers.
- Everything outside `/api/` is the Remote web page (HTML, CSS, scripts). It's served without a token and holds no data of its own; it reads everything through the API.
  - **Proposed (2026-10-05, Download for Mac):** one thing outside `/api/` isn't part of the page: the Mac app's zip at `/mac/app.zip`, also served without a token, with its own headers and plain-text errors (section 17).
- **Host check.** The bar answers only when the `Host` header is its current mDNS name (`minibar.local`, or the name it got after a conflict, section 3), its IPv4 address, or `4.3.2.1` (the setup network's address) while it's in setup mode, each with or without `:80`. Anything else gets `421 wrong_host`. This stops a web page from reaching the bar through a DNS-rebinding trick. *(2026-10-04, security review:)* the Remote page and everything else outside `/api/` get the same check, answered with a plain-text `421`. The one exception is the setup network, where any other name gets a `302` to `http://4.3.2.1/` so phones open the setup page (section 13).

### 2.3 JSON

- Names are `snake_case`.
- **Every response object has `ok`**, `true` or `false`.
- **Response fields are always present.** `null` means "doesn't apply" or "not known". Request fields marked optional may be left out; sending `null` for one means the same as leaving it out, unless the field says otherwise.
- The bar ignores request fields it doesn't know, and clients ignore response fields they don't know (2.1).
- **Times** are RFC 3339 with the bar's UTC offset, to the second: `"2026-10-04T14:12:00-07:00"`. If the bar doesn't know the time yet (no network time and the clock chip was never set), time fields are `null` and `time_source` is `"none"`.
- **Times of day** that a person types (Away's "back at") are `"HH:MM"`, 24-hour, in the bar's local time: `"13:30"`.
- **Durations** are whole seconds in fields ending `_s`. Settings in minutes end `_min`.
- **Text** is UTF-8. Before using any text, the bar removes control characters and maps typographic punctuation to the plain characters its fonts have (’ ‘ to `'`, “ ” to `"`, – — to `-`, … to `...`), because Macs and phones type curly apostrophes by default ("Don’t interrupt"). What happens to characters the fonts still can't draw depends on the field, and is said next to each one.
  - **Proposed (2026-10-04, pairing fix round): characters nobody can see** are mapped too, so pasted text never fails over something invisible. Tab, line breaks, U+2028 and U+2029, and the spaces U+2000 to U+200A, U+202F (the narrow no-break space Apple's and ICU's time formats put before AM and PM), U+205F and U+3000 become a space. The zero-width characters and marks U+200B to U+200F, U+202A to U+202E, U+2060 to U+2064, U+2066 to U+206F, U+FEFF (byte-order mark) and the variation selectors U+FE0E and U+FE0F are dropped. A letter followed by a combining accent (U+0300, U+0301, U+0302, U+0303, U+0308, U+030A or U+0327) becomes the precomposed Latin-1 letter when there is one ("e" + U+0301 to "é"), since macOS file names and some pasted text are decomposed; this is NFC limited to Latin-1, a table of about 60 pairs. The Remote's check (8.2) uses the same mapping.

### 2.4 Errors

An error has an HTTP status and this body:

```json
{"ok": false, "error": "bad_value", "message": "focus_min must be between 1 and 120.", "field": "pomodoro.focus_min"}
```

| Field | Meaning |
|---|---|
| `error` | A stable code from Appendix B. Clients decide what to do from this. |
| `message` | One English sentence for logs and developer tools. Its wording can change, so clients don't show it as is or parse it. |
| `field` | The request field at fault, as a dotted path, or `null`. |

Some errors add a field: `retry_after_s` (with `429 rate_limited` and `409 pairing_busy`, matching a `Retry-After` header), `attempts_left` (with `403 wrong_code`) and `chars` (with `400 unsupported_chars`). Over USB the same object comes back with the request's `id` (section 6).

### 2.5 Limits

| What | Limit | Over the limit |
|---|---|---|
| HTTP request body | 2,048 bytes | `413 too_large`, and the bar closes the connection rather than read the rest |
| JSON nesting (a body or a USB line) | 16 levels of objects and arrays; the deepest real request has 3 | `400 bad_json` |
| Time to send a request's headers and body | 3 seconds from its first byte | The bar closes the connection (`408` if it can still answer) |
| HTTP request headers, all together | 2,048 bytes (the firmware raises esp_http_server's 512-byte default, section 15) | `431`, from the HTTP server, possibly without a JSON body |
| Request path and query | 512 bytes | `414`, from the HTTP server |
| USB line, Mac to bar | 2,048 bytes including the `@tb ` marker, not counting the line ending | `too_large` reply (section 6) |
| USB line, bar to Mac, and any response body | 8,192 bytes | The bar never sends more |
| Requests from one IP address | 10 a second on average, bursts up to 20 | `429 rate_limited` |
| Requests without a valid token, from one IP address | 5 a second | `429 rate_limited` |
| Open HTTP connections | 7 at once; the bar closes the least recently used | Clients keep at most one open |
| Paired tokens | 10; a code on the screen holds a place (4.3) | `409 token_limit` |
| Macs the bar keeps call state for | 4; the least recently heard is dropped | — |
| `app` (call) | 1 to 64 bytes; the bar shows up to 24 characters | `400 bad_value` |
| Message text | 1 to 80 characters (the Remote's field allows 80) | `400 bad_value` |
| Client name (pairing, USB `hello`) | 1 to 32 characters | `400 bad_value` |
| Bar name (`device.name`) | 1 to 24 characters | `400 bad_value` |
| Away note | 1 to 40 characters | `400 bad_value` |
| Calendar address | up to 1,024 bytes | `400 bad_value` |
| Reply time | Within 1 second. Slow work (checking a calendar address, syncing, joining Wi-Fi) answers `202` at once and reports progress through a `GET`. | — |

### 2.6 Retries and time-outs

- Clients time out an HTTP request after **5 seconds** and a USB reply after **3 seconds**.
- `GET`, `PUT`, `PATCH`, `DELETE`, `POST /api/v1/call` and `POST /api/v1/pair/cancel` are safe to repeat: each carries the whole state it sets (a repeated `pair/cancel` answers `409 not_pairing` and changes nothing, counting no second failed pairing). The other `POST`s (Pomodoro actions, message, set aside, `pair/start` and `pair`) aren't: a repeated Skip skips twice, a repeated `pair` costs a try. Clients repeat those only when the connection failed before the request was sent.
- The bar may close an idle connection at any time. When a request fails because of that, the client retries once on a new connection.

---

## 3. Finding the bar (mDNS)

The bar announces itself with mDNS (Bonjour) on the office Wi-Fi.

- **Host name:** `minibar.local`, with an IPv4 address record. If another device already has that name (two MiniBars in one office), the bar takes the next free name, `minibar-2.local` and so on (ESP-IDF's mDNS component renames on a conflict; check the exact form on the device). The bar reports the name it actually has as `host` in `info`, `hello` and `status`, and shows it wherever the mock-up shows `minibar.local` (the Connected screen and the Wi-Fi menu).
- **Service:** `_minibar._tcp`, port 80. The instance name is the bar's name (`device.name` in settings). **Proposed:** the default name is "MiniBar" plus the last four characters of its ID, for example **"MiniBar 2A1C"**, so bars in one office can be told apart.
- **TXT record:**

  | Key | Example | Meaning |
  |---|---|---|
  | `api` | `1.0` | API version |
  | `id` | `f412fa3f2a1c` | `device_id`: 12 lowercase hex digits, the bar's Wi-Fi MAC address |
  | `fw` | `1.0.0` | Firmware version |
  | `path` | `/api/v1` | Where the API is |
  | `auth` | `bearer` | `bearer` when pairing is required, `none` when it isn't (section 4) |

  As `dns-sd -L "MiniBar 2A1C" _minibar._tcp` on a Mac would show it:

  ```text
  MiniBar\0322A1C._minibar._tcp.local. can be reached at minibar.local.:80
   api=1.0 id=f412fa3f2a1c fw=1.0.0 path=/api/v1 auth=bearer
  ```

- The bar also announces `_http._tcp`, port 80, TXT `path=/`, so general tools can find the Remote page.
- **How clients find their bar:** browse `_minibar._tcp`, pick the instance whose `id` matches the paired `device_id`, and resolve it. Use `minibar.local` only to find a bar the first time. Keep the last address that worked, as a fallback for networks where multicast is unreliable.
- **Some office networks block multicast or traffic between devices** ("client isolation"). Then the bar can't be reached over Wi-Fi at all; USB still works. The Mac app says so plainly rather than retrying forever.
- IPv4 only in version 1.

---

## 4. Pairing and tokens

**Decided (2026-10-04): the user approved pairing as proposed.** `decisions.md` had left this open ("the Remote and the API have no PIN"); this section is the answer. It covers everything below, including pairing over USB without a code, the Mac app's `call` scope, `pair/cancel` and a code holding a place.

### 4.1 Why

MiniBar lives on an **open office network**. Everyone in the office is on the same Wi-Fi, and so are their laptops, phones and scripts. Without pairing, anyone on it could open `minibar.local` or send one `curl` command to set your bar to Available during a focus session, show a fake On a call, stop your Pomodoro, read your meeting times, or replace your calendar address. Usually by accident or as a prank, but it would make the bar untrustworthy, and the people around you would stop believing it.

Pairing ties control of the bar to someone who can **see its screen** at that moment, which in practice means you at your desk. **USB needs no pairing:** plugging in a cable takes the same physical presence.

**If the user doesn't want pairing,** the firmware ships with `"auth": "none"` in `info` and the TXT record: tokens aren't required (any sent are ignored), and clients that read `auth` skip pairing. Both sides of this contract work either way.

### 4.2 How it works

1. The client asks to pair: `POST /api/v1/pair/start` with its name, kind and scope. The bar shows a **6-digit code** on its screen, with the client's name, for **2 minutes**. The reply carries a `pairing_id`.
2. You read the code off the bar and type it into the client.
3. The client sends `POST /api/v1/pair` with the `pairing_id` and the code. The bar answers **once** with a long random **token** and confirms on screen ("Paired · Mac").
4. The client keeps the token (the Mac app in the **Keychain**; the Remote page as a cookie the bar sets) and sends it with every request: `Authorization: Bearer <token>`.

The `pairing_id` ties the code to the client that asked for it, so someone who reads the code over your shoulder can't use it from another device.

**Over USB** the Mac app can get a token without a code (`pair`, section 6.6), so a bar that has been plugged into the Mac once also works over Wi-Fi afterwards.

### 4.3 Tokens

- **Format:** `tb1_` followed by 43 characters of base64url: 32 random bytes from the ESP32-S3's hardware random number generator, 47 characters in all, for example `tb1_w1rV1lN4jm2ohruSAozMZxVlcceAL7yS8r45__-ref4`. The prefix makes tokens easy to spot in logs and for secret scanners.
- **The bar keeps only a SHA-256 hash** of each token, with a public `token_id` (8 hex digits), the client's name, kind, scope and `client` ID, when and how it was paired, and when and from which address it was last used. It never shows a token again after the pairing reply.
- **Tokens don't expire.** A token stops working when it's revoked (section 12), when the same `client` pairs again (the new token replaces the old one), or after a factory reset. Setting up Wi-Fi again keeps them.
- **At most 10 tokens.** Pairing an eleventh is refused (`409 token_limit`) until one is revoked.
  - **Decided (2026-10-04, pairing fix round): a code on the screen holds a place.** While a code shows for a device that isn't paired yet, it counts as one of the 10, so the code can always work. Meanwhile pairing another new device over USB (`pair`, 6.6) is refused with `token_limit` if it would take that place; the cable still works without a token. A device that's paired already replaces its own token, so it always has room. Before this, a USB pairing made while another device's code showed could take the last place and leave 11 tokens.

### 4.4 Scopes

| Scope | For | Allows |
|---|---|---|
| `call` | The Mac app | `GET /api/v1/info`, `GET /api/v1/status`, `POST /api/v1/call`, `DELETE /api/v1/clients/self` |
| `full` | The Remote page, automations | Everything |

The Mac app pairs with the `call` scope. If its token ever leaked, the most anyone could do with it is fake a call, and the Remote's list shows which device's token it was. Any other endpoint with a `call` token gets `403 wrong_scope`.

### 4.5 Sending the token

- **Apps and scripts:** the header `Authorization: Bearer tb1_…`.
- **The Remote page:** a cookie the bar sets when the page pairs with `"cookie": true`: `tb_token=<token>; HttpOnly; SameSite=Strict; Path=/api/; Max-Age=31536000` (no `Secure` flag, since the bar serves plain HTTP). Scripts on the page can't read it, and Safari keeps a cookie the server sets much longer than storage a page sets for itself, so a phone doesn't have to pair again every week. When a request authenticated by the cookie has an `Origin` header, it must be `http://` plus one of the accepted hosts (2.2); otherwise `403 bad_origin`. An `Origin` header the bar can't read (over 255 bytes) or that's empty counts as another origin, never as none.
- A missing, unknown or revoked token gets `401 unauthorized` with `WWW-Authenticate: Bearer realm="MiniBar"`. The client's answer is to pair again.

```json
{"ok": false, "error": "unauthorized", "message": "Pair with this MiniBar first.", "field": null}
```

### 4.6 `POST /api/v1/pair/start`

No token needed.

| Field | Type | Required | Meaning |
|---|---|---|---|
| `name` | string, 1 to 32 characters | no | A label for the pairing screen and the paired-devices list, such as "iPhone", or a name the user gave the Mac. Left out, or if its characters can't all be drawn, the bar uses "Mac", "Phone" or "Script" (from `kind`). The Mac app sends none unless the user names it, since it never sends the computer's name (`docs/mac-app.md`). |
| `kind` | `"mac"`, `"remote"` or `"automation"` | yes | What the client is, for the list and the screen. *Open:* clients send one of these; the bar labels unknown future kinds "Device". |
| `scope` | `"call"` or `"full"` | yes | See 4.4. |
| `client` | string, 8 to 64 of `A-Z a-z 0-9 -` | no | The client's stable ID (the Mac app's install ID). When pairing succeeds, an older token with the same `client` is revoked, so re-pairing doesn't pile up tokens. A token paired with a `client` reports calls only for that client (5.2). |

```http
POST /api/v1/pair/start HTTP/1.1
Host: minibar.local
Content-Type: application/json

{"kind": "mac", "scope": "call", "client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60"}
```

The Remote page pairing itself:

```json
{"name": "iPhone", "kind": "remote", "scope": "full"}
```

```http
HTTP/1.1 202 Accepted

{"ok": true, "pairing_id": "d407580a9215e992", "pairing_seq": 7, "expires_in_s": 120, "code_length": 6, "attempts": 3}
```

`pairing_seq` is this code's number, counted from the bar's start-up. `GET /api/v1/info` carries the same number while the code is on the screen (7.1), so the device that asked can tell its own code from a later one, even one that took its place within a 2-second poll, without anyone learning the `pairing_id`. The Remote's prompt uses it; the Mac app can ignore it. *(Added 2026-10-05, from the review of the firmware's alignment. Not secret: it says nothing but which code is up.)*

| Error | When |
|---|---|
| `409 pairing_busy` | Another code is on the screen. `retry_after_s` says when it expires. |
| `409 token_limit` | 10 tokens already exist. Revoke one first (section 12). Checked here, and the code then holds a place until it ends (4.3), so no one is shown a code that can't work. |
| `429 rate_limited` | Too many pairings ended without success (4.9). `retry_after_s` says when to try again. |
| `409 in_setup` | The bar is on its Wi-Fi setup screens: over USB on any of them, and over Wi-Fi on the Connected screen (the bar is back on the office Wi-Fi there). On the QR code, Connecting and Couldn't connect screens the bar can't be reached over Wi-Fi at all, so a device there gets no answer, not this error. |
| `503 busy` | The bar is powering off (`retry_after_s` 1): no code is shown that the power-off would take away a moment later. Clients show "didn't answer", as for no reply. |

```json
{"ok": false, "error": "pairing_busy", "message": "Another device is pairing. Try again in 74 seconds.", "field": null, "retry_after_s": 74}
```

### 4.7 `POST /api/v1/pair`

No token needed.

| Field | Type | Required | Meaning |
|---|---|---|---|
| `pairing_id` | string | yes | From `pair/start`. |
| `code` | string | yes | The 6 digits on the bar. Spaces and dashes are ignored, so "482 913" works. |
| `cookie` | boolean | no | `true` (the Remote page only): the bar sets the `tb_token` cookie instead of returning the token in the body. |

```http
POST /api/v1/pair HTTP/1.1
Host: minibar.local
Content-Type: application/json

{"pairing_id": "d407580a9215e992", "code": "482913"}
```

```http
HTTP/1.1 200 OK

{"ok": true, "token": "tb1_w1rV1lN4jm2ohruSAozMZxVlcceAL7yS8r45__-ref4", "token_id": "74d8a526", "scope": "call", "device_id": "f412fa3f2a1c", "name": "MiniBar 2A1C", "host": "minibar.local"}
```

With `"cookie": true` the body has `"token": null` and the response carries `Set-Cookie: tb_token=…`:

```json
{"ok": true, "token": null, "token_id": "27110499", "scope": "full", "device_id": "f412fa3f2a1c", "name": "MiniBar 2A1C", "host": "minibar.local"}
```

| Error | When |
|---|---|
| `403 wrong_code` | The code doesn't match. `attempts_left` says how many tries this code has left; at 0 the pairing ends. |
| `409 not_pairing` | No code is on the screen for this `pairing_id`: it expired, was canceled on the bar, was already used, or ran out of tries. Start again. |
| `409 token_limit` | A safeguard only, since the code holds a place (4.3): the bar has no room for this device. The pairing ends ("Pairing canceled" on the bar) without counting as a failed pairing. |
| `429 rate_limited` | More than one `pair` request a second (`pair/cancel` counts too). `retry_after_s` is 1, and the Mac app waits exactly that long. |

```json
{"ok": false, "error": "wrong_code", "message": "That code doesn't match. 2 tries left.", "field": "code", "attempts_left": 2}
```

**Canceling: `POST /api/v1/pair/cancel`** (2026-10-04, pairing fix round). No token needed. The device that asked takes its code off the bar, so a person who changes their mind doesn't hold up every other device for 2 minutes. The Remote's Cancel and the Mac app's Back or Cancel on its Wi-Fi page call it. Only the `pairing_id` from `pair/start` works, so no one else can cancel your code. The bar ends the pairing as a tap would ("Pairing canceled"), and it **counts as a failed pairing** (4.9), so canceling and asking again can't be used to get more guesses. Safe to repeat.

| Field | Type | Required | Meaning |
|---|---|---|---|
| `pairing_id` | string | yes | From `pair/start`. |

```json
{"ok": true}
```

| Error | When |
|---|---|
| `409 not_pairing` | No code is on the screen for this `pairing_id` (it already ended). Nothing to do. |
| `429 rate_limited` | Shares `pair`'s limit of one request a second (`retry_after_s` 1). |

### 4.8 On the bar

These screens are drawn in the mock-up's pairing round and built into the firmware (decisions.md "Pairing", approved by the user on 2026-10-04):

- **Pairing screen:** on a dark surface (#0E1013, like the menus), since it isn't a status. Kicker "PAIRING · MAC" (the client's name, or its kind's word when none was sent or it can't be drawn: "Mac", "Phone" for `remote`, "Script" for `automation`), the code as the headline in tabular digits ("482 913"), and a sub line that names the device: "Type it on your Mac · tap to cancel", "Type it on your phone · tap to cancel", otherwise "Type this code on that device · tap to cancel". In the info column, "Code expires in" over an m:ss countdown, with the bar's name ("MiniBar 2A1C") as the foot; the progress bar fills as the 2 minutes run out.
- **The 2 minutes count from when the code appears on the screen.** It waits for the power screens to finish (the splash, Keep holding), and the countdown and `retry_after_s` wait with it. It never appears on the Wi-Fi setup screens, Connected included (`409 in_setup`).
- It **wakes a dark screen** (someone is pairing right now), **replaces an open menu** and the toast, and **holds a ringing alarm**: the Pomodoro keeps waiting, and when pairing ends the waiting screen chimes and flashes once. A phase that ends while the code shows waits the same way. A change made underneath (a call, a meeting, the Remote) shows after the pairing's own confirmation.
- **No dead ends:** a tap, swipe, hold or BOOT cancels it ("Pairing canceled"); a PWR press cancels it and turns the screen off as usual; a flip cancels it and does what a flip always does ("Pairing canceled · Focus started"). A touch that began before the code appeared is ignored. It also ends on success ("Paired · Mac"), after 2 minutes ("Pairing timed out"), after 3 wrong codes ("Pairing canceled · wrong code"), when the device that asked cancels (`pair/cancel`, "Pairing canceled"), when Wi-Fi setup starts, and at Power off or Restart.
- **USB pairing** (section 6.6) has no code screen. If another device's code is on the bar, that code stays up and valid, `info.pairing` stays `"showing"`, and "Paired · Mac · over USB" shows once that pairing ends.
- **Forgetting devices on the bar:** the Wi-Fi menu is Network, **Devices**, Set up again, Back. Devices reads "3 paired" ("Full" at 10) and opens a confirmation: the paired devices' names, **Forget all** (a second, deliberate tap: one in its first 600 ms is ignored), and Keep, which goes back. With nothing paired the tile stays, reads "None" with where to pair as its foot ("pair at" over the bar's `host`, for example `minibar.local`; if the host doesn't fit the tile on one line, as `minibar-2.local` doesn't, the IP address; if that doesn't fit either, "pair at its" over "IP address"; and "set up Wi-Fi" over "to pair" when it's offline), and is read-only. The Remote lists each device with a Remove button (section 12). Neither affects USB.

### 4.9 Rate limits

- **One code at a time.** `pair/start` while a code is showing gets `409 pairing_busy`.
- **A code lasts 2 minutes, works once, and allows 3 tries.** The third wrong code ends it.
- **Back-off:** after **two** pairings in a row end without success (timed out, canceled on the bar or by the device with `pair/cancel`, or out of tries), `pair/start` is refused for 30 seconds; each further failure doubles the wait (1, 2, 4 minutes…) up to **1 hour**. A successful pairing resets it. The first failure costs nothing, so a typo doesn't make you wait. This state is kept in memory; a restart clears it.
- **`POST /api/v1/pair`** and **`POST /api/v1/pair/cancel`** are limited to one request a second in total.
- **Why that's enough:** each guess has a 1 in 1,000,000 chance. With 3 tries per code and at most one code an hour once the back-off tops out, someone guessing gets 72 tries a day, about a 1 in 14,000 chance a day, and every code they ask for **lights up a pairing screen on your bar**, which you'd notice the same day.

### 4.10 What pairing doesn't protect against

- **Plain HTTP:** someone who can capture the Wi-Fi traffic (for example, anyone with the password of a WPA2-Personal network, or the network's administrators) could read a token or your status in transit. The `call` scope limits what a stolen Mac token can do. HTTPS with a certificate pinned at pairing is a possible later step (section 14).
- **Someone at your desk** can pair, just as they can tap the bar.
- **USB:** any program on the Mac the bar is plugged into can talk to the bar.

---

## 5. Calls from the Mac: call state and heartbeat

`POST /api/v1/call`, scope `call` or `full`. Over USB: `"cmd": "call"` (section 6).

### 5.1 Request

| Field | Type | Required | Meaning |
|---|---|---|---|
| `client` | string, 8 to 64 of `A-Z a-z 0-9 -` | yes | This Mac app install's ID: a UUID the app makes once and keeps. The **same on USB and Wi-Fi**, so switching links never leaves a stale call behind. |
| `session` | string, 1 to 16 of `A-Z a-z 0-9` | no (the Mac app always sends it) | Random, new each time the app starts. |
| `seq` | integer, 1 to 4294967295 | no (the Mac app always sends it) | Goes up by 1 with every call message in a session. |
| `active` | boolean | yes | `true` while the Mac is on a call (its microphone or camera is in use). |
| `app` | string, 1 to 64 bytes, or `null` | no | A short name for the app using the mic, for example "Slack", "Zoom", "FaceTime" or "Chrome" (a browser sends its own name, never the website). Left out or `null` when it isn't known, for camera-only calls, for apps not on the Mac app's list of call apps, or when the user turned names off (`docs/mac-app.md`). |
| `inputs` | array of `"mic"`, `"camera"` | no, **Proposed** | Which are in use. Not sent until the user agrees (section 14). The bar reports it in `status` but doesn't show it; no screen uses it yet. |
| `call_id` | integer, 1 to 4294967295 | no | Changes for each new call. Lets the bar tell back-to-back calls apart even if the "call ended" message was lost. |
| `elapsed_s` | integer, 0 to 86400 | no | How long the call has been on, by the Mac's count. Used when the bar first learns of a call (for example after it restarted), so the duration it shows is right. |
| `leaving` | boolean | no | `true` when the app is quitting or the Mac is going to sleep, restarting or shutting down. Requires `"active": false`. |

### 5.2 Rules

- **Send on change, repeat every 30 seconds.** The app sends a message as soon as its state changes, and repeats its full current state every `heartbeat_s` (30) seconds while it has a link to the bar, **whether or not it's on a call**. That repeat is the heartbeat. Each message carries the whole state, so the one after a lost message puts everything right.
- **Connected:** the bar counts a Mac as connected from its first call message until `timeout_s` (90) seconds after its last one, or until a `leaving` message. While at least one Mac is connected, the info column's status row shows the Mac icon, and the Remote says "Connected over USB · heard from it just now". Only `call` messages count as heartbeats (and USB `hello`); `GET /api/v1/status` doesn't.
- **When the heartbeats stop:** after **90 seconds** with no call message from a Mac, the bar ends that Mac's call, marks it not connected (the Remote says "Not connected · last heard 2:04 PM"), and, if the call was on screen, drops to the next source down with the toast "Lost contact with your Mac · call ended". So On a call can never get stuck.
- **A new call** starts when a Mac goes from not active to active, or reports a different `call_id` while active. A new call takes over the screen even if the previous call was set aside, pauses a running Pomodoro and mutes sound, as `decisions.md` says. Its start time is the bar's clock minus `elapsed_s`.
- **The same call repeating** keeps its start time; the app name is updated if it changed.
- **`"active": false`** ends that Mac's call. When no Mac has a call left, the bar's call ends: it goes back to In a meeting if a calendar meeting is on, otherwise to your own status, with the toast "Call ended".
- **`"leaving": true`** ends that Mac's call at once and marks it not connected, without the "Lost contact" toast.
- **Calls from your Mac turned off** on the Remote: the bar still records each Mac's state and heartbeat, so the Remote shows the Mac as connected, and turning the switch back on during a call shows the call at once. The reply is a normal `200` with `"sources": {"mac": false}` and `showing` other than `"call"`. *(The mock-up answered `403 calls_off`; see section 14.)*
- **Screens that a call doesn't interrupt** (Wi-Fi setup, power screens, an open menu) and a **dark screen**: the bar records the call and shows it once they close or the screen is woken, as `decisions.md` says. The reply says so through `showing` and `screen`.
- **Order:** the bar ignores a message from a `client` whose `session` matches the last one it accepted and whose `seq` is not higher. It still replies `200`, with `"stale": true` and nothing changed. A new `session` is always accepted. This stops a late Wi-Fi request from undoing a newer USB one. Messages without `session` and `seq` (scripts) are always accepted.
- **Several Macs:** the bar keeps each Mac's state separately, by `client`, for up to 4 Macs. The bar's call is on while any Mac reports one. The app name and `via` shown are those of the call that started most recently. Set aside and Show again apply to the bar's call as a whole. When the 4 are taken, a Mac without a call is dropped first.
- **Who may report for a `client`** *(2026-10-04, security review)*: a call over Wi-Fi is tied to the token it came with, so one device can't end or restart another's call.
  - A token paired with a `client` (the Mac app's always is) reports only for that `client`.
  - A token paired without one (a script) reports for one `client` at a time, and only one that no other paired device holds and that isn't the latest from the Mac on USB. Reporting for a new `client` drops its previous one, and that call ends.
  - Anything else gets `403 wrong_client`, with `field` `"client"`, and changes nothing. For the Mac app this can only mean its token was paired for another install ID (the app's settings were reset while the Keychain kept the token), so the app reads it like a `401` (section 16) and pairs again. It may first send `DELETE /api/v1/clients/self` with the refused token (12.3): the token is still valid, only tied to another `client`, so the bar revokes it and toasts "Removed Mac" as for any self-unpairing, and the stale token doesn't keep one of the 10 places.
  - USB needs no token: a USB message is accepted for any `client`, and that Mac's call then belongs to the cable until its token reports for it again over Wi-Fi.
- **Links:** the app uses USB when the bar answers on it and Wi-Fi otherwise, one link at a time. Switching needs nothing special: same `client`, next `seq`. `via` follows the link of the Mac's latest message.
- **After the bar restarts** it has forgotten every Mac (this state isn't saved). The next heartbeat brings the call back within 30 seconds, at once over USB thanks to the `ready` event (6.7), with its duration from `elapsed_s`. That's how "after a restart or power-on, the bar picks up a call that's still going" works for calls.
- **The app name on the bar:** up to 24 characters show ("From your Mac · Slack"); a longer name is cut to 23 characters and "…". If the name has characters the bar's fonts can't draw, the bar shows "From your Mac" without it, but still reports the name in `status`.

### 5.3 Reply

`200` with:

| Field | Meaning |
|---|---|
| `device_id` | Which bar answered (7.1). The app checks it against the bar it paired with, in case the address now belongs to another bar. |
| `showing` | What the bar shows now (7.3): `"call"`, `"meeting"`, `"own"` or `"setup"`. |
| `screen` | `"on"` or `"dark"`. |
| `stale` | `true` if this message was ignored as out of order (5.2). |
| `call` | The bar's call, the same object as in `status` (7.3). |
| `sources` | `{"calendar": bool, "mac": bool}`: the Remote's two Automatic status switches. |
| `heartbeat_s`, `timeout_s` | The intervals the bar wants now (30 and 90). The app uses the values from the latest reply, so the bar can tune them. `timeout_s` is always at least 3 times `heartbeat_s`. |
| `time` | The bar's clock. |

The Mac app can label its menu from this: `showing` is `"call"` → "Showing on MiniBar"; `call.aside` → "Set aside on MiniBar"; `sources.mac` is `false` → "Calls from your Mac is off on MiniBar".

### 5.4 Examples

A call starts:

```json
{"client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "session": "q8Zr2Lx0", "seq": 41, "active": true, "app": "Slack", "call_id": 7, "elapsed_s": 0}
```

```json
{"ok": true, "device_id": "f412fa3f2a1c", "showing": "call", "screen": "on", "stale": false, "call": {"active": true, "app": "Slack", "inputs": null, "via": "wifi", "since": "2026-10-04T14:12:00-07:00", "aside": false}, "sources": {"calendar": true, "mac": true}, "heartbeat_s": 30, "timeout_s": 90, "time": "2026-10-04T14:12:00-07:00"}
```

The heartbeat 30 seconds later, same call (you tapped the bar meanwhile, so it's set aside):

```json
{"client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "session": "q8Zr2Lx0", "seq": 42, "active": true, "app": "Slack", "call_id": 7, "elapsed_s": 30}
```

```json
{"ok": true, "device_id": "f412fa3f2a1c", "showing": "own", "screen": "on", "stale": false, "call": {"active": true, "app": "Slack", "inputs": null, "via": "wifi", "since": "2026-10-04T14:12:00-07:00", "aside": true}, "sources": {"calendar": true, "mac": true}, "heartbeat_s": 30, "timeout_s": 90, "time": "2026-10-04T14:12:30-07:00"}
```

The call ends during a calendar meeting:

```json
{"client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "session": "q8Zr2Lx0", "seq": 43, "active": false}
```

```json
{"ok": true, "device_id": "f412fa3f2a1c", "showing": "meeting", "screen": "on", "stale": false, "call": {"active": false, "app": null, "inputs": null, "via": null, "since": null, "aside": false}, "sources": {"calendar": true, "mac": true}, "heartbeat_s": 30, "timeout_s": 90, "time": "2026-10-04T14:31:10-07:00"}
```

A heartbeat with no call (the app sends this every 30 seconds while idle):

```json
{"client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "session": "q8Zr2Lx0", "seq": 44, "active": false}
```

```json
{"ok": true, "device_id": "f412fa3f2a1c", "showing": "own", "screen": "dark", "stale": false, "call": {"active": false, "app": null, "inputs": null, "via": null, "since": null, "aside": false}, "sources": {"calendar": true, "mac": true}, "heartbeat_s": 30, "timeout_s": 90, "time": "2026-10-04T14:31:40-07:00"}
```

A call with Calls from your Mac turned off on the Remote (no app name shared):

```json
{"client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "session": "q8Zr2Lx0", "seq": 45, "active": true, "call_id": 8, "elapsed_s": 0}
```

```json
{"ok": true, "device_id": "f412fa3f2a1c", "showing": "own", "screen": "on", "stale": false, "call": {"active": true, "app": null, "inputs": null, "via": "wifi", "since": "2026-10-04T15:02:00-07:00", "aside": false}, "sources": {"calendar": true, "mac": false}, "heartbeat_s": 30, "timeout_s": 90, "time": "2026-10-04T15:02:00-07:00"}
```

With `inputs` (**Proposed**, only once the user agrees):

```json
{"client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "session": "q8Zr2Lx0", "seq": 46, "active": true, "app": "Zoom", "inputs": ["mic", "camera"], "call_id": 9, "elapsed_s": 4}
```

The Mac goes to sleep:

```json
{"client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "session": "q8Zr2Lx0", "seq": 47, "active": false, "leaving": true}
```

```json
{"ok": true, "device_id": "f412fa3f2a1c", "showing": "own", "screen": "on", "stale": false, "call": {"active": false, "app": null, "inputs": null, "via": null, "since": null, "aside": false}, "sources": {"calendar": true, "mac": true}, "heartbeat_s": 30, "timeout_s": 90, "time": "2026-10-04T18:05:12-07:00"}
```

A late message that arrived after a newer one (ignored):

```json
{"ok": true, "device_id": "f412fa3f2a1c", "showing": "call", "screen": "on", "stale": true, "call": {"active": true, "app": "Slack", "inputs": null, "via": "usb", "since": "2026-10-04T14:12:00-07:00", "aside": false}, "sources": {"calendar": true, "mac": true}, "heartbeat_s": 30, "timeout_s": 90, "time": "2026-10-04T14:13:02-07:00"}
```

Errors:

```json
{"ok": false, "error": "bad_request", "message": "\"active\" is required and must be true or false.", "field": "active"}
```

```json
{"ok": false, "error": "bad_value", "message": "\"leaving\" needs \"active\": false.", "field": "leaving"}
```

```json
{"ok": false, "error": "bad_json", "message": "The body isn't a JSON object.", "field": null}
```

---

## 6. USB serial

When the bar is powered from the Mac, the Mac app talks to it over the same USB cable. This also works when the bar has no Wi-Fi.

### 6.1 The port

- The board's USB-C goes to the ESP32-S3's built-in **USB Serial/JTAG controller**: **VID `0x303A`** (Espressif), **PID `0x1001`**, product string "USB JTAG/serial debug unit". It's a standard CDC-ACM serial port, so macOS needs no driver and names it `/dev/cu.usbmodem…`. Use the `cu.` device, not `tty.` (opening `tty.` can wait for a carrier signal that never comes).
- Every ESP32-S3 board in this mode, and the chip's own ROM download mode, has the **same VID and PID**. Only the `hello` reply (`"device": "MiniBar"`) says it's a MiniBar.
- **Unverified, check on the board:** the USB serial number appears to be the chip's MAC address (for example `F4:12:FA:3F:2A:1C`), which would match `device_id`. Clients may use it to recognize a known bar before `hello`, but `hello` is what counts.
- When the bar is powered off on USB (a deep sleep, per `decisions.md`), the port disappears. A restart (from the Power menu, after flashing, or by the watchdog) makes it disappear and come back about a second later.

### 6.2 Finding it on the Mac

1. Watch for serial ports with IOKit: match `IOSerialBSDClient` services (`kIOSerialBSDServiceValue`), and for each one search its parents (`IORegistryEntrySearchCFProperty` with `kIORegistryIterateRecursively | kIORegistryIterateParents`) for `idVendor` = `0x303A` and `idProduct` = `0x1001`. Take the path from `kIOCalloutDeviceKey`. Register for arrival and removal notifications (`IOServiceAddMatchingNotification` with `kIOFirstMatchNotification` and `kIOTerminatedNotification`) rather than polling.
2. Open the port and send `hello` (6.6). If a `hello` reply with `"device": "MiniBar"` comes back, use the port.
3. If nothing answers within 10 seconds (one `hello` every 2 seconds), close the port and leave that device alone until it's unplugged and plugged in again. It's another ESP32-S3 board, or a MiniBar in download mode being flashed.

### 6.3 Port settings and the reset lines

- Raw mode (`cfmakeraw`), 8 data bits, no parity, 1 stop bit, no flow control, `CLOCAL` and `CREAD` set. The baud rate doesn't matter (USB Serial/JTAG isn't a real UART); set 115200.
- **Don't touch DTR or RTS.** On the ESP32-S3's USB Serial/JTAG port, the host's DTR and RTS lines drive the chip's reset and boot-mode logic; that's how esptool resets it. The app never changes them after opening the port (no `TIOCMSET`, `TIOCMBIS`, `TIOCMBIC`, `TIOCSDTR` or `TIOCCDTR`), and clears `HUPCL` so that closing the port doesn't drop them. **Unverified, check on the board:** whether macOS's ordinary open and close reset the bar. If they do, the app must open the port once and keep it open, and the firmware team should know.
- **Proposed:** the app opens the port **exclusively** (`TIOCEXCL`), and its menu has **Pause USB** for flashing. Two programs reading one serial port split its data unpredictably, which would corrupt a firmware flash; with exclusive access, esptool fails clearly ("Resource busy") instead. Pause USB closes the port until it's chosen again or the bar is unplugged and plugged in again; the app falls back to Wi-Fi meanwhile.
- The app **reads continuously**, even when it has nothing to send, so the bar's output never backs up.

### 6.4 Lines and the `@tb ` marker

The port also carries the firmware's log output (ESP-IDF logs, the boot ROM's messages, crash reports). Protocol messages are told apart by a marker.

- **Lines** are UTF-8 and end with LF (`\n`). A CR before the LF is allowed and ignored by both sides (ESP-IDF turns `\n` into `\r\n` on its console by default).
- **Every protocol line, in both directions, starts with the 4 characters `@tb `** (at sign, t, b, space), followed by **one JSON object** and the line ending. JSON never needs a raw line break inside it.
- **The bar guarantees:** each protocol line is written whole, never split by log output, and always starts at the beginning of a line (if the previous output didn't end with a line break, the bar writes one first). The firmware never writes `@tb ` at the start of any other line, and never logs tokens, the calendar address, Wi-Fi passwords or raw protocol lines.
- **The Mac app:** splits what it reads at each LF, drops a trailing CR, strips any ANSI color codes at the start of the line, and treats a line that then starts with `@tb ` as a protocol message. Everything else is log output: the app may show it in a debug view, and otherwise drops it. A protocol line that isn't valid JSON is dropped too.
- **The bar ignores lines from the Mac without the marker** (no reply), so a person typing in a terminal does no harm, and a later debug console can use them.
- **One reply per message, in order.** Every marker line from the Mac gets exactly one reply line, in the order received, including lines that couldn't be parsed. The Mac may send up to 4 messages before the first reply, but the Mac app normally waits for each reply.
- **`id`:** the Mac may put an integer `id` (1 to 2147483647) in any message; the reply carries the same `id`. A reply to a line that couldn't be parsed has `"id": null`.
- **Too long:** a line over 2,048 bytes is thrown away up to its line ending and answered with `too_large`.
- **Events:** the bar may also send marker lines that aren't replies. They have an `event` field and no `id`. Version 1 has one, `ready` (6.7). *Open:* clients ignore events they don't know.

A stretch of the port's output, as the Mac app sees it (the arrows aren't part of the data):

```text
bar → mac  I (24312) wifi: connected to Office-WiFi, ip 10.0.4.42
mac → bar  @tb {"cmd": "status", "id": 3}
bar → mac  W (24890) cal: sync took 4.2 s
bar → mac  @tb {"id": 3, "ok": true, "device_id": "f412fa3f2a1c", "rev": 1843, "time": "2026-10-04T14:24:05-07:00", "time_source": "ntp", "showing": "own", "screen": "on", "own": {"status": "busy", "since": "2026-10-04T14:01:00-07:00", "previous": "busy"}, "message": {"text": null, "set_at": null}, "away": {"back_at": null, "note": null}, "call": {"active": false, "app": null, "inputs": null, "via": null, "since": null, "aside": false}, "meeting": {"active": false, "aside": false, "current": null, "next": null, "left_today": null}, "pomodoro": {"state": "ready", "phase": "focus", "round": 1, "rounds": 4, "length_s": 1500, "remaining_s": 1500, "ends_at": null, "paused_by": null, "ringing": false, "done_today": 0, "focused_today_s": 0}, "sources": {"calendar": false, "mac": true}, "macs": [{"client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "name": "Mac", "via": "usb", "connected": true, "active": false, "last_heard": "2026-10-04T14:24:01-07:00"}], "calendar": {"saved": false, "last_sync": null, "syncing": false, "error": null}, "wifi": {"state": "connected", "ssid": "Office-WiFi", "ip": "10.0.4.42", "host": "minibar.local", "rssi": -61}}
```

### 6.5 Commands

| `cmd` | Same as | Notes |
|---|---|---|
| `hello` | `GET /api/v1/info` | Also tells the bar who's connected, and can set its clock (6.6). |
| `call` | `POST /api/v1/call` | Same fields and reply (section 5). |
| `status` | `GET /api/v1/status` | Same reply (7.3). |
| `pair` | — | USB only: get a Wi-Fi token without a code (6.6). |
| `request` | Any endpoint | `method`, `path` and `body`, for everything else (6.6). |

- **USB needs no token.** The router skips token checks for USB messages, and every endpoint is allowed.
- A reply is the same object the HTTP endpoint would return, plus `id`. An unknown `cmd` gets `unknown_cmd`.

### 6.6 Messages

**`hello`.** The app sends it right after opening the port, after a `ready` event, and whenever replies stop (6.8).

| Field | Type | Required | Meaning |
|---|---|---|---|
| `client` | string | yes | As in `call` (5.1). |
| `name` | string, 1 to 32 characters | no | A label for the Remote's Mac line and the paired-devices list. Left out, the bar says "Mac". The Mac app sends one only if the user names the Mac in its settings; it never sends the computer's name. |
| `app_version` | string | no | For the bar's logs. |
| `api` | string | yes | The API version the app speaks, `"1.0"`. A different major version gets `unsupported_api`. |
| `time` | RFC 3339 time | no | The Mac's clock. The bar sets its clock from it only if it hasn't had network time in the last 24 hours (for example with Wi-Fi skipped) and its clock is more than 2 seconds off; `time_source` then says `"mac"`. |
| `time_zone` | IANA name | no | Used only if the bar has no time zone yet. |

```text
mac → bar  @tb {"cmd": "hello", "id": 1, "client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "app_version": "1.0 (12)", "api": "1.0", "time": "2026-10-04T14:11:58-07:00", "time_zone": "America/Los_Angeles"}
bar → mac  @tb {"id": 1, "ok": true, "device": "MiniBar", "device_id": "f412fa3f2a1c", "name": "MiniBar 2A1C", "time_format": "12h", "fw": "1.0.0", "api": "1.0", "host": "minibar.local", "auth": "bearer", "pairing": "idle", "pairing_seq": null, "paired": 3, "heartbeat_s": 30, "timeout_s": 90, "time": "2026-10-04T14:11:58-07:00", "time_source": "ntp", "wifi": "connected"}
```

The reply is the `info` object (7.1) plus `id`. A `hello` counts as a heartbeat and marks the Mac connected over USB, but doesn't change its call state; the app sends a `call` right after it.

**`call`** (section 5):

```text
mac → bar  @tb {"cmd": "call", "id": 2, "client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "session": "q8Zr2Lx0", "seq": 1, "active": false}
bar → mac  @tb {"id": 2, "ok": true, "device_id": "f412fa3f2a1c", "showing": "own", "screen": "on", "stale": false, "call": {"active": false, "app": null, "inputs": null, "via": null, "since": null, "aside": false}, "sources": {"calendar": true, "mac": true}, "heartbeat_s": 30, "timeout_s": 90, "time": "2026-10-04T14:11:58-07:00"}
```

**`status`** (7.3): see the transcript in 6.4.

```text
mac → bar  @tb {"cmd": "status", "id": 3}
```

**`pair`**. Gives the Mac app a `call`-scope token for Wi-Fi with no code, since the cable proves someone is at the desk. Like any pairing, it replaces an older token with the same `client`, and the bar confirms on screen ("Paired · Mac · over USB").

```text
mac → bar  @tb {"cmd": "pair", "id": 4, "client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60"}
bar → mac  @tb {"id": 4, "ok": true, "token": "tb1_w1rV1lN4jm2ohruSAozMZxVlcceAL7yS8r45__-ref4", "token_id": "74d8a526", "scope": "call", "device_id": "f412fa3f2a1c", "name": "MiniBar 2A1C", "host": "minibar.local"}
```

It can fail with `token_limit` (10 tokens already, or 9 while another new device's code on the screen holds the tenth place, 4.3). USB keeps working without the token:

```text
bar → mac  @tb {"id": 4, "ok": false, "error": "token_limit", "message": "MiniBar already has 10 paired devices. Remove one on the Remote.", "field": null}
```

**`request`.** Any endpoint over USB. The reply is the endpoint's response body plus `id` and `http_status`.

| Field | Type | Required | Meaning |
|---|---|---|---|
| `method` | `"GET"`, `"POST"`, `"PUT"`, `"PATCH"` or `"DELETE"` | yes | |
| `path` | string starting `/api/v1/` | yes | May include a query. |
| `body` | object or `null` | no | The JSON body, for methods that take one. |

```text
mac → bar  @tb {"cmd": "request", "id": 5, "method": "PATCH", "path": "/api/v1/settings", "body": {"display": {"brightness": 40}}}
bar → mac  @tb {"id": 5, "http_status": 200, "ok": true, "settings": {"pomodoro": {"focus_min": 25, "short_min": 5, "long_min": 15, "long_every": 4, "auto_start": false, "chime": true, "ticking": false, "tick_volume": "soft"}, "display": {"brightness": 40}, "automatic": {"calendar": true, "mac": true, "meeting_titles": false}, "device": {"name": "MiniBar 2A1C", "time_zone": "America/Los_Angeles"}}}
```

Errors over USB:

```text
mac → bar  @tb {"cmd": "call", "id": 6, "active": "yes"
bar → mac  @tb {"id": null, "ok": false, "error": "bad_json", "message": "That line isn't a JSON object.", "field": null}
mac → bar  @tb {"cmd": "dance", "id": 7}
bar → mac  @tb {"id": 7, "ok": false, "error": "unknown_cmd", "message": "Unknown cmd \"dance\".", "field": "cmd"}
mac → bar  @tb {"cmd": "hello", "id": 8, "client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "api": "2.0"}
bar → mac  @tb {"id": 8, "ok": false, "error": "unsupported_api", "message": "This MiniBar speaks API 1.0.", "field": "api"}
mac → bar  (a line of 3,000 bytes)
bar → mac  @tb {"id": null, "ok": false, "error": "too_large", "message": "Lines can be up to 2048 bytes.", "field": null}
```

### 6.7 The `ready` event

The bar sends this once when its USB protocol handler starts after a boot. If no program has the port open, it's simply lost.

```text
bar → mac  @tb {"event": "ready", "device_id": "f412fa3f2a1c", "api": "1.0", "fw": "1.0.0"}
```

When the app sees it, it sends `hello` and then its current call state at once, so a restart costs no more than about a second of a call.

### 6.8 Keeping the link

- The app sends its call state every `heartbeat_s` seconds over USB, like over Wi-Fi.
- If **3 replies in a row** don't come within 3 seconds, the app closes the port, switches to Wi-Fi (if paired) at once, and reopens the port when IOKit next reports it, or after 10 seconds.
- When the port disappears (unplugged, bar restarting or powered off), the app switches to Wi-Fi at once and sends its full state there. When the port comes back and answers `hello`, it switches back to USB.

---

## 7. Reading the bar: info and status

### 7.1 `GET /api/v1/info`

No token needed. Who this bar is, before pairing. The USB `hello` reply is the same object.

```json
{"ok": true, "device": "MiniBar", "device_id": "f412fa3f2a1c", "name": "MiniBar 2A1C", "time_format": "12h", "fw": "1.0.0", "api": "1.0", "host": "minibar.local", "auth": "bearer", "pairing": "idle", "pairing_seq": null, "paired": 3, "heartbeat_s": 30, "timeout_s": 90, "time": "2026-10-04T14:11:58-07:00", "time_source": "ntp", "wifi": "connected"}
```

| Field | Meaning |
|---|---|
| `device` | Always `"MiniBar"`. Firmware 1.0.2 and earlier answered `"TinyBar"`, the product's old name; a client that may meet one accepts both (14.6). |
| `device_id` | 12 lowercase hex digits, the bar's Wi-Fi MAC address. The key clients store tokens under. |
| `name` | The bar's name (`device.name`), also its mDNS instance name. |
| `time_format` | `"12h"` or `"24h"`: the bar's Time format (`display.time_format`, 10.5), so the Mac app writes its clock times the way the bar does. Firmware 1.0.8 and earlier don't send it (the Mac's own format applies). Not secret, like the rest of this reply. *(Added with firmware 1.0.9.)* |
| `fw`, `api` | Firmware and API versions. |
| `host` | The mDNS name the bar has now. |
| `auth` | `"bearer"` (pairing required) or `"none"` (4.1). |
| `pairing` | `"idle"`, `"showing"` (a code is on screen) or `"locked"` (back-off, 4.9). |
| `pairing_seq` | While `pairing` is `"showing"`, the number of the code on the screen, the same `pairing_seq` its `pair/start` reply carried (4.6); otherwise `null`. A device waiting on its code compares the two, so it notices its code is gone even when the next device's code took its place within the same poll. *(Added 2026-10-05, from the review of the firmware's alignment; the Remote's prompt uses it, and clients that don't need it ignore it.)* |
| `paired` | How many devices are paired, 0 to 10 (4.3). *(Added 2026-10-05 in the firmware's alignment with the mock-up's pairing round, which left this open: a phone that isn't paired can't read the list (12.1), so the Remote's pairing prompt reads this to clear "already has 10 paired devices" as soon as a place is free (counting a code on the screen, which holds one), and after a `401` to say "MiniBar forgot this phone" when it's 0 (Forget all) rather than "This phone isn't paired … anymore". Together with `pairing` and `wifi` (`"setup"` until the Connected screen is over), it lets every refusal on the prompt clear once its cause is over. Clients that don't need it ignore it.)* |
| `heartbeat_s`, `timeout_s` | 5.3. |
| `time`, `time_source` | The bar's clock, and where it came from: `"ntp"`, `"rtc"` (the clock chip, kept since the last sync), `"mac"` (set over USB) or `"none"`. |
| `wifi` | `"connected"`, `"offline"` (skipped or dropped) or `"setup"`. Over USB, this tells the Mac app whether Wi-Fi is worth trying. |
| `mac_app` | **Proposed (2026-10-05):** the copy of the Mac app the bar carries for Download for Mac, `null` when it carries none. The Remote's pairing prompt reads it without a token. Its fields are in 17.2. |

### 7.2 Polling

- `GET /api/v1/status` has an `ETag` (`"r1842"`, from `rev`). A client that sends `If-None-Match` with it gets `304 Not Modified` and no body while nothing has changed.
- The Remote page polls every 2 seconds while it's visible, and stops when it's hidden. The Mac app doesn't poll: every `call` reply already tells it what it needs.
- *Later, not in version 1:* a WebSocket for pushed updates.

### 7.3 `GET /api/v1/status`

Scope `call` or `full`. Over USB: `"cmd": "status"`. Everything the Remote shows, in one object. It **never contains the calendar's secret address**, and it leaves out meeting titles and locations while Show meeting titles is off.

```json
{
  "ok": true,
  "device_id": "f412fa3f2a1c",
  "rev": 1842,
  "time": "2026-10-04T14:24:05-07:00",
  "time_source": "ntp",
  "showing": "call",
  "screen": "on",
  "own": {"status": "pomodoro", "since": "2026-10-04T13:58:00-07:00", "previous": "busy"},
  "message": {"text": "On a deadline until 3 PM, message me instead", "set_at": "2026-10-04T11:02:00-07:00"},
  "away": {"back_at": null, "note": null},
  "call": {"active": true, "app": "Slack", "inputs": null, "via": "usb", "since": "2026-10-04T14:12:00-07:00", "aside": false},
  "meeting": {
    "active": false,
    "aside": false,
    "current": null,
    "next": {"start": "2026-10-04T15:00:00-07:00", "end": "2026-10-04T15:45:00-07:00", "title": null, "location": null},
    "left_today": 3
  },
  "pomodoro": {"state": "paused", "phase": "focus", "round": 2, "rounds": 4, "length_s": 1500, "remaining_s": 1122, "ends_at": null, "paused_by": "call", "ringing": false, "done_today": 1, "focused_today_s": 1878},
  "sources": {"calendar": true, "mac": true},
  "macs": [
    {"client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "name": "Mac", "via": "usb", "connected": true, "active": true, "last_heard": "2026-10-04T14:24:01-07:00"}
  ],
  "calendar": {"saved": true, "last_sync": "2026-10-04T14:20:00-07:00", "syncing": false, "error": null},
  "wifi": {"state": "connected", "ssid": "Office-WiFi", "ip": "10.0.4.42", "host": "minibar.local", "rssi": -61}
}
```

| Field | Meaning |
|---|---|
| `device_id` | Which bar answered (7.1). |
| `rev` | Goes up whenever anything in this object changes, except `time` and the countdowns. Also the `ETag`. |
| `time`, `time_source` | As in `info`. Clients work out "heard from it just now" and countdowns against this, not their own clock. |
| `showing` | *Open.* What the bar shows, by the rules in `decisions.md`: `"call"` (On a call), `"meeting"` (In a meeting from the calendar), `"own"` (your own status, including while a call or meeting is set aside), or `"setup"` (the Wi-Fi setup screens). Menus, toasts, the pairing screen and the power screens are passing overlays and don't change it. Clients treat an unknown value like `"own"`. |
| `screen` | `"on"`, or `"dark"` after a PWR press. A dark bar keeps following calls, meetings and the timer, and shows the right screen when woken. A powered-off bar doesn't answer at all. **Proposed (2026-10-05, with the controls tour in `decisions.md`, Help and the controls tour):** `"tour"` while the bar shows its quick tour of the controls. Everything else in this object stays real (`showing`, `own`, the call, the meeting, the timer), since nothing real changes during the tour; the screen underneath shows when it ends. Clients treat an unknown `screen` value like `"on"`. |
| `own.status` | Your own status: `"available"`, `"busy"`, `"meeting"` (In a meeting **picked by hand**), `"pomodoro"`, `"away"`, `"message"` or `"clock"`. *Open:* clients show an unknown value by name only. |
| `own.since` | When you picked it. |
| `own.previous` | The status Stop on the Pomodoro returns to (the last one that isn't Pomodoro or Clock). |
| `message` | The last custom message and when it was set. Kept when another status is picked, so the Remote's field can show it. `null` text if none was ever set. |
| `away` | **Proposed** (8.1): the back-at time and note for Away, or `null`s. |
| `call` | The bar's call (section 5). `active` is `true` while any connected Mac reports a call, **whether or not** Calls from your Mac is on; `showing` and `sources.mac` say whether it's on screen. `app`, `inputs` and `via` are from the call that started most recently. `since` is when the bar's call started. `aside` is `true` while it's set aside. All `null` (and `false`) when there's no call. |
| `meeting` | From the bar's copy of today's calendar, **whether or not** Calendar meetings is on (`showing` and `sources.calendar` say whether it's on screen). `active`: a meeting is in progress. `aside`: it's set aside. `current` and `next`: `{start, end, title, location}` or `null`, where `next` is the rest of today only. `title` and `location` are `null` while Show meeting titles is off, always for events marked Private, and a location that's a web address is left out. `left_today`: meetings still to come or in progress today. With no calendar saved, or with Wi-Fi skipped, `active` and `aside` are `false` and the rest is `null`. |
| `pomodoro` | `state`: `"ready"` (not started), `"running"`, `"paused"` or `"waiting"` (a phase has ended and the next waits for a start). `phase`: `"focus"`, `"short"` or `"long"`. `round` of `rounds` ("Focus 2 of 4"). `length_s` and `remaining_s` of the current phase. `ends_at`: when it ends if running, else `null`. `paused_by`: `"you"`, `"call"`, `"meeting"` or `null`. `ringing`: the alarm is ringing or flashing now. `done_today` (tomatoes) and `focused_today_s`. |
| `sources` | The Remote's Automatic status switches: `calendar` (Calendar meetings) and `mac` (Calls from your Mac). The same values as `settings.automatic`. |
| `macs` | Each Mac the bar has heard from since it started, most recently heard first, at most 4: `client`, `name` (from `hello` or its pairing), `via` (`"usb"` or `"wifi"`), `connected`, `active` (that Mac reports a call), `last_heard`. The Remote's Mac line uses the first one: "Connected over USB · heard from it just now", or "Not connected · last heard 2:04 PM". |
| `calendar` | A summary: `saved` (an address is saved), `last_sync`, `syncing`, and `error` (the last sync's error code, or `null`). The full details are in `GET /api/v1/calendar`. |
| `wifi` | `state` (`"connected"`, `"offline"` or `"setup"`), `ssid`, `ip`, `host` and `rssi` (dBm), `null` when not connected. |

---

## 8. Your status, message, set aside and Show again

All scope `full`. Every successful change shows the **same confirmation on the bar** as the Remote's controls do in the mock-up, and the reply is the **full status object** (7.3), so the Remote redraws from one place.

`set_aside`, in the requests below, works like the Remote: a choice you make during a call or meeting **sets it aside** until it ends (`decisions.md`). It's `true` if left out. An automation that changes your status in the background without hiding a call sends `"set_aside": false`.

### 8.1 `POST /api/v1/status`: pick your own status

| Field | Type | Required | Meaning |
|---|---|---|---|
| `status` | `"available"`, `"busy"`, `"meeting"`, `"pomodoro"`, `"away"`, `"message"` or `"clock"` | yes | `"meeting"` is In a meeting picked by hand. `"pomodoro"` shows the Pomodoro screen without starting it. `"message"` shows the last message (use 8.2 for a new one). On a call isn't a choice: it's automatic only (`decisions.md`). |
| `back_at` | `"HH:MM"` | no, **Proposed** | Only with `"away"`: the headline becomes "Back at 1:30". |
| `note` | string, 1 to 40 characters | no, **Proposed** | Only with `"away"`: the sub line, for example "Grabbing lunch". |
| `set_aside` | boolean | no | See above. |

The mock-up's Away screen shows "Back at 12:30" and "Grabbing lunch" as sample data, but nothing sets them yet. **Proposed:** these two fields are how; without them the bar shows its plain Away screen. The Remote needs a way to enter them (section 14).

```json
{"status": "away", "back_at": "13:30", "note": "Grabbing lunch"}
```

```json
{"status": "busy", "set_aside": false}
```

The reply is the full status object (7.3), for example after the first request above during a call:

```json
{"ok": true, "device_id": "f412fa3f2a1c", "rev": 1850, "time": "2026-10-04T12:58:00-07:00", "time_source": "ntp", "showing": "own", "screen": "on", "own": {"status": "away", "since": "2026-10-04T12:58:00-07:00", "previous": "away"}, "message": {"text": "On a deadline until 3 PM, message me instead", "set_at": "2026-10-04T11:02:00-07:00"}, "away": {"back_at": "13:30", "note": "Grabbing lunch"}, "call": {"active": true, "app": "Slack", "inputs": null, "via": "wifi", "since": "2026-10-04T12:50:00-07:00", "aside": true}, "meeting": {"active": false, "aside": false, "current": null, "next": null, "left_today": 0}, "pomodoro": {"state": "ready", "phase": "focus", "round": 1, "rounds": 4, "length_s": 1500, "remaining_s": 1500, "ends_at": null, "paused_by": null, "ringing": false, "done_today": 2, "focused_today_s": 3000}, "sources": {"calendar": true, "mac": true}, "macs": [{"client": "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60", "name": "Mac", "via": "wifi", "connected": true, "active": true, "last_heard": "2026-10-04T12:57:41-07:00"}], "calendar": {"saved": true, "last_sync": "2026-10-04T12:50:00-07:00", "syncing": false, "error": null}, "wifi": {"state": "connected", "ssid": "Office-WiFi", "ip": "10.0.4.42", "host": "minibar.local", "rssi": -58}}
```

| Error | When |
|---|---|
| `400 bad_value` | Unknown `status`, a bad `back_at`, or `back_at` or `note` with a status other than `"away"`. |
| `409 no_message` | `"message"` when no message was ever set. |
| `409 in_setup` | The bar is on its Wi-Fi setup screens (over USB, or over Wi-Fi on the Connected screen; section 13 and 4.6). |

```json
{"ok": false, "error": "bad_value", "message": "Unknown status \"on_a_call\". On a call is set by the Mac app only.", "field": "status"}
```

### 8.2 `POST /api/v1/message`: show a custom message

| Field | Type | Required | Meaning |
|---|---|---|---|
| `text` | string, 1 to 80 characters after trimming | yes | Shown on the Message screen (scrolling when long, as in the mock-up). |
| `set_aside` | boolean | no | See above. |

```json
{"text": "On a deadline until 3 PM, message me instead"}
```

The reply is the full status object (7.3) with `"own": {"status": "message", …}` and the new `message`. If the text has characters the bar's fonts can't draw (after the mapping in 2.3), it's refused, so the Remote can say which:

```json
{"ok": false, "error": "unsupported_chars", "message": "MiniBar can't show some of these characters.", "field": "text", "chars": ["🍕"]}
```

The Remote runs the same check as you type (decisions.md, Remote) and names the characters before anything is sent: "MiniBar can't show 🍕. Remove it to show this message." A character with no ink of its own that the mapping in 2.3 doesn't cover can't be quoted, so the Remote names it by where it is: "a hidden character after "Busy"" (**Proposed**, 2026-10-04).

### 8.3 `POST /api/v1/aside`: set aside, or Show again

The Remote's banner button ("Set aside" / "Show again") and its Show again button.

| Field | Type | Required | Meaning |
|---|---|---|---|
| `aside` | boolean | yes | `true`: set aside the call or meeting on screen, and show your own status (toast "Call set aside · hold to show it again"). `false`: Show again, bringing back the call or meeting that's set aside (toast "Showing the call again"). |

```json
{"aside": true}
```

```json
{"aside": false}
```

The reply is the full status object (7.3).

| Error | When |
|---|---|
| `409 nothing_to_set_aside` | `true`, but no call or meeting is on screen. |
| `409 nothing_set_aside` | `false`, but nothing is set aside (the bar's toast in the mock-up says "Nothing set aside"). |

```json
{"ok": false, "error": "nothing_set_aside", "message": "Nothing is set aside.", "field": "aside"}
```

---

## 9. Pomodoro

### 9.1 `POST /api/v1/pomodoro`

Scope `full`. The Remote's Start/Pause, Skip and Stop buttons and the timer menu's +5.

| Field | Type | Required | Meaning |
|---|---|---|---|
| `action` | see below | yes | |
| `minutes` | integer, 1 to 60 | no | Only for `"extend"`; 5 if left out. |
| `set_aside` | boolean | no | As in section 8; `true` if left out. |

| `action` | Does (as the mock-up's Remote does) |
|---|---|
| `"start"` | Starts a ready session, starts the phase that's waiting (silencing the alarm), or resumes a paused timer. Already running: nothing changes, still `200`. Shows the Pomodoro screen. |
| `"pause"` | Pauses a running timer. Shows the Pomodoro screen. |
| `"toggle"` | `"start"` if not running, else `"pause"`: the Remote's main button and a tap on the bar. |
| `"skip"` | Ends the current phase without counting it and starts the next one running. Shows the Pomodoro screen. |
| `"stop"` | Ends the run, keeps today's tomatoes, and returns to `own.previous`. |
| `"extend"` | Adds `minutes` to a running or paused phase (the timer menu's +5). |

```json
{"action": "toggle"}
```

```json
{"action": "extend", "minutes": 5}
```

The reply is the full status object (7.3). Every action silences a ringing alarm first, as any control does.

| Error | When |
|---|---|
| `400 bad_value` | Unknown `action`, or `minutes` out of range. |
| `409 not_running` | `"pause"` when the timer isn't running. |
| `409 nothing_to_extend` | `"extend"` when the timer is ready or waiting (the +5 tile only appears mid-phase). |

```json
{"ok": false, "error": "not_running", "message": "The timer isn't running.", "field": "action"}
```

The Pomodoro's lengths, auto-start, chime and ticking are settings (section 10).

---

## 10. Settings

### 10.1 `GET /api/v1/settings`

Scope `full`.

```json
{
  "ok": true,
  "settings": {
    "pomodoro": {"focus_min": 25, "short_min": 5, "long_min": 15, "long_every": 4, "auto_start": false, "chime": true, "ticking": false, "tick_volume": "soft"},
    "display": {"brightness": 70, "time_format": "12h"},
    "automatic": {"calendar": true, "mac": true, "meeting_titles": false},
    "device": {"name": "MiniBar 2A1C", "time_zone": "America/Los_Angeles"}
  }
}
```

| Setting | Values | Default | Notes |
|---|---|---|---|
| `pomodoro.focus_min` | 1 to 120 | 25 | The Remote offers 15, 25 and 50. |
| `pomodoro.short_min` | 1 to 60 | 5 | The Remote offers 5 and 10. |
| `pomodoro.long_min` | 1 to 60 | 15 | The Remote offers 15, 20 and 30. |
| `pomodoro.long_every` | 2 to 8 | 4 | "Long break after" this many sessions. The Remote offers 3, 4 and 5. |
| `pomodoro.auto_start` | boolean | `false` | "Start next phase on its own". |
| `pomodoro.chime` | boolean | `true` | The alarm chime. |
| `pomodoro.ticking` | boolean | `false` | Ticking during focus (decided 2026-10-04: optional, off by default). |
| `pomodoro.tick_volume` | `"soft"` or `"medium"` | `"soft"` | Kept while ticking is off, as on the Remote. |
| `display.brightness` | 10 to 100 (percent) | 70 | The bar's Light tile steps through 40, 70 and 100; from any other value its next tap goes to the next of those above it (from 100, to 40). |
| `display.time_format` | `"12h"` or `"24h"` | `"12h"` | **Accepted, firmware 1.0.9, see 10.5.** 12-hour (3:30 PM) or 24-hour (15:30) for every time the bar and the Remote show. |
| `display.theme` | `"bold_signal"` or `"low_glare_pixel"` (*open*) | `"bold_signal"` | **Proposed (2026-10-05), see 10.3.** The bar's look: Bold Signal, or Low Glare Pixel (the Low Glare layout set in Handjet). Not in the example above until it's approved. |
| `sound.tap_sound` | boolean | `true` (**Proposed**) | **Proposed (2026-10-05), see 10.4.** Tap sound: a short, quiet click when the bar acts on a tap, swipe or hold on its screen. It's always silent during a call or meeting. Not in the example above until it's approved. |
| `automatic.calendar` | boolean | `true` once an address is saved, `false` before | Calendar meetings. Can't be `true` with no address saved. |
| `automatic.mac` | boolean | `true` | Calls from your Mac. |
| `automatic.meeting_titles` | boolean | `false` | Show meeting titles. Needs a saved address. |
| `device.name` | 1 to 24 characters | "MiniBar" plus the last 4 of `device_id` (**Proposed**, section 3) | Also the mDNS instance name. |
| `device.time_zone` | IANA name | From the setup page or the Mac's `hello`, else `null` | The bar turns it into a POSIX time-zone rule with a built-in table. |

### 10.2 `PATCH /api/v1/settings`

Scope `full`. Send only what changes; everything left out stays as it is. `null` isn't allowed for any setting. The whole request is checked before anything changes, so it either all applies or none of it does. The reply is the full settings, as in 10.1.

```json
{"pomodoro": {"ticking": true, "tick_volume": "medium"}}
```

```json
{"automatic": {"mac": false}}
```

```json
{"pomodoro": {"focus_min": 50}, "display": {"brightness": 100}}
```

What changes on the bar, as in the mock-up:

- Each change shows its toast ("Ticking on · Medium", "Meeting titles on", "Calls from your Mac off"…).
- **Turning a source off** ends its status at once and forgets a set-aside call or meeting from it. **Turning it back on** during a call or meeting shows it at once.
- **Changing the length of the phase that's running** restarts it at the new length. Lowering `long_every` below the current round moves the round down to it.
- During a call or meeting, a length change doesn't pull the bar off the call or meeting screen.

| Error | When |
|---|---|
| `400 bad_value` | A value out of range or of the wrong type; `field` names it, for example `"pomodoro.focus_min"`. |
| `409 no_calendar` | `automatic.calendar` or `automatic.meeting_titles` set to `true` with no address saved. |

```json
{"ok": false, "error": "no_calendar", "message": "Add a calendar address first.", "field": "automatic.calendar"}
```

### 10.3 Proposed: `display.theme`, the bar's theme

**Proposed (2026-10-05), waiting for the user's OK.** On 2026-10-05 the user decided to add a second look, **Low Glare Pixel** (the Low Glare layout set entirely in Handjet), as an alternate theme beside the default, **Bold Signal** (`decisions.md`, Look). This is how the setting travels. It's built in the mock-up first; the firmware adds it after the user has seen it there.

| | |
|---|---|
| Name | `display.theme` |
| Values | `"bold_signal"` or `"low_glare_pixel"`. *Open:* a later version may add themes. A client that doesn't know a value shows it by name and leaves it alone. |
| Default | `"bold_signal"`, on a new bar and on a bar updated from firmware that had no themes. |
| Read | `GET /api/v1/settings` (10.1): `settings.display.theme`, always present. |
| Change | `PATCH /api/v1/settings` (10.2), scope `full`. Over USB, `request` (6.6) with no token, like every other setting. |
| On the bar | The Theme tile in the quick menu's Display menu (**Proposed**, `decisions.md`). |
| On the Remote | The Theme choice in its Display section (**Proposed**). |

```json
{"display": {"theme": "low_glare_pixel"}}
```

```text
mac → bar  @tb {"cmd": "request", "id": 9, "method": "PATCH", "path": "/api/v1/settings", "body": {"display": {"theme": "low_glare_pixel"}}}
bar → mac  @tb {"id": 9, "http_status": 200, "ok": true, "settings": {"pomodoro": {"focus_min": 25, "short_min": 5, "long_min": 15, "long_every": 4, "auto_start": false, "chime": true, "ticking": false, "tick_volume": "soft"}, "display": {"brightness": 70, "theme": "low_glare_pixel"}, "automatic": {"calendar": true, "mac": true, "meeting_titles": false}, "device": {"name": "MiniBar 2A1C", "time_zone": "America/Los_Angeles"}}}
```

- **Checks:** a string that is exactly one of the values. Anything else (an unknown name, other capitals such as `"Low_Glare_Pixel"`, a number, `null`) is `400 bad_value` with `"field": "display.theme"`, and nothing else in the request applies (10.2). No new error codes.

  ```json
  {"ok": false, "error": "bad_value", "message": "theme must be \"bold_signal\" or \"low_glare_pixel\".", "field": "display.theme"}
  ```

- **What changes on the bar:** every screen redraws in the new theme at once, an open menu and a pairing code included, and the bar shows the toast "Theme · Low Glare Pixel" (or "Theme · Bold Signal"). Nothing else changes: the status, a running or paused Pomodoro, a ringing alarm, a call or meeting and whether it's set aside, ticking, the light and a dark screen stay as they are, and nothing makes a sound. On a dark screen, or while a pairing code shows, the toast waits, as it does for any change from the Remote.
- **The theme it already has:** changes nothing and shows no toast. The reply is still `200` with the full settings.
- **Kept** with the other settings, across Restart and power off and on. Wi-Fi setup, Skip and Forget all don't change it.
- **`rev`:** a theme change, including one made on the bar, bumps `rev` in `GET /api/v1/status` (7.3), so the Remote, which reads the settings again whenever `rev` changes, shows it within about 2 seconds. `status` itself doesn't carry the theme, and neither do the `call` replies.
- **Version:** a new field and an open set of values, so it's an addition under 2.1 (API 1.1). Clients that speak 1.0 ignore it.
- **The Mac app needs nothing.** It doesn't read or change settings, and it ignores fields it doesn't know (2.1).
- **The setup network (section 13) doesn't offer it.** A bar in setup shows its setup screens in the saved theme.

### 10.4 Proposed: `sound.tap_sound`, the click on touch

**Proposed (2026-10-05), waiting for the user's OK.** On 2026-10-05 the user asked for a click sound to confirm that a tap went through, since the bar has no haptics (`decisions.md`, Sound, Tap sound). This section is how the setting travels. It's built in the mock-up first, and the firmware follows.

| | |
|---|---|
| Name | `sound.tap_sound`, in a new `sound` group |
| Values | `true` or `false` |
| Default | `true` (**Proposed**; the other choice is `false`, like ticking). It applies on a new bar and on a bar updated from firmware that had no touch click. |
| Read | `GET /api/v1/settings` (10.1): `settings.sound.tap_sound`, always present. |
| Change | `PATCH /api/v1/settings` (10.2), scope `full`. Over USB, `request` (6.6) with no token, like every other setting. |
| On the bar | A Tap sound tile, On or Off, in the quick menu's Display menu beside Theme (placement decided 2026-10-05, `decisions.md`, Sound). |
| On the Remote | A Tap sound switch in its Display section, next to Theme (placement decided 2026-10-05). |

```json
{"sound": {"tap_sound": false}}
```

- **Checks:** the value must be `true` or `false`. Anything else (a string such as `"off"`, a number, `null`) is `400 bad_value` with `"field": "sound.tap_sound"`, and nothing else in the request applies (10.2). No new error codes.

  ```json
  {"ok": false, "error": "bad_value", "message": "tap_sound must be true or false.", "field": "sound.tap_sound"}
  ```

- **What changes on the bar:** from then on, touches click or don't. The bar shows the toast "Tap sound on" or "Tap sound off". During a call or meeting, the toast also says the click is silent for now (copy from the UX designer). A change made through the API never makes the bar click, because only touches on the bar click. Like any change from the Remote, the toast waits on a dark screen or while a pairing code shows. Nothing else changes.
- **The value it already has:** changes nothing and shows no toast. The reply is still `200` with the full settings.
- **When the bar clicks** is decided on the bar, not here: only for a touch it acts on, never during a call or meeting, never on a dark screen, and never for BOOT, PWR or a flip (`decisions.md`). No status or event field reports clicks.
- **Kept** with the other settings, across Restart and power off and on. Wi-Fi setup, Skip and Forget all don't change it.
- **`rev`:** a change, including one made on the bar, bumps `rev` in `GET /api/v1/status` (7.3), so the Remote shows it within about 2 seconds. `status` itself doesn't carry it.
- **Why a `sound` group:** the chime and ticking are Pomodoro settings (`pomodoro.chime`, `pomodoro.ticking`), but the click isn't. If a loudness choice is wanted later, it would be a separate field (for example `sound.tap_sound_volume`), as `pomodoro.tick_volume` is, so this field never changes type.
- **Version:** a new group and field, so it's an addition under 2.1 (API 1.1). Clients that speak 1.0 ignore it.
- **The Mac app needs nothing.** It doesn't read or change settings, and it ignores fields it doesn't know (2.1).
- **The setup network (section 13) doesn't offer it.**

### 10.5 `display.time_format`, 12-hour or 24-hour

**Accepted (the user, 2026-10-07), in firmware 1.0.9.** Every time the bar shows, and every time the Remote and the Mac app write, follows one setting (`decisions.md`, Time format).

| | |
|---|---|
| Name | `display.time_format` |
| Values | `"12h"` (3:30 PM, the default) or `"24h"` (15:30). Exactly these two strings, in lower case. |
| Default | `"12h"`, on a new bar and on a bar updated from firmware that had no such setting (it loses no other setting either). |
| Read | `GET /api/v1/settings` (10.1): `settings.display.time_format`, always present; and `time_format` in `GET /api/v1/info` (7.1), which needs no token, for the Mac app. |
| Change | `PATCH /api/v1/settings` (10.2), scope `full`; on the bar, the Time tile in the quick menu's Display menu (hold, then Display, then Time); on the Remote, Time format in its Display section. |

```json
{"display": {"time_format": "24h"}}
```

- **Checks:** a string that is exactly `"12h"` or `"24h"`. Anything else (`"24H"`, `"25h"`, `""`, a number, `true`, `null`, an array) is `400 bad_value` with `"field": "display.time_format"`, and nothing else in the request applies (10.2).

  ```json
  {"ok": false, "error": "bad_value", "message": "display.time_format must be \"12h\" or \"24h\".", "field": "display.time_format"}
  ```

- **What changes:** the clock, the status row's clock, a meeting's start and end ("14:30-15:15"), Next up, Free until, Posted, Away's "Back at", "since" and "left at", and the times in toasts and the Calendar tile's "synced at" foot. **24-hour has no AM or PM anywhere and zero-pads the hour** (09:05, 00:15, 23:59; midnight is 00:00, never 24:00). 12-hour is unchanged. The bar says "Time format · 24-hour" (or "12-hour") in a toast; the value it already has changes nothing and shows no toast. A change from the API or the Remote never clicks.
- **What does not change:** times on the wire. `back_at` for Away stays `"HH:MM"`, 24-hour, in `POST /api/v1/status` (8.1), whatever the setting, and every `time`, `since`, `start`, `end` and the like stays RFC 3339 (2.3). Only how a screen writes them follows the setting.
- **`rev`:** a change, including one made on the bar, bumps `rev` in `GET /api/v1/status` (7.3), so the Remote re-reads the settings within about 2 seconds.
- **The Mac app follows it:** it reads `time_format` from `info` each time it connects (and again whenever it repeats `hello`), so "Paused until 15:15" matches the bar's own clock; with no `time_format` (older firmware) or a value it doesn't know, it keeps the Mac's own format. A change made while the Mac stays connected shows in its menu from its next `info`, not at once.
- **Version:** a new field, an addition under 2.1 (API 1.1). Clients that speak 1.0 ignore it.
- **The setup network (section 13) doesn't offer it.** A bar in Wi-Fi setup writes its times in the saved format.

---

## 11. Calendar

All scope `full`. The secret address is **write-only**: once saved, no screen, page or reply shows it, in full or masked (`decisions.md`). The bar keeps it encrypted (section 15).

*(Firmware 1.0.8, 2026-10-07: the bar reads up to **3 calendars**, merged soonest first. Sections 11.1 to 11.4 are the earlier single-address calls, kept working; 11.5 is the list API for several. See `decisions.md`, Multiple calendars.)*

### 11.1 `GET /api/v1/calendar`

```json
{
  "ok": true,
  "calendar": {
    "saved": true,
    "address": null,
    "last_sync": "2026-10-04T14:20:00-07:00",
    "syncing": false,
    "error": null,
    "check": null,
    "today": [
      {"start": "2026-10-04T15:00:00-07:00", "end": "2026-10-04T15:45:00-07:00", "title": null, "location": null},
      {"start": "2026-10-04T16:00:00-07:00", "end": "2026-10-04T16:30:00-07:00", "title": null, "location": null},
      {"start": "2026-10-04T17:30:00-07:00", "end": "2026-10-04T18:30:00-07:00", "title": null, "location": null}
    ],
    "left_today": 3
  }
}
```

| Field | Meaning |
|---|---|
| `saved` | At least one calendar is saved. When `false`, `last_sync`, `today` and `left_today` are `null`. With several calendars this object describes them as one: `last_sync` is the latest good sync, `error` the first calendar that can't sync, `today` and `left_today` the merged list. Per calendar: 11.5. |
| `address` | **Always `null` since 1.0.8** (the key stays so clients that read it keep working). Not even the masked form is returned. *(Before 1.0.8:* the masked form the Remote showed ("calendar.google.com/…/basic.ics · ending 3f2a"): the host, the file name, and the last four characters of the private token in the path. Never the full address. *(2026-10-04, security review:)* some feeds use the private token as the file name (".../8c1d5e2a…3f2a.ics"). When the file name is long or looks random, `file` is just `"….ics"` and `ending` comes from it, so the token never shows. Short word-like names ("basic.ics", "calendar.ics", "team-standup.ics") still show.)* |
| `last_sync`, `syncing` | The last successful sync, and whether one is running. |
| `error` | The last sync's problem, or `null`: `{"error": code, "message": text, "at": time}` with a code from the table in 11.2. While it's set, the bar keeps following the last good copy. |
| `check` | The result of the last `PUT` (11.2) or `POST /api/v1/calendars` (11.5): `null`, or `{"state": "checking" \| "saved" \| "failed", "error": code or null, "message": text or null}`. Kept for 10 minutes. |
| `today` | Today's meetings that count (timed, shown as busy, not declined; `decisions.md`), still to come or in progress, in order. `title` and `location` follow Show meeting titles and Private, as in 7.3. *(2026-10-04, security review:)* the list stops before the reply would pass 8,192 bytes (2.5). Usually every meeting fits; on a very busy day with long titles shown, it holds the earliest ones (about 24 at the longest titles). |
| `left_today` | How many there are, always all of them, so a `today` shorter than `left_today` was cut. |

### 11.2 `PUT /api/v1/calendar`: add or replace the address

```json
{"url": "https://calendar.google.com/calendar/ical/you%40example.com/private-8c1d5e2a9b7f40c3a6e1d2b3c4f53f2a/basic.ics"}
```

1. The bar checks the format at once, with the same rules as the Remote page. A `webcal://` address is turned into `https://`.
2. It answers **`202`** and fetches the address once, in the background, to check it with the calendar's server.
3. The client polls `GET /api/v1/calendar` until `check.state` is `"saved"` or `"failed"`.
4. With no calendar saved this adds one (named "Calendar 1"); with one saved it replaces that one's address; with two or three it answers `409 several_calendars` (use 11.5).
5. **Nothing changes until the check passes:** on `"failed"` the old address (if any) stays. On `"saved"` the new address replaces the old one, a first address turns Calendar meetings on, and the bar shows "Calendar synced · 3 meetings left today".

```json
{"ok": true, "calendar": {"saved": true, "address": null, "last_sync": "2026-10-04T14:20:00-07:00", "syncing": true, "error": null, "check": {"state": "checking", "error": null, "message": null}, "today": [], "left_today": 0}}
```

Format errors (`400`, at once, nothing saved):

| `error` | When (the Remote's wording, from the mock-up) |
|---|---|
| `bad_request` | No `url`. "Paste your secret address first." |
| `not_a_url` | Not a web address. "Copy the whole address, starting with https://." |
| `http_not_allowed` | `http://`. "It isn't encrypted. Use the https:// address Google gives you." |
| `public_address` | A Google `/public/` address. "Copy the Secret address in iCal format instead." |
| `not_ics` | The path doesn't end in `.ics`. |
| `bad_value` | Over 1,024 bytes. |

```json
{"ok": false, "error": "public_address", "message": "That's the calendar's public address. Use the secret address in iCal format.", "field": "url"}
```

Check results (in `check`, after the `202`), and the same codes for later syncs (in `error`):

| `error` | When |
|---|---|
| `calendar_rejected` | The server answered 401, 403 or 404: "Google didn't recognize that address. It may have been reset in Google Calendar." |
| `calendar_unreachable` | No answer, a name lookup or TLS failure, or a time-out. |
| `not_a_calendar` | The answer isn't an iCal calendar. |
| `offline` | The bar has no Wi-Fi. |

```json
{"ok": true, "calendar": {"saved": false, "address": null, "last_sync": null, "syncing": false, "error": null, "check": {"state": "failed", "error": "calendar_rejected", "message": "Google didn't recognize that address. It may have been reset in Google Calendar."}, "today": null, "left_today": null}}
```

### 11.3 `DELETE /api/v1/calendar`: remove the address

No body. Removes the only calendar's address and saved copy, turns Calendar meetings and Show meeting titles off, and ends a calendar meeting on screen (toast "Calendar 1 removed", with the calendar's name). The reply is the calendar object with `"saved": false`. With two or three calendars saved: `409 several_calendars` (use `DELETE /api/v1/calendars/{id}`).

```json
{"ok": true, "calendar": {"saved": false, "address": null, "last_sync": null, "syncing": false, "error": null, "check": null, "today": null, "left_today": null}}
```

`409 no_calendar` if none is saved.

### 11.4 `POST /api/v1/calendar/sync`: Sync now

An empty body or `{}`. Syncs **every** calendar, one after another. Answers `202` with the calendar object (`"syncing": true`); the result shows in `last_sync` or `error` (and per calendar in 11.5), and the bar toasts "Calendar synced", or "Couldn't sync the calendar" if any failed.

```json
{}
```

```json
{"ok": true, "calendar": {"saved": true, "address": null, "last_sync": "2026-10-04T14:20:00-07:00", "syncing": true, "error": null, "check": null, "today": [{"start": "2026-10-04T15:00:00-07:00", "end": "2026-10-04T15:45:00-07:00", "title": null, "location": null}], "left_today": 1}}
```

| Error | When |
|---|---|
| `409 no_calendar` | No address saved. |
| `503 offline` | The bar has no Wi-Fi. (Reachable over USB only, then.) |

The bar also syncs on its own about every 10 minutes and switches into and out of In a meeting at each event's exact start and end from its saved copy (`decisions.md`).

### 11.5 Several calendars (firmware 1.0.8)

Up to **3 calendars**, each with a **name** (up to 24 characters, default "Calendar 1", "Calendar 2", the lowest number free) and a **tag** (up to 4 letters or digits, kept in capitals, default C1, C2, C3). Names and tags must differ from each other, compared without case. Both show on the bar and the Remote, so nothing private belongs in them. The bar merges the calendars' meetings soonest first; the same event in two calendars (same UID and start) shows once, with the earlier calendar's tag. A calendar that can't sync is **left out** of the bar until it works. With 2 or more calendars the bar's Next up carries the tag.

An `id` is 1 to 3 and stays with a calendar until it's removed (ids don't shift). **No reply here has any part of an address**, only that one is saved.

`GET /api/v1/calendars` and the replies of `POST`, `PATCH` and `DELETE` below:

```json
{
  "ok": true,
  "max": 3,
  "syncing": false,
  "calendars": [
    {"id": 1, "name": "Work", "tag": "WRK", "address_saved": true, "last_sync": "2026-10-04T14:20:00-07:00", "syncing": false, "status": "ok", "error": null, "left_today": 3},
    {"id": 3, "name": "Home", "tag": "HOM", "address_saved": true, "last_sync": "2026-10-04T13:58:00-07:00", "syncing": false, "status": "error",
     "error": {"error": "calendar_unreachable", "message": "MiniBar couldn't reach the calendar's server. Try again in a minute.", "at": "2026-10-04T14:20:05-07:00"}, "left_today": 0}
  ],
  "check": null
}
```

| Field | Meaning |
|---|---|
| `calendars` | The saved calendars in list order (by `id`). |
| `status` | `"ok"`, or `"error"` when its last sync failed; then `error` is as in 11.1 and its meetings are left out of the bar (`left_today` is 0). |
| `left_today` | That calendar's own meetings still to come or in progress today. |
| `check` | The last add or address change: `null`, or `{"state": "checking" \| "saved" \| "failed", "id": 2 or null, "error": code or null, "message": text or null}`. `id` is the calendar being changed, `null` for a new one. Kept 10 minutes. |

**`POST /api/v1/calendars`**: add. Body `{"url": "...", "name": "Work", "tag": "WRK"}`; `name` and `tag` are optional. The format is checked at once (the errors of 11.2, plus `400 bad_value` with `field` `name` or `tag`), and a name or tag another calendar has gives `409 already_used`. At 3, `409 calendar_limit`. Then `202` with the list and `check.state` `"checking"`; the bar fetches the address once, and **only a good fetch adds the calendar** (poll `check`). A first calendar turns Calendar meetings on; the bar shows "Calendar synced · 3 meetings left today".

**`PATCH /api/v1/calendars/{id}`**: change. Send at least one of `name`, `tag`, `url`. A new name or tag takes effect at once (`200`; the new tag reaches the bar's Next up). A `url` is checked like an add and replaces the saved address only if it works (`202`, poll `check` with its `id`); an empty or missing `url` keeps the saved address. `404 not_found` for an id that isn't saved.

**`DELETE /api/v1/calendars/{id}`**: remove that calendar and its saved copy (`200` with the list). If the meeting on the bar came only from it, the meeting ends at once with the toast "Calendar 2 removed" (its name); a copy of the same event in another calendar carries on. Removing the last one turns Calendar meetings and Show meeting titles off, as 11.3 does. `404 not_found` if there's no such id.

Sync now (11.4) reads every calendar. `GET /api/v1/status` has `calendar.count` and `calendar.failing`, and with 2 or more calendars each meeting object (`meeting.current`, `meeting.next`, `calendar.today`) has `"calendar": id`.

| Error | When |
|---|---|
| `409 calendar_limit` | Already 3 calendars. |
| `409 already_used` | Another calendar has that name or tag. |
| `409 several_calendars` | `PUT` or `DELETE /api/v1/calendar` with 2 or 3 calendars saved. |

---

## 12. Paired devices

### 12.1 `GET /api/v1/clients`

Scope `full`. The Remote's "Paired devices" list.

```json
{
  "ok": true,
  "max": 10,
  "clients": [
    {"token_id": "74d8a526", "name": "Mac", "kind": "mac", "scope": "call", "paired_at": "2026-10-04T09:12:00-07:00", "paired_via": "usb", "last_used": "2026-10-04T14:24:01-07:00", "last_ip": "10.0.4.17", "self": false},
    {"token_id": "27110499", "name": "iPhone", "kind": "remote", "scope": "full", "paired_at": "2026-10-04T09:30:00-07:00", "paired_via": "wifi", "last_used": "2026-10-04T14:24:03-07:00", "last_ip": "10.0.4.63", "self": true}
  ]
}
```

`self` marks the token this request came with. `last_ip` is `null` for a token only used over USB. `kind` is `"mac"`, `"remote"`, `"automation"`, or `"device"` for a token paired with a kind the bar didn't know (4.6 lets future clients send one; the bar labels it "Device" on the screen and keeps only that, not the original string). The Remote shows "Device" for it.

### 12.2 `DELETE /api/v1/clients/{token_id}`

Scope `full`. Revokes that token at once: its next request gets `401`. Any call reported over Wi-Fi with that token ends too, whatever `client` it named, and so does a Wi-Fi call from the `client` it was paired with. A call that Mac reports over USB carries on. The bar toasts "Removed" and the device's name ("Removed Mac").

```json
{"ok": true, "revoked": "74d8a526"}
```

`404 not_found` for an unknown `token_id`.

### 12.3 `DELETE /api/v1/clients/self`

Scope `call` or `full`. A client unpairing itself (the Mac app's Unpair; the Remote's "Forget this phone"). With the cookie, the reply also clears it (`Set-Cookie: tb_token=; Max-Age=0; Path=/api/`).

```json
{"ok": true, "revoked": "27110499"}
```

Forgetting all devices is on the bar only (the Devices tile, 4.8), so no single stolen token can lock everyone out.

---

## 13. The Wi-Fi setup network

While the bar shows its Wi-Fi setup screens, it runs its own network, **`MiniBar-Setup`**, and the setup page at `http://4.3.2.1/`. These endpoints exist **only then**, need no token, and are gone once the bar joins the office Wi-Fi. On the setup network the bar serves only them and `info`.

*(2026-10-05, firmware 1.0.4, lead developer: how long "only then" lasts; decisions.md, Wi-Fi.)* Once a join works, `setup/wifi` is refused at once (13.2), while `setup/networks` and `setup/state` keep answering on the setup network so the page can read the result: during the Connected screen (3 s, or until a tap) and the **15 s** the network stays up after it. Then `MiniBar-Setup` closes (about 18 s after the join) and **never opens again on its own**: not after a restart or power-off, which join the saved network, and not when the office Wi-Fi drops or can't be rejoined. Only Set up again on the bar (hold, Wi-Fi, Set up) opens it. Flashing the merged image at 0x0 wipes the saved network, so that bar starts like a new one, on the QR code (firmware README, Flash).

*(2026-10-05, firmware 1.0.1, lead developer, Proposed:)* the setup network's address is **4.3.2.1** (a /24), not 192.168.4.1. Some Android phones report "Connected, no internet" and never open the sign-in sheet when the captive-portal check's host resolves to a private address (Android's NetworkMonitor, "private IP DNS response means no internet"); see `decisions.md`, Wi-Fi. The setup network has no way out, so the address only stands in for the hosts phones check while they're on it. Phones' check paths (`/generate_204`, `/hotspot-detect.html`, `/connecttest.txt` and the like) get the `302` too, whatever their `Host`. The DHCP offer names the bar as gateway and DNS server and carries no captive-portal option (114), since RFC 8908 wants an HTTPS API address there.

Over USB the whole API keeps working during setup: calls are recorded and show once setup closes, as `decisions.md` says. Picking a status, a message, a Pomodoro action and set aside or Show again (sections 8 and 9) answer `409 in_setup` meanwhile, as the bar's own controls do ("Finish setup, or hold to skip"); so do `pair/start` and `pair` (`pair/cancel` answers `409 not_pairing`, since starting setup ends any pairing). The USB `pair` command still works.

*Open, not proposed here:* whether `MiniBar-Setup` has a password. The QR code can carry one (`WIFI:T:WPA;S:MiniBar-Setup;P:…;;`), which would keep passers-by off the setup page.

### 13.1 `GET /api/v1/setup/networks`

The networks the bar can see, strongest first.

```json
{
  "ok": true,
  "networks": [
    {"ssid": "Office-WiFi", "security": "password", "rssi": -52, "signal": "good"},
    {"ssid": "Office-Corp", "security": "work_login", "rssi": -60, "signal": "good"},
    {"ssid": "Office-Guest", "security": "open", "rssi": -63, "signal": "good"},
    {"ssid": "Printer-Direct", "security": "password", "rssi": -84, "signal": "weak"}
  ]
}
```

`security` is `"password"` (WPA2 or WPA3 Personal), `"work_login"` (WPA2 Enterprise, username and password) or `"open"`. The page warns that an open network may need a sign-in page, which MiniBar can't fill in.

### 13.2 `POST /api/v1/setup/wifi`

| Field | Type | Required | Meaning |
|---|---|---|---|
| `ssid` | string | yes | 1 to 32 bytes, with no control characters (a line break, say): `400 bad_value` otherwise. |
| `password` | string | for `password` and `work_login` | For a `password` network, 8 to 63 characters, or the key itself as 64 hex digits (WPA2 accepts both). |
| `username` | string | for `work_login` | |
| `calendar_url` | string | no | The secret iCal address, checked as in 11.2 once connected. A failure shows "Calendar address didn't work · add it on the Remote" on the bar, and nothing is saved. |
| `time_zone` | IANA name | no | The page sends the phone's (`Intl.DateTimeFormat().resolvedOptions().timeZone`). |

```json
{"ssid": "Office-Corp", "username": "alex", "password": "correct horse battery staple", "calendar_url": null, "time_zone": "America/Los_Angeles"}
```

Answers `202` at once and starts connecting; the bar shows its Connecting screen.

```json
{"ok": true, "state": "connecting"}
```

Format errors (`400 bad_request`, `400 bad_value`, and the calendar codes in 11.2) come back at once.

**Connected is the end of setup.** From the moment a join works (the bar has an address on the office Wi-Fi, which can be a moment before the Connected screen shows) until setup starts again on the bar, `setup/wifi` answers `404 not_found`, "MiniBar isn't in Wi-Fi setup anymore.", over the setup network and over USB alike, and the bar stays on Connected. So nobody else on the open network can point the bar at another network during the Connected screen, or keep the setup network up by sending again. While a join is still connecting, or after one failed (the Couldn't connect screen, or the QR code after it), the page can send again. *(2026-10-05, firmware 1.0.4: before 1.0.4 a second send during the Connected screen was taken, and sent the bar back to Connecting.)*

### 13.3 `GET /api/v1/setup/state`

The page polls this. The phone may lose the setup network while the bar connects (the bar's radio moves to the office network's channel), so the bar's own screen always shows the result too.

```json
{"ok": true, "state": "failed", "error": "wrong_password", "message": "The password didn't work.", "host": null, "ip": null}
```

```json
{"ok": true, "state": "connected", "error": null, "message": null, "host": "minibar.local", "ip": "10.0.4.42"}
```

`state` is `"idle"`, `"connecting"`, `"connected"` or `"failed"`. `error` is `null`, `"wrong_password"`, `"login_failed"` (work login refused), `"not_found"` (the network is gone), `"no_signal"` or `"no_address"` (joined but got no IP address, often a sign-in-page network).

---

## 14. Changes from the mock-up, and open questions

### 14.1 Where this contract differs from the mock-up's API section

The mock-up's "How the Mac app talks to TinyBar" and `decisions.md` need these brought in line (product manager and lead developer, next mock-up round):

1. **Paths are versioned:** `/api/v1/call` and `/api/v1/status` instead of `/api/call` and `/api/status`.
2. **The token is checked** (section 4), instead of "reserved for pairing, not checked yet". New: pairing endpoints, scopes, revoking, the pairing screen and the Devices tile.
3. **Calls from your Mac turned off is not an error.** The mock-up answered `403 {"ok": false, "error": "calls_off"}`. But the bar still records the call and the heartbeat (the mock-up does this too: the Remote shows the Mac as connected, and turning the switch on mid-call shows the call at once), so the request has succeeded. Now `200` with `"sources": {"mac": false}`. A 403 would also look like a token problem to the Mac app.
4. **`showing` no longer has `"off"`.** A dark screen is `"screen": "dark"` next to what the bar would show when woken; a powered-off bar doesn't answer at all.
5. **USB lines carry the `@tb ` marker,** so they can share the port with log output, and an `id` to match replies. `hello` carries the client's ID, an optional label, the API version and (optionally) the time. A `ready` event, `pair` and `request` are new.
6. **Call messages carry `client`** (and `session`, `seq`, `call_id`, `elapsed_s`, `leaving`), so the bar can keep several Macs apart, survive link switches and restarts, and end a call at once when the Mac sleeps.
7. **`meeting.next` is an object** with RFC 3339 times, not `"15:00"`.
8. **Errors carry `message` and `field`,** and there's a full list of codes (Appendix B).
9. **`app`:** the bar accepts up to 64 bytes and shows up to 24 characters (the mock-up's limit), cut with "…".

### 14.2 Needs the user's OK

- ~~**Pairing** (section 4) as a whole, including the `call` scope for the Mac app and pairing over USB without a code.~~ **Approved by the user on 2026-10-04.**
- **`inputs`** (mic, camera, or both). It goes beyond the decided "only on a call yes or no, and optionally the app name, leaves the Mac". The field is in the contract so the bar can accept it, but the Mac app doesn't send it until the user agrees, and the bar doesn't show it (no screen uses it yet). Useful later, for example "On camera" in the kicker, so people know not to walk behind you.
- **What else the Mac app sends:** a random install ID with a per-launch session ID and a counter (`client`, `session`, `seq`), and over USB the Mac's time and time zone, so a bar used without Wi-Fi still has a clock for the Clock screen and "since 2:04 PM". None of it says anything about calls, and none of it is a hardware ID or the computer's or user's name, but it isn't on the Mac app spec's list of what leaves the Mac yet (14.5).
- **Away's back-at time and note** (8.1).
- **The default bar name "MiniBar 2A1C"** (section 3).
- **The theme setting `display.theme`** (10.3), for the alternate theme the user decided on 2026-10-05.
- **The touch click setting `sound.tap_sound`** (10.4), for the click the user asked for on 2026-10-05, and its default (`true`, Proposed).
- **Download for Mac** (section 17): the download's address, headers and errors, and `mac_app` in `info`. The button itself and the carried app are decided.

### 14.3 Needs design (UX designer, then the mock-up)

- The **pairing screen** (4.8).
- The **Devices tile** in the Wi-Fi menu, with its confirm step (4.8).
- The Remote's **pairing prompt** ("Type the code on your TinyBar") and **Paired devices** list (section 12).
- The Remote's way to enter **Away's back-at time and note**, if the user agrees.
- The Remote's error message for **unsupported characters** in a message (8.2).
- The bar's **name and real address** ("TinyBar 2A1C", `tinybar.local`) on the Connected screen and the Wi-Fi menu's Network tile, so you can tell which bar to pick in the Mac app when an office has several.
- The Remote's **Pair a Mac** button (`docs/mac-app.md`) becomes instructions: "In the Mac app, choose Pair with a code. This TinyBar will show the code." (14.5).

### 14.4 Open

- **HTTPS.** Version 1 is plain HTTP on the office network (4.10). A later version could serve HTTPS with a self-signed certificate whose fingerprint the client pins at pairing: easy for the Mac app, but browsers warn about self-signed certificates, so the Remote page would need more thought.
- **A password on `MiniBar-Setup`** (section 13).
- **Pushed updates** (WebSocket) instead of polling (7.2).
- **Bars in the same office:** each picks its own `.local` name, but the Remote's address is no longer simply `minibar.local` when a second bar is around. The bar shows its real address; the mock-up could say so.
- **Unverified on the hardware** (6.1, 6.3): whether opening or closing the port from macOS resets the bar, and whether the USB serial number is the MAC address.
- **Two Macs, one bar** (open in `decisions.md`): settled at the protocol level. The bar keeps each Mac's state by `client` (5.2), so an idle second Mac can't end the first Mac's call. The Mac app itself needs nothing extra.

### 14.5 Where this contract differs from the Mac app spec

`docs/mac-app.md` was written before this file and follows the mock-up's wire format; where they differ, this file wins (`decisions.md`, Mac app). The spec's acceptance criteria 12, 14, 18 and 22 need updating:

1. **Call messages have more fields:** `client` (required), and `session`, `seq`, `call_id`, `elapsed_s` and `leaving` (section 5), so criterion 12's golden messages and "no other fields are sent" change. `client` is a random install ID, not a hardware ID, so it fits the spec's privacy rules; the spec's "What leaves the Mac" should list it.
2. **Paths are `/api/v1/…`**, and USB lines carry the `@tb ` marker (6.4).
3. **`calls_off` is gone:** Calls from your Mac turned off is a `200` with `"sources": {"mac": false}` (14.1, item 3). The menu text stays the same.
4. **The Wi-Fi pairing code is shown on the bar, not on the Remote,** and lasts 2 minutes, not 5. The Remote has to pair too, and the only display that proves someone is at the desk is the bar's, so the bar's pairing screen is needed anyway; one code flow is less to build and test, and pairing a Mac doesn't need a phone. The Mac app's "Pair with a code…" asks the chosen bar to show a code (`pair/start`), then sends what you type (`pair`).
5. **Pairing replies say `device_id`, not `id`** (`id` is the USB request ID), and a wrong code is `wrong_code`, not `bad_code`.
6. **The bar ID is `device_id`** in `info`, `hello`, every `call` reply and `status`; the short display form is the bar's `name` ("TinyBar 2A1C").
7. **`hello` is retried** every 2 seconds for up to 10 seconds before a port is given up, not a single 2-second wait, because a bar that has just started may not answer at once; the `ready` event (6.7) tells the app when it can. Criterion 14 changes accordingly.
8. **Sleep and quit** send `"leaving": true` with `"active": false`, which also marks the Mac not connected at once.
9. **Time in `hello`** (**Proposed**, 14.2): the spec doesn't send it yet.

### 14.6 The rename to MiniBar (2026-10-05)

The product was renamed from TinyBar to MiniBar on 2026-10-05 (`decisions.md`, Product), and this file with it. What changed on the wire, all in firmware 1.0.3: `device` is `"MiniBar"` (7.1), the 401 realm is `MiniBar` (4.5), the host name is `minibar.local` (`minibar-2.local` after a clash) and the service `_minibar._tcp` (section 3), the setup network is `MiniBar-Setup` (13), the default bar name is "MiniBar" plus the last four of the ID ("MiniBar 2A1C"), the User-Agent the Mac app sends is `MiniBarMac/<version> (api 1.0)` (2.1), and its Keychain service is `MiniBar` (16). What didn't: the paths, the `@tb ` line marker, the `tb1_` token prefix, the `tb_token` cookie and every token already issued, so a paired Mac keeps working over USB. For one release the Mac app **accepts both `device` values** ("MiniBar" and "TinyBar") and **browses both service types** (`_minibar._tcp` first, then `_tinybar._tcp`), because a bar on firmware 1.0.2 or earlier still answers with the old name; the firmware advertises only the new type. A bar whose stored name is still the old default ("TinyBar" plus its four characters) takes the new default once at start-up; a name a person typed is left alone. The host-name change signs paired phones out of the Remote (the cookie is per host): the phone pairs again once, and its old row can be removed on Paired devices. The entries above that quote the old name record what the mock-up and the Mac app spec said at the time and are kept as written.

---

## 15. Notes for the firmware

- **One router.** HTTP handlers and the USB line reader both turn a request into (method, path, body, auth) and call the same router, which returns (status, JSON). `call`, `status` and `hello` over USB are aliases. This keeps both links identical by construction.
- **esp_http_server** runs every handler in one task, so a slow handler stalls every client. Keep handlers short; calendar fetches, Wi-Fi joins and anything else slow run in their own tasks and report through state that `GET` reads (hence the `202`s). Raise `CONFIG_HTTPD_MAX_REQ_HDR_LEN` from its 512-byte default to 2048 (phone browsers' headers alone can pass 512), keep `max_open_sockets` at 7 with `lru_purge_enable`, and check `Host` (2.2) before routing.
- **JSON:** cJSON is fine at these sizes. Reject a body over 2,048 bytes before parsing it.
- **Tokens:** generate them with `esp_fill_random` only while the RF is on (Wi-Fi started), or after `bootloader_random_enable()` (needed for USB pairing with Wi-Fi skipped; it borrows the SAR ADC, so disable it again right after). Store only SHA-256 hashes, in an NVS namespace of their own, encrypted along with the calendar address (NVS encryption with the HMAC-based scheme the ESP32-S3 supports). Compare hashes in constant time. Rate-limit and back-off state stays in RAM.
- **USB:** install the `usb_serial_jtag` driver and read lines in a task of its own. Route ESP-IDF logging through the same writer as protocol replies (`esp_log_set_vprintf`), behind one mutex that also tracks whether the last byte written was a line break (6.4). **Never block on USB writes:** when no program is reading, the TX buffer fills; use a short time-out and drop log output rather than stall the UI or the router. Release builds log at Warning level, so the port stays mostly quiet. Never log tokens, the calendar address, Wi-Fi passwords or raw protocol lines.
- **Macs:** keep each Mac's state (up to 4) in RAM, keyed by `client`, with its link, last heard, session and seq, call state, app, `call_id` and start time. Run the 90-second time-out from a timer, not from incoming messages.
- **mDNS:** use the `espressif/mdns` component: host name `minibar`, instance name `device.name`, `_minibar._tcp` and `_http._tcp` on port 80 with the TXT record in section 3. Update the TXT record if the name changes, and report the host name mDNS ended up with after a conflict.
- **Calendar:** Google's secret iCal feed holds every past event too and can run to megabytes. Fetch it with `esp_http_client` over TLS (with the certificate bundle) and **parse it as a stream**, keeping only today's and tomorrow's events that count; don't buffer the file. TLS needs roughly 40 KB of RAM while it runs; use PSRAM for buffers.
- **Time:** SNTP when on Wi-Fi, the PCF85063 clock chip otherwise, set from the Mac's `hello` only under the rule in 6.6. Turn the IANA time zone into a POSIX TZ string with a built-in table (the posix_tz_db list is tens of KB, so keep only the zones people are likely to use if flash gets tight).
- **Text:** apply the mapping in 2.3, and measure strings in the font that will draw them, to decide when to cut the app name at 24 characters and which characters count as unsupported.

## 16. Notes for the Mac app

- **Which link:** USB when a MiniBar answers `hello` on it, else Wi-Fi if paired, one at a time (6.8). Send the full state right after every switch.
- **Heartbeat:** send on every change and every `heartbeat_s` (30) seconds, idle or not; keep `seq` going up within a `session`; send `"leaving": true` on quit and on `NSWorkspace.willSleepNotification` and `willPowerOffNotification`, and a fresh state on `didWakeNotification`. Only one request in flight at a time.
- **Pairing:** when the bar is on USB and the app has no token, pair over USB at once (6.6). Over Wi-Fi only, pair with the code (4.2), showing "Look at your MiniBar and type the code it shows".
- **Keychain:** one generic-password item per bar: service `MiniBar`, account = `device_id`, accessible after first unlock on this device only (`kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly`). *(Note 2026-10-04, Mac app only, no change to the wire format: an ad-hoc-signed app can't use the data-protection keychain, and the file-based login keychain it uses ignores this attribute (Apple TN3137), so "this device only" isn't enforced there. The item stays in the user's login keychain.)*
- **On `401`,** first check `GET /api/v1/info`: if its `device_id` isn't the paired bar's, the address now belongs to another bar (an office network can hand the same address to a different device), so find the right bar again and keep the token. Only when the paired bar itself refuses the token, delete it and ask to pair again. Check `device_id` in every `call` reply for the same reason.
- **On `403 wrong_client` from a call** (5.2), do the same as on `401`: the token was paired for another install ID (the app's settings were reset while the Keychain kept the token), so check `info`, drop the token and offer to pair again. The app may also send `DELETE /api/v1/clients/self` with the refused token first, best effort, so the stale token doesn't keep one of the bar's 10 places or show twice in the Remote's Paired devices; the bar accepts it and toasts "Removed Mac", as for any self-unpairing. *(2026-10-05: this is what the Mac app does; recorded here so the contract names it.)*
- **Discovery:** browse `_minibar._tcp` (for example with `NWBrowser`) and match `id` to the paired `device_id` (section 3). macOS 15 asks the user for **Local Network** access: add `NSLocalNetworkUsageDescription` and list `_minibar._tcp` under `NSBonjourServices` in Info.plist. Without that permission, Wi-Fi fails silently, so the app explains it.
- **USB:** find and open the port as in 6.1 to 6.3. Never touch DTR or RTS, and offer Pause USB for flashing.
- **Retries:** on a network error, retry at the next heartbeat; back off to at most every 30 seconds while the bar can't be reached, and say so in the menu ("Can't reach MiniBar · last sent 2:04 PM").
- **What the menu shows** comes from the latest reply (5.3), and the Mac's connection line from `status.macs` if the app reads it.

---

## 17. Download for Mac

**Status:** the button and the carried app are decided (2026-10-05, `decisions.md`, Mac app › Download for Mac); the address, headers and `info` fields in this section are **Proposed**, written by the product manager and waiting for the lead developer's review and the user's OK.

The bar carries the Mac app's zip in its own flash and hands it out on the office Wi-Fi, so a Mac gets the app from the bar itself. The Remote's **Download for Mac** button (in Connect your Mac, and as a link on the pairing prompt) links to it, with a helper line made from `mac_app` (17.2). Detecting a call still needs the app running on the Mac; this only makes installing it easy.

- **Where the copy lives:** a read-only partition after the two app slots (`0xC30000`, `0x3D0000` long), starting with a small header the firmware checks once at start-up (its length and the zip's SHA-256), so a blank or damaged copy is never served. It's flashed as a file of its own (`dist/minibar-macapp-<version>.bin` at `0xC30000`); the usual merged image at `0x0` doesn't carry it, an app-only flash at `0x30000` leaves it alone, and so does a firmware built without the zip.
- **The copy only changes when the bar is flashed,** which restarts it, so clients read `mac_app` once (at load, and after the bar comes back) rather than polling for it.

### 17.1 `GET /mac/app.zip`

**Proposed.** No token. The path is outside `/api/`, because every `/api/` reply is JSON (2.2) and this one is a file, and it carries no product name, so a rename doesn't move it; the readable name travels in `Content-Disposition`. Like the Remote page, it needs no pairing: the file is the project's own app with nothing private in it, and a Mac that comes to get it usually isn't paired, so pairing its browser would only use up one of the 10 places (4.3). The host check applies (2.2): another `Host` gets a plain-text `421`.

```http
GET /mac/app.zip HTTP/1.1
Host: minibar.local
```

```http
HTTP/1.1 200 OK
Content-Type: application/zip
Content-Length: 1834211
Content-Disposition: attachment; filename="MiniBar-1.0.zip"
ETag: "9f2e…"
Cache-Control: no-cache
Accept-Ranges: bytes
X-Content-Type-Options: nosniff
```

The body is the zip, byte for byte. The `ETag` is the zip's SHA-256 in 64 hex digits, the same as `mac_app.sha256`.

| Request | Answer |
|---|---|
| `GET`, the app carried | `200` with the headers above and the zip. |
| `HEAD` | The same headers, no body. |
| `If-None-Match` with the `ETag` | `304 Not Modified`, no body. |
| One range: `Range: bytes=a-b`, `bytes=a-` or `bytes=-n` | `206 Partial Content` with `Content-Range: bytes a-b/<length>` and the matching `Content-Length`, so a browser can resume a download that broke off. |
| A range that starts past the end | `416 Range Not Satisfiable` with `Content-Range: bytes */<length>`, no body. |
| More than one range, a `Range` the bar can't read, or an `If-Range` that isn't the `ETag` | `200` with the whole file. |
| No app carried (`mac_app` is `null`) | `404`, plain text: "This MiniBar doesn't carry the Mac app." |
| A damaged copy (`mac_app.state` is `"damaged"`) | `404`, plain text: "The Mac app on this MiniBar is damaged, so it can't be downloaded. Flash its file onto the bar again." |
| Another download is running | `503`, plain text: "Another download is in progress. Try again in a few seconds.", with `Retry-After: 5`. One download at a time. |
| Any other method | `405` with `Allow: GET, HEAD`. |
| On the setup network (section 13) | The `302` to the setup page, as for every other path. `MiniBar-Setup` never serves the zip. |

- **Errors are plain text** (`text/plain; charset=utf-8`), not the JSON of 2.4, since the path is outside `/api/` and a browser that follows the link shows the line as it is.
- **The file name** comes from the header and is checked twice (by the build tool and by the firmware): only letters, digits, `.`, `_` and `-`, at most 63 characters, ending in `.zip`. If it ever isn't, the bar sends `app.zip`.
- **No CORS headers,** as for everything the bar serves (2.2).
- **For the firmware:** the zip is sent in pieces straight from the memory-mapped partition, never copied into RAM, by a task of its own below the screen, touch and sound, so the screen keeps time and every other request (the Remote's polling, a Mac's calls) still answers within 1 second (2.5) during a download. A client that takes nothing for 3 seconds is dropped (the existing send time-out), and a download is ended after 5 minutes, so nobody can hold the one download slot for long.

### 17.2 `mac_app` in `GET /api/v1/info`

**Proposed.** `info` (7.1, no token, so the pairing prompt can read it) and the USB `hello` reply, which is the same object, gain `mac_app`: what the bar carries, from the partition's header and the start-up check.

```json
"mac_app": {"state": "ok", "reason": null, "version": "1.0", "build": "12", "bytes": 1834211, "archs": ["arm64", "x86_64"], "min_macos": "14", "file": "MiniBar-1.0.zip", "path": "/mac/app.zip", "sha256": "9f2e…"}
```

```json
"mac_app": null
```

```json
"mac_app": {"state": "damaged", "reason": "hash", "version": "1.0", "build": "12", "bytes": 1834211, "archs": ["arm64"], "min_macos": "14", "file": "MiniBar-1.0.zip", "path": "/mac/app.zip", "sha256": "9f2e…"}
```

| Field | Meaning |
|---|---|
| `mac_app` | `null` when the bar carries no app (a partition that was never flashed, or was erased). Otherwise this object, with every key present (2.3). |
| `state` | `"ok"`, or `"damaged"` when the copy failed the start-up check. *Open:* a client treats an unknown value like `"damaged"`, and offers no download. |
| `reason` | `null` when `ok`. For `damaged`, the check that failed: `"header"`, `"format"`, `"length"` or `"hash"`. For logs; the Remote says the same thing for all four. |
| `version`, `build` | The app's version and build as the app itself reports them (`CFBundleShortVersionString`, `CFBundleVersion`): the Remote shows "MiniBar for Mac 1.0 (12)". `build` is a string, since Apple allows dotted builds. Both `null` when the header itself failed (`"header"`, `"format"`), and likewise the fields below. |
| `bytes` | The zip's size. The Remote shows it as Finder does: decimal megabytes with one decimal ("1.8 MB"), or whole kilobytes under 1 MB. |
| `archs` | The Macs it runs on: `"arm64"` (Apple silicon), `"x86_64"` (Intel), or both, for a build made with `UNIVERSAL=1`. The Remote shows "Apple silicon and Intel", "Apple silicon" or "Intel". |
| `min_macos` | The oldest macOS it opens on, as `"14"`. The Remote shows "macOS 14 or later". |
| `file` | The download's file name, the same as in `Content-Disposition`: `"MiniBar-1.0.zip"`. |
| `path` | Where to get it: `"/mac/app.zip"`. Relative, so the link works at `minibar.local`, `minibar-2.local` or the bar's IP address alike. |
| `sha256` | The zip's SHA-256 in 64 hex digits, also the `ETag`, so a script can check what it saved (`shasum -a 256`). |

- **The Remote** shows Download for Mac only when `state` is `"ok"`. With `null` or `"damaged"` it shows no button anywhere, and the Connect your Mac card says why in one line (the mock-up's Remote has the words).
- **Version:** a new field, so an addition under 2.1 (API 1.1). Clients that speak 1.0 ignore it.
- **The Mac app needs nothing.** It ignores fields it doesn't know (2.1), and it doesn't check for a newer copy on the bar in version 1.

---

## Appendix A: every endpoint

| Method and path | Token scope | Over USB | Section |
|---|---|---|---|
| `GET /api/v1/info` | none | `hello` | 7.1 |
| `POST /api/v1/pair/start` | none | `request` (pointless: use `pair`) | 4.6 |
| `POST /api/v1/pair` | none | `request` (likewise) | 4.7 |
| `POST /api/v1/pair/cancel` | none | `request` (likewise) | 4.7 |
| — | — | `pair` | 6.6 |
| `GET /api/v1/status` | `call`, `full` | `status` | 7.3 |
| `POST /api/v1/call` | `call`, `full` | `call` | 5 |
| `POST /api/v1/status` | `full` | `request` | 8.1 |
| `POST /api/v1/message` | `full` | `request` | 8.2 |
| `POST /api/v1/aside` | `full` | `request` | 8.3 |
| `POST /api/v1/pomodoro` | `full` | `request` | 9.1 |
| `GET /api/v1/settings` | `full` | `request` | 10.1 |
| `PATCH /api/v1/settings` | `full` | `request` | 10.2 |
| `GET /api/v1/calendar` | `full` | `request` | 11.1 |
| `PUT /api/v1/calendar` | `full` | `request` | 11.2 |
| `DELETE /api/v1/calendar` | `full` | `request` | 11.3 |
| `POST /api/v1/calendar/sync` | `full` | `request` | 11.4 |
| `GET /api/v1/calendars` | `full` | `request` | 11.5 |
| `POST /api/v1/calendars` | `full` | `request` | 11.5 |
| `PATCH /api/v1/calendars/{id}` | `full` | `request` | 11.5 |
| `DELETE /api/v1/calendars/{id}` | `full` | `request` | 11.5 |
| `GET /api/v1/clients` | `full` | `request` | 12.1 |
| `DELETE /api/v1/clients/{token_id}` | `full` | `request` | 12.2 |
| `DELETE /api/v1/clients/self` | `call`, `full` | — (USB has no token) | 12.3 |
| `GET /api/v1/setup/networks` | none, setup network only | `request` | 13.1 |
| `POST /api/v1/setup/wifi` | none, setup network only | `request` | 13.2 |
| `GET /api/v1/setup/state` | none, setup network only | `request` | 13.3 |
| `GET /mac/app.zip` and `HEAD` (Proposed; outside `/api/`, plain-text errors) | none | — (USB carries no files) | 17.1 |

A method a path doesn't support gets `405 method_not_allowed` with an `Allow` header, `OPTIONS` and `HEAD` included (a reply to `HEAD` has no body); an unknown path under `/api/v1/` gets `404 not_found`.

## Appendix B: error codes

| HTTP | `error` | Meaning |
|---|---|---|
| 400 | `bad_json` | The body or line isn't a JSON object. |
| 400 | `bad_request` | A required field is missing or has the wrong type. |
| 400 | `bad_value` | A value is out of range, too long, or not one of the allowed values. |
| 400 | `unsupported_chars` | Text has characters the bar can't draw; `chars` lists them. |
| 400 | `not_a_url`, `http_not_allowed`, `public_address`, `not_ics` | Calendar address format (11.2). |
| 401 | `unauthorized` | No token, or an unknown or revoked one. Pair again. |
| 403 | `wrong_scope` | The token's scope doesn't allow this endpoint. |
| 403 | `bad_origin` | A cookie-authenticated request from another origin. |
| 403 | `wrong_code` | Pairing code doesn't match; `attempts_left`. |
| 403 | `wrong_client` | A call's `client` isn't one this token may report for (5.2). |
| 404 | `not_found` | Unknown path or `token_id`; or `setup/wifi` once a join has worked, or outside setup (13.2). |
| 405 | `method_not_allowed` | See the `Allow` header. |
| 409 | `pairing_busy` | Another code is on screen; `retry_after_s`. |
| 409 | `not_pairing` | No pairing in progress for that `pairing_id`. |
| 409 | `token_limit` | 10 tokens already, counting a code on the screen (4.3). |
| 409 | `in_setup` | The bar is on its Wi-Fi setup screens. |
| 409 | `no_message` | No message was ever set. |
| 409 | `nothing_to_set_aside`, `nothing_set_aside` | 8.3. |
| 409 | `not_running`, `nothing_to_extend` | 9.1. |
| 409 | `no_calendar` | No calendar address saved. |
| 409 | `calendar_limit`, `already_used`, `several_calendars` | Several calendars (11.5). |
| 413 | `too_large` | Body or USB line too long. |
| 415 | `unsupported_media_type` | The body isn't sent as `application/json`. |
| 421 | `wrong_host` | The `Host` header isn't the bar's (2.2). |
| 429 | `rate_limited` | Too many requests or failed pairings; `retry_after_s`. |
| 500 | `internal` | A firmware bug. The `message` helps the firmware team. |
| 503 | `offline` | Needs Wi-Fi, and the bar has none. |
| 503 | `busy` | The bar is starting up, saving or powering off (`pair/start` on the Powering off screen, 4.6); `retry_after_s` 1. |
| — | `unknown_cmd`, `unsupported_api` | USB only (6.6). |
| — | `calendar_rejected`, `calendar_unreachable`, `not_a_calendar` | Calendar check and sync results, reported in `GET /api/v1/calendar` (11.2), never as an HTTP status. |
