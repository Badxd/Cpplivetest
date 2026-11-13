#pragma once
// Bot configuration, read from environment variables (systemd EnvironmentFile in production).
// Secrets live only in memory; nothing here prints them. See deploy/env.example.
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace livebot {

struct ProxyProfile {
    std::string host;
    std::uint16_t port = 0;
    std::string secret;  // hex, base64url or base64 text, as accepted by cppgram::mtproto::parse_proxy_secret
};

struct Config {
    std::int32_t api_id = 0;
    std::string api_hash;
    std::string bot_token;
    std::vector<std::int64_t> admin_ids;
    std::int32_t dc_id = 2;
    std::string data_dir = "/var/lib/cppgram-livebot";
    std::string log_dir = "/var/log/cppgram-livebot";
    std::optional<ProxyProfile> proxy_plain;  // profile A: 16-byte secret, intermediate
    std::optional<ProxyProfile> proxy_dd;     // profile B: 17-byte secret with dd prefix, padded intermediate
    int flood_max_wait_seconds = 60;          // longest FLOOD_WAIT the library may sleep through
    bool debug = false;                       // library stage output to stderr (no secrets)
};

// Reads the variables through `getenv_fn`. Returns an empty string on success, otherwise a message that
// names the variable (never its value).
std::string load_config(Config& out, const std::function<const char*(const char*)>& getenv_fn);

// Returns an empty string when the configuration is usable, otherwise the first problem found.
std::string validate_config(const Config& cfg);

bool is_valid_bot_token(const std::string& token);  // "<digits>:<35+ url-safe chars>"
bool is_valid_api_hash(const std::string& hash);    // 32 hex characters

// Telegram production DC address for a DC id 1..5, from the CppGram client defaults.
std::optional<std::string> dc_address(std::int32_t dc_id);

}  // namespace livebot
