# Test results

Labels: PASS = executed and passed. FAIL = executed and failed. BLOCKED = cannot run here (missing credential,
endpoint or environment). SKIP = not run in this context. NOT RUN = not executed.

## 1. Build and offline (executed in this environment)

| Check | Toolchain | Command | Result |
|---|---|---|---|
| Bot build, library linked prebuilt from the unmodified attached tree | GCC 13.3.0, Release, C++20 | `CPPGRAM_PREBUILT_DIR=<lib> ./build.sh` | PASS, 0 warnings |
| Bot build and unit tests | Clang 18.1.3, Release, C++20 (linked against the GCC-built library) | `CXX=clang++ CC=clang ./build.sh` | PASS, 0 warnings |
| Offline unit tests | GCC Release | `ctest` / `livebot_unit_tests` | PASS: 132 of 132 checks, 0 failures |
| Offline unit tests under ASan + UBSan | GCC 13.3.0, Debug, `-fsanitize=address,undefined` | `livebot_unit_tests` | PASS: 132 of 132, no sanitizer reports |
| Offline unit tests under TSan | GCC 13.3.0, Debug, `-fsanitize=thread` | `livebot_unit_tests` | PASS: 132 of 132, no sanitizer reports |
| Config error path | GCC Release | run with empty environment | PASS: exit 2, message names `TG_API_ID` only |
| Startup with fake credentials (no network) | GCC Release | run with well-formed fake values | PASS: exit 1 after the library's connect timeout. Log contains the token and the hash 0 times |
| Env checker | bash | `livebotctl.sh check-env` on placeholder, bad-mode and complete files | PASS: rejects placeholders and mode 644, accepts a complete mode-600 file. Values are never printed |
| Shell scripts | bash | `bash -n` | PASS |
| systemd unit | systemd-analyze | `systemd-analyze verify` | BLOCKED for a full check: the binary path does not exist until install. Syntax is not verified beyond that |

Coverage note: the unit tests exercise the logic of every command, the authorization and the runner. They do not
exercise the update loop with a Telegram connection.

## 2. Live Telegram (not run)

No API id, API hash, bot token, admin id, or MTProxy endpoint is configured in this environment, and no VPS access is
available. Every live row is BLOCKED. None is PASS.

| Test | Plain proxy profile | dd proxy profile | Direct (no proxy) |
|---|---|---|---|
| Connection | BLOCKED | BLOCKED | BLOCKED |
| Bot login | BLOCKED | BLOCKED | BLOCKED |
| get_me | BLOCKED | BLOCKED | BLOCKED |
| Messaging (`/test message`) | — | — | BLOCKED |
| Session export and restore | — | — | BLOCKED |
| Reconnect | BLOCKED | BLOCKED | BLOCKED |
| FloodWait handling (offline mock) | — | — | PASS for the offline mock only. Live: NOT OBSERVED (no live run) |
| Plain MTProxy | BLOCKED | — | — |
| dd MTProxy | — | BLOCKED | — |
| Startup, graceful shutdown | BLOCKED | BLOCKED | BLOCKED |
| systemd restart and reboot | BLOCKED | BLOCKED | BLOCKED |
| One-hour stability run | NOT RUN | NOT RUN | NOT RUN |

LIVE FLOODWAIT: NOT OBSERVED. This is a statement about the absence of a live run, not a statement about Telegram.

## 3. Not run

| Item | Reason |
|---|---|
| Live tests of every row in section 2 | No credentials, endpoint or VPS access in this environment |
| One-hour soak | Needs a running VPS |
| systemd start, restart, reboot | Needs a VPS with systemd and the install step |
| Sanitizers on the library itself | The library is the prebuilt, unmodified build from the attached tree. The sanitizer runs cover the bot code only |
| Full CTest of the library | Not part of this deliverable. The library was not changed |
