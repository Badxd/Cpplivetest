// Offline unit tests for the bot. No network, no Telegram. Run: ctest, or ./livebot_unit_tests.
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "cppgram/mtproto/retry.h"
#include "commands.h"
#include "config.h"
#include "log.h"
#include "proxy_profile.h"
#include "state.h"
#include "test_runner.h"
#include "tests.h"

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        ++g_checks;                                                                  \
        if (!(cond)) {                                                               \
            ++g_failures;                                                            \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
        }                                                                            \
    } while (0)

using livebot::Command;

std::function<const char*(const char*)> env_from(const std::vector<std::pair<std::string, std::string>>& kv) {
    return [kv](const char* name) -> const char* {
        for (const auto& p : kv) {
            if (p.first == name) return p.second.c_str();
        }
        return nullptr;
    };
}

const std::string kToken = "123456789:AAExampleTokenValueOnlyForUnitTests0123";
const std::string kHash = "0123456789abcdef0123456789abcdef";
const std::string kPlainSecret = "0123456789abcdef0123456789abcdef";
const std::string kDdSecret = "dd0123456789abcdef0123456789abcdef";
const std::string kFakeTlsSecret = "ee0123456789abcdef0123456789abcdef6578616d706c652e636f6d";

std::vector<std::pair<std::string, std::string>> valid_env() {
    return {{"TG_API_ID", "12345"}, {"TG_API_HASH", kHash}, {"TG_BOT_TOKEN", kToken},
            {"TG_ADMIN_IDS", "111,222"}, {"TG_DC", "2"}, {"TG_DATA_DIR", "/var/lib/cppgram-livebot"},
            {"TG_LOG_DIR", "/var/log/cppgram-livebot"}};
}

void test_config_load_and_validate() {
    livebot::Config cfg;
    CHECK(livebot::load_config(cfg, env_from(valid_env())).empty());
    CHECK(livebot::validate_config(cfg).empty());
    CHECK(cfg.api_id == 12345);
    CHECK(cfg.admin_ids.size() == 2 && cfg.admin_ids[1] == 222);
    CHECK(cfg.dc_id == 2);

    auto missing_token = valid_env();
    missing_token.erase(missing_token.begin() + 2);
    livebot::Config c2;
    CHECK(livebot::load_config(c2, env_from(missing_token)).empty());  // an empty token is not a load error
    CHECK(livebot::validate_config(c2).find("TG_BOT_TOKEN") != std::string::npos);

    auto bad_hash = valid_env();
    bad_hash[1].second = "not-a-hash";
    livebot::Config c3;
    CHECK(livebot::load_config(c3, env_from(bad_hash)).empty());
    CHECK(livebot::validate_config(c3).find("TG_API_HASH") != std::string::npos);

    auto bad_admin = valid_env();
    bad_admin[3].second = "111,abc";
    livebot::Config c4;
    CHECK(livebot::load_config(c4, env_from(bad_admin)).find("TG_ADMIN_IDS") != std::string::npos);

    auto bad_dc = valid_env();
    bad_dc[4].second = "9";
    livebot::Config c5;
    CHECK(livebot::load_config(c5, env_from(bad_dc)).empty());
    CHECK(livebot::validate_config(c5).find("TG_DC") != std::string::npos);

    auto half_proxy = valid_env();
    half_proxy.push_back({"TG_PROXY_PLAIN_HOST", "proxy.example"});
    livebot::Config c6;
    CHECK(livebot::load_config(c6, env_from(half_proxy)).find("must all be set together") != std::string::npos);

    auto full_proxy = valid_env();
    full_proxy.push_back({"TG_PROXY_DD_HOST", "proxy.example"});
    full_proxy.push_back({"TG_PROXY_DD_PORT", "443"});
    full_proxy.push_back({"TG_PROXY_DD_SECRET", kDdSecret});
    livebot::Config c7;
    CHECK(livebot::load_config(c7, env_from(full_proxy)).empty());
    CHECK(c7.proxy_dd && c7.proxy_dd->port == 443 && c7.proxy_dd->secret == kDdSecret);
    CHECK(!c7.proxy_plain);

    auto bad_port = valid_env();
    bad_port.push_back({"TG_PROXY_PLAIN_HOST", "h"});
    bad_port.push_back({"TG_PROXY_PLAIN_PORT", "70000"});
    bad_port.push_back({"TG_PROXY_PLAIN_SECRET", kPlainSecret});
    livebot::Config c8;
    CHECK(livebot::load_config(c8, env_from(bad_port)).find("1..65535") != std::string::npos);
}

