#pragma once
// Command parsing and administrator authorization. Pure functions, unit-tested offline.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cppgram/types.h"
#include "test_runner.h"

namespace livebot {

enum class Command { Start, Help, Status, Test, TestFull, Report, Health, Unknown };

struct ParsedCommand {
    Command command = Command::Unknown;
    std::optional<TestId> test;  // set for "/test <name>"
    std::string raw_argument;    // the text after the command, trimmed (for error messages)
};

// Accepts "/cmd", "/cmd args", and "/cmd@botname args". Command names are case-insensitive.
// "/test" with no argument, or an unknown test name, yields Command::Unknown with a usable raw_argument.
ParsedCommand parse_command(const std::string& text);

// Sender id for an incoming message. Telegram omits from_id in private chats, so for a user-to-bot chat the
// chat peer is the sender. Outgoing messages and messages without a sender yield nullopt.
std::optional<std::int64_t> sender_id_of(const cppgram::Message& m);

// True only when the sender id is in the configured admin list. An absent sender is never an admin.
bool is_admin(const std::vector<std::int64_t>& admins, std::optional<std::int64_t> sender);

std::string help_text();

// Human-readable latest results, one block per test. Empty input gives a hint to run a test.
std::string format_report(const std::vector<TestResult>& results);

// One JSON object per line for results.jsonl. Escapes quotes, backslashes and control characters.
std::string result_json_line(const TestResult& r);

std::string json_escape(const std::string& s);

}  // namespace livebot
