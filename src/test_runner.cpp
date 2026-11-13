#include "test_runner.h"

#include <algorithm>
#include <chrono>

#include "log.h"

namespace livebot {

std::string_view to_string(Status status) {
    switch (status) {
        case Status::Pass: return "PASS";
        case Status::Fail: return "FAIL";
        case Status::Blocked: return "BLOCKED";
        case Status::Skip: return "SKIP";
        case Status::NotRun: return "NOT RUN";
    }
    return "?";
}

std::string_view test_name(TestId id) {
    switch (id) {
        case TestId::Direct: return "direct";
        case TestId::Login: return "login";
        case TestId::Message: return "message";
        case TestId::Session: return "session";
        case TestId::Reconnect: return "reconnect";
        case TestId::Flood: return "flood";
        case TestId::ProxyPlain: return "proxy-plain";
        case TestId::ProxyDd: return "proxy-dd";
    }
    return "?";
}

std::optional<TestId> parse_test_name(std::string_view s) {
    for (TestId id : full_suite()) {
        if (test_name(id) == s) return id;
    }
    return std::nullopt;
}

std::vector<TestId> full_suite() {
    return {TestId::Direct, TestId::Login, TestId::Session, TestId::Reconnect, TestId::Flood,
            TestId::Message, TestId::ProxyPlain, TestId::ProxyDd};
}

TestResult make_result(TestId id, Status status, std::string category, std::string detail) {
    TestResult r;
    r.id = id;
    r.status = status;
    r.category = std::move(category);
    if (detail.size() > 300) detail = detail.substr(0, 300) + "...";
    r.detail = std::move(detail);
    return r;
}

TestRunner::~TestRunner() {
    join();
}

bool TestRunner::start(std::vector<TestId> ids, Body body) {
    if (busy()) return false;
    // The previous worker has cleared busy_ and is only returning now, so this join does not wait on a test.
    join();
    std::lock_guard<std::mutex> lock(mu_);
    if (busy_) return false;  // another start() won the race between the check and the lock
    busy_ = true;
    std::string names;
    for (TestId id : ids) {
        if (!names.empty()) names += ",";
        names += std::string(test_name(id));
    }
    running_ = names;
    worker_ = std::thread([this, ids = std::move(ids), body = std::move(body)]() {
        for (TestId id : ids) {
            const std::string started = iso_now();
            const auto t0 = std::chrono::steady_clock::now();
            TestResult r;
            try {
                r = body(id);
            } catch (const std::exception& e) {
                // Exceptions never escape the worker: they become a FAIL with the message, and the next test runs.
                r = make_result(id, Status::Fail, "unexpected", e.what());
            } catch (...) {
                r = make_result(id, Status::Fail, "unexpected", "non-standard exception");
            }
            r.id = id;
            r.started = started;
            r.finished = iso_now();
            r.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - t0).count();
            record(r);
            Logger::instance().log(Level::Info, "test",
                                   std::string(test_name(id)) + " " + std::string(to_string(r.status)) +
                                       " in " + std::to_string(r.duration_ms) + " ms" +
                                       (r.category.empty() ? "" : " category=" + r.category));
        }
        std::lock_guard<std::mutex> done(mu_);
        busy_ = false;
        running_.clear();
    });
    return true;
}

bool TestRunner::busy() const {
    std::lock_guard<std::mutex> lock(mu_);
    return busy_;
}

std::string TestRunner::running_description() const {
    std::lock_guard<std::mutex> lock(mu_);
    return busy_ ? running_ : std::string();
}

std::vector<TestResult> TestRunner::results() const {
    std::lock_guard<std::mutex> lock(mu_);
    return results_;
}

void TestRunner::join() {
    std::thread t;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!worker_.joinable()) return;
        if (worker_.get_id() == std::this_thread::get_id()) return;
        t = std::move(worker_);
    }
    t.join();
}

void TestRunner::record(const TestResult& in) {
    TestResult r = in;
    r.detail = Logger::instance().redact_text(r.detail);  // shown in chat via /report, so redact here
    std::lock_guard<std::mutex> lock(mu_);
    auto it = std::find_if(results_.begin(), results_.end(), [&](const TestResult& x) { return x.id == r.id; });
    if (it != results_.end()) {
        *it = r;
    } else {
        results_.push_back(r);
    }
}

}  // namespace livebot
