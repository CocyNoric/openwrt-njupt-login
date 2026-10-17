#include "http.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <cstring>
#ifndef _WIN32
#include <sys/socket.h>
#endif

namespace njupt {
volatile std::sig_atomic_t stopping = 0;
namespace {
struct Sink { std::string body; };
std::size_t write_body(char* data, std::size_t size, std::size_t count, void* context) noexcept {
    auto& sink = *static_cast<Sink*>(context);
    constexpr std::size_t limit = 65536;
    if (size && count > limit / size) return 0;
    const auto bytes = size * count;
    if (bytes > limit - sink.body.size()) return 0;
    try { sink.body.append(data, bytes); } catch (...) { return 0; }
    return bytes;
}
int progress(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept { return stopping ? 1 : 0; }
int socket_options(void* context, curl_socket_t fd, curlsocktype purpose) noexcept {
    if (purpose != CURLSOCKTYPE_IPCXN) return CURL_SOCKOPT_ERROR;
    const auto& c = *static_cast<const Config*>(context);
#ifdef _WIN32
    (void)fd;
    return c.mark == 0 && c.device.empty() ? CURL_SOCKOPT_OK : CURL_SOCKOPT_ERROR;
#else
    if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, c.device.c_str(),
                   static_cast<socklen_t>(c.device.size() + 1)) != 0) return CURL_SOCKOPT_ERROR;
    if (c.mark && setsockopt(fd, SOL_SOCKET, SO_MARK, &c.mark, sizeof(c.mark)) != 0)
        return CURL_SOCKOPT_ERROR;
    return CURL_SOCKOPT_OK;
#endif
}
template<typename T> void option(CURL* h, CURLoption name, T value) {
    if (curl_easy_setopt(h, name, value) != CURLE_OK) throw std::runtime_error("libcurl option failed");
}
using Url = std::unique_ptr<CURLU, decltype(&curl_url_cleanup)>;
std::string part(CURLU* u, CURLUPart name, unsigned flags = 0) {
    char* ptr = nullptr;
    if (curl_url_get(u, name, &ptr, flags) != CURLUE_OK) return {};
    std::unique_ptr<char, decltype(&curl_free)> guard(ptr, curl_free);
    return ptr;
}
std::string resolve_entry(const Probe& p) {
    Url url(curl_url(), curl_url_cleanup);
    if (!url || curl_url_set(url.get(), CURLUPART_URL, p.url.c_str(), 0) != CURLUE_OK)
        throw std::runtime_error("invalid probe URL");
    const auto scheme = part(url.get(), CURLUPART_SCHEME);
    const auto host = part(url.get(), CURLUPART_HOST);
    if ((scheme != "http" && scheme != "https") || host.empty() ||
        !part(url.get(), CURLUPART_USER).empty() || !part(url.get(), CURLUPART_PASSWORD).empty() ||
        !part(url.get(), CURLUPART_FRAGMENT).empty()) throw std::runtime_error("invalid probe URL components");
    // Avoid a resolver which might itself be tunneled, intercepted, or inaccessible before login.
    if (p.ip.empty() && !ipv4(host)) throw std::runtime_error("hostname probe requires probe_ip");
    return p.ip.empty() ? "" : host + ":" + part(url.get(), CURLUPART_PORT, CURLU_DEFAULT_PORT) + ":" + p.ip;
}
}
Http::Http(const Config& config) : c_(config) {
    for (const auto& p : c_.probes) (void)resolve_entry(p);
    handle_ = curl_easy_init();
    if (!handle_) throw std::runtime_error("cannot initialize libcurl");
}
Http::~Http() { if (handle_) curl_easy_cleanup(handle_); }
Response Http::request(const std::string& url, const std::string* post, const Probe* probe) {
    Sink sink;
    sink.body.reserve(2048);
    curl_easy_reset(handle_); // Retains libcurl's connection pool and DNS cache.
    option(handle_, CURLOPT_URL, url.c_str());
    option(handle_, CURLOPT_PROXY, "");
    option(handle_, CURLOPT_NOPROXY, "*");
    option(handle_, CURLOPT_IPRESOLVE, static_cast<long>(CURL_IPRESOLVE_V4));
    const std::string source = "host!" + c_.client_ip;
    option(handle_, CURLOPT_INTERFACE, source.c_str());
    option(handle_, CURLOPT_SOCKOPTFUNCTION, socket_options);
    option(handle_, CURLOPT_SOCKOPTDATA, &c_);
    option(handle_, CURLOPT_WRITEFUNCTION, write_body);
    option(handle_, CURLOPT_WRITEDATA, &sink);
    option(handle_, CURLOPT_CONNECTTIMEOUT, static_cast<long>(c_.connect_timeout));
    option(handle_, CURLOPT_TIMEOUT, static_cast<long>(c_.timeout));
    option(handle_, CURLOPT_NOSIGNAL, 1L);
    option(handle_, CURLOPT_FOLLOWLOCATION, 0L); // Never send credentials to a redirect target.
#if LIBCURL_VERSION_NUM >= 0x075500
    option(handle_, CURLOPT_PROTOCOLS_STR, "http,https");
#else
    option(handle_, CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    option(handle_, CURLOPT_SSL_VERIFYPEER, 1L);
    option(handle_, CURLOPT_SSL_VERIFYHOST, 2L);
    option(handle_, CURLOPT_NETRC, static_cast<long>(CURL_NETRC_IGNORED));
    option(handle_, CURLOPT_USERAGENT, "Mozilla/5.0 njupt-login/0.1");
    option(handle_, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
    option(handle_, CURLOPT_NOPROGRESS, 0L);
    option(handle_, CURLOPT_XFERINFOFUNCTION, progress);
    if (post) {
        option(handle_, CURLOPT_POST, 1L);
        option(handle_, CURLOPT_POSTFIELDS, post->c_str());
        option(handle_, CURLOPT_POSTFIELDSIZE, static_cast<long>(post->size()));
    }
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> resolve(nullptr, curl_slist_free_all);
    if (probe) {
        const auto entry = resolve_entry(*probe);
        if (!entry.empty()) {
            resolve.reset(curl_slist_append(nullptr, entry.c_str()));
            if (!resolve) throw std::runtime_error("cannot allocate resolve entry");
            option(handle_, CURLOPT_RESOLVE, resolve.get());
        }
    }
    Response r;
    r.code = curl_easy_perform(handle_);
    curl_easy_getinfo(handle_, CURLINFO_RESPONSE_CODE, &r.status);
    char* source_ip = nullptr;
    curl_easy_getinfo(handle_, CURLINFO_LOCAL_IP, &source_ip);
    r.bound = source_ip && c_.client_ip == source_ip;
    r.body = std::move(sink.body);
    // Clear borrowed pointers before their owners are destroyed (including on handle cleanup).
    curl_easy_reset(handle_);
    return r;
}
bool Http::online() {
    for (const auto& p : c_.probes) {
        if (stopping) return false;
        const auto r = request(p.url, nullptr, &p);
        if (r.code == CURLE_OK && r.bound && probe_matches(p, r.status, r.body)) return true;
    }
    return false;
}
LoginResult Http::login() {
    const auto page = request("http://" + c_.portal_ip + ":" + std::to_string(c_.page_port) + c_.page_path);
    if (stopping) return LoginResult::unconfirmed;
    if (page.code != CURLE_OK || !page.bound || page.status != 200) {
        std::cerr << "portal fetch failed: curl=" << static_cast<int>(page.code) << " http=" << page.status << '\n';
        return LoginResult::unconfirmed;
    }
    std::string mac;
    try { mac = portal_mac(page.body); }
    catch (const std::exception&) { std::cerr << "portal MAC not found\n"; return LoginResult::unconfirmed; }
    const auto body = login_body(c_);
    const auto r = request(login_url(c_, mac), &body);
    if (r.code != CURLE_OK || !r.bound || r.status != 200) {
        std::cerr << "authentication request failed: curl=" << static_cast<int>(r.code) << " http=" << r.status << '\n';
        return LoginResult::unconfirmed;
    }
    if (billing_rejected(r.body)) {
        std::cerr << "portal rejected login: account billing limit exceeded; use is prohibited\n";
        return LoginResult::billing_rejected;
    }
    const auto code = rejection_code(r.body);
    if (!code.empty()) {
        std::cerr << "portal rejected login: server code " << code << '\n';
        return LoginResult::unconfirmed;
    }
    const bool ack = login_ack(r.body);
    std::cerr << (ack ? "portal acknowledged login\n" : "portal did not acknowledge login; checking WAN connectivity\n");
    // Caller checks connectivity even without ACK (e.g. portal variant / response lost).
    return ack ? LoginResult::acknowledged : LoginResult::unconfirmed;
}
} // namespace njupt
