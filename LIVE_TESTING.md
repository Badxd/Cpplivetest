# Live testing: owner runbook (CppGramLiveTest 1.0.0)

Release status: **NOT READY**. No live Telegram result is recorded for CppGram 1.0.0. This file says how the owner runs
the live checks and what the results mean. The commands below were checked against `CMakeLists.txt`, `COMMANDS.md` and
the runner source (`src/livetest/cli.cpp`). Their checked status is in `TEST_RESULTS.md`, section 7.

## 1. The network gate

Every check that can send packets to Telegram or to an external proxy runs through the same gate first. That covers the
`live_no_credentials`, `live_credentials` and `owner_approval` categories, and the bot at startup
(`cppgram_live_test_bot`).

| Step | Rule | Result if it fails |
|---|---|---|
| 1. Interception probe | TCP connect to `10.255.255.1:443`, 1500 ms. The probe must **time out**. | A connection that is made means **intercepted**. A fast refusal or socket error means **inconclusive**. Both block every network check. |
| 2. Declaration | `CGLT_NETWORK_UNRESTRICTED=1` must be set. | BLOCKED. The declaration never overrides step 1. |
| 3. Owner approval | Owner-gated checks also need `CGLT_OWNER_APPROVED=1`. | PENDING USER APPROVAL. Nothing is contacted and no credential is read. |
| 4. Credentials | `TG_API_ID`, `TG_API_HASH`, `TG_BOT_TOKEN` are read only after steps 1 to 3 pass. | Never read for a blocked or pending check. |

The probe sends one SYN to `10.255.255.1`, which is not a Telegram address. Nothing reaches Telegram unless the probe is
clean and the test is allowed.

Blocked checks report `telegram_contact=no`. A test reports `telegram_contact=yes` only after it has sent a packet to
Telegram or a proxy.

## 2. Statuses

- **PASS**: the check ran on an unrestricted network and its expected result was observed.
- **FAIL**: the check ran and the result was wrong. Keep the JSON and the log, and record them in `LIVE_RESULT_TEMPLATE.md`.
  Do not edit the status.
- **BLOCKED**: the gate stopped the check, or an input is missing. This is not a result, and it never counts as a PASS or FAIL.
- **NOT RUN**: the check is not implemented, or has no safe trigger.
- **PENDING USER APPROVAL**: the check needs owner approval (`CGLT_OWNER_APPROVED=1`) and its own inputs.

Exit codes of `cppgram_livetest`: `0` no FAIL, `1` at least one FAIL, `2` a bad command line, an unknown category or test,
a selection that matches nothing, or a `--json` file that cannot be written. Nothing runs in the `2` cases.

## 3. Build

Use the exact library source that was verified. The commands below are checked against `CMakeLists.txt`.

```sh
# A. Build the library from the verified archive (CppGram-v1.0.0-STABLE-READY.zip)
unzip CppGram-v1.0.0-STABLE-READY.zip
cmake -S CppGram-v1.0.0-STABLE-READY -B cppgram-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=OFF
cmake --build cppgram-build --target cppgram

# B. Build the live-test project against that library
unzip CppGramLiveTest-v1.0.0.zip
cmake -S CppGramLiveTest-v1.0.0 -B livetest-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCPPGRAM_SOURCE_DIR="$PWD/CppGram-v1.0.0-STABLE-READY" \
      -DCPPGRAM_PREBUILT_DIR="$PWD/cppgram-build"
cmake --build livetest-build --target cppgram_livetest livebot_unit_tests livetest_gate_tests cppgram_live_test_bot
```

`-DENABLE_TESTS=OFF` is the library's default and is accepted by its `option(ENABLE_TESTS ...)`. `CPPGRAM_PREBUILT_DIR`
must contain `libcppgram.a`. The library puts it in its binary directory. `CPPGRAM_PREBUILT_INCLUDE_DIR` is not needed
when `CPPGRAM_SOURCE_DIR` points at the same tree.

Check the offline tests before any live run:

