#include "core.hpp"
#include <algorithm>

namespace njupt {
const char* state_name(State s) {
    switch (s) {
    case State::waiting_link: return "waiting_link";
    case State::online: return "online";
    case State::suspect: return "suspect";
    case State::authenticating: return "authenticating";
    case State::backoff: return "backoff";
    case State::account_rejected: return "account_rejected";
    case State::stopped: return "stopped";
    }
    return "unknown";
}
Decision Controller::observed(bool link, bool online) {
    if (!link) {
        confirmed_ = false; failures_ = 0; retry_ = c_.retry_initial;
        return {false, 5, State::waiting_link};
    }
    if (online) {
        confirmed_ = true; failures_ = 0; retry_ = c_.retry_initial;
        return {false, c_.interval, State::online};
    }
    if (failures_ < c_.failure_threshold) ++failures_;
    if (confirmed_ && failures_ < c_.failure_threshold)
        return {false, c_.interval, State::suspect};
    return {true, 0, State::authenticating};
}
Decision Controller::authenticated(bool online, bool rejected_for_billing) {
    if (online) return observed(true, true);
    if (rejected_for_billing) return {false, c_.rejected_retry, State::account_rejected};
    const unsigned delay = retry_;
    retry_ = std::min(c_.retry_max, retry_ > c_.retry_max / 2 ? c_.retry_max : retry_ * 2);
    return {false, delay, State::backoff};
}
} // namespace njupt
