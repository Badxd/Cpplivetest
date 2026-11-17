#include "netgate.h"

#include "cppgram/mtproto/connection.h"

namespace mt = cppgram::mtproto;

namespace livegate {

Probe classify_probe(ProbeOutcome outcome) {
    switch (outcome) {
    case ProbeOutcome::Connected:
        return Probe::Intercepted;
    case ProbeOutcome::TimedOut:
        return Probe::Clean;
    case ProbeOutcome::RefusedOrError:
    case ProbeOutcome::Other:
        return Probe::Inconclusive;
    }
    return Probe::Inconclusive;
}

const char* probe_text(Probe p) {
    switch (p) {
    case Probe::NotRun:
        return "not run";
    case Probe::Clean:
        return "clean";
    case Probe::Intercepted:
        return "intercepted";
    case Probe::Inconclusive:
        return "inconclusive";
    }
    return "inconclusive";
}

Probe probe_network() {
    try {
        mt::Connection c;
        c.set_connect_timeout(1500);
        c.connect("10.255.255.1", 443);
        c.close();
        return classify_probe(ProbeOutcome::Connected);
    } catch (const mt::ConnectionTimeout&) {
        return classify_probe(ProbeOutcome::TimedOut);
    } catch (const mt::ConnectionError&) {
        return classify_probe(ProbeOutcome::RefusedOrError);
    } catch (...) {
        return classify_probe(ProbeOutcome::Other);
    }
}

TestGate gate_for_category(const std::string& category) {
    if (category == "offline" || category == "environment")
        return { false, false };
    if (category == "live_no_credentials" || category == "live_credentials")
        return { true, false };
    if (category == "owner_approval")
        return { true, true };
    return { true, true }; // unknown category: fail closed
}

Decision evaluate(const TestGate& gate, const GateInputs& in) {
    if (gate.network) {
        switch (in.probe) {
        case Probe::Clean:
            break;
        case Probe::Intercepted:
            return { Verdict::Blocked,
                     "not valid from this network: TCP to 10.255.255.1 was accepted, so connections are intercepted. "
                     "Telegram was not contacted and no credential was read. CGLT_NETWORK_UNRESTRICTED does not "
                     "override this." };
        case Probe::Inconclusive:
            return {
                Verdict::Blocked,
                "network gate inconclusive: the interception probe did not give a clean result (it must time out). "
                "Telegram was not contacted and no credential was read."
            };
        case Probe::NotRun:
            return { Verdict::Blocked,
                     "network gate did not run: Telegram was not contacted and no credential was read." };
        }
        if (!in.unrestricted_declared) {
            return {
                Verdict::Blocked,
                "not run: set CGLT_NETWORK_UNRESTRICTED=1 on an unrestricted network. The declaration does not bypass "
                "the interception probe. Telegram was not contacted."
            };
        }
    }
    if (gate.owner_approval && !in.owner_approved) {
        return {
            Verdict::Pending,
            "owner approval (CGLT_OWNER_APPROVED=1) not given. Nothing was contacted and no credential was read."
        };
    }
    return { Verdict::Allow, "" };
}

} // namespace livegate