void test_token_and_hash_shapes() {
    CHECK(livebot::is_valid_bot_token(kToken));
    CHECK(!livebot::is_valid_bot_token("nocolon"));
    CHECK(!livebot::is_valid_bot_token(":AAExample"));
    CHECK(!livebot::is_valid_bot_token("12x:AAExampleTokenValueOnlyForUnitTests0123"));
    CHECK(!livebot::is_valid_bot_token("123:short"));
    CHECK(!livebot::is_valid_bot_token("123:AAExample with space 0123456789012345"));
    CHECK(livebot::is_valid_api_hash(kHash));
    CHECK(!livebot::is_valid_api_hash("0123"));
    CHECK(!livebot::is_valid_api_hash("g123456789abcdef0123456789abcdef"));
    CHECK(livebot::dc_address(2) && *livebot::dc_address(2) == "149.154.167.51");
    CHECK(!livebot::dc_address(6));
}

void test_proxy_profiles() {
    livebot::ProxyProfile plain{"proxy.example", 443, kPlainSecret};
    livebot::ProxyProfile dd{"proxy.example", 443, kDdSecret};
    livebot::ProxyProfile tls{"proxy.example", 443, kFakeTlsSecret};
    CHECK(livebot::check_proxy_profile(plain, false).empty());
    CHECK(!livebot::check_proxy_profile(plain, true).empty());  // plain secret in the dd profile is rejected
    CHECK(livebot::check_proxy_profile(dd, true).empty());
    CHECK(!livebot::check_proxy_profile(dd, false).empty());    // dd secret in the plain profile is rejected
    CHECK(livebot::check_proxy_profile(tls, false).find("fake-TLS") != std::string::npos);
    CHECK(livebot::check_proxy_profile(tls, true).find("fake-TLS") != std::string::npos);
    CHECK(!livebot::check_proxy_profile({"", 443, kPlainSecret}, false).empty());
    CHECK(!livebot::check_proxy_profile({"h", 0, kPlainSecret}, false).empty());
    const std::string reason = livebot::check_proxy_profile({"h", 443, "zz-not-a-secret"}, false);
    CHECK(!reason.empty());
    CHECK(reason.find("zz-not-a-secret") == std::string::npos);  // the reason never echoes the secret
}

void test_command_parsing() {
    CHECK(livebot::parse_command("/start").command == Command::Start);
    CHECK(livebot::parse_command("/HELP").command == Command::Help);
    CHECK(livebot::parse_command("/status@my_bot").command == Command::Status);
    CHECK(livebot::parse_command("  /report  ").command == Command::Report);
    CHECK(livebot::parse_command("/health").command == Command::Health);
    CHECK(livebot::parse_command("hello").command == Command::Unknown);
    CHECK(livebot::parse_command("").command == Command::Unknown);
    CHECK(livebot::parse_command("/nope").command == Command::Unknown);

    auto direct = livebot::parse_command("/test direct");
    CHECK(direct.command == Command::Test && direct.test == livebot::TestId::Direct);
    CHECK(livebot::parse_command("/test PROXY-DD").test == livebot::TestId::ProxyDd);
    CHECK(livebot::parse_command("/test all").command == Command::TestFull);
    CHECK(livebot::parse_command("/test").command == Command::Unknown);
    CHECK(livebot::parse_command("/test bogus").command == Command::Unknown);
    CHECK(livebot::parse_command("/test session").test == livebot::TestId::Session);
    CHECK(livebot::parse_command("/test flood").test == livebot::TestId::Flood);
    CHECK(livebot::parse_command("/test reconnect").test == livebot::TestId::Reconnect);
    CHECK(livebot::parse_command("/test message").test == livebot::TestId::Message);
    CHECK(livebot::parse_command("/test login").test == livebot::TestId::Login);
}

