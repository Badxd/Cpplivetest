# Test catalogue

Status labels used everywhere: PASS (executed and passed), FAIL (executed and failed), BLOCKED (cannot run because
of a missing credential or endpoint), SKIP (not run in this context, see detail), NOT RUN (not executed).

## Offline (`livebot_unit_tests`, run by `ctest` and `build.sh`)

Covers: config loading and validation, bot-token and API-hash shapes, MTProxy profile checks (plain, dd, fake-TLS,
wrong kind, reasons never echo the secret), command parsing (aliases, `@bot` suffix, unknown and incomplete
commands), admin authorization, test-name round trip, redaction (registered secrets, token shape, long runs,
short-secret rule), result formatting and truncation, the FloodWait classification and policy mock, private file
permissions and shredding, runner lock (overlapping start refused), runner exception isolation (a throwing test
does not stop the next one), registered secrets never reaching stored results, report and JSON-line formatting,
offline outcomes (proxy BLOCKED without a profile, FAIL on a wrong-kind profile without network, message SKIP,
flood mock PASS with the live status stated).

## Live (need credentials and network; see TEST_RESULTS.md for what was run)

| Test | What it proves | Needs |
|---|---|---|
| direct | TCP, key exchange and initConnection to the DC | API id and hash |
| login | Bot login and `get_me`, id matches startup | API id, hash, bot token |
| session | Export, file storage, restore into a fresh client, identity match | same |
| reconnect | Controlled disconnect and connect, identity unchanged | same |
| flood | Offline mock. Live FloodWait is only recorded if one happens naturally | same |
| message | Send and read back a controlled message | same, and the admin has started the bot |
| proxy-plain | Connect through profile A, login, `get_me`, reconnect | profile A |
| proxy-dd | Connect through profile B, login, `get_me`, reconnect | profile B |

Not covered by these tests: network-drop recovery under `run()` with `auto_reconnect` (the library's own live test
covers it), a one-hour soak, and systemd restart and reboot behaviour. Those need the VPS.

## Diagnostics for the open MTProxy issue

When a proxy test fails, the result detail contains the stage name (`tcp+handshake`, `login_bot/get_me`,
`reconnect`) and the library's own error text. For `plain packet has N bytes after its length field` that text
includes N, which is the diagnostic number the library investigation needs. The bot does not add or change any
byte counts itself. `TG_DEBUG=1` adds the library's stage lines to stderr. Those lines carry stage names and sizes,
not secrets.
