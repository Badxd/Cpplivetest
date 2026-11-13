#include "tests.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <random>
#include <stdexcept>
#include <vector>

#include "cppgram/client.h"
#include "cppgram/errors.h"
#include "cppgram/mtproto/retry.h"
#include "log.h"
#include "proxy_profile.h"
#include "state.h"

namespace livebot {
namespace {

std::string random_hex(std::size_t n) {
    static const char* digits = "0123456789abcdef";
    std::random_device rd;
    std::string out;
    for (std::size_t i = 0; i < n; ++i) out.push_back(digits[rd() % 16]);
    return out;
}

std::string tmp_path(const TestEnv& env, const std::string& label) {
    return ensure_tmp_dir(env.cfg.data_dir) + "/" + label + "-" + random_hex(12) + ".session";
}

cppgram::ClientOptions base_options(const TestEnv& env, const std::string& session_path) {
    cppgram::ClientOptions o;
    o.api_id = env.cfg.api_id;
    o.api_hash = env.cfg.api_hash;
    o.dc_id = env.cfg.dc_id;
    o.dc_ip = *dc_address(env.cfg.dc_id);
    o.dc_port = 443;
    o.session_path = session_path;
    o.debug = env.cfg.debug;
    o.auto_reconnect = false;
    o.connect_timeout_ms = 10000;
    o.retry.max_flood_wait = std::chrono::seconds(env.cfg.flood_max_wait_seconds);
    return o;
}

std::string category_of(const std::exception& e) {
    return std::string(cppgram::errors::to_string(cppgram::errors::categorize(e)));
}

// Runs `fn` and converts any exception into a FAIL result labelled with the stage that was running.
template <typename Fn>
TestResult guarded(TestId id, const std::string& stage, Fn&& fn) {
    try {
        return fn();
    } catch (const std::exception& e) {
        return make_result(id, Status::Fail, category_of(e), "stage=" + stage + ": " + e.what());
    }
}

// Bot login plus identity check. Returns the verified user id through `out_id`.
void login_and_verify(cppgram::Client& client, const TestEnv& env, std::int64_t& out_id) {
    client.connect();
    client.login_bot(env.cfg.bot_token);
    const cppgram::User me = client.get_me();
    if (!me.is_bot) throw std::runtime_error("get_me returned a non-bot account for a bot login");
    if (env.bot_id && me.id != *env.bot_id) throw std::runtime_error("get_me id differs from the startup identity");
    out_id = me.id;
}

TestResult test_direct(const TestEnv& env) {
    return guarded(TestId::Direct, "connect", [&]() {
        const std::string path = tmp_path(env, "direct");
        cppgram::Client client(base_options(env, path));
        client.connect();
        const bool up = client.is_connected();
        client.disconnect();
        shred_and_remove(path);
        if (!up) return make_result(TestId::Direct, Status::Fail, "network", "connect returned but is_connected() is false");
        return make_result(TestId::Direct, Status::Pass, "",
                           "TCP connect, MTProto key exchange and initConnection to DC " +
                               std::to_string(env.cfg.dc_id) + " succeeded");
    });
}

TestResult test_login(const TestEnv& env) {
    const std::string path = tmp_path(env, "login");
    TestResult r = guarded(TestId::Login, "login_bot/get_me", [&]() {
        cppgram::Client client(base_options(env, path));
        std::int64_t id = 0;
        login_and_verify(client, env, id);
        client.disconnect();
        return make_result(TestId::Login, Status::Pass, "",
                           "bot login and get_me succeeded (bot id " + std::to_string(id) + ")");
    });
    shred_and_remove(path);
    return r;
}

TestResult test_session(const TestEnv& env) {
    const std::string dir = ensure_tmp_dir(env.cfg.data_dir);
    const std::string origin_path = tmp_path(env, "session-origin");
    const std::string restored_path = tmp_path(env, "session-restored");
    std::string stored_file;
    TestResult r = guarded(TestId::Session, "export/restore", [&]() {
        std::int64_t origin_id = 0;
        std::string exported;
        {
            cppgram::Client client(base_options(env, origin_path));
            login_and_verify(client, env, origin_id);
            exported = client.export_session_string();
            client.disconnect();
        }
        if (exported.empty()) throw std::runtime_error("export_session_string returned an empty string");

        // Store in a 0600 file, read it back, and never print it.
        stored_file = write_private_file(dir, "session-export-" + random_hex(8) + ".txt", exported);
        if (stored_file.empty()) throw std::runtime_error("could not create the restricted session file");
        std::string reloaded;
        {
            FILE* f = std::fopen(stored_file.c_str(), "rb");
            if (!f) throw std::runtime_error("could not reopen the restricted session file");
            char buf[4096];
            std::size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) reloaded.append(buf, n);
            std::fclose(f);
        }
        if (reloaded != exported) throw std::runtime_error("session file contents changed on disk");

        cppgram::ClientOptions o = base_options(env, restored_path);  // no saved session yet: restore from string
        o.session_string = reloaded;
        cppgram::Client restored(o);
        restored.connect();
        const cppgram::User me = restored.get_me();
        restored.disconnect();
        if (me.id != origin_id) throw std::runtime_error("restored identity does not match the original bot");
        return make_result(TestId::Session, Status::Pass, "",
                           "session exported (" + std::to_string(exported.size()) +
                               " chars, not shown), restored into a fresh client, get_me id matches (" +
                               std::to_string(me.id) + ")");
    });
    shred_and_remove(origin_path);
    shred_and_remove(restored_path);
    if (!stored_file.empty()) shred_and_remove(stored_file);
    return r;
}

TestResult test_reconnect(const TestEnv& env) {
    const std::string path = tmp_path(env, "reconnect");
    TestResult r = guarded(TestId::Reconnect, "connect/disconnect/connect", [&]() {
        cppgram::Client client(base_options(env, path));
        std::int64_t id = 0;
        login_and_verify(client, env, id);
        client.disconnect();
        client.connect();
        const cppgram::User me = client.get_me();
        client.disconnect();
        if (me.id != id) throw std::runtime_error("identity changed after reconnect");
        return make_result(TestId::Reconnect, Status::Pass, "",
                           "controlled disconnect then connect, get_me id unchanged. "
                           "Network-drop recovery under run() is not exercised by this test.");
    });
    shred_and_remove(path);
    return r;
}

TestResult test_flood(const TestEnv& env) {
    // Deterministic offline mock. Uses the library's own classification and wait policy on synthetic errors.
    // This never sends a request to Telegram, so it can not produce a live FloodWait.
    namespace mt = cppgram::mtproto;
    mt::RetryPolicy policy;
    policy.max_flood_wait = std::chrono::seconds(env.cfg.flood_max_wait_seconds);

    const mt::ErrorDecision short_wait = mt::classify_rpc_error(420, "FLOOD_WAIT_5");
    const mt::ErrorDecision long_wait = mt::classify_rpc_error(420, "FLOOD_WAIT_100000");
    const mt::ErrorDecision other = mt::classify_rpc_error(400, "PEER_ID_INVALID");

    std::vector<std::string> failures;
    if (short_wait.kind != mt::ErrorClass::FloodWait || short_wait.wait.count() != 5)
        failures.push_back("FLOOD_WAIT_5 not parsed as a 5 s FloodWait");
    if (!mt::flood_wait_allowed(policy, short_wait)) failures.push_back("5 s wait refused by the policy");
    if (long_wait.kind != mt::ErrorClass::FloodWait) failures.push_back("FLOOD_WAIT_100000 not classified as FloodWait");
    if (mt::flood_wait_allowed(policy, long_wait)) failures.push_back("wait above the policy limit was allowed");
    if (other.kind != mt::ErrorClass::Permanent) failures.push_back("non-flood error not Permanent");

    const auto observed = BotState::instance().floods();
    const std::string live = observed.empty()
                                 ? "LIVE FLOODWAIT: NOT OBSERVED"
                                 : "LIVE FLOODWAIT: observed " + std::to_string(observed.size()) + " time(s) on the update loop";
    if (!failures.empty()) {
        std::string msg = "offline mock failed:";
        for (const auto& f : failures) msg += " " + f + ";";
        return make_result(TestId::Flood, Status::Fail, "flood_wait", msg + " " + live);
    }
    return make_result(TestId::Flood, Status::Pass, "",
                       "OFFLINE MOCK PASS (no request sent): parse, retry-after, limit check. " + live);
}

TestResult test_proxy(TestId id, const TestEnv& env) {
    const bool dd = (id == TestId::ProxyDd);
    const std::optional<ProxyProfile>& profile = dd ? env.cfg.proxy_dd : env.cfg.proxy_plain;
    const std::string label = dd ? "proxy-dd" : "proxy-plain";
    if (!profile) {
        return make_result(id, Status::Blocked, "not_configured",
                           std::string("no ") + (dd ? "TG_PROXY_DD_*" : "TG_PROXY_PLAIN_*") +
                               " profile in the environment: no authorized endpoint to test");
    }
    const std::string problem = check_proxy_profile(*profile, dd);
    if (!problem.empty()) return make_result(id, Status::Fail, "config", label + " profile rejected: " + problem);

    const std::string path = tmp_path(env, label);
    // The stage variable is updated as the flow advances, so a failure names the step that failed.
    std::string stage = "tcp+handshake";
    TestResult r;
    try {
        cppgram::ClientOptions o = base_options(env, path);
        cppgram::MtprotoProxyOptions p;
        p.host = profile->host;
        p.port = profile->port;
        p.secret = profile->secret;
        o.mtproto_proxy = p;

        cppgram::Client client(o);
        client.connect();
        stage = "login_bot/get_me";
        std::int64_t id_out = 0;
        login_and_verify(client, env, id_out);
        client.disconnect();
        stage = "reconnect";
        client.connect();
        const cppgram::User again = client.get_me();
        client.disconnect();
        if (again.id != id_out) throw std::runtime_error("identity changed after proxy reconnect");
        r = make_result(id, Status::Pass, "",
                        label + ": connected through the proxy, bot login, get_me, reconnect and clean disconnect ok");
    } catch (const std::exception& e) {
        r = make_result(id, Status::Fail, category_of(e), label + " stage=" + stage + ": " + e.what());
    }
    shred_and_remove(path);
    return r;
}

}  // namespace

