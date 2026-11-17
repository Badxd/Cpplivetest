# Test results: CppGramLiveTest 1.0.0

These are the runs made for this release. No live Telegram check has a PASS. Every live check is BLOCKED or PENDING, and
the reasons are recorded below. Commands are relative to the project root. Logs are in `reports/logs/`.

**Authoritative results: section 7 (network-gate fix).** Sections 2, 3 and 6 are the earlier runs, kept for history. Two
of their claims are superseded: the owner checks were reported as PENDING on an intercepted network, and the override was
tested only for `live_no_credentials`. See section 7.

## 1. Bot unit tests (offline)

- Command: `./build/livebot_unit_tests`
- Result: exit 0, `136 checks, 0 failures`.
- Log: `reports/logs/livebot-unit.log`.
- Note: the log contains lines such as `direct FAIL category=rpc`. These come from deliberate failure fixtures that check
  secrets are removed from echoed server text. They are expected.

## 2. Runner, default mode (no credentials, no override)

- Command: `./build/cppgram_livetest --workdir <dir> --json <file>`
- Result: exit 0. Library version 1.0.0, tree digest `57e313ea306c76c3f2de6cc97e6275b9e6664c1f53f3c4fb53daf409037ad58d`.
- Log: `reports/logs/livetest-default-mode.log`.

| Test | Status | Telegram contacted |
|---|---|---|
| offline.session_roundtrip | PASS | no |
| offline.fake_tls_refused | PASS | no |
| environment.connect_refused | PASS | no |
| environment.connect_unroutable | BLOCKED (this network accepted the connection to 10.255.255.1) | no |
| environment.proxy_unreachable | PASS | no |
| live.handshake_intermediate_dc2 | BLOCKED (not valid from this network) | no |
| live.handshake_abridged_dc2 | BLOCKED | no |
| live.handshake_full_dc2 | BLOCKED | no |
| live.encrypted_ping_dc2 | BLOCKED | no |
| live.reconnect_restored_key_dc2 | BLOCKED | no |
| live.lifecycle_repeat_dc2 | BLOCKED | no |
| live.login_bot_and_get_me | BLOCKED (no TG_API_ID, TG_API_HASH, TG_BOT_TOKEN) | no |
| live.session_restore | BLOCKED (no TG_API_ID, TG_API_HASH) | no |
| owner.proxy_dd | PENDING USER APPROVAL | no |
| owner.send_message | PENDING USER APPROVAL | no |
| owner.flood_wait_controlled | PENDING USER APPROVAL | no |

Totals (16 tests): 4 PASS, 0 FAIL, 9 BLOCKED, 0 NOT RUN, 3 PENDING USER APPROVAL. The exit code is 0 because there are no FAILs.

## 3. Interception gate with the owner override set

- Command: `CGLT_NETWORK_UNRESTRICTED=1 ./build/cppgram_livetest --category live_no_credentials`
- Result: exit 0. All six live checks BLOCKED, each with `telegram_contact=no`, with the reason
  "not valid from this network: TCP to 10.255.255.1 was accepted".
- Log: `reports/logs/livetest-override-interception.log`.

This shows that the override cannot turn an intercepted network into a live result.

## 4. Live Telegram checks

None were run with a valid network. The details are in `diagnostics/sandbox-interception-probe.md`. To record real
results, use an unrestricted network and fill in `LIVE_RESULT_TEMPLATE.md`.

## 5. Not run

- Owner-approval checks: they need `CGLT_OWNER_APPROVED=1` and the inputs listed in `TESTS.md`. Not run, and not
  approved.
- Live account checks: they need credentials and an unrestricted network. Not run.

## 6. Final run for the 1.0.0 release (this package)

