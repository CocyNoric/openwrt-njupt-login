#include "core.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<typename F> void rejects(F f) {
    ++checks;
    try { f(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("expected rejection");
}
njupt::Config parse(const std::string& extra = "") {
    std::istringstream in("username=user\npassword= p#=+&'尾 \nclient_ip=127.0.0.1\n" + extra);
    return njupt::read_config(in);
}
}
int main() {
    try {
        auto c = parse();
        require(c.password == " p#=+&'尾 ", "literal credential parsing");
        require(njupt::encode("a b+&%") == "a%20b%2B%26%25", "form encoding");
        require(njupt::encode("尾") == "%E5%B0%BE", "UTF-8 byte encoding");
        require(njupt::login_body(c).find("&upass=%20p%23%3D%2B%26%27") != std::string::npos, "credential escaping");
        require(njupt::portal_mac("var ss1 = 'aABBCCDDeE01';") == "aABBCCDDeE01", "single quote MAC");
        require(njupt::portal_mac("ss11='111111111111'; ss1=\"001122334455\";") == "001122334455", "identifier boundary");
        rejects([] { njupt::portal_mac("ss1='00112233445';"); });
        rejects([] { njupt::portal_mac("ss1='00112233445566';"); });
        rejects([] { njupt::portal_mac("ss1='00112233445Z';"); });
        const auto url = njupt::login_url(c, "001122334455");
        require(url.find("wlanuserip=127.0.0.1") != std::string::npos, "WAN IP in auth");
        require(url.find("R6=0") != std::string::npos, "sample protocol fields");
        require(njupt::login_ack("\x81\x82 Login succeed"), "ASCII inside GBK");
        require(!njupt::login_ack("Login failed"), "negative ACK");
        require(njupt::billing_rejected(u8"msga='本账号费用超支，禁止使用'"), "UTF-8 billing rejection");
        require(njupt::billing_rejected("msga='\xb1\xbe\xd5\xcb\xba\xc5\xb7\xd1\xd3\xc3\xb3\xac\xd6\xa7'"), "GBK billing rejection");
        require(!njupt::billing_rejected("Msg=01; Login fail"), "generic rejection is not a billing failure");
        require(njupt::rejection_code("Msg=01;msga='AC999';") == "AC999", "observed server code");
        require(njupt::rejection_code("msga=\"AC123\";") == "AC123", "quoted server code");
        require(njupt::rejection_code("msga='AC999extra';").empty(), "do not log arbitrary suffix");
        require(njupt::rejection_code("msga='ACabc';").empty(), "code requires digits");
        require(njupt::rejection_code("msga='personal account text';").empty(), "do not log portal text");
        require(c.probes.front().url == "http://archive.ubuntu.com/ubuntu/" &&
            c.probes.front().ip == "91.189.92.22", "default uses the verified pinned Ubuntu probe");
        require(parse("probe_url=http://127.0.0.1/probe\n").probes.front().ip.empty(), "URL overrides do not inherit default pin");
        njupt::Probe p{"http://127.0.0.1/", "", 204, ""};
        require(njupt::probe_matches(p, 204, ""), "204 probe");
        require(!njupt::probe_matches(p, 200, ""), "HTTP success is insufficient");
        require(!njupt::probe_matches(p, 302, ""), "redirect is captive");
        p.status = 200; p.body = "expected";
        require(!njupt::probe_matches(p, 200, "expected Authentication is required"), "captive detection");
        rejects([] { parse("username=duplicate\n"); });
        rejects([] { parse("interval=-1\n"); });
        rejects([] { parse("interval=9999999999999999999999999999\n"); });
        rejects([] { parse("retry_max=1\n"); });
        rejects([] { parse("rejected_retry=0\n"); });
        rejects([] { parse("unknown=x\n"); });
        rejects([] { parse("portal_ip=portal.example\n"); });
        rejects([] { parse("probe_url=file:///tmp/data\n"); });
        rejects([] { parse("probe_status=200\nprobe_body=\n"); });
        njupt::Controller state(c);
        require(state.observed(true, false).login, "startup logs in immediately");
        auto d = state.authenticated(false);
        require(d.delay == 10 && d.state == njupt::State::backoff, "first retry");
        require(state.authenticated(false).delay == 20, "retry doubles");
        for (int i = 0; i < 8; ++i) d = state.authenticated(false);
        require(d.delay == 300, "retry is capped");
        require(state.authenticated(true).state == njupt::State::online, "post-login confirmed");
        require(!state.observed(true, false).login, "one failed heartbeat tolerated");
        require(!state.observed(true, false).login, "two failed heartbeats tolerated");
        require(state.observed(true, false).login, "threshold triggers reauth");
        state.observed(true, true);
        require(state.authenticated(false).delay == 10, "success resets retry");
        require(state.observed(false, false).state == njupt::State::waiting_link, "link down suspends auth");
        require(state.failures() == 0, "link down resets failures");
        require(state.observed(true, false).login, "new WAN session starts auth");
        d = state.authenticated(false, true);
        require(d.state == njupt::State::account_rejected && d.delay == 900, "billing rejection gets a separate slow retry");
        require(state.authenticated(true, true).state == njupt::State::online, "confirmed connectivity takes precedence");
        std::cout << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
