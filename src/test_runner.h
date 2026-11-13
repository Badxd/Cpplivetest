#pragma once
// Sequential test runner. One test (or the full suite) runs at a time on a worker thread, so the update loop
// keeps answering commands. Each test gets its own cppgram::Client; the update-loop client is never shared.
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace livebot {

enum class TestId { Direct, Login, Message, Session, Reconnect, Flood, ProxyPlain, ProxyDd };

enum class Status { Pass, Fail, Blocked, Skip, NotRun };

std::string_view to_string(Status status);
std::string_view test_name(TestId id);                      // "direct", "login", ... (command argument form)
std::optional<TestId> parse_test_name(std::string_view s);  // inverse of test_name
std::vector<TestId> full_suite();                           // every test in run order

struct TestResult {
    TestId id = TestId::Direct;
    Status status = Status::NotRun;
    std::string category;  // error category or "" on PASS
    std::string detail;    // safe, redacted, length-limited
    std::string started;   // ISO-8601 UTC
    std::string finished;
    long long duration_ms = 0;
};

class TestRunner {
public:
    using Body = std::function<TestResult(TestId)>;

    TestRunner() = default;
    ~TestRunner();
    TestRunner(const TestRunner&) = delete;
    TestRunner& operator=(const TestRunner&) = delete;

    // Starts `ids` in order on a worker thread. Returns false (and changes nothing) when a run is already active.
    bool start(std::vector<TestId> ids, Body body);
    bool busy() const;
    std::string running_description() const;  // "" when idle
    std::vector<TestResult> results() const;  // newest result per test, in the order first run
    void join();                              // waits for the worker to finish

private:
    void record(const TestResult& r);

    mutable std::mutex mu_;
    bool busy_ = false;
    std::string running_;
    std::thread worker_;
    std::vector<TestResult> results_;
};

// Builds a result. detail is redacted and truncated to 300 characters.
TestResult make_result(TestId id, Status status, std::string category, std::string detail);

}  // namespace livebot