Built against the exact final CppGram source, taken from the extracted `CppGram-v1.0.0-STABLE-READY.zip`. The library
is that archive's GCC 13.3 build. Build: `cmake -S CppGramLiveTest -B build -G Ninja -DCMAKE_CXX_COMPILER=g++
-DCPPGRAM_SOURCE_DIR=<extracted CppGram> -DCPPGRAM_PREBUILT_DIR=<that build>`, then `cmake --build build`. Build exit 0.

| Run | Command | Exit | Result | Log |
|---|---|---|---|---|
| Default mode | `cppgram_livetest --workdir <dir> --json <file>` (no credentials) | 0 | 4 PASS, 9 BLOCKED, 3 PENDING USER APPROVAL. Every line `telegram_contact=no`. Library digest `57e313ea…`. | `reports/logs/livetest-default-mode.log`, `.json` |
| Override | `CGLT_NETWORK_UNRESTRICTED=1 cppgram_livetest --category live_no_credentials --workdir <dir> --json <file>` | 0 | 6 BLOCKED, `telegram_contact=no` on all. The override does not bypass the gate. | `reports/logs/livetest-override-interception.log`, `.json` |
| Bot unit tests | `livebot_unit_tests` | 0 | 136 checks, 0 failures | `reports/logs/livebot-unit.log` |

No live Telegram check has a PASS. All live checks are BLOCKED on this network.

## 7. Network-gate fix (authoritative for this release)

Recorded 2026-11-16 after a review of the earlier gate. The review found that `live_credentials` and `owner_approval`
had no interception probe, and that `owner.proxy_dd` read its proxy secret before the approval check.

### What changed

- One gate (`src/netgate.h`, `src/netgate.cpp`) is applied to every test that can send packets to Telegram or a proxy:
  `live_no_credentials`, `live_credentials` and `owner_approval`. It is also applied to `cppgram_live_test_bot` at startup.
- The gate order is: interception probe (must be clean), then `CGLT_NETWORK_UNRESTRICTED=1`, then `CGLT_OWNER_APPROVED=1`
  for owner tests. A test body runs only on Allow. Credentials are read only inside a body.
- `CGLT_NETWORK_UNRESTRICTED=1` never overrides the probe.
- An inconclusive probe (a fast refusal, a socket error or any other exception) blocks network tests. Only a timeout is clean.
- Unknown `--category` or `--test` values, a selection that matches nothing, and a `--json` path that cannot be written now
  exit `2` before any test runs. Previously an unknown category ran zero tests and exited `0`.
- The header prints `network probe`, `unrestricted` and `owner approval`. The JSON carries the same three values.
- The bot exits `3` at startup when the gate refuses. It reads no `TG_*` value before the gate passes.

### Results

| Check | Command | Exit | Result | Log |
|---|---|---|---|---|
| Gate regression tests | `livetest_gate_tests` | 0 | 63 checks, 0 failures | `reports/logs/gate-fix/j-gate-tests.out` |
| Gate tests under ASan and UBSan | `gate_tests.cpp` and `netgate.cpp` compiled with `-fsanitize=address,undefined`, linked to the library | 0 | 63 checks, 0 failures, no sanitizer report. The library itself was not sanitized. | run in the build session; not archived |
| Bot unit tests | `livebot_unit_tests` | 0 | 136 checks, 0 failures | `reports/logs/gate-fix/k-bot-unit-tests.out` |
| Default run, no env | `cppgram_livetest --workdir ... --json ...` | 0 | 4 PASS, 12 BLOCKED, 0 PENDING. Every line `telegram_contact=no`. | `reports/logs/gate-fix/a-default-no-env.out`, `.json` |
| Credentials, no override | `--category live_credentials` with credential fixtures | 0 | 2 BLOCKED, `telegram_contact=no` | `b-credentials-no-override.out`, `.json` |
| No-credential checks, override | `CGLT_NETWORK_UNRESTRICTED=1 --category live_no_credentials` | 0 | 6 BLOCKED, `telegram_contact=no` | `c-no-creds-override.out`, `.json` |
| Credentials, override | `CGLT_NETWORK_UNRESTRICTED=1` with credential fixtures, `live_credentials` | 0 | 2 BLOCKED, `telegram_contact=no` | `c2-credentials-override.out`, `.json` |
| Owner, override, no approval | `CGLT_NETWORK_UNRESTRICTED=1` with proxy fixture, `owner_approval` | 0 | 3 BLOCKED, `telegram_contact=no` | `d-owner-override-no-approval.out`, `.json` |
| Owner, override, approval | `CGLT_OWNER_APPROVED=1 CGLT_NETWORK_UNRESTRICTED=1` with proxy fixture | 0 | 3 BLOCKED, `telegram_contact=no` | `e-owner-override-approved.out`, `.json` |
| Bot startup, override | `cppgram_live_test_bot` with credential fixtures | 3 | refused by the gate. The gate runs before `load_config` in `main()`, so no `TG_*` value is read on this path (code order; not observable under `strace`). | `f-bot-startup.out` |
| Unknown category | `--category live_credential` | 2 | usage error, nothing runs | `g-typo-category.out` |
| Unknown test | `--test live.nope` | 2 | usage error, nothing runs | `h-typo-test.out` |
| Unwritable JSON | `--json /nonexistent-dir/x.json` | 2 | usage error, nothing runs | `i-bad-json-path.out` |

The credential values in these runs are the unit-test fixtures (`123456789:AAExample…`, `0123…cdef`), not real credentials.
They are not in any log or JSON file. Each run was made with `strace -e trace=connect`. The only non-local `connect()`
address in any run is `10.255.255.1`, the probe address (`reports/logs/gate-fix/connect-targets.txt`). No run contacted
Telegram or a proxy.

Gate-regression coverage (`tests/gate_tests.cpp`):

- intercepted network with credentials configured: BLOCKED, body not run, no credential read, no contact;
- intercepted network with the override: BLOCKED, body not run, no contact (also for owner tests, even with approval);
- inconclusive or missing probe: BLOCKED for network and owner tests;
- clean probe without the declaration: BLOCKED;
- unrestricted network without owner approval: owner tests PENDING, body not run, no credential read;
- unrestricted network with approval: allowed; credential tests need no approval;
- offline and environment tests are unaffected by the probe;
- every category maps to the expected gate, and an unknown category fails closed.

### Corrections to earlier claims

- T-3 ("the gate holds, even with the override"). Earlier it was tested only for `live_no_credentials`. It now also holds for
  `live_credentials`, `owner_approval`, and the bot at startup. Those cases are in the table above.
- T-4 ("owner-gated checks do not run without approval, default mode: 3 PENDING"). On an intercepted network the owner
  checks are now BLOCKED, because the network gate runs before the approval check. They are PENDING only on a clean,
  unrestricted network without approval. That case is covered by the gate tests, not by a live run.

### Not verified

- The allowed path with a real network (clean probe, declaration, credentials) was not run. This sandbox intercepts TCP to
  `10.255.255.1`, so the gate blocks everything here. The allowed path is covered by the gate tests only. A live result
  needs the owner's unrestricted network.

## 8. Round 3: failure injection and owner tests (this package)

Offline, run in the workspace (`cppgram_livetest --category offline`):

| Test | Result | Detail |
|---|---|---|
| offline.failure_mtproxy_refused | PASS | ProxyError transient after about 1090 ms |
| offline.failure_mtproxy_silent_timeout | PASS | ProxyError transient after about 1028 ms (1000 ms window) |
| offline.failure_mtproxy_closes_early | PASS | ConnectionError (broken pipe) |
| offline.failure_recovery | PASS | new connection to a healthy loopback listener succeeded |
| offline.failure_socks5_refused | PASS | Socks5Error transient |
| offline.failure_socks5_silent_timeout | PASS | Socks5Error transient after about 1019 ms |
| offline.failure_socks5_wrong_version | PASS | Socks5Error final |
| offline.failure_socks5_refused_reply | PASS | reply code 5 kept, final |

Three repeat runs of the failure tests: 10 PASS, 0 FAIL each time. Outputs: `reports/logs/failure-injection/`.

Default run after this change: 12 PASS, 14 BLOCKED, 0 FAIL. Owner-approval run with no network: 5 BLOCKED by the interception
gate, and no credential was read.

Owner-approval tests implemented but not run in the workspace: `owner.socks5`, `owner.message_loop`. Live results for them
are not recorded in this package.

Owner-reported live results carried forward (not reproduced here): direct connection, bot login and `get_me()`, session
restore, reconnect: PASS. Plain MTProxy: LIVE PASS, 6439 ms. DD MTProxy: LIVE PASS, 6981 ms.