void test_sender_resolution() {
    cppgram::Message dm;
    dm.chat.kind = cppgram::PeerKind::User;
    dm.chat.id = 111;
    CHECK(livebot::sender_id_of(dm) && *livebot::sender_id_of(dm) == 111);  // private chat, no from_id
    cppgram::Message group;
    group.chat.kind = cppgram::PeerKind::Channel;
    group.chat.id = -1001;
    CHECK(!livebot::sender_id_of(group));                                  // group without sender: rejected
    group.sender = cppgram::PeerRef{};
    group.sender->id = 222;
    CHECK(livebot::sender_id_of(group) && *livebot::sender_id_of(group) == 222);
    cppgram::Message own = dm;
    own.outgoing = true;
    CHECK(!livebot::sender_id_of(own));                                    // our own messages are never commands
}

void test_admin_authorization() {
    const std::vector<std::int64_t> admins{111, 222};
    CHECK(livebot::is_admin(admins, 111));
    CHECK(livebot::is_admin(admins, 222));
    CHECK(!livebot::is_admin(admins, 333));
    CHECK(!livebot::is_admin(admins, std::nullopt));
    CHECK(!livebot::is_admin({}, 111));
}

void test_test_names_round_trip() {
    for (livebot::TestId id : livebot::full_suite()) {
        const auto back = livebot::parse_test_name(livebot::test_name(id));
        CHECK(back && *back == id);
    }
    CHECK(!livebot::parse_test_name("unknown"));
    CHECK(livebot::full_suite().size() == 8);
}

void test_redaction() {
    const std::string token = kToken;
    const std::string line = "login with " + token + " done";
    const std::string out = livebot::redact(line, {token});
    CHECK(out.find(token) == std::string::npos);
    CHECK(out.find("[REDACTED]") != std::string::npos);

    // Unregistered bot-token shape is still masked by the long-run rule.
    CHECK(livebot::redact("tok " + token, {}).find(token) == std::string::npos);

    // Long base64 session-like text is masked even when it was never registered.
    const std::string session = "AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==";
    CHECK(livebot::redact("session=" + session, {}).find(session) == std::string::npos);

    // Ordinary words and short numbers are left alone.
    CHECK(livebot::redact("connected to DC 2 in 340 ms", {}) == "connected to DC 2 in 340 ms");

    // Short registered strings are not used for masking (avoids mangling unrelated text).
    CHECK(livebot::redact("abc abc", {"abc"}) == "abc abc");
}

void test_result_formatting() {
    const std::string long_detail(400, 'x');
    const livebot::TestResult r = livebot::make_result(livebot::TestId::Direct, livebot::Status::Fail, "network", long_detail);
    CHECK(r.detail.size() == 303);  // 300 characters plus "..."
    CHECK(livebot::to_string(livebot::Status::Blocked) == "BLOCKED");
    CHECK(livebot::to_string(livebot::Status::NotRun) == "NOT RUN");
    CHECK(livebot::to_string(livebot::Status::Skip) == "SKIP");
}

