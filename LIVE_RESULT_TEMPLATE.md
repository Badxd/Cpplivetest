# Live result record: CppGramLiveTest

Fill this in for each owner run. Do not paste API hashes, bot tokens, proxy secrets, session bytes or phone numbers.
Attach the JSON file the runner wrote (`--json`). Its contents are redacted.

## Run identification

- Date and time (UTC):
- Operator:
- Library version (from the runner header): ____
- Library tree digest (from the runner header): ____
- Runner binary built from: (commit or archive name) ____
- Compiler and version: ____
- Host OS and kernel: ____
- Network: (describe, for example "home fibre, no proxy") ____

## Network validity

- `CGLT_NETWORK_UNRESTRICTED` set to 1 by the operator? yes / no
- Interception probe result (the line starting "probe:" or the BLOCKED reason): ____
- Result: the network is unrestricted (the probe reported refused or timed out) / intercepted (results are not valid)

If the network is intercepted, stop here. Results from it are not valid. Do not record them as PASS or FAIL.

## Owner approvals in this run

- `CGLT_OWNER_APPROVED`: not set / set to 1 for: ____
- Owner-approved tests run: ____ (leave empty if none)

## Results

Copy one row per test from the runner output.

| Test ID | Category | Status (PASS, FAIL, BLOCKED, NOT RUN, PENDING USER APPROVAL) | Stage reached | Error class | Telegram contacted (yes/no) | Duration (ms) | Notes (no secrets) |
|---|---|---|---|---|---|---|---|
| | | | | | | | |

## Failures

For each FAIL: the test ID, the stage, the error class, the redacted detail, and whether it reproduces on a second run.
Do not classify a FAIL as a library bug until it reproduces on an unrestricted network.

## Sign-off

- Result summary (one sentence):
- Follow-up actions:
