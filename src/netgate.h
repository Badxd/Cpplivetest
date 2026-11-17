// Network-interception gate shared by cppgram_livetest and cppgram_live_test_bot.
//
// Every test or process that can send packets to Telegram or to an external proxy goes through this gate first.
// The gate runs before any credential is read from the environment. A test body is reached only through
// run_if_allowed(), and only when evaluate() returns Allow.
//
//   1. Interception probe (probe_network): TCP connect to 10.255.255.1:443 with a 1500 ms timeout.
//        Timed out          -> Clean         (packets are dropped, as on a direct route)
//        Connection made    -> Intercepted   (the network answers for an address nothing should accept)
//        Refused or error   -> Inconclusive
//        Anything else     -> Inconclusive
//   2. CGLT_NETWORK_UNRESTRICTED=1 must be set. It only declares the owner's intent. It never overrides step 1.
//   3. Owner-gated tests also need CGLT_OWNER_APPROVED=1.
//
// Only a Clean probe with a declared unrestricted network allows a network test. Everything else is BLOCKED or
// PENDING USER APPROVAL, with no packet sent to Telegram and no credential read.
#pragma once

#include <string>

namespace livegate {

enum class Probe { NotRun, Clean, Intercepted, Inconclusive };

// What one probe attempt observed. classify_probe maps it to a Probe.
enum class ProbeOutcome { Connected, TimedOut, RefusedOrError, Other };

Probe classify_probe(ProbeOutcome outcome);
const char* probe_text(Probe p);

// Runs the real probe. Sends one TCP SYN to 10.255.255.1:443 and nothing else. Never contacts Telegram.
Probe probe_network();

// Whether a test category sends packets to Telegram or to a proxy, and whether it needs owner approval.
// Unknown categories fail closed: network and owner approval are both required.
struct TestGate {
    bool network;
    bool owner_approval;
};
TestGate gate_for_category(const std::string& category);

struct GateInputs {
    Probe probe = Probe::NotRun;
    bool unrestricted_declared = false; // CGLT_NETWORK_UNRESTRICTED=1
    bool owner_approved = false; // CGLT_OWNER_APPROVED=1
};

enum class Verdict { Allow, Blocked, Pending };

struct Decision {
    Verdict verdict = Verdict::Blocked;
    std::string reason; // never contains a secret
};

// Pure decision. No I/O.
Decision evaluate(const TestGate& gate, const GateInputs& in);

// The only way a test body is reached. Credentials and sockets belong inside `body`, so they are never touched
// unless the gate allows the test. Returns the decision; `body` runs only when the verdict is Allow.
template<class Body>
Decision run_if_allowed(const TestGate& gate, const GateInputs& in, Body&& body) {
    Decision d = evaluate(gate, in);
    if (d.verdict == Verdict::Allow)
        body();
    return d;
}

} // namespace livegate