void test_flood_policy_offline() {
    namespace mt = cppgram::mtproto;
    mt::RetryPolicy policy;
    policy.max_flood_wait = std::chrono::seconds(60);
    const auto five = mt::classify_rpc_error(420, "FLOOD_WAIT_5");
    CHECK(five.kind == mt::ErrorClass::FloodWait && five.wait.count() == 5);
    CHECK(mt::flood_wait_allowed(policy, five));
    const auto big = mt::classify_rpc_error(420, "FLOOD_WAIT_3600");
    CHECK(big.kind == mt::ErrorClass::FloodWait);
    CHECK(!mt::flood_wait_allowed(policy, big));
    CHECK(mt::classify_rpc_error(400, "PEER_ID_INVALID").kind == mt::ErrorClass::Permanent);
}

void test_private_files_and_shred() {
    const std::string dir = "/tmp/livebot-unit-" + std::to_string(::getpid());
    ::mkdir(dir.c_str(), 0700);
    const std::string path = livebot::write_private_file(dir, "probe.txt", "not-a-real-session");
    CHECK(!path.empty());
    struct stat st{};
    CHECK(::stat(path.c_str(), &st) == 0);
    CHECK((st.st_mode & 0777) == 0600);
    livebot::shred_and_remove(path);
    CHECK(::stat(path.c_str(), &st) != 0);  // removed
    ::rmdir(dir.c_str());
}

void test_runner_lock_and_results() {
    livebot::TestRunner runner;
    std::mutex mu;
    std::condition_variable cv;
    bool release = false;
    const bool started = runner.start({livebot::TestId::Direct}, [&](livebot::TestId id) {
        std::unique_lock<std::mutex> lock(mu);
        cv.wait(lock, [&] { return release; });
        return livebot::make_result(id, livebot::Status::Pass, "", "ok");
    });
    CHECK(started);
    CHECK(runner.busy());
    CHECK(!runner.start({livebot::TestId::Login}, [](livebot::TestId id) {
        return livebot::make_result(id, livebot::Status::Pass, "", "should not run");
    }));  // overlapping start is refused
    {
        std::lock_guard<std::mutex> lock(mu);
        release = true;
    }
    cv.notify_all();
    runner.join();
    CHECK(!runner.busy());
    const auto results = runner.results();
    CHECK(results.size() == 1 && results[0].id == livebot::TestId::Direct);
    CHECK(results[0].status == livebot::Status::Pass);
    CHECK(!results[0].started.empty() && !results[0].finished.empty());
}

void test_runner_survives_exceptions() {
    livebot::TestRunner runner;
    const bool started = runner.start({livebot::TestId::Direct, livebot::TestId::Login}, [](livebot::TestId id) -> livebot::TestResult {
        if (id == livebot::TestId::Direct) throw std::runtime_error("boom");
        return livebot::make_result(id, livebot::Status::Pass, "", "second test still ran");
    });
    CHECK(started);
    runner.join();
    const auto results = runner.results();
    CHECK(results.size() == 2);
    CHECK(results[0].status == livebot::Status::Fail && results[0].category == "unexpected");
    CHECK(results[1].status == livebot::Status::Pass);
}

void test_results_never_hold_registered_secrets() {
    const std::string secret = "SuperSecretProxyValue-0123456789";
    livebot::Logger::instance().add_secret(secret);
    livebot::TestRunner runner;
    runner.start({livebot::TestId::Direct}, [&](livebot::TestId id) {
        return livebot::make_result(id, livebot::Status::Fail, "rpc", "server echoed " + secret);
    });
    runner.join();
    const auto results = runner.results();
    CHECK(results.size() == 1);
    CHECK(!results.empty() && results[0].detail.find(secret) == std::string::npos);
    CHECK(!results.empty() && results[0].detail.find("[REDACTED]") != std::string::npos);
    livebot::BotState::instance().record_error("test", "leaked " + secret);
    CHECK(livebot::BotState::instance().last_error().find(secret) == std::string::npos);
}

