# Security notes: CppGramLiveTest 1.0.0

- **No secrets in output.** The runner prints test ids, categories, stages, durations and redacted error text. It does
  not print API hashes, bot tokens, proxy secrets, session bytes or phone numbers.
- **Session files.** Written under the work directory with mode 0600. A corrupted file is reported and never silently
  replaced (`SessionCorruptError`). Keep the work directory out of version control.
- **Owner gates.** Live checks that use an account need `TG_API_*` and `TG_BOT_TOKEN`. Checks that change state or use
  owner infrastructure need `CGLT_OWNER_APPROVED=1`. Without it they stay PENDING USER APPROVAL and nothing is sent.
- **Interception gate.** Every check that contacts Telegram or a proxy (`live_no_credentials`, `live_credentials`,
  `owner_approval`), and the bot at startup, first runs a probe (TCP to `10.255.255.1`, which must time out). A probe
  that connects, or gives no clean result, blocks the check. `CGLT_NETWORK_UNRESTRICTED=1` never overrides the probe. No
  `TG_*` credential is read before the gate allows the check, so a blocked run sends no credential anywhere.
- **Bot access.** Only the ids in `TG_ADMIN_IDS` can issue bot test commands.
- **Deploy scripts.** `deploy/` contains VPS deployment scripts for the bot. They were not run for this release. They
  do not change any service unless the owner runs them.
- **Public repository.** The public Cpplivetest repository commits a 10.8 MB binary under `bin/`. This archive does not
  include it. Removing it from the public repository is an owner decision and has not been made.
- **Credential rotation.** Credentials used during development have not been rotated. This is an owner decision.
- **Placeholders only.** The files in `config/` contain no values. Fill them in on the machine that runs the tests, not
  in the repository.