std::string ensure_tmp_dir(const std::string& data_dir) {
    const std::string dir = data_dir + "/tmp";
    mkdir(data_dir.c_str(), 0700);
    mkdir(dir.c_str(), 0700);
    chmod(dir.c_str(), 0700);
    return dir;
}

std::string write_private_file(const std::string& dir, const std::string& name, const std::string& text) {
    const std::string path = dir + "/" + name;
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd < 0) return "";
    std::size_t off = 0;
    while (off < text.size()) {
        const ssize_t n = ::write(fd, text.data() + off, text.size() - off);
        if (n <= 0) {
            ::close(fd);
            ::unlink(path.c_str());
            return "";
        }
        off += static_cast<std::size_t>(n);
    }
    ::fchmod(fd, 0600);
    ::close(fd);
    return path;
}

void shred_and_remove(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) == 0 && st.st_size > 0) {
        const int fd = ::open(path.c_str(), O_WRONLY);
        if (fd >= 0) {
            std::vector<char> zeros(static_cast<std::size_t>(st.st_size), 0);
            const ssize_t wr = ::write(fd, zeros.data(), zeros.size());
            (void)wr;
            ::fsync(fd);
            ::close(fd);
        }
    }
    ::unlink(path.c_str());
}

TestResult run_test(TestId id, const TestEnv& env) {
    switch (id) {
        case TestId::Direct: return test_direct(env);
        case TestId::Login: return test_login(env);
        case TestId::Session: return test_session(env);
        case TestId::Reconnect: return test_reconnect(env);
        case TestId::Flood: return test_flood(env);
        case TestId::Message:
            // Needs the admin's chat and the update-loop client, so it runs from the command handler.
            return make_result(id, Status::Skip, "", "message test runs from the update loop: send /test message");
        case TestId::ProxyPlain:
        case TestId::ProxyDd: return test_proxy(id, env);
    }
    return make_result(id, Status::Fail, "unexpected", "unknown test");
}

}  // namespace livebot
