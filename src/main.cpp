#include "http.hpp"
#include <chrono>
#include <cerrno>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifndef _WIN32
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
void stop(int) { njupt::stopping = 1; }
void pause(unsigned seconds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!njupt::stopping && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
bool link_ready(const njupt::Config& c) {
#ifdef _WIN32
    (void)c;
    return true; // Native Windows is a protocol-test build; Linux validates WAN addresses.
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return false;
    bool found = false;
    for (auto* i = list; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) ||
            c.device != i->ifa_name) continue;
        char ip[INET_ADDRSTRLEN]{};
        auto* address = reinterpret_cast<sockaddr_in*>(i->ifa_addr);
        if (inet_ntop(AF_INET, &address->sin_addr, ip, sizeof(ip)) && c.client_ip == ip) found = true;
    }
    freeifaddrs(list);
    return found;
#endif
}
bool refresh() {
#ifdef _WIN32
    return false;
#else
    // Fixed path, no shell and no user credentials in argv. Refreshes routes and OpenClash exemption.
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        setpgid(0, 0);
        execl("/usr/libexec/njupt-login/network", "network", "setup", static_cast<char*>(nullptr));
        _exit(127);
    }
    (void)setpgid(pid, pid);
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    for (;;) {
        const auto waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (waited < 0 && errno != EINTR) return false;
        if (njupt::stopping || std::chrono::steady_clock::now() >= deadline) {
            // Include ubus / ip / firewall children, so a stuck helper cannot stall procd shutdown.
            kill(-pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
#endif
}
void protect_config(const std::string& path) {
#ifndef _WIN32
    struct stat st{};
    if (lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_uid != geteuid() || (st.st_mode & 0077))
        throw std::runtime_error("config must be a regular file owned by the current user, mode 600");
#else
    (void)path;
#endif
}
}
int main(int argc, char** argv) {
    bool initialized = false;
    try {
        std::string path, device, ip;
        bool once = false, check = false, validate_only = false, openwrt = false;
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--once") once = true;
            else if (a == "--check") check = true;
            else if (a == "--validate") validate_only = true;
            else if (a == "--openwrt") openwrt = true;
            else if (a == "--version") { std::cout << "njupt-login 0.1.0-r4\n"; return 0; }
            else if (a == "--help") {
                std::cout << "njupt-login --config FILE [--interface DEVICE --client-ip IPv4] [--once|--check|--validate] [--openwrt]\n";
                return 0;
            } else if ((a == "--config" || a == "--interface" || a == "--client-ip") && i + 1 < argc) {
                const std::string value = argv[++i];
                if (a == "--config") path = value;
                else if (a == "--interface") device = value;
                else ip = value;
            } else throw std::runtime_error("unknown or incomplete command line option");
        }
        if (path.empty() || static_cast<int>(once) + static_cast<int>(check) + static_cast<int>(validate_only) > 1)
            throw std::runtime_error("use --config FILE and at most one of --once/--check/--validate");
        protect_config(path);
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("cannot open config file");
        auto c = njupt::read_config(input);
        if (!device.empty()) c.device = device;
        if (!ip.empty()) c.client_ip = ip;
        njupt::validate(c);
#ifdef _WIN32
        if (c.mark || !c.device.empty() || openwrt)
            throw std::runtime_error("Windows test build requires mark=0, no interface, no --openwrt");
#else
        if (openwrt && c.mark != 20042) throw std::runtime_error("OpenWrt integration reserves mark 20042");
#endif
        std::signal(SIGINT, stop); std::signal(SIGTERM, stop);
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) throw std::runtime_error("curl global init failed");
        initialized = true;
        if (validate_only) {
            { njupt::Http http(c); }
            curl_global_cleanup(); initialized = false;
            std::cout << "configuration valid\n";
            return 0;
        }
        int result = 1;
        {
            njupt::Http http(c);
            njupt::Controller controller(c);
            njupt::State previous = njupt::State::stopped;
            auto log = [&](njupt::State state) {
                if (state != previous) {
                    std::cerr << "state=" << njupt::state_name(state) << '\n'; previous = state;
                }
            };
            while (!njupt::stopping) {
                const bool link = link_ready(c) && (!openwrt || refresh());
                auto d = controller.observed(link, link && http.online());
                if (njupt::stopping) break;
                log(d.state);
                if (check) { result = d.state == njupt::State::online ? 0 : 1; break; }
                if (d.login) {
                    const auto login_result = http.login();
                    pause(c.settle);
                    d = controller.authenticated(!njupt::stopping && http.online(),
                        login_result == njupt::LoginResult::billing_rejected);
                    log(d.state);
                    if (d.state == njupt::State::backoff || d.state == njupt::State::account_rejected)
                        std::cerr << "retry in " << d.delay << " seconds\n";
                }
                if (once) { result = d.state == njupt::State::online ? 0 : 1; break; }
                pause(d.delay);
            }
            if (njupt::stopping) { log(njupt::State::stopped); result = 0; }
        }
        curl_global_cleanup(); initialized = false;
        return result;
    } catch (const std::exception& e) {
        if (initialized) curl_global_cleanup();
        std::cerr << "error: " << e.what() << '\n';
        return 2;
    }
}