void test_report_and_json_lines() {
    CHECK(livebot::format_report({}).find("no test has run yet") != std::string::npos);
    livebot::TestResult r = livebot::make_result(livebot::TestId::Login, livebot::Status::Pass, "", "bot id 42");
    r.duration_ms = 250;
    const std::string report = livebot::format_report({r});
    CHECK(report.find("login: PASS 250 ms") != std::string::npos);
    CHECK(report.find("bot id 42") != std::string::npos);

    livebot::TestResult q = livebot::make_result(livebot::TestId::Direct, livebot::Status::Fail, "rpc", "a \"quoted\" \\ back\nnewline");
    const std::string line = livebot::result_json_line(q);
    CHECK(line.size() > 2 && line.back() == '\n');
    CHECK(line.find("\\\"quoted\\\"") != std::string::npos);
    CHECK(line.find("\\n") != std::string::npos);
    CHECK(line.find("\"test\":\"direct\"") != std::string::npos);
}

void test_state() {
    livebot::BotState& s = livebot::BotState::instance();
    s.record_flood(7, "rpc");
    CHECK(!s.floods().empty());
    s.set_connection("connected");
    CHECK(s.connection() == "connected");
    s.record_error("network", "redacted message");
    CHECK(s.last_error().find("redacted message") != std::string::npos);
}

// These run without any network: the live tests must report BLOCKED / SKIP rather than fail or hang.
void test_offline_test_outcomes() {
    livebot::TestEnv env;
    env.cfg.api_id = 12345;
    env.cfg.api_hash = kHash;
    env.cfg.bot_token = kToken;
    env.cfg.admin_ids = {111};
    env.cfg.data_dir = "/tmp/livebot-unit-env-" + std::to_string(::getpid());

    const livebot::TestResult plain = livebot::run_test(livebot::TestId::ProxyPlain, env);
    CHECK(plain.status == livebot::Status::Blocked && plain.category == "not_configured");
    const livebot::TestResult dd = livebot::run_test(livebot::TestId::ProxyDd, env);
    CHECK(dd.status == livebot::Status::Blocked);

    env.cfg.proxy_plain = livebot::ProxyProfile{"proxy.example", 443, kDdSecret};  // wrong kind for profile A
    const livebot::TestResult wrong = livebot::run_test(livebot::TestId::ProxyPlain, env);
    CHECK(wrong.status == livebot::Status::Fail && wrong.category == "config");
    CHECK(wrong.detail.find(kDdSecret) == std::string::npos);

    const livebot::TestResult message = livebot::run_test(livebot::TestId::Message, env);
    CHECK(message.status == livebot::Status::Skip);

    const livebot::TestResult flood = livebot::run_test(livebot::TestId::Flood, env);
    CHECK(flood.status == livebot::Status::Pass);
    CHECK(flood.detail.find("OFFLINE MOCK") != std::string::npos);
    CHECK(flood.detail.find("LIVE FLOODWAIT: NOT OBSERVED") != std::string::npos ||
          flood.detail.find("LIVE FLOODWAIT: observed") != std::string::npos);
    ::rmdir((env.cfg.data_dir + "/tmp").c_str());
    ::rmdir(env.cfg.data_dir.c_str());
}

void test_help_mentions_all_commands() {
    const std::string help = livebot::help_text();
    for (const char* word : {"/start", "/help", "/status", "/health", "/report", "/test direct", "/test flood",
                             "/test proxy-dd"}) {
        CHECK(help.find(word) != std::string::npos);
    }
    CHECK(help.find(kToken) == std::string::npos);
}

}  // namespace

int main() {
    test_config_load_and_validate();
    test_token_and_hash_shapes();
    test_proxy_profiles();
    test_command_parsing();
    test_sender_resolution();
    test_admin_authorization();
    test_test_names_round_trip();
    test_redaction();
    test_result_formatting();
    test_flood_policy_offline();
    test_private_files_and_shred();
    test_runner_lock_and_results();
    test_runner_survives_exceptions();
    test_state();
    test_results_never_hold_registered_secrets();
    test_report_and_json_lines();
    test_help_mentions_all_commands();
    test_offline_test_outcomes();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
