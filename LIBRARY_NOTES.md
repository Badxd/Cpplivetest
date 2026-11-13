# CppGram behaviour observed while building the bot

The library was not modified. Each item below is a fact about the library or a limit of this bot, with the
evidence used. None of these are fixed here.

1. **Synchronous calls from another thread while `run()` is active are not safe.** The header says only
   `stop()` and `is_connected()` may be called from another thread during `run()`. The bot therefore uses the
   update-loop client only on the run thread, and every test creates its own client on a worker thread.
2. **Handlers must be registered before `connect()` or `run()`.** The bot does so.
3. **`ClientOptions::session_string` is used only when no saved session exists.** The session test uses a fresh
   `session_path` that does not exist, so the string is the only source of authorization for that client.
4. **FloodWait.** The library sleeps and retries for FLOOD_WAIT up to `retry.max_flood_wait`, and reports each one
   through the RPC error hook. The bot records passive observations. It does not provoke a FloodWait, so a live
   FloodWait may not appear in any run. Live status is reported as NOT OBSERVED until one is seen.
5. **Open issue: `plain packet has N bytes after its length field` (N = 22, 102).** The library rejects plain
   replies with more than 15 bytes after the length field. The same rule is in the library's own audit documents
   (MTProxy_AUDIT.md, section 6). The bot records the error text and the stage. It does not relax the rule. Root
   cause is not determined.
6. **Padding rule.** The published MTProto transport rule defines padding for client-to-server padded
   intermediate frames only. The library tolerates up to 15 trailing bytes on plain replies. That is a
   compatibility choice, not part of the rule.
7. **`auto_reconnect` in `run()`** is not exercised by the bot's tests. The bot restarts the update loop itself
   (up to 5 times) when `run()` throws.
8. **MTProxy.** Only plain (16-byte) and dd (17-byte) secrets are accepted. Fake-TLS (`ee`) secrets are rejected
   by the library, and the bot checks this before any network use.
