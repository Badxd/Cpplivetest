# cppgram live-test bot: final report

## Verdict

The bot is implemented and builds with GCC and Clang. Its offline unit tests pass, including under ASan, UBSan and
TSan. It has not been run against Telegram. No live test is PASS. It has not been deployed. Use the
deployment steps in README.md to deploy it on your VPS.

## Delivered

| Area | Files | Status |
|---|---|---|
| Bot code (C++20, uses `cppgram::Client`, `login_bot`, `get_me`, `on_message`, `export_session_string`, `ClientOptions::session_string`, `ClientOptions::mtproto_proxy`, `on_rpc_error`, `stop`) | `src/*.cpp`, `src/*.h` | IMPLEMENTED, OFFLINE TESTED |
| Admin-only commands, one-test lock, sequential runner, exception isolation | `src/commands.*`, `src/test_runner.*`, `src/main.cpp` | IMPLEMENTED, OFFLINE TESTED (logic). LIVE: BLOCKED |
| Session string flow (export, 0600 file, restore into fresh client, identity check, secure delete) | `src/tests.cpp` | IMPLEMENTED. LIVE: BLOCKED |
| FloodWait (offline mock; passive live observation via `on_rpc_error`) | `src/tests.cpp`, `src/main.cpp` | IMPLEMENTED, OFFLINE TESTED. LIVE FLOODWAIT: NOT OBSERVED |
| MTProxy profiles A (plain) and B (dd), with stage names in failure details | `src/proxy_profile.*`, `src/tests.cpp` | IMPLEMENTED. LIVE: BLOCKED |
| Redaction of logs, chat replies, stored results | `src/log.*`, `src/state.cpp`, `src/test_runner.cpp` | IMPLEMENTED, OFFLINE TESTED |
| Structured logs (0600), `results.jsonl` | `src/log.cpp`, `src/main.cpp` | IMPLEMENTED |
| Graceful shutdown (SIGTERM, watcher thread, joins the runner) | `src/main.cpp` | IMPLEMENTED. Shutdown path not run live |
| Update-loop restart (bounded, with backoff) | `src/main.cpp` | IMPLEMENTED. Not exercised live |
| Deployment: env template, systemd unit, install script, control helper, logrotate | `deploy/` | IMPLEMENTED. Env checker OFFLINE TESTED. Install and systemd: NOT RUN |
| Documentation | `README.md`, `COMMANDS.md`, `TESTS.md`, `LIBRARY_NOTES.md`, `TEST_RESULTS.md` | COMPLETE |

## Library

The bot does not modify CppGram. It builds against an unmodified tree. `CPPGRAM_SOURCE_DIR` builds the library from
source, and `CPPGRAM_PREBUILT_DIR` links an already built `libcppgram.a`. The prebuilt library used in the tests was
built from the attached `cppgram-mtproxy-compatibility-fixed` tree, which is identical to the library source in the files you supplied (checked with a recursive diff, excluding build output). No library file, header or
test was changed.

Observed library behaviour and limits are in `LIBRARY_NOTES.md`. The most important is the open
`plain packet has N bytes after its length field` (N = 22, 102) error. The bot captures its text and the stage. It does
not change the parser or the 15-byte rule. The root cause is still not determined. Live bytes are still needed.

## Security review

- No credentials, hashes, tokens, session strings, keys or proxy secrets are in the source, the docs, the test data or
  the package. The only secret-shaped value is a fixed test secret in the unit tests (`0123...`, `dd0123...`).
- Secrets are registered for redaction at startup. Logs, chat replies and stored results pass through redaction.
  Tested: a registered secret does not reach stored results or `/status`.
- Session strings are written only to a 0600 file under a 0700 directory, read back for the restore check, and
  overwritten with zeros before deletion.
- The env file is root-owned, mode 600. The service runs as a system user with hardening options.
- Non-admin commands get no reply.
- Parsing is bounded: command text, config values and results are length-checked. Frame and packet parsing belongs to
  the library, which was not changed.
- The bot does not touch `/opt/MTProxy`. The installer touches only the bot's own paths and its own unit.

## Remaining work (needs the VPS and credentials)

1. Fill `/etc/cppgram-livebot.env` on the VPS. Run `install.sh`, then `livebotctl.sh start`.
2. Run `/test direct`, `/test login`, `/test session`, `/test reconnect`, `/test message`, then `/test proxy-plain` and
   `/test proxy-dd` if you have endpoints. Send `/report` and `results.jsonl` back, with secrets removed.
3. Watch for `FLOOD_WAIT` in `/health` and `results.jsonl`. Keep the status NOT OBSERVED until one appears.
4. Keep the service running for at least one hour. Check `/health` and `journalctl`. Then restart and reboot to
   check recovery.
5. For the open MTProxy error, capture the failing attempt's detail text from `/report` or `results.jsonl`. It
   contains the byte counts the library's investigation needs. Do not send the secret.
