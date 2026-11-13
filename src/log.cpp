#include "log.h"

#include <sys/stat.h>

#include <cctype>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>

namespace livebot {
namespace {

bool is_token_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '+' || c == '/' || c == '=';
}

// Masks runs of 32+ token characters (session strings, hashes, keys) and "<digits>:<token>" bot tokens.
std::string mask_long_runs(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    std::size_t i = 0;
    while (i < in.size()) {
        if (is_token_char(in[i])) {
            std::size_t j = i;
            while (j < in.size() && is_token_char(in[j])) ++j;
            const std::size_t len = j - i;
            if (len >= 32) {
                out += "[REDACTED]";
            } else {
                out.append(in, i, len);
            }
            i = j;
        } else {
            out.push_back(in[i]);
            ++i;
        }
    }
    return out;
}

}  // namespace

std::string redact(const std::string& text, const std::vector<std::string>& secrets) {
    std::string out = text;
    for (const std::string& s : secrets) {
        if (s.size() < 4) continue;  // too short to be a meaningful secret; avoids masking ordinary words
        std::size_t pos = 0;
        while ((pos = out.find(s, pos)) != std::string::npos) {
            out.replace(pos, s.size(), "[REDACTED]");
            pos += 10;
        }
    }
    return mask_long_runs(out);
}

std::string iso_now() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

const char* level_name(Level level) {
    switch (level) {
        case Level::Debug: return "DEBUG";
        case Level::Info: return "INFO";
        case Level::Warn: return "WARN";
        case Level::Error: return "ERROR";
    }
    return "?";
}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::open_file(const std::string& log_dir) {
    std::lock_guard<std::mutex> lock(mu_);
    path_ = log_dir + "/livebot.log";
    mkdir(log_dir.c_str(), 0700);
}

void Logger::add_secret(const std::string& secret) {
    std::lock_guard<std::mutex> lock(mu_);
    if (secret.size() >= 4) secrets_.push_back(secret);
}

std::string Logger::redact_text(const std::string& text) {
    std::lock_guard<std::mutex> lock(mu_);
    return redact(text, secrets_);
}

void Logger::log(Level level, const std::string& component, const std::string& message) {
    std::lock_guard<std::mutex> lock(mu_);
    const std::string line = iso_now() + " " + level_name(level) + " [" + component + "] " +
                             redact(message, secrets_) + "\n";
    std::fwrite(line.data(), 1, line.size(), stderr);
    if (!path_.empty()) {
        const int fd = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (fd >= 0) {
            const ssize_t wr = ::write(fd, line.data(), line.size());
            (void)wr;
            ::close(fd);
        }
    }
}

}  // namespace livebot
