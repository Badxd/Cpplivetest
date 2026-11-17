# CppGramLiveTest 1.0.0: final report

**Status: the runner and bot are complete for this package. Offline coverage is complete for the failure cases below.
Live results are recorded by owner report only, as stated in `TEST_MATRIX.md`. No live result was produced in this
workspace.**

## What is complete in this package

- `cppgram_livetest`: five categories, five statuses, JSON output, stage and error reporting, the `telegram_contact` flag,
  the library version and tree digest, and the network gate on every network and owner test.
- Offline failure injection (new): 8 loopback tests for MTProxy and SOCKS5 refusal, timeout, early close, wrong version
  and REP 0x05, plus recovery and descriptor-leak checks. All pass, and repeat runs are stable (3 of 3).
- Owner-approval live tests (new, implemented, not run here):
  - `owner.socks5`: SOCKS5 connect, bot login, `get_me()`, reconnect, clean disconnect.
  - `owner.message_loop`: `client.run()` receives `/test` from the one authorised chat, replies once, and shuts down
    cleanly. Other chats are ignored.
- Default run: 12 PASS, 14 BLOCKED (workspace), 0 FAIL. The network gate blocks every network test here.
- Gate regression tests: 63 checks, 0 failures.
- Bot unit tests: 136 checks, 0 failures (unchanged).

## Live results

| Item | Result | Source |
|---|---|---|
| Direct Telegram connection, bot login, `get_me()` | PASS | Owner report |
| Session export and restore | PASS | Owner report |
| Reconnect | PASS | Owner report |
| Plain MTProxy | LIVE PASS, 6439 ms | Owner report |
| DD MTProxy | LIVE PASS, 6981 ms | Owner report |
| SOCKS5 live (`owner.socks5`) | Unverified. No result recorded | |
| Message/update loop live (`owner.message_loop`) | Unverified. No result recorded. Earlier owner report says it was skipped pending `/test` | |
| Real FLOOD_WAIT | NOT OBSERVED / NOT RUN. Not provoked, by design | |

The owner-run results are not in this archive's logs. The run identity (commit or archive hash) is not recorded here.

## Limits

- SOCKS5 and the message loop need owner approval, an owner endpoint or a `/test` from the authorised chat. Until those
  runs are recorded, they stay Unverified.
- The offline failure tests run on loopback with scripted peers. They show error handling, not behaviour against a real
  proxy.
- A real FLOOD_WAIT is not triggered. Offline FLOOD_WAIT tests are in the CppGram library suite.
- The workspace network is intercepted, so every network test is BLOCKED there. That is an environment limit.

## Owner actions

1. Run `owner.socks5` with `CGLT_OWNER_APPROVED=1`, `CGLT_SOCKS5_HOST`, `CGLT_SOCKS5_PORT` and the bot credentials, from an
   unrestricted network. Record the output in `TEST_RESULTS.md`.
2. Run `owner.message_loop` with `CGLT_OWNER_APPROVED=1`, `CGLT_TEST_CHAT_ID` and the bot credentials. Send `/test` from that
   chat while the run waits. Record the output.
3. Record the commit or archive hash used for the owner's live runs.
4. Decide on removing `bin/cppgram_live_test_bot` from the external Cpplivetest repository, and rotate development
   credentials. Both are outside this project.

Full per-test detail: `TEST_MATRIX.md`.
