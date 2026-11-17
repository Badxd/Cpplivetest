// Regression tests for the network-interception gate (src/netgate.h). Offline: no socket is opened, no Telegram
// address is used, and no real credential exists. Every "credential" and "contact" below is a counter in this file.
// Run: ctest, or ./livetest_gate_tests.
#include <cstdio>
#include <string>

#include "netgate.h"

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        if (!(cond)) {                                                                                                 \
            ++g_failures;                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                       \
        }                                                                                                              \
    } while (0)

using livegate::Decision;
using livegate::GateInputs;
using livegate::Probe;
using livegate::ProbeOutcome;
using livegate::TestGate;
using livegate::Verdict;

// Stand-in for a test body. `credential_reads` counts reads of TG_API_ID / TG_API_HASH / TG_BOT_TOKEN and
// `contacts` counts packets that would leave for Telegram or a proxy. Both are only reachable inside the body.
struct FakeTest {
    int body_runs = 0;
    int credential_reads = 0;
    int contacts = 0;

    void operator()() {
        ++body_runs;
        ++credential_reads; // the body reads its credentials first
        ++contacts; // and then opens a connection to Telegram
    }
};

const TestGate kLiveNoCreds = livegate::gate_for_category("live_no_credentials");
const TestGate kLiveCreds = livegate::gate_for_category("live_credentials");
const TestGate kOwner = livegate::gate_for_category("owner_approval");
const TestGate kOffline = livegate::gate_for_category("offline");
const TestGate kEnvironment = livegate::gate_for_category("environment");

GateInputs make(Probe p, bool declared, bool approved) {
    GateInputs in;
    in.probe = p;
    in.unrestricted_declared = declared;
    in.owner_approved = approved;
    return in;
}

// Full path a test takes in the runner: gate first, body only on Allow.
bool attempt(const TestGate& gate, const GateInputs& in, FakeTest& t, Decision& out) {
    out = livegate::run_if_allowed(gate, in, [&]() {
        t();
    });
    return out.verdict == Verdict::Allow;
}

void test_classify_probe() {
    CHECK(livegate::classify_probe(ProbeOutcome::TimedOut) == Probe::Clean);
    CHECK(livegate::classify_probe(ProbeOutcome::Connected) == Probe::Intercepted);
    CHECK(livegate::classify_probe(ProbeOutcome::RefusedOrError) == Probe::Inconclusive);
    CHECK(livegate::classify_probe(ProbeOutcome::Other) == Probe::Inconclusive);
    CHECK(std::string(livegate::probe_text(Probe::Clean)) == "clean");
    CHECK(std::string(livegate::probe_text(Probe::Intercepted)) == "intercepted");
}

void test_category_mapping() {
    CHECK(!kOffline.network && !kOffline.owner_approval);
    CHECK(!kEnvironment.network && !kEnvironment.owner_approval);
    CHECK(kLiveNoCreds.network && !kLiveNoCreds.owner_approval);
    CHECK(kLiveCreds.network && !kLiveCreds.owner_approval);
    CHECK(kOwner.network && kOwner.owner_approval);
    const TestGate unknown = livegate::gate_for_category("some_future_category");
    CHECK(unknown.network && unknown.owner_approval); // unknown categories fail closed
}

// Scenario 1: intercepted network, credentials configured. Blocked, body never runs, nothing sent.
void test_intercepted_with_credentials_is_blocked() {
    FakeTest t;
    Decision d;
    CHECK(!attempt(kLiveCreds, make(Probe::Intercepted, false, false), t, d));
    CHECK(d.verdict == Verdict::Blocked);
    CHECK(t.body_runs == 0 && t.credential_reads == 0 && t.contacts == 0);
    CHECK(d.reason.find("intercepted") != std::string::npos);
    CHECK(d.reason.find("does not override") != std::string::npos);
}

// Scenario 2: intercepted network, owner override declared. Still blocked, nothing sent, no credential read.
void test_intercepted_with_override_is_blocked() {
    FakeTest t;
    Decision d;
    CHECK(!attempt(kLiveCreds, make(Probe::Intercepted, true, false), t, d));
    CHECK(d.verdict == Verdict::Blocked);
    CHECK(t.body_runs == 0 && t.credential_reads == 0 && t.contacts == 0);

    FakeTest n;
    CHECK(!attempt(kLiveNoCreds, make(Probe::Intercepted, true, true), n, d));
    CHECK(d.verdict == Verdict::Blocked);
    CHECK(n.body_runs == 0 && n.contacts == 0);
}

// Scenario 2b: override and owner approval do not open an owner test on an intercepted network.
void test_intercepted_owner_test_is_blocked() {
    FakeTest t;
    Decision d;
    CHECK(!attempt(kOwner, make(Probe::Intercepted, true, true), t, d));
    CHECK(d.verdict == Verdict::Blocked);
    CHECK(t.body_runs == 0 && t.credential_reads == 0 && t.contacts == 0);
}

