# Test catalogue: CppGramLiveTest 1.0.0

Every test in `cppgram_livetest`, its category, what it proves, what it needs, and its status in this release.
"Status now" is the result of the run recorded in `TEST_RESULTS.md`. No live test has a PASS in this release.

## offline

| ID | Proves | Needs | Status now |
|---|---|---|---|
| `offline.session_roundtrip` | A saved session loads with the same values. A corrupted byte is rejected with `SessionCorruptError`. | nothing | PASS |
| `offline.fake_tls_refused` | A fake-TLS (`ee`) proxy secret is parsed as fake-TLS and refused before any network use. | nothing | PASS |

## environment

Local failure paths. They do not contact Telegram.

| ID | Proves | Needs | Status now |
|---|---|---|---|
| `environment.connect_refused` | A closed local port reports `ConnectionError`. | nothing | PASS |
| `environment.connect_unroutable` | An unroutable address fails with a timeout or `ConnectionError`. | a network that does not accept TCP to `10.255.255.1` | BLOCKED (this network accepted the connection) |
| `environment.proxy_unreachable` | An unreachable MTProto proxy reports a transient `ProxyError`. | nothing | PASS |

## live_no_credentials

Real Telegram DCs. No account is used. The runner refuses to contact Telegram unless the network gate allows it: a clean
interception probe (TCP to `10.255.255.1` must time out) and `CGLT_NETWORK_UNRESTRICTED=1`. A connection accepted by the
probe, or an inconclusive probe, blocks the test even with the override.

| ID | Proves | Needs | Status now |
|---|---|---|---|
| `live.handshake_intermediate_dc2` | A real MTProto handshake over the intermediate transport with DC 2 produces a 256-byte auth key. | unrestricted network | BLOCKED (not run: no unrestricted network) |
| `live.handshake_abridged_dc2` | The same over the abridged transport. | unrestricted network | BLOCKED |
| `live.handshake_full_dc2` | The same over the full transport. | unrestricted network | BLOCKED |
| `live.encrypted_ping_dc2` | After a handshake, an encrypted `ping` gets a `pong` with the same `ping_id`. This exercises real encryption and decryption. | unrestricted network | BLOCKED |
| `live.reconnect_restored_key_dc2` | After a dropped connection, a new connection reuses the saved key with no second handshake, and an encrypted ping succeeds. | unrestricted network | BLOCKED |
| `live.lifecycle_repeat_dc2` | Three connect, ping and close cycles over one saved key. | unrestricted network | BLOCKED |

## live_credentials

Real account actions. They pass the same network gate as `live_no_credentials` first. Only after the gate allows the test
does the body read `TG_API_ID`, `TG_API_HASH` and `TG_BOT_TOKEN`. Nothing is read from the environment for a blocked test.

| ID | Proves | Needs | Status now |
|---|---|---|---|
| `live.login_bot_and_get_me` | A bot login succeeds and `get_me` returns the bot's identity. The session is saved. | unrestricted network, bot credentials | BLOCKED (no credentials in this environment) |
| `live.session_restore` | A second client restores the saved session and calls `get_me` without a new login. | unrestricted network, API id and hash, and a session from `live.login_bot_and_get_me` | BLOCKED |

## owner_approval

These change state or use owner infrastructure. They never run without `CGLT_OWNER_APPROVED=1`.

| ID | Proves | Needs | Status now |
|---|---|---|---|
| `owner.proxy_dd` | A dd (padded intermediate) MTProxy you run handshakes and answers an encrypted ping. | `CGLT_PROXY_HOST`, `CGLT_PROXY_PORT`, `CGLT_PROXY_SECRET` (a dd secret), `CGLT_OWNER_APPROVED=1` | PENDING USER APPROVAL |
| `owner.send_message` | A message can be posted to a test chat. | `CGLT_OWNER_APPROVED=1`, `TG_TEST_CHAT_ID`. Not implemented in this build. | PENDING USER APPROVAL (with approval: NOT RUN) |
| `owner.flood_wait_controlled` | A real FLOOD_WAIT is observed and handled. | `CGLT_OWNER_APPROVED=1`. No safe trigger is known. | PENDING USER APPROVAL (with approval: NOT RUN) |

## Checks in the library suite (not in this runner)

The following are covered by the library's own offline suite, which the runner does not repeat. See
`TEST_RESULTS.md` in the library archive.

- Framing parse for intermediate, abridged and padded intermediate: `TransportFraming.*`, `ObfuscatedPadded.*`.
- Encryption interop with the reference vectors: `CryptoAudit.*`, `Crypto.*`.
- Drop recovery with no second handshake against a fake DC: `ClientFakeDc.DropRecoveryReusesAuthKeyWithoutNewHandshake`.
- FLOOD_WAIT handling against a fake DC: `Session.FloodWait*`, `Retry.FloodWait*`, `ClientFakeDc.StopCancelsAFloodWaitInProgress`.

Passing these shows the code paths work against the fake DC. It does not show the server behaves this way.

## Bot tests

`cppgram_live_test_bot` runs checks from a chat. Commands: `/test direct`, `/test login`, `/test message`,
`/test session`, `/test reconnect`, `/test flood`, `/test proxy-plain`, `/test proxy-dd`. Its statuses are PASS, FAIL,
BLOCKED, SKIP and NOT RUN, which are the bot's own names.

- The bot has no `CGLT_OWNER_APPROVED` gate. Only the admin ids in `TG_ADMIN_IDS` can issue `/test`, so the owner
  controls it through its configuration. Do not run `/test message`, `/test proxy-dd` or `/test flood` without the
  owner's approval.
- Its offline unit tests (`livebot_unit_tests`, 136 checks, 0 failures) run without network.
- No bot live test was run in this release.
