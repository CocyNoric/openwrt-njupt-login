#include "core.hpp"
#include <cctype>
#include <regex>
#include <stdexcept>

namespace njupt {
std::string encode(const std::string& bytes) {
    const char* digits = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 3);
    for (unsigned char c : bytes) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            out += static_cast<char>(c);
        else { out += '%'; out += digits[c >> 4]; out += digits[c & 15]; }
    }
    return out;
}
std::string portal_mac(const std::string& html) {
    std::size_t pos = 0;
    while ((pos = html.find("ss1", pos)) != std::string::npos) {
        auto i = pos + 3;
        const auto id = [](unsigned char c) { return std::isalnum(c) || c == '_'; };
        if ((pos && id(static_cast<unsigned char>(html[pos - 1]))) ||
            (i < html.size() && id(static_cast<unsigned char>(html[i])))) { pos = i; continue; }
        auto space = [&]() { while (i < html.size() && std::isspace(static_cast<unsigned char>(html[i]))) ++i; };
        space();
        if (i >= html.size() || html[i++] != '=') { pos = i; continue; }
        space();
        if (i >= html.size() || (html[i] != '\'' && html[i] != '"')) { pos = i; continue; }
        const char quote = html[i++];
        const auto start = i;
        while (i < html.size() && std::isxdigit(static_cast<unsigned char>(html[i]))) ++i;
        if (i - start == 12 && i < html.size() && html[i] == quote) return html.substr(start, 12);
        pos = i;
    }
    throw std::runtime_error("portal did not provide a valid ss1 MAC");
}
std::string login_url(const Config& c, const std::string& mac) {
    return "http://" + c.portal_ip + ":" + std::to_string(c.login_port) +
        "/eportal/?c=ACSetting&a=Login&loginMethod=1&wlanuserip=" + encode(c.client_ip) +
        "&wlanacip=" + encode(c.portal_ip) + "&wlanacname=&mac=" + encode(mac) +
        "&vlan=0&R1=0&R2=0&R3=0&R6=0&para=00&0MKKey=123456";
}
std::string login_body(const Config& c) {
    return "DDDDD=" + encode(c.username) + "&upass=" + encode(c.password);
}
std::string eportal_login_url(const Config& c) {
    // The current portal's browser client uses JSONP GET, a PC account prefix,
    // and a zero MAC to let the server identify the client from its WAN IPv4.
    // POST to this endpoint is not supported. Never log this credential-bearing URL.
    return "http://" + c.portal_ip + ":" + std::to_string(c.login_port) +
        "/eportal/portal/login?callback=dr1003&login_method=1&user_account=" +
        encode(",0," + c.username) + "&user_password=" + encode(c.password) +
        "&wlan_user_ip=" + encode(c.client_ip) +
        "&wlan_user_ipv6=&wlan_user_mac=000000000000&wlan_ac_ip=&wlan_ac_name="
        "&terminal_type=1&lang=zh-cn&jsVersion=4.1.3";
}
bool login_ack(const std::string& body) {
    // The supplied protocol uses an ASCII success marker inside a GBK page.
    // No full GBK conversion is needed; the actual success is verified by WAN probes.
    if (body.find("Login succeed") != std::string::npos) return true;
    static const std::regex ack(R"ack(^\s*dr1003\(\s*\{\s*"result"\s*:\s*(1|"1"|"ok")\s*[,}])ack");
    return std::regex_search(body, ack);
}
bool billing_rejected(const std::string& body) {
    // Observed portal response: Msg=01; msga='本账号费用超支，禁止使用'.
    // Match the specific message, since Msg=01 is shared by other rejection reasons.
    const std::string gbk = "\xb1\xbe\xd5\xcb\xba\xc5\xb7\xd1\xd3\xc3\xb3\xac\xd6\xa7";
    return body.find(gbk) != std::string::npos || body.find(u8"本账号费用超支") != std::string::npos;
}
std::string rejection_code(const std::string& body) {
    // Only expose a tightly bounded machine code, never arbitrary portal text.
    for (const char quote : {'\'', '"'}) {
        const std::string prefix = std::string("msga=") + quote + "AC";
        auto pos = body.find(prefix);
        if (pos == std::string::npos) continue;
        pos += prefix.size();
        if (body.size() - pos < 4 || body[pos + 3] != quote) continue;
        bool digits = true;
        for (std::size_t i = 0; i < 3; ++i)
            digits = digits && body[pos + i] >= '0' && body[pos + i] <= '9';
        if (digits) return "AC" + body.substr(pos, 3);
    }
    return {};
}
bool probe_matches(const Probe& p, long status, const std::string& body) {
    if (status != p.status || body.find("Authentication is required") != std::string::npos) return false;
    if (p.status == 204) return body.empty();
    return !p.body.empty() && body.find(p.body) != std::string::npos;
}
} // namespace njupt
