# Commands

All commands are admin-only. A message from any other user gets no reply. The attempt is logged with the
sender id only.

| Command | What it does |
|---|---|
| `/start` | Bot id, username, connection state and uptime |
| `/help` | Command list |
| `/status` | Uptime, connection state, the running test or `idle`, and the last error (redacted) |
| `/health` | Uptime, resident memory (VmRSS), CPU user and system time, load average, connection state, FLOOD_WAIT count |
| `/report` | Latest result per test: status, category, duration and detail |
| `/test direct` | TCP connect and handshake to the configured DC. No login |
| `/test login` | Fresh client: bot login and `get_me`, id checked against the startup identity |
| `/test session` | Fresh client: export session string, store it in a 0600 file, restore into a second fresh client, `get_me` id must match. The string is never shown |
| `/test reconnect` | Fresh client: login, controlled disconnect, connect again, `get_me` |
| `/test flood` | Offline mock of FloodWait classification and the wait limit. Sends no request. Also reports passively whether a real FLOOD_WAIT was observed |
| `/test message` | Runs from the update loop: sends a controlled message to this chat, reads it back, compares the text |
| `/test proxy-plain` | MTProxy profile A. BLOCKED if not configured |
| `/test proxy-dd` | MTProxy profile B. BLOCKED if not configured |
| `/test all` | Full suite in order. `message` is SKIP here: send `/test message` separately |

Tests run one at a time. A request while one is running gets `busy: another test is running (<name>)`.

Results appear in `/report` once a test finishes. The bot does not push results into the chat. Check `/report` or
`results.jsonl`.
