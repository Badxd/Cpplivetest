# Commands and exit codes

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCPPGRAM_SOURCE_DIR=/path/to/cppgram
cmake --build build --target cppgram_livetest livebot_unit_tests livetest_gate_tests cppgram_live_test_bot
```

Prebuilt library instead of source:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCPPGRAM_SOURCE_DIR=/path/to/cppgram \
  -DCPPGRAM_PREBUILT_DIR=/path/with/libcppgram.a \
  -DCPPGRAM_PREBUILT_INCLUDE_DIR=/path/to/cppgram/include
```

## Runner

```
cppgram_livetest [--list] [--category NAME] [--test ID] [--json FILE] [--workdir DIR]
```

| Flag | Meaning |
|---|---|
| `--list` | Print every test with its category and summary. Runs nothing. |
| `--category NAME` | Run only one of `offline`, `environment`, `live_no_credentials`, `live_credentials`, `owner_approval`. |
| `--test ID` | Run only one test, for example `live.encrypted_ping_dc2`. |
| `--json FILE` | Write a redacted JSON report, with the library version and tree digest. |
| `--workdir DIR` | Where session files are written (mode 0600). Default `cglt-work`. |

Environment variables:

| Variable | Used by | Meaning |
|---|---|---|
| `CGLT_NETWORK_UNRESTRICTED=1` | every network category, and the bot at startup | Declares the network unrestricted. Required for every test that contacts Telegram or a proxy. It never overrides the interception probe: an intercepted or inconclusive probe still BLOCKs the test. |
| `TG_API_ID`, `TG_API_HASH`, `TG_BOT_TOKEN` | `live_credentials` | Account credentials. Never commit them. |
| `CGLT_OWNER_APPROVED=1` | `owner_approval` | Owner approval. Required before any owner test runs. |
| `CGLT_PROXY_HOST`, `CGLT_PROXY_PORT`, `CGLT_PROXY_SECRET` | `owner.proxy_dd` | The dd proxy under test. Secret is "dd" plus 32 hex characters. |
| `CGLT_SOCKS5_HOST`, `CGLT_SOCKS5_PORT` | `owner.socks5` | Owner-provided SOCKS5 endpoint. |
| `CGLT_SOCKS5_USER`, `CGLT_SOCKS5_PASS` | `owner.socks5` | Optional SOCKS5 credentials. Set both or neither. |
| `CGLT_TEST_CHAT_ID` | `owner.message_loop` | The one authorised test chat id. Messages from any other chat are ignored and never answered. |
| `CGLT_MESSAGE_WAIT_SECONDS` | `owner.message_loop` | How long to wait for `/test`, 10 to 600 seconds. Default 120. |

Exit codes: `0` when no test FAILed (BLOCKED, NOT RUN and PENDING are not failures). `1` when at least one test FAILed.
`2` for a bad command line, an unknown `--category` or `--test` value, a selection that matches no test, or a `--json`
file that cannot be written. Nothing runs in the `2` cases.

The header of every run prints `network probe`, `unrestricted` and `owner approval`. The JSON report has the same three
values as `network_probe`, `unrestricted_declared` and `owner_approved`.

## Bot

```sh
./build/cppgram_live_test_bot
```

Exit `3` when the network gate refuses startup (the probe is not clean, or `CGLT_NETWORK_UNRESTRICTED=1` is not set). No
`TG_*` credential is read in that case. Exit `2` for a configuration error, and `1` when login fails.

## Bot unit tests and gate tests

```sh
./build/livebot_unit_tests
./build/livetest_gate_tests
```

Exit `0` when all checks pass. `livebot_unit_tests` ends with `136 checks, 0 failures` in this release. The gate tests
cover: an intercepted network with credentials (BLOCKED, nothing sent); an intercepted network with the override (BLOCKED);
an unrestricted network without owner approval (owner tests PENDING); an unrestricted network with approval (allowed); and
credentials not read before the gate passes. They open no socket and use no real credential.

## Library test suite

Run inside the CppGram source tree, not this project:

```sh
make test BUILD_DIR=build/gcc
```
