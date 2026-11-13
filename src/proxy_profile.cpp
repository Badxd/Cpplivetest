#include "proxy_profile.h"

#include <stdexcept>

#include "cppgram/mtproto/proxy_secret.h"

namespace livebot {

std::string check_proxy_profile(const ProxyProfile& profile, bool expect_padded) {
    if (profile.host.empty()) return "host is empty";
    if (profile.port == 0) return "port must be 1..65535";
    cppgram::mtproto::ProxySecret parsed;
    try {
        parsed = cppgram::mtproto::parse_proxy_secret(profile.secret);
    } catch (const std::invalid_argument&) {
        return "secret is not a valid MTProxy secret (hex, base64url or base64 form expected)";
    }
    switch (parsed.kind) {
        case cppgram::mtproto::ProxySecretKind::FakeTls:
            return "fake-TLS (ee) secrets are not supported by CppGram";
        case cppgram::mtproto::ProxySecretKind::PaddedIntermediate:
            if (!expect_padded) return "this profile is plain: use a 16-byte secret, not a dd secret";
            return "";
        case cppgram::mtproto::ProxySecretKind::Plain:
            if (expect_padded) return "this profile is dd: use a dd (17-byte, dd-prefixed) secret";
            return "";
    }
    return "unknown secret kind";
}

}  // namespace livebot