```sh
./livetest-build/livetest_gate_tests                 # expect: N checks, 0 failures
./livetest-build/livebot_unit_tests                  # expect: 136 checks, 0 failures
./livetest-build/cppgram_livetest --list             # 16 tests in 5 categories
```

## 4. Run: no credentials first

Run the no-credential checks before any credential exists in the shell.

```sh
mkdir -p run
CGLT_NETWORK_UNRESTRICTED=1 ./livetest-build/cppgram_livetest --category live_no_credentials \
    --workdir ./run --json ./run/no-credentials.json 2>&1 | tee ./run/no-credentials.log
```

Read the header first. `network probe` must read `clean`. If it reads `intercepted` or `inconclusive`, stop. Every
network check is then BLOCKED and this network cannot give live results.

## 5. Run: credentials, only after step 4 is clean

Keep the credentials in a protected file outside any repository. Do not paste them into the shell or a report.

```sh
install -m 600 /dev/null ~/.config/cppgram-live.env      # then edit it in your editor
#   TG_API_ID=...
#   TG_API_HASH=...
#   TG_BOT_TOKEN=...
set -a; . ~/.config/cppgram-live.env; set +a
CGLT_NETWORK_UNRESTRICTED=1 ./livetest-build/cppgram_livetest --category live_credentials \
    --workdir ./run --json ./run/credentials.json 2>&1 | tee ./run/credentials.log
```

`live.session_restore` needs the `bot.session` that `live.login_bot_and_get_me` writes in the work directory. Run them in
that order. `bot.session` is a credential. Keep it at mode 0600 inside `./run`, or delete it when the run ends.

## 6. Owner-approved checks (optional, off by default)

`owner.proxy_dd`, `owner.socks5` and `owner.message_loop` run only when you approve them, and each needs its own
endpoint or chat (see TESTS.md). `owner.message_loop` answers only the chat in `CGLT_TEST_CHAT_ID`, and only the exact
command `/test`. `owner.flood_wait_controlled` is NOT RUN by design: the runner never triggers a rate limit. `owner.send_message`
is superseded by the reply inside `owner.message_loop`.

```sh
CGLT_NETWORK_UNRESTRICTED=1 CGLT_OWNER_APPROVED=1 \
CGLT_PROXY_HOST=<host> CGLT_PROXY_PORT=<port> CGLT_PROXY_SECRET=<dd secret> \
    ./livetest-build/cppgram_livetest --category owner_approval --workdir ./run --json ./run/owner.json 2>&1 | tee ./run/owner.log
```

Without `CGLT_OWNER_APPROVED=1`, `owner.proxy_dd` is PENDING USER APPROVAL and reads nothing.

## 7. Check the evidence, then clean up

```sh
# Names of files that contain a secret. Empty output means clean. Prints no secret.
for v in TG_API_HASH TG_BOT_TOKEN CGLT_PROXY_SECRET; do
  val="$(printenv "$v" || true)"
  [ -n "$val" ] && find ./run -type f -exec grep -lF -- "$val" {} + || true
done
```

The scan must print nothing. If it prints a file name, do not share that file. Then clear the shell:

```sh
unset TG_API_ID TG_API_HASH TG_BOT_TOKEN CGLT_NETWORK_UNRESTRICTED CGLT_OWNER_APPROVED CGLT_PROXY_HOST CGLT_PROXY_PORT CGLT_PROXY_SECRET
```

Record each JSON file and log in `LIVE_RESULT_TEMPLATE.md`, with its probe result, and the statuses as printed.

## 8. Bot startup (VPS)

`cppgram_live_test_bot` runs the same probe before it reads any `TG_*` value. It exits `3` if the probe is not clean or
`CGLT_NETWORK_UNRESTRICTED=1` is missing. This change does not modify any running service. The VPS is changed only when
the owner deploys this build.

## 9. Known limits

- A network that rejects the probe address with a fast error is **inconclusive**, so every network check is blocked.
  This is deliberate. An owner must find a network where that address times out.
- `environment.connect_unroutable` is outside the gate because it contacts only the probe address, not Telegram or a
  proxy. It reports BLOCKED on an intercepted network.
- MSan, ARM64, Windows and macOS are not covered by this project's results.
