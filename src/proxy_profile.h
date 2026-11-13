#pragma once
#include <string>

#include "config.h"

namespace livebot {

// Checks an MTProxy profile against the secret kind it claims. Returns "" when valid, else a reason
// (never the secret). Profile A must be a plain 16-byte secret; profile B must be a dd (padded) secret.
// Fake-TLS secrets are rejected because CppGram does not support them.
std::string check_proxy_profile(const ProxyProfile& profile, bool expect_padded);

}  // namespace livebot
