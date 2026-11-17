# CppGramLiveTest 1.0.0: readiness report

Status: **NOT READY for v1.0.0 STABLE.** The release decision for the library is in CppGram's own
`FINAL_V1.0.0_READINESS_REPORT.md`. This report covers the live-test project only.

Live Telegram validation has not run on an unrestricted network. Every live check is BLOCKED. The development sandbox
intercepts connections to Telegram, so its results are not live evidence. The owner must run the live checks.

Results and logs: `TEST_RESULTS.md` and `reports/logs/`. Library digest checked by this build:
`57e313ea306c76c3f2de6cc97e6275b9e6664c1f53f3c4fb53daf409037ad58d`. This matches the final CppGram source.

Status values: PASS, FAIL, BLOCKED, NOT RUN, PENDING USER APPROVAL.

## Blockers

| ID | Item | Blocks | Owner action |
|---|---|---|---|
| RB-1 | Live Telegram checks: handshakes, encrypted ping, reconnect, lifecycle, bot login, session restore | Stable release | Run on an unrestricted network with `CGLT_NETWORK_UNRESTRICTED=1`, owner credentials and `TG_BOT_TOKEN`. Record results with `LIVE_RESULT_TEMPLATE.md`. |
| RB-2 | Rotation of development credentials used with the VPS and this project | Publication | Rotate. Nothing was rotated here. |
| RB-3 | Public 8443 proxy | Publication until decided | Decide. Not changed here. |
| RB-4 | Committed binary `bin/cppgram_live_test_bot` in the external `Badxd/Cpplivetest` repository (10.8 MB) | No | Decide whether to remove it from that repository. This archive does not include it. |
| RB-5 | Owner approval for the dd proxy check and for any message send | Those checks only | Provide `CGLT_PROXY_*` and `CGLT_OWNER_APPROVED=1` if approved. `owner.send_message` is not implemented in this build. |

## Requirement status

| ID | Requirement | Status | Evidence | Note |
|---|---|---|---|---|
| T-1 | Runner with five categories | PASS | `reports/logs/livetest-default-mode.log`: 16 tests | |
| T-2 | Five statuses only | PASS | Log statuses | |
| T-3 | Interception gate holds, even with the override, for every network category and for the bot | PASS | `reports/logs/gate-fix/`: credentials and owner tests BLOCKED with the override on an intercepted network, `telegram_contact=no`; bot refused with exit 3; `connect-targets.txt` shows only the probe address. | Corrected in this round: the earlier T-3 tested only `live_no_credentials`. |
| T-4 | Owner-gated checks do not run without approval, and the network gate runs first | PASS | `reports/logs/gate-fix/`: default run gives 0 PENDING on an intercepted network, 12 BLOCKED. Owner checks are PENDING only on a clean unrestricted network without approval (gate tests). | Corrected in this round: earlier "3 PENDING" in default mode predates the network gate on owner tests. |
| T-16 | Gate regression tests: intercepted with credentials, intercepted with override, unrestricted without approval, unrestricted with approval, credentials not read before the gate | PASS | `reports/logs/gate-fix/j-gate-tests.out`: 63 checks, 0 failures | Offline tests of the gate logic. Allowed path on a real network: not run. |
| T-17 | Unknown category, unknown test and unwritable JSON are usage errors | PASS | `g-typo-category.out`, `h-typo-test.out`, `i-bad-json-path.out`: exit 2 | |
| T-5 | Offline checks | PASS | `offline.session_roundtrip`, `offline.fake_tls_refused` | |
| T-6 | Environment checks | PASS (3) | `environment.connect_refused`, `environment.proxy_unreachable` PASS. `environment.connect_unroutable` BLOCKED. | |
| T-7 | Live handshakes, encrypted ping, reconnect, lifecycle | BLOCKED | Intercepted network | RB-1 |
| T-8 | Bot login, `get_me`, session restore | BLOCKED | No credentials, intercepted network | RB-1 |
| T-9 | dd proxy | PENDING USER APPROVAL | | RB-5 |
| T-10 | Message loop and reply (`owner.message_loop`) | PENDING USER APPROVAL | Implemented. Not run in this workspace (intercepted network, no owner approval or endpoint). See TEST_MATRIX.md | RB-5 |
| T-11 | Controlled FLOOD_WAIT | NOT RUN | Not provoked, by design. Offline FLOOD_WAIT tests are in the library suite | |
| T-12 | Bot unit tests | PASS | `reports/logs/livebot-unit.log`: 136 checks, 0 failures | |
| T-13 | Build against the exact final CppGram source | PASS | Built against the extracted `CppGram-v1.0.0-STABLE-READY.zip` source and its GCC library. | Digest matches. |
| T-14 | Library digest reported | PASS | Runner header and JSON | |
| T-15 | No secrets in the archive | PASS | Scan: only dummy `123456789:AAExample…` fixtures in unit tests. | |

## What would change the decision

1. RB-1 is resolved with real PASS or FAIL results from an unrestricted network.
2. RB-2 and RB-3 are decided by the owner.
3. The owner explicitly approves publication.
