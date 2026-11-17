# CppGramLiveTest 1.0.0

Live-test project for CppGram 1.0.0. The library is a separate archive, `CppGram-v1.0.0-STABLE-READY.zip`. It has two parts:

1. **`cppgram_livetest`**: a categorised runner. Each test reports PASS, FAIL, BLOCKED, NOT RUN or PENDING USER
   APPROVAL, with the stage it reached, its error class, whether it contacted Telegram, and the library revision.
2. **`cppgram_live_test_bot`**: an MTProto bot built on CppGram. It runs the same kinds of checks from a chat, for its
   administrators.

It is a separate project. It builds the library from the CppGram source tree (unmodified) or links a prebuilt
`libcppgram.a`.

**Status of this release: no live result has been recorded.** Every live check is BLOCKED or PENDING. See
[`TEST_RESULTS.md`](TEST_RESULTS.md), and [`FINAL_V1.0.0_READINESS_REPORT.md`](FINAL_V1.0.0_READINESS_REPORT.md).

## Categories

Every test is in exactly one category.

| Category | Contacts Telegram? | Needs | Runs by default? |
|---|---|---|---|
| `offline` | no | nothing | yes |
| `environment` | no (local failure paths) | nothing | yes |
| `live_no_credentials` | yes, real DC, no account | `CGLT_NETWORK_UNRESTRICTED=1` on an unrestricted network | no: BLOCKED |
| `live_credentials` | yes, real account | network gate (clean interception probe and `CGLT_NETWORK_UNRESTRICTED=1`), then `TG_API_ID`, `TG_API_HASH`, `TG_BOT_TOKEN`. Credentials are read only after the gate allows the test. | no: BLOCKED |
| `owner_approval` | yes, changes a chat or uses owner infrastructure | network gate, `CGLT_OWNER_APPROVED=1`, and its own inputs | no: PENDING USER APPROVAL |

## Statuses

- **PASS**: the check ran and its expected result was observed.
- **FAIL**: the check ran and the result was wrong. The detail and error class say what happened.
- **BLOCKED**: the check cannot give a valid result here. The reason is in the detail. This includes a missing
  credential, a missing override, and a network that intercepts connections.
- **NOT RUN**: the check exists but was not executed in this build.
- **PENDING USER APPROVAL**: the check needs an owner decision or owner infrastructure. It does not run until the owner
  sets the approval variable.

A BLOCKED result never counts as a library PASS or FAIL.

## Quick start

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCPPGRAM_SOURCE_DIR=/path/to/cppgram
cmake --build build --target cppgram_livetest livebot_unit_tests
./build/livebot_unit_tests                  # offline unit tests of the bot code
./build/cppgram_livetest --list             # every test, with its category
./build/cppgram_livetest --workdir ./work   # default run: offline and environment checks only
```

To link a prebuilt library instead of building from source, pass `-DCPPGRAM_PREBUILT_DIR=/dir/with/libcppgram.a` and
`-DCPPGRAM_PREBUILT_INCLUDE_DIR=/path/to/cppgram/include`. Pass `-DCPPGRAM_SOURCE_DIR` as well so the runner can report
the library tree digest.

## Running the live checks (owner)

Live results are valid only on a network that does not intercept connections. The runner tests this first.

```sh
export CGLT_NETWORK_UNRESTRICTED=1
export TG_API_ID=...                 # your API id
export TG_API_HASH=...               # your API hash (never commit it)
export TG_BOT_TOKEN=...              # your bot token (never commit it)
./build/cppgram_livetest --category live_no_credentials --json live-result.json
./build/cppgram_livetest --category live_credentials --json live-result-account.json
```

The network gate is applied to every check that contacts Telegram or a proxy (`live_no_credentials`, `live_credentials`
and `owner_approval`). It requires a clean interception probe, and the probe must time out. If the probe finds
interception, or cannot give a clean result, every such check is BLOCKED, Telegram is not contacted and no credential is
read. `CGLT_NETWORK_UNRESTRICTED=1` never overrides the probe. Details: [`LIVE_TESTING.md`](LIVE_TESTING.md).

Owner-approval checks need `CGLT_OWNER_APPROVED=1` and their own inputs (see [`TESTS.md`](TESTS.md)). Do not set
`CGLT_OWNER_APPROVED` for a run you have not approved. Use `config/owner-live.env.example` as a template. It contains no
values.

## Credentials and secrets

- The runner never prints API hashes, bot tokens, proxy secrets or session bytes. Reports contain test ids, stages,
  categories, durations and redacted error text.
- Session files are written under the work directory with mode 0600. Keep the work directory out of version control.
- The sample environment files in `config/` contain placeholders only.

## Files

- `src/livetest/cli.cpp`: the runner.
- `src/`, `tests/unit_tests.cpp`: the bot and its unit tests.
- `TESTS.md`: the test catalogue and the owner-approval requirements.
- `TEST_RESULTS.md`: the runs made for this release.
- `LIVE_RESULT_TEMPLATE.md`: the form to record an owner run.
- `COMMANDS.md`: commands and exit codes.
- `SECURITY.md`: security notes for this project.
- `diagnostics/`: evidence from the development sandbox (why its results are not valid).
- `deploy/`: VPS deployment scripts for the bot. They were not run for this release and do not run from this project.

## Licence

This project carries no licence file of its own. Use the terms of the CppGram repository it was built from.
