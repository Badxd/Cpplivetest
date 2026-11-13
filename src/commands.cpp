#include "commands.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace livebot {
namespace {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

}  // namespace

ParsedCommand parse_command(const std::string& text) {
    ParsedCommand out;
    const std::string t = trim(text);
    if (t.empty() || t[0] != '/') return out;

    const auto space = t.find_first_of(" \t\r\n");
    std::string head = lower(t.substr(1, space == std::string::npos ? std::string::npos : space - 1));
    const std::string rest = space == std::string::npos ? std::string() : trim(t.substr(space));
    out.raw_argument = rest;

    // Strip "@botname" so "/status@my_bot" works in group chats too.
    const auto at = head.find('@');
    if (at != std::string::npos) head = head.substr(0, at);

    if (head == "start") out.command = Command::Start;
    else if (head == "help") out.command = Command::Help;
    else if (head == "status") out.command = Command::Status;
    else if (head == "report") out.command = Command::Report;
    else if (head == "health") out.command = Command::Health;
    else if (head == "test") {
        const std::string arg = lower(rest);
        if (arg.empty()) {
            out.command = Command::Unknown;
        } else if (arg == "all" || arg == "full") {
            out.command = Command::TestFull;
        } else if (auto id = parse_test_name(arg)) {
            out.command = Command::Test;
            out.test = id;
        } else {
            out.command = Command::Unknown;
        }
    }
    return out;
}

bool is_admin(const std::vector<std::int64_t>& admins, std::optional<std::int64_t> sender) {
    if (!sender) return false;
    return std::find(admins.begin(), admins.end(), *sender) != admins.end();
}

std::string help_text() {
    return "cppgram live-test bot (admin only)\n"
           "/start  identity and startup status\n"
           "/help  this list\n"
           "/status  uptime, connection, running test, last error\n"
           "/health  process and connection health\n"
           "/report  latest result per test\n"
           "tests (one at a time):\n"
           "/test direct  TCP connect and handshake\n"
           "/test login  bot login and get_me\n"
           "/test session  export, restore and verify a session string\n"
           "/test reconnect  controlled disconnect and reconnect\n"
           "/test flood  FloodWait handling (offline mock; live is reported separately)\n"
           "/test message  send a controlled message and read it back\n"
           "/test proxy-plain  MTProxy profile A (16-byte secret)\n"
           "/test proxy-dd  MTProxy profile B (dd secret)\n"
           "/test all  run the full suite in order\n";
}

std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    return out;
}

std::string result_json_line(const TestResult& r) {
    std::ostringstream line;
    line << "{\"test\":\"" << test_name(r.id) << "\",\"status\":\"" << to_string(r.status)
         << "\",\"category\":\"" << json_escape(r.category) << "\",\"started\":\"" << r.started
         << "\",\"finished\":\"" << r.finished << "\",\"duration_ms\":" << r.duration_ms
         << ",\"detail\":\"" << json_escape(r.detail) << "\"}\n";
    return line.str();
}

std::string format_report(const std::vector<TestResult>& results) {
    if (results.empty()) return "no test has run yet. try /test direct";
    std::string out = "latest results:\n";
    for (const auto& r : results) {
        out += std::string(test_name(r.id)) + ": " + std::string(to_string(r.status));
        if (!r.category.empty()) out += " (" + r.category + ")";
        out += " " + std::to_string(r.duration_ms) + " ms\n  " + r.detail + "\n";
    }
    return out;
}

}  // namespace livebot
