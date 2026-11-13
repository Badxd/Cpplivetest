#include "state.h"

#include "log.h"

namespace livebot {

BotState::BotState() : started_(std::chrono::steady_clock::now()) {}

BotState& BotState::instance() {
    static BotState state;
    return state;
}

void BotState::set_identity(std::int64_t id, const std::string& username) {
    std::lock_guard<std::mutex> lock(mu_);
    bot_id_ = id;
    bot_username_ = username;
}

std::optional<std::int64_t> BotState::bot_id() const {
    std::lock_guard<std::mutex> lock(mu_);
    return bot_id_;
}

std::string BotState::bot_username() const {
    std::lock_guard<std::mutex> lock(mu_);
    return bot_username_;
}

void BotState::set_connection(const std::string& text) {
    std::lock_guard<std::mutex> lock(mu_);
    connection_ = text;
}

std::string BotState::connection() const {
    std::lock_guard<std::mutex> lock(mu_);
    return connection_;
}

void BotState::set_current_test(const std::string& name) {
    std::lock_guard<std::mutex> lock(mu_);
    current_test_ = name;
}

std::string BotState::current_test() const {
    std::lock_guard<std::mutex> lock(mu_);
    return current_test_;
}

void BotState::record_error(const std::string& category, const std::string& message) {
    std::lock_guard<std::mutex> lock(mu_);
    last_error_ = iso_now() + " [" + category + "] " + Logger::instance().redact_text(message);
}

std::string BotState::last_error() const {
    std::lock_guard<std::mutex> lock(mu_);
    return last_error_;
}

void BotState::record_flood(std::int32_t seconds, const std::string& method) {
    std::lock_guard<std::mutex> lock(mu_);
    if (floods_.size() >= 200) floods_.erase(floods_.begin());  // bounded memory
    floods_.push_back(FloodObservation{iso_now(), seconds, method});
}

std::vector<FloodObservation> BotState::floods() const {
    std::lock_guard<std::mutex> lock(mu_);
    return floods_;
}

}  // namespace livebot
