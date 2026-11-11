#pragma once
#include <cstdint>
#include <functional>
#include <istream>
#include <string>
#include <vector>

namespace njupt {
struct Probe {
    std::string url;
    std::string ip; // Optional fixed address; prevents Fake-IP / pre-auth DNS dependency.
    long status = 200;
    std::string body;
};
struct Config {
    std::string username, password;
    std::string portal_ip = "10.10.244.11";
    unsigned page_port = 80, login_port = 801;
    std::string page_path = "/a79.htm";
    std::string device, client_ip;
    std::uint32_t mark = 0x4e4a;
    unsigned interval = 15, failure_threshold = 3;
    unsigned retry_initial = 10, retry_max = 300, settle = 2;
    unsigned rejected_retry = 900;
    unsigned timeout = 10, connect_timeout = 5;
    std::vector<Probe> probes;
};
bool ipv4(const std::string& s);
Config read_config(std::istream& input);
void validate(const Config& c, bool require_network = true);
std::string encode(const std::string& bytes);
std::string portal_mac(const std::string& html);
std::string login_url(const Config& c, const std::string& mac);
std::string login_body(const Config& c);
bool login_ack(const std::string& body);
bool billing_rejected(const std::string& body);
std::string rejection_code(const std::string& body);
bool probe_matches(const Probe& p, long status, const std::string& body);

enum class State { waiting_link, online, suspect, authenticating, backoff, account_rejected, stopped };
const char* state_name(State s);
struct Decision { bool login = false; unsigned delay = 0; State state = State::waiting_link; };
// Time-independent transition logic. Clock and I/O are injected by the caller.
class Controller {
public:
    explicit Controller(const Config& c) : c_(c), retry_(c.retry_initial) {}
    Decision observed(bool link, bool online);
    Decision authenticated(bool online, bool rejected_for_billing = false);
    unsigned failures() const { return failures_; }
private:
    const Config& c_;
    unsigned failures_ = 0, retry_;
    bool confirmed_ = false;
};
} // namespace njupt
