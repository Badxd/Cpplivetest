#pragma once
// Structured, timestamped logging with redaction. Every line passes through redact() before it is written,
// so a registered secret or a token-shaped / session-shaped string never reaches stderr or the log file.
#include <mutex>
#include <string>
#include <vector>

namespace livebot {

enum class Level { Debug, Info, Warn, Error };

// Replaces every registered secret, bot-token shaped text and long base64/hex run with a marker.
std::string redact(const std::string& text, const std::vector<std::string>& secrets);

// ISO-8601 UTC timestamp, e.g. 2026-11-13T05:38:01Z.
std::string iso_now();

class Logger {
public:
    static Logger& instance();

    // Opens <log_dir>/livebot.log in append mode with 0600 permissions. Failure falls back to stderr only.
    void open_file(const std::string& log_dir);
    void add_secret(const std::string& secret);  // registered secrets are redacted from every line
    void log(Level level, const std::string& component, const std::string& message);
    // Redacts text with the registered secrets. Used for anything shown in chat or stored in state.
    std::string redact_text(const std::string& text);

private:
    Logger() = default;
    std::mutex mu_;
    std::string path_;
    std::vector<std::string> secrets_;
};

const char* level_name(Level level);

}  // namespace livebot