// An inconclusive or missing probe blocks every network test, even with the declaration and approval.
void test_inconclusive_probe_blocks() {
    const Probe bad[] = { Probe::Inconclusive, Probe::NotRun };
    for (Probe p : bad) {
        FakeTest t;
        Decision d;
        CHECK(!attempt(kLiveCreds, make(p, true, true), t, d));
        CHECK(d.verdict == Verdict::Blocked);
        CHECK(t.body_runs == 0 && t.credential_reads == 0 && t.contacts == 0);
        FakeTest o;
        CHECK(!attempt(kOwner, make(p, true, true), o, d));
        CHECK(d.verdict == Verdict::Blocked);
        CHECK(o.body_runs == 0 && o.contacts == 0);
    }
}

// A clean probe without the declaration is still blocked: the declaration is required, the probe alone is not enough.
void test_clean_without_declaration_is_blocked() {
    FakeTest t;
    Decision d;
    CHECK(!attempt(kLiveCreds, make(Probe::Clean, false, false), t, d));
    CHECK(d.verdict == Verdict::Blocked);
    CHECK(t.body_runs == 0 && t.credential_reads == 0 && t.contacts == 0);
}

// Scenario 3: unrestricted network, owner approval missing. Owner-gated tests do not run and nothing is read or sent.
void test_unrestricted_without_approval_keeps_owner_tests_gated() {
    FakeTest t;
    Decision d;
    CHECK(!attempt(kOwner, make(Probe::Clean, true, false), t, d));
    CHECK(d.verdict == Verdict::Pending);
    CHECK(t.body_runs == 0 && t.credential_reads == 0 && t.contacts == 0);
    CHECK(d.reason.find("CGLT_OWNER_APPROVED=1") != std::string::npos);
}

// Scenario 4: unrestricted network and required approval. Tests may proceed.
void test_unrestricted_with_approval_proceeds() {
    FakeTest owner;
    Decision d;
    CHECK(attempt(kOwner, make(Probe::Clean, true, true), owner, d));
    CHECK(d.verdict == Verdict::Allow);
    CHECK(owner.body_runs == 1 && owner.credential_reads == 1 && owner.contacts == 1);

    FakeTest creds;
    CHECK(attempt(kLiveCreds, make(Probe::Clean, true, false), creds, d)); // credential tests need no approval
    CHECK(d.verdict == Verdict::Allow);
    CHECK(creds.body_runs == 1);

    FakeTest live;
    CHECK(attempt(kLiveNoCreds, make(Probe::Clean, true, false), live, d));
    CHECK(d.verdict == Verdict::Allow);
    CHECK(live.body_runs == 1);
}

// Scenario 5: credentials are not read before the gate passes. The body is the only place they are read, so
// a blocked or pending test must leave the read counter at zero.
void test_credentials_not_read_before_gate() {
    FakeTest blocked_case;
    Decision d;
    attempt(kLiveCreds, make(Probe::Intercepted, true, true), blocked_case, d);
    CHECK(blocked_case.credential_reads == 0);

    FakeTest pending_case;
    attempt(kOwner, make(Probe::Clean, true, false), pending_case, d);
    CHECK(pending_case.credential_reads == 0);

    FakeTest allowed_case;
    attempt(kLiveCreds, make(Probe::Clean, true, false), allowed_case, d);
    CHECK(allowed_case.credential_reads == 1);
}

// Offline and environment tests never reach the network gate, so an intercepted network does not affect them.
void test_offline_tests_unaffected() {
    FakeTest t;
    Decision d;
    CHECK(attempt(kOffline, make(Probe::Intercepted, false, false), t, d));
    CHECK(d.verdict == Verdict::Allow && t.body_runs == 1);
    FakeTest e;
    CHECK(attempt(kEnvironment, make(Probe::NotRun, false, false), e, d));
    CHECK(d.verdict == Verdict::Allow && e.body_runs == 1);
}

// Blocked and pending reasons never contain a secret or a value from the environment.
void test_reasons_are_free_of_secrets() {
    const std::string token_fixture = "123456789:AAExampleTokenValueOnlyForUnitTests0123";
    Decision d;
    FakeTest t;
    attempt(kLiveCreds, make(Probe::Intercepted, true, true), t, d);
    CHECK(d.reason.find(token_fixture) == std::string::npos);
    attempt(kOwner, make(Probe::Clean, true, false), t, d);
    CHECK(d.reason.find(token_fixture) == std::string::npos);
}

} // namespace

int main() {
    test_classify_probe();
    test_category_mapping();
    test_intercepted_with_credentials_is_blocked();
    test_intercepted_with_override_is_blocked();
    test_intercepted_owner_test_is_blocked();
    test_inconclusive_probe_blocks();
    test_clean_without_declaration_is_blocked();
    test_unrestricted_without_approval_keeps_owner_tests_gated();
    test_unrestricted_with_approval_proceeds();
    test_credentials_not_read_before_gate();
    test_offline_tests_unaffected();
    test_reasons_are_free_of_secrets();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
