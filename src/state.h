#pragma once
// Process-wide bot state shared by the update loop, the test worker and the watchdog. Every accessor locks.
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace livebot {

struct FloodObservation {
    std::string at;        // ISO-8601 timestamp
    std::int32_t seconds;  // server-given wait, from the error name FLOOD_WAIT_<n>
    std::string method;    // RPC method name only, never its arguments
};

class BotState {
public:
    static BotState& instance();

    void set_identity(std::int64_t id, const std::string& username);
    std::optional<std::int64_t> bot_id() const;
    std::string bot_username() const;

    void set_connection(const std::string& text);  // "connected", "disconnected", "reconnecting", ...
    std::string connection() const;

    void set_current_test(const std::string& name);  // empty when idle
    std::string current_test() const;

    void record_error(const std::string& category, const std::string& message);  // message is already redacted
    std::string last_error() const;

    void record_flood(std::int32_t seconds, const std::string& method);
    std::vector<FloodObservation> floods() const;

    std::chrono::steady_clock::time_point started() const { return started_; }

private:
    BotState();
    const std::chrono::steady_clock::time_point started_;
    mutable std::mutex mu_;
    std::optional<std::int64_t> bot_id_;
    std::string bot_username_;
    std::string connection_ = "starting";
    std::string current_test_;
    std::string last_error_ = "none";
    std::vector<FloodObservation> floods_;
};

}  // namespace livebot
