#pragma once
#include "core.hpp"
#include <csignal>
#include <curl/curl.h>

namespace njupt {
extern volatile std::sig_atomic_t stopping;
struct Response { CURLcode code = CURLE_OK; long status = 0; std::string body; bool bound = false; };
enum class LoginResult { acknowledged, unconfirmed, billing_rejected };
class Http {
public:
    explicit Http(const Config& config);
    ~Http();
    Http(const Http&) = delete;
    Http& operator=(const Http&) = delete;
    Response request(const std::string& url, const std::string* post = nullptr, const Probe* probe = nullptr);
    bool online();
    LoginResult login();
private:
    const Config& c_;
    CURL* handle_ = nullptr;
};
} // namespace njupt
