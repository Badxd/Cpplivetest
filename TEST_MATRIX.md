# CppGramLiveTest 1.0.0: test matrix

Every row says whether the test is implemented, its offline status, its live status, the exact evidence, and any limit.
Statuses: PASS, FAIL, BLOCKED, NOT RUN, PENDING USER APPROVAL. "Owner report" means a result the project owner reported
from a live run. The run output is not in this archive, so it is not reproduced here.

Workspace: the build environment has no credentials, no owner endpoint and an intercepted network (TCP to 10.255.255.1 is
accepted). Live runner tests are BLOCKED there by the network gate. That is an environment limit, not a feature result.

## A. Direct Telegram and session

| Check | Implemented | Offline status | Live status | Evidence | Limitation |
|---|---|---|---|---|---|
| Session save, load, CRC rejection (`offline.session_roundtrip`) | Yes | PASS | n/a | `reports/logs/failure-injection/offline-run.out` | Offline only |
| Direct connection and key exchange (`live.handshake_*_dc2`, three transports) | Yes | n/a | Owner report: direct connection PASS. Runner per-transport result: BLOCKED in workspace | Owner report; `reports/logs/livetest-default-mode.log` (earlier round) | Per-transport owner run not recorded |
| Encrypted ping and lifecycle (`live.encrypted_ping_dc2`, `live.lifecycle_repeat_dc2`) | Yes | n/a | Runner: BLOCKED in workspace | Default run output | Owner run not recorded |
| Bot login and `get_me()` (`live.login_bot_and_get_me`) | Yes | n/a | Owner report: PASS. Runner: BLOCKED in workspace | Owner report | Runner run not recorded |
| Session export and restore into a fresh client (`live.session_restore`) | Yes | PASS (offline round trip above) | Owner report: PASS. Runner: BLOCKED in workspace | Owner report | Runner run not recorded |
| Reconnect with the saved key (`live.reconnect_restored_key_dc2`) | Yes | n/a | Owner report: PASS. Runner: BLOCKED in workspace | Owner report | Runner run not recorded |

## B. Proxies

| Check | Implemented | Offline status | Live status | Evidence | Limitation |
|---|---|---|---|---|---|
| Plain MTProxy: connection, handshake, bot login, `get_me()`, reconnect, clean disconnect | Owner-run check (not a runner test in this archive) | PASS (CppGram library suite, MTProxy tests) | **LIVE PASS**, 6439 ms. Owner report | Owner report | No runner test in this archive reproduces it |
| DD MTProxy: same steps (`owner.proxy_dd` covers handshake and encrypted ping) | Yes (runner), plus owner-run check | PASS (CppGram library suite) | **LIVE PASS**, 6981 ms. Owner report. Runner `owner.proxy_dd`: PENDING in workspace | Owner report | Runner run not recorded |
| Padded-intermediate framing against a live proxy | Covered by DD MTProxy | PASS (library) | PASS, covered by the DD run above | Owner report | Not a separate run |
| Proxy clean disconnect and cleanup | Yes | PASS (offline, descriptor checks in section D) | PASS for plain and DD (owner report, clean disconnect) | Owner report | |
| SOCKS5 through a live proxy (`owner.socks5`) | Yes | Offline SOCKS5 failure tests PASS (section D) | **Unverified**: no owner result recorded. Workspace: PENDING / BLOCKED | | No live SOCKS5 result exists |
| MTProxy and SOCKS5 failure and timeout, live | Not a live test | See section D | Unverified | | |

## C. Update loop and messages

| Check | Implemented | Offline status | Live status | Evidence | Limitation |
|---|---|---|---|---|---|
| `client.run()` receives `/test` from the authorised chat, replies once, `stop()` ends `run()` (`owner.message_loop`) | Yes | Compiles; the body is reached only through the network gate, so it cannot be exercised offline | **Unverified**: not run. Workspace: BLOCKED by network gate. Owner report earlier said it was skipped pending `/test` | | Needs owner approval, `CGLT_TEST_CHAT_ID`, and a `/test` sent from that chat |
| Messages from other chats are ignored and never answered | Yes (filter in the handler) | Code path, not run | Unverified | | |
| Clean shutdown and handler error reporting | Yes (`on_error` records errors; `run()` returns after `stop()`) | Not run | Unverified | | |
| Single send (`owner.send_message`) | Superseded: the reply inside `owner.message_loop` | n/a | NOT RUN | | No standalone send test |

## D. Failure injection (offline, loopback only)

| Check | Implemented | Offline status | Live status | Evidence | Limitation |
|---|---|---|---|---|---|
| MTProxy to a closed port: transient `ProxyError`, no descriptor leak (`offline.failure_mtproxy_refused`) | Yes | PASS, 3 repeats | n/a | `reports/logs/failure-injection/offline-repeat1..3.out` | Loopback only |
| MTProxy peer that never answers: timeout within window (`offline.failure_mtproxy_silent_timeout`) | Yes | PASS, 3 repeats (about 1028 ms for a 1000 ms timeout) | n/a | same | |
| MTProxy peer that closes at once (`offline.failure_mtproxy_closes_early`) | Yes | PASS, 3 repeats. Observed class `ConnectionError` (broken pipe) | n/a | same | The error class depends on timing |
| Recovery after a failed attempt (`offline.failure_recovery`) | Yes | PASS | n/a | `offline-run.out` | Recovery is shown on a loopback listener, not a real proxy |
| SOCKS5 to a closed port: transient `Socks5Error` (`offline.failure_socks5_refused`) | Yes | PASS | n/a | `offline-run.out` | |
| SOCKS5 silent peer: transient timeout (`offline.failure_socks5_silent_timeout`) | Yes | PASS (about 1019 ms) | n/a | `offline-run.out` | |
| SOCKS5 wrong version: final error (`offline.failure_socks5_wrong_version`) | Yes | PASS | n/a | `offline-run.out` | |
| SOCKS5 REP 0x05: reply code kept, final (`offline.failure_socks5_refused_reply`) | Yes | PASS | n/a | `offline-run.out` | |

## E. FLOOD_WAIT

| Check | Implemented | Offline status | Live status | Evidence | Limitation |
|---|---|---|---|---|---|
| Parsing, retry policy and cancellation of FLOOD_WAIT | Library | PASS: `Session.FloodWait*`, `Retry.FloodWait*`, `ClientFakeDc.FloodWait*`, `ClientFakeDc.StopCancelsAFloodWaitInProgress` in the 711-case CTest run | n/a | CppGram release evidence `reports/evidence/ctest-gcc.txt` (library repository, not in this archive) | Offline only |
| Real FLOOD_WAIT from Telegram | `owner.flood_wait_controlled` (by design: never triggered) | n/a | NOT OBSERVED / NOT RUN. Not provoked. Would be recorded by `owner.message_loop` if it happened naturally | | No deliberate rate limit is generated |

## F. Summary of this package

- Offline: 10 of 10 `offline.*` tests PASS (three repeats of the failure tests, all pass).
- Gate: `livetest_gate_tests`: 63 checks, 0 failures (unchanged).
- Default run: 12 PASS, 14 BLOCKED (workspace), 0 FAIL, 0 PENDING. Output in `reports/logs/failure-injection/default-mode-after-round3.out`.
- Owner-approval run without network: all five owner tests BLOCKED by the interception gate. Output in `owner-gate-no-network.out`.
- Live Telegram or proxy results added in this package: none. Owner reports are carried forward as stated above.
