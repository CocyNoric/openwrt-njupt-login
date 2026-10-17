#include "core.hpp"
#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <stdexcept>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace njupt {
namespace {
std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}
unsigned number(const std::string& s, const std::string& key, unsigned lo, unsigned hi) {
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("invalid integer: " + key);
    unsigned long long n = 0;
    for (char ch : s) {
        n = n * 10 + static_cast<unsigned>(ch - '0');
        if (n > hi) throw std::runtime_error("out of range: " + key);
    }
    if (n < lo) throw std::runtime_error("out of range: " + key);
    return static_cast<unsigned>(n);
}
bool controls(const std::string& s) {
    return std::any_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
}
bool ipv4(const std::string& s) {
    in_addr address{};
    return inet_pton(AF_INET, s.c_str(), &address) == 1;
}
Config read_config(std::istream& in) {
    std::map<std::string, std::string> m;
    std::string line;
    std::size_t total = 0;
    while (std::getline(in, line)) {
        total += line.size();
        if (total > 32768) throw std::runtime_error("config too large");
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (trim(line).empty() || trim(line).front() == '#') continue;
        const auto equal = line.find('=');
        if (equal == std::string::npos) throw std::runtime_error("config line needs key=value");
        const auto key = trim(line.substr(0, equal));
        // Values are literal: preserve password whitespace, #, =, quotes and non-ASCII bytes.
        const auto value = line.substr(equal + 1);
        if (controls(value)) throw std::runtime_error("control character in config: " + key);
        if (!m.emplace(key, value).second) throw std::runtime_error("duplicate config key: " + key);
    }
    if (in.bad()) throw std::runtime_error("cannot read config");
    Config c;
    auto str = [&](const char* key, std::string& target) {
        auto i = m.find(key);
        if (i != m.end()) { target = i->second; m.erase(i); }
    };
    auto num = [&](const char* key, unsigned& target, unsigned lo, unsigned hi) {
        auto i = m.find(key);
        if (i != m.end()) { target = number(i->second, key, lo, hi); m.erase(i); }
    };
    str("username", c.username); str("password", c.password);
    str("portal_ip", c.portal_ip); str("page_path", c.page_path);
    str("interface", c.device); str("client_ip", c.client_ip);
    num("page_port", c.page_port, 1, 65535); num("login_port", c.login_port, 1, 65535);
    num("interval", c.interval, 1, 86400); num("failure_threshold", c.failure_threshold, 1, 20);
    num("retry_initial", c.retry_initial, 1, 3600); num("retry_max", c.retry_max, 1, 86400);
    num("rejected_retry", c.rejected_retry, 60, 86400);
    num("settle", c.settle, 0, 60); num("timeout", c.timeout, 1, 300);
    num("connect_timeout", c.connect_timeout, 1, 300);
    unsigned mark = c.mark;
    num("mark", mark, 0, std::numeric_limits<std::uint32_t>::max()); c.mark = mark;
    Probe p{"http://archive.ubuntu.com/ubuntu/", "91.189.92.22", 200, "Index of /ubuntu"};
    // A URL override must not inherit the default endpoint's pinned address.
    if (m.count("probe_url")) p.ip.clear();
    str("probe_url", p.url); str("probe_ip", p.ip); str("probe_body", p.body);
    unsigned code = 200; num("probe_status", code, 200, 299); p.status = code;
    c.probes.push_back(p);
    Probe p2;
    str("probe2_url", p2.url); str("probe2_ip", p2.ip); str("probe2_body", p2.body);
    code = 204; num("probe2_status", code, 200, 299); p2.status = code;
    if (!p2.url.empty()) c.probes.push_back(p2);
    else if (!p2.ip.empty() || !p2.body.empty()) throw std::runtime_error("probe2_url required");
    if (!m.empty()) throw std::runtime_error("unknown config key: " + m.begin()->first);
    validate(c, false);
    return c;
}
void validate(const Config& c, bool require_network) {
    if (c.username.empty() || c.password.empty()) throw std::runtime_error("username/password required");
    if (c.username.size() > 512 || c.password.size() > 512)
        throw std::runtime_error("credential exceeds 512 bytes");
    if (!ipv4(c.portal_ip)) throw std::runtime_error("portal_ip must be an IPv4 literal");
    if (!c.client_ip.empty() && !ipv4(c.client_ip)) throw std::runtime_error("invalid client_ip");
    if (require_network && c.client_ip.empty()) throw std::runtime_error("client_ip required");
#ifndef _WIN32
    if (require_network && c.device.empty()) throw std::runtime_error("interface required on Linux");
#endif
    if (c.device.size() > 15 || c.device.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-") != std::string::npos)
        throw std::runtime_error("invalid interface name");
    if (c.page_path.empty() || c.page_path.front() != '/' || controls(c.page_path) ||
        c.page_path.find_first_of("?# ") != std::string::npos)
        throw std::runtime_error("invalid page_path");
    if (c.retry_initial > c.retry_max || c.connect_timeout > c.timeout)
        throw std::runtime_error("invalid retry/timeout ordering");
    for (const auto& p : c.probes) {
        if (p.url.size() > 2048 || controls(p.url) ||
            (p.url.rfind("http://", 0) != 0 && p.url.rfind("https://", 0) != 0))
            throw std::runtime_error("probe URL must use HTTP(S)");
        if (!p.ip.empty() && !ipv4(p.ip)) throw std::runtime_error("probe_ip must be IPv4");
        if (p.status != 204 && p.body.empty()) throw std::runtime_error("non-204 probe needs probe_body");
    }
}
} // namespace njupt
