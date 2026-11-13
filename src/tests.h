#pragma once
// Test implementations. Every test builds its own cppgram::Client (never the update-loop client), uses a
// private session file under <data_dir>/tmp, and returns a TestResult. Nothing here prints a secret.
#include <cstdint>
#include <optional>
#include <string>

#include "config.h"
#include "test_runner.h"

namespace livebot {

struct TestEnv {
    Config cfg;
    std::optional<std::int64_t> bot_id;  // identity verified at startup, when known
};

TestResult run_test(TestId id, const TestEnv& env);

// Writes text to <dir>/<name> with mode 0600 inside a 0700 directory, and returns the path. Empty on failure.
std::string write_private_file(const std::string& dir, const std::string& name, const std::string& text);

// Overwrites a file with zeros, then removes it. Best effort; errors are ignored.
void shred_and_remove(const std::string& path);

// Creates <data_dir>/tmp with mode 0700 and returns its path.
std::string ensure_tmp_dir(const std::string& data_dir);

}  // namespace livebot
