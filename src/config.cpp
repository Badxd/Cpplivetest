#include "config.h"

#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace livebot {
namespace {

std::string env_text(const std::function<const char*(const char*)>& get, const char* name) {
    const char* v = get(name);
    return v ? std::string(v) : std::string();
}

bool parse_int64(const std::string& text, std::int64_t& out) {
    if (text.empty()) return false;
    std::size_t i = 0;
    if (text[0] == '-') i = 1;
    if (i >= text.size()) return false;
    for (std::size_t j = i; j < text.size(); ++j) {
        if (!std::isdigit(static_cast<unsigned char>(text[j]))) return false;
    }
    try {
        out = std::stoll(text);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

std::string trim(std::string s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Reads TG_PROXY_<PREFIX>_HOST / _PORT / _SECRET. Returns nullopt when none of the three is set.
// Returns a sentinel-free error string through `error` when only some are set.
std::optional<ProxyProfile> read_profile(const std::function<const char*(const char*)>& get,
                                         const std::string& prefix, std::string& error) {
    const std::string host = trim(env_text(get, ("TG_PROXY_" + prefix + "_HOST").c_str()));
    const std::string port = trim(env_text(get, ("TG_PROXY_" + prefix + "_PORT").c_str()));
    const std::string secret = trim(env_text(get, ("TG_PROXY_" + prefix + "_SECRET").c_str()));
    if (host.empty() && port.empty() && secret.empty()) return std::nullopt;
    if (host.empty() || port.empty() || secret.empty()) {
        error = "TG_PROXY_" + prefix + "_HOST, _PORT and _SECRET must all be set together";
        return std::nullopt;
    }
    std::int64_t p = 0;
    if (!parse_int64(port, p) || p < 1 || p > 65535) {
        error = "TG_PROXY_" + prefix + "_PORT must be 1..65535";
        return std::nullopt;
    }
    ProxyProfile profile;
    profile.host = host;
    profile.port = static_cast<std::uint16_t>(p);
    profile.secret = secret;
    return profile;
}

}  // namespace

bool is_valid_bot_token(const std::string& token) {
    const auto colon = token.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= token.size()) return false;
    for (std::size_t i = 0; i < colon; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(token[i]))) return false;
    }
    const std::string rest = token.substr(colon + 1);
    if (rest.size() < 30) return false;
    for (char c : rest) {
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

bool is_valid_api_hash(const std::string& hash) {
    if (hash.size() != 32) return false;
    for (char c : hash) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

std::optional<std::string> dc_address(std::int32_t dc_id) {
    switch (dc_id) {
        case 1: return "149.154.175.53";
        case 2: return "149.154.167.51";
        case 3: return "149.154.175.100";
        case 4: return "149.154.167.91";
        case 5: return "91.108.56.130";
        default: return std::nullopt;
    }
}

std::string load_config(Config& out, const std::function<const char*(const char*)>& get) {
    const std::string api_id = trim(env_text(get, "TG_API_ID"));
    std::int64_t id = 0;
    if (api_id.empty()) return "TG_API_ID is not set";
    if (!parse_int64(api_id, id) || id <= 0 || id > 2147483647) return "TG_API_ID must be a positive 32-bit integer";
    out.api_id = static_cast<std::int32_t>(id);

    out.api_hash = trim(env_text(get, "TG_API_HASH"));
    out.bot_token = trim(env_text(get, "TG_BOT_TOKEN"));

    const std::string admins = trim(env_text(get, "TG_ADMIN_IDS"));
    if (admins.empty()) return "TG_ADMIN_IDS is not set";
    out.admin_ids.clear();
    std::size_t start = 0;
    while (start <= admins.size()) {
        const std::size_t comma = admins.find(',', start);
        const std::string item = trim(admins.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (!item.empty()) {
            std::int64_t v = 0;
            if (!parse_int64(item, v) || v <= 0) return "TG_ADMIN_IDS must be a comma list of positive user ids";
            out.admin_ids.push_back(v);
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }

    const std::string dc = trim(env_text(get, "TG_DC"));
    if (!dc.empty()) {
        std::int64_t v = 0;
        if (!parse_int64(dc, v)) return "TG_DC must be a number from 1 to 5";
        out.dc_id = static_cast<std::int32_t>(v);
    }

    const std::string data = trim(env_text(get, "TG_DATA_DIR"));
    if (!data.empty()) out.data_dir = data;
    const std::string logs = trim(env_text(get, "TG_LOG_DIR"));
    if (!logs.empty()) out.log_dir = logs;

    const std::string flood = trim(env_text(get, "TG_FLOOD_MAX_WAIT_SECONDS"));
    if (!flood.empty()) {
        std::int64_t v = 0;
        if (!parse_int64(flood, v) || v < 1 || v > 900) return "TG_FLOOD_MAX_WAIT_SECONDS must be 1..900";
        out.flood_max_wait_seconds = static_cast<int>(v);
    }
    out.debug = trim(env_text(get, "TG_DEBUG")) == "1";

    std::string error;
    out.proxy_plain = read_profile(get, "PLAIN", error);
    if (!error.empty()) return error;
    out.proxy_dd = read_profile(get, "DD", error);
    if (!error.empty()) return error;
    return "";
}

std::string validate_config(const Config& cfg) {
    if (cfg.api_id <= 0) return "api_id must be positive";
    if (!is_valid_api_hash(cfg.api_hash)) return "TG_API_HASH must be 32 hex characters";
    if (!is_valid_bot_token(cfg.bot_token)) return "TG_BOT_TOKEN has the wrong shape";
    if (cfg.admin_ids.empty()) return "TG_ADMIN_IDS needs at least one id";
    if (!dc_address(cfg.dc_id)) return "TG_DC must be a number from 1 to 5";
    if (cfg.data_dir.empty() || cfg.data_dir[0] != '/') return "TG_DATA_DIR must be an absolute path";
    if (cfg.log_dir.empty() || cfg.log_dir[0] != '/') return "TG_LOG_DIR must be an absolute path";
    if (cfg.flood_max_wait_seconds < 1 || cfg.flood_max_wait_seconds > 900) return "flood wait limit out of range";
    return "";
}

}  // namespace livebot
