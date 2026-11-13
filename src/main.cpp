// cppgram live-test bot: a dedicated MTProto bot that exercises the CppGram library against real Telegram.
// Commands are admin-only. Tests run one at a time, each on its own client. See README.md.
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "cppgram/client.h"
#include "cppgram/errors.h"
#include "commands.h"
#include "config.h"
#include "log.h"
#include "proxy_profile.h"
#include "state.h"
#include "test_runner.h"
#include "tests.h"

namespace {

std::atomic<bool> g_stop{false};

void on_signal(int) {
    g_stop.store(true);  // async-signal-safe: only an atomic store
}

// Appends one JSON line per finished test to <log_dir>/results.jsonl (mode 0600). Contains no secrets:
// details are already redacted and carry no request payloads.
void append_result_line(const std::string& log_dir, const livebot::TestResult& r) {
    const std::string path = log_dir + "/results.jsonl";
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return;
    const std::string s = livebot::result_json_line(r);
    const ssize_t wr = ::write(fd, s.data(), s.size());
    (void)wr;
    ::close(fd);
}

long rss_kb() {
    std::ifstream in("/proc/self/status");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("VmRSS:", 0) == 0) return std::atol(line.c_str() + 6);
    }
    return -1;
}

std::string uptime_text(std::chrono::steady_clock::time_point started) {
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
    return std::to_string(secs / 3600) + "h " + std::to_string((secs / 60) % 60) + "m " + std::to_string(secs % 60) + "s";
}

}  // namespace

