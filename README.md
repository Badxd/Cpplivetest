# cppgram live-test bot

A dedicated MTProto bot that uses the CppGram C++20 library to test real Telegram behaviour from a VPS:
bot login, `get_me`, messaging, session export and restore, reconnect, FloodWait handling, and two MTProxy
profiles (plain 16-byte secret, and padded-intermediate "dd" secret).

The CppGram library is the dependency under test. This project does not modify it, and it has no copy of
its source. Point the build at your own unmodified CppGram tree.

## Status

Read `FINAL_REPORT.md` and `TEST_RESULTS.md` for what has been run and what is BLOCKED. A compiled bot is not
a deployed bot, and a direct connection is not proof of MTProxy compatibility.

## Layout

```
CMakeLists.txt                 bot build (library built from CPPGRAM_SOURCE_DIR, or linked prebuilt)
build.sh                       build + offline unit tests
src/                           bot source (config, logging+redaction, state, commands, test runner, tests, main)
tests/unit_tests.cpp           offline tests (no network)
deploy/env.example             environment template (no real values)
deploy/cppgram-livebot.service systemd unit (hardened, restart on failure)
deploy/install.sh              creates user, directories, installs binary and unit, enables at boot
deploy/livebotctl.sh           check-env | start | stop | restart | status | logs | results | health
deploy/logrotate.conf          log rotation
docs: COMMANDS.md, TESTS.md, LIBRARY_NOTES.md, TEST_RESULTS.md, FINAL_REPORT.md
```

## Build (Ubuntu 24.04)

Build dependencies (install them yourself; this project does not install packages):

```sh
sudo apt-get install -y build-essential cmake libboost-dev libcurl4-openssl-dev \
    nlohmann-json3-dev libssl-dev zlib1g-dev libsqlite3-dev
```

Build the bot, with the library built from your unmodified CppGram tree:

```sh
CPPGRAM_SOURCE_DIR=/path/to/cppgram ./build.sh
```

Or link an already built `libcppgram.a`:

```sh
CPPGRAM_PREBUILT_DIR=/path/to/lib CPPGRAM_PREBUILT_INCLUDE_DIR=/path/to/cppgram/include ./build.sh
```

`build.sh` runs the offline unit tests with `ctest`. The binary is `build/cppgram_live_test_bot`.

## Deploy (after the build)

```sh
sudo ./deploy/install.sh /path/to/build/cppgram_live_test_bot
sudo nano /etc/cppgram-livebot.env          # fill every CHANGE_ME, keep mode 600
sudo /opt/cppgram-livebot/livebotctl.sh check-env
sudo /opt/cppgram-livebot/livebotctl.sh start
sudo /opt/cppgram-livebot/livebotctl.sh status
```

The service is enabled at boot. `install.sh` does not start it. It does not touch `/opt/MTProxy` or any other
service. Layout:

- `/opt/cppgram-livebot/` binary, helper, README (root-owned)
- `/etc/cppgram-livebot.env` secrets (root, mode 600)
- `/var/lib/cppgram-livebot/` session files and temp files (cppgram-livebot, mode 700)
- `/var/log/cppgram-livebot/` `livebot.log` and `results.jsonl` (mode 600, rotated by logrotate)

## Configuration

See `deploy/env.example`. Variables:

| Name | Meaning |
|---|---|
| `TG_API_ID`, `TG_API_HASH` | Telegram API credentials |
| `TG_BOT_TOKEN` | Token of a dedicated test bot |
| `TG_ADMIN_IDS` | Comma-separated admin user ids. Only these get replies |
| `TG_DC` | Data centre 1..5 for the direct connection (default 2) |
| `TG_DATA_DIR`, `TG_LOG_DIR` | Absolute paths |
| `TG_FLOOD_MAX_WAIT_SECONDS` | Longest FLOOD_WAIT the library will sleep through |
| `TG_PROXY_PLAIN_HOST/PORT/SECRET` | Profile A (16-byte secret). All three or none |
| `TG_PROXY_DD_HOST/PORT/SECRET` | Profile B (dd secret). All three or none |
| `TG_DEBUG` | `1` prints library stage lines to stderr |

Secrets are never printed. The bot registers them for redaction at startup, and it also masks any long
token-like run in log lines and chat replies.

## Operation

- Commands are admin-only, and non-admins get no reply. See `COMMANDS.md`.
- One test runs at a time. A second request gets "busy". The update loop keeps answering while a test runs,
  because each test uses its own client on a worker thread.
- Results go to chat through `/report`, and to `results.jsonl`.
- SIGTERM (`systemctl stop`) stops the update loop and waits for a running test. systemd kills the process
  after `TimeoutStopSec`.
- An unexpected error in the update loop is logged and the loop restarts up to 5 times, with backoff.

## Security notes

- The env file is root-owned and mode 600. The service runs as an unprivileged system user with
  `NoNewPrivileges`, `ProtectSystem=strict` and `PrivateTmp`.
- Session data lives in `/var/lib/cppgram-livebot/tmp` (mode 700). Files are written mode 600, and they are
  overwritten with zeros before deletion. This is best effort on journaled file systems.
- The session string is exported only into a 0600 temporary file and read back for the restore test. It is
  never printed, never sent through Telegram, and never placed in a report or in the log.
- Non-admin commands produce no reply. The attempt is logged with the sender id only.

## Limits

See `LIBRARY_NOTES.md` for CppGram behaviour that the bot observed but did not change. The most important one
is the unresolved `plain packet has N bytes after its length field` error (N = 22, 102). The bot records
its message text and the stage in which it occurred, and it does not change the parser.