int main() {
    umask(077);
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    livebot::Config cfg;
    std::string problem = livebot::load_config(cfg, [](const char* n) { return std::getenv(n); });
    if (problem.empty()) problem = livebot::validate_config(cfg);
    if (problem.empty() && cfg.proxy_plain) problem = livebot::check_proxy_profile(*cfg.proxy_plain, false);
    if (problem.empty() && cfg.proxy_dd) problem = livebot::check_proxy_profile(*cfg.proxy_dd, true);
    if (!problem.empty()) {
        std::fprintf(stderr, "configuration error: %s\n", problem.c_str());
        return 2;
    }

    auto& log = livebot::Logger::instance();
    log.add_secret(cfg.bot_token);
    log.add_secret(cfg.api_hash);
    if (cfg.proxy_plain) log.add_secret(cfg.proxy_plain->secret);
    if (cfg.proxy_dd) log.add_secret(cfg.proxy_dd->secret);
    log.open_file(cfg.log_dir);

    ::mkdir(cfg.data_dir.c_str(), 0700);
    ::chmod(cfg.data_dir.c_str(), 0700);
    const std::string tmp_dir = livebot::ensure_tmp_dir(cfg.data_dir);
    (void)tmp_dir;

    log.log(livebot::Level::Info, "startup", "cppgram live-test bot starting; dc=" + std::to_string(cfg.dc_id) +
                                                 " admins=" + std::to_string(cfg.admin_ids.size()) +
                                                 " proxy_plain=" + (cfg.proxy_plain ? "configured" : "not configured") +
                                                 " proxy_dd=" + (cfg.proxy_dd ? "configured" : "not configured"));

    livebot::BotState& state = livebot::BotState::instance();

    // Declared in dependency order: the client is destroyed first, then the runner (joins its worker), then env.
    livebot::TestEnv env;
    env.cfg = cfg;
    livebot::TestRunner runner;
    std::mutex inline_mu;  // serializes the inline /test message run against worker runs

    cppgram::ClientOptions opts;
    opts.api_id = cfg.api_id;
    opts.api_hash = cfg.api_hash;
    opts.dc_id = cfg.dc_id;
    opts.dc_ip = *livebot::dc_address(cfg.dc_id);
    opts.dc_port = 443;
    opts.session_path = cfg.data_dir + "/bot.session";
    opts.debug = cfg.debug;
    opts.auto_reconnect = true;
    opts.max_reconnects = 5;
    opts.connect_timeout_ms = 10000;
    opts.retry.max_flood_wait = std::chrono::seconds(cfg.flood_max_wait_seconds);

    cppgram::Client client(opts);

    client.on_error([](cppgram::Client&, const std::string& stage, const std::string& message) {
        const std::string category = "stage:" + stage;
        livebot::BotState::instance().record_error(category, message);
        livebot::Logger::instance().log(livebot::Level::Error, "client", category + ": " + message);
    });
    client.on_connection_state([](cppgram::Client&, cppgram::Client::ConnectionEvent e) {
        using E = cppgram::Client::ConnectionEvent;
        const char* text = e == E::Connected ? "connected" : e == E::Disconnected ? "disconnected" : "reconnecting";
        livebot::BotState::instance().set_connection(text);
        livebot::Logger::instance().log(livebot::Level::Info, "connection", text);
    });
    client.on_rpc_error([](cppgram::Client&, std::int32_t code, const std::string& name) {
        if (name.rfind("FLOOD_WAIT_", 0) == 0) {
            const int seconds = std::atoi(name.c_str() + 11);
            livebot::BotState::instance().record_flood(seconds, "rpc");
            livebot::Logger::instance().log(livebot::Level::Warn, "flood",
                                            "observed FLOOD_WAIT seconds=" + std::to_string(seconds) +
                                                " (passive, no request was made for this)");
        }
        (void)code;
    });

    client.on_message(cppgram::filters::incoming(), [&](cppgram::Client& c, const cppgram::Message& m) {
        const std::optional<std::int64_t> sender = m.sender ? std::optional<std::int64_t>(m.sender->id) : std::nullopt;
        const livebot::ParsedCommand cmd = livebot::parse_command(m.text);
        if (cmd.command == livebot::Command::Unknown && (m.text.empty() || m.text[0] != '/')) return;  // not a command
        if (!livebot::is_admin(cfg.admin_ids, sender)) {
            // Silent for non-admins: no reply, no information. Only the attempt is logged, with the id.
            livebot::Logger::instance().log(livebot::Level::Warn, "auth",
                                            "ignored command from non-admin id " + std::to_string(sender.value_or(0)));
            return;
        }
        auto reply = [&](const std::string& text) {
            try {
                c.send_message(m.chat, text);
            } catch (const std::exception& e) {
                livebot::Logger::instance().log(livebot::Level::Error, "reply", std::string("send failed: ") + e.what());
            }
        };

        switch (cmd.command) {
            case livebot::Command::Start:
                reply("cppgram live-test bot\nid " + std::to_string(state.bot_id().value_or(0)) + " @" +
                      state.bot_username() + "\nconnection: " + state.connection() +
                      "\nuptime: " + uptime_text(state.started()) + "\n/help for commands");
                break;
            case livebot::Command::Help:
                reply(livebot::help_text());
                break;
            case livebot::Command::Status: {
                const std::string running = runner.busy() ? runner.running_description() : "idle";
                reply("uptime: " + uptime_text(state.started()) + "\nconnection: " + state.connection() +
                      "\ntest: " + running + "\nlast error: " + state.last_error());
                break;
            }
            case livebot::Command::Health: {
                rusage ru{};
                getrusage(RUSAGE_SELF, &ru);
                double load[3] = {0, 0, 0};
                getloadavg(load, 3);
                reply("uptime: " + uptime_text(state.started()) + "\nconnection: " + state.connection() +
                      "\nrss: " + std::to_string(rss_kb()) + " kB\ncpu user/sys: " +
                      std::to_string(ru.ru_utime.tv_sec) + "s/" + std::to_string(ru.ru_stime.tv_sec) + "s" +
                      "\nload: " + std::to_string(load[0]).substr(0, 4) + " " + std::to_string(load[1]).substr(0, 4) +
                      "\nfloods seen: " + std::to_string(state.floods().size()));
                break;
            }
            case livebot::Command::Report:
                reply(format_report(runner.results()));
                break;
            case livebot::Command::Test:
            case livebot::Command::TestFull: {
                if (cmd.command == livebot::Command::Test && cmd.test == livebot::TestId::Message) {
                    std::unique_lock<std::mutex> lock(inline_mu, std::try_to_lock);
                    if (!lock.owns_lock() || runner.busy()) {
                        reply("busy: another test is running (" + runner.running_description() + ")");
                        break;
                    }
                    const auto t0 = std::chrono::steady_clock::now();
                    const std::string started = livebot::iso_now();
                    livebot::TestResult r;
                    try {
                        const std::string text = "livebot message test " + started;
                        const cppgram::Message sent = c.send_message(m.chat, text);
                        const auto back = c.get_messages(m.chat, {sent.id});
                        if (back.empty() || back.front().text != text) {
                            r = livebot::make_result(livebot::TestId::Message, livebot::Status::Fail, "rpc",
                                                     "sent message id " + std::to_string(sent.id) + " not read back unchanged");
                        } else {
                            r = livebot::make_result(livebot::TestId::Message, livebot::Status::Pass, "",
                                                     "sent message " + std::to_string(sent.id) + " and read it back unchanged");
                        }
                    } catch (const std::exception& e) {
                        r = livebot::make_result(livebot::TestId::Message, livebot::Status::Fail,
                                                 std::string(cppgram::errors::to_string(cppgram::errors::categorize(e))),
                                                 e.what());
                    }
                    r.started = started;
                    r.finished = livebot::iso_now();
                    r.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - t0).count();
                    append_result_line(cfg.log_dir, r);
                    reply("message: " + std::string(livebot::to_string(r.status)) + " " + r.detail);
                    break;
                }
                std::vector<livebot::TestId> ids;
                if (cmd.command == livebot::Command::TestFull) ids = livebot::full_suite();
                else ids.push_back(*cmd.test);
                const std::string names = [&]() {
                    std::string s;
                    for (auto id : ids) s += (s.empty() ? "" : ",") + std::string(livebot::test_name(id));
                    return s;
                }();
                if (runner.busy()) {
                    reply("busy: another test is running (" + runner.running_description() + ")");
                    break;
                }
                const bool started = runner.start(ids, [&](livebot::TestId id) {
                    state.set_current_test(std::string(livebot::test_name(id)));
                    livebot::TestResult r = livebot::run_test(id, env);
                    state.set_current_test("");
                    if (r.status == livebot::Status::Fail) {
                        state.record_error("test:" + std::string(livebot::test_name(id)), r.detail);
                    }
                    append_result_line(cfg.log_dir, r);
                    return r;
                });
                reply(started ? "started: " + names + " (use /report for results, /status while it runs)"
                              : "busy: another test is running");
                break;
            }
            case livebot::Command::Unknown:
                reply("unknown or incomplete command. /help lists the commands");
                break;
        }
    });

    // Handlers are registered before connect(), as the client requires.
    try {
        client.connect();
        client.login_bot(cfg.bot_token);
        const cppgram::User me = client.get_me();
        if (!me.is_bot) throw std::runtime_error("login did not produce a bot account");
        state.set_identity(me.id, me.username);
        env.bot_id = me.id;
        log.log(livebot::Level::Info, "auth", "bot login ok: id " + std::to_string(me.id) + " @" + me.username);
    } catch (const std::exception& e) {
        const std::string category(cppgram::errors::to_string(cppgram::errors::categorize(e)));
        state.record_error("startup:" + category, e.what());
        log.log(livebot::Level::Error, "startup", "login failed (" + category + "): " + e.what());
        return 1;
    }

    // Watcher: turns the signal flag into client.stop(), which is safe from another thread.
    std::thread watcher([&]() {
        while (!g_stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
        log.log(livebot::Level::Info, "shutdown", "signal received: stopping the update loop");
        client.stop();
    });

    // Update loop. run() returns on stop(); an unexpected exception is logged and the loop is restarted a
    // bounded number of times with backoff, so one failure does not silently end the bot.
    int restarts = 0;
    while (!g_stop.load()) {
        try {
            client.run();
            break;  // returned normally: stop() was called
        } catch (const std::exception& e) {
            const std::string category(cppgram::errors::to_string(cppgram::errors::categorize(e)));
            state.record_error("run:" + category, e.what());
            log.log(livebot::Level::Error, "run", "update loop stopped (" + category + "): " + e.what());
            if (++restarts > 5) {
                log.log(livebot::Level::Error, "run", "restart budget spent: exiting");
                g_stop.store(true);
                break;
            }
            for (int i = 0; i < 10 * restarts && !g_stop.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            try {
                client.connect();
                state.set_connection("reconnected");
            } catch (const std::exception& e2) {
                log.log(livebot::Level::Warn, "run", std::string("reconnect attempt failed: ") + e2.what());
            }
        }
    }

    g_stop.store(true);
    watcher.join();
    if (runner.busy()) log.log(livebot::Level::Info, "shutdown", "waiting for the running test to finish: " + runner.running_description());
    runner.join();
    try {
        client.disconnect();
    } catch (const std::exception& e) {
        log.log(livebot::Level::Warn, "shutdown", std::string("disconnect: ") + e.what());
    }
    log.log(livebot::Level::Info, "shutdown", "stopped cleanly");
    return 0;
}
