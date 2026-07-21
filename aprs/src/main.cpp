#include "aprsformat.h"
#include "aprsis.h"
#include "config.h"
#include "jsonlite.h"
#include "logger.h"
#include "ratelimiter.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {
std::atomic<bool> g_shutdown{false};

void handleSignal(int) { g_shutdown = true; }

int openUdpListenSocket(const AprsConfig& cfg, Logger& log) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* result = nullptr;
    const std::string port_str = std::to_string(cfg.udp_listen_port);
    int rc = ::getaddrinfo(cfg.udp_listen_host.empty() ? nullptr : cfg.udp_listen_host.c_str(), port_str.c_str(),
                            &hints, &result);
    if (rc != 0 || result == nullptr) {
        log.warn("UDP: could not resolve listen address " + cfg.udp_listen_host + ":" + port_str);
        return -1;
    }

    int fd = -1;
    for (addrinfo* p = result; p != nullptr; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        int yes = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (::bind(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(result);

    if (fd < 0) {
        log.warn("UDP: could not bind " + cfg.udp_listen_host + ":" + port_str);
        return -1;
    }

    timeval tv{};
    tv.tv_sec = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    log.info("UDP: listening on " + cfg.udp_listen_host + ":" + port_str);
    return fd;
}

void stationBeaconLoop(const AprsConfig& cfg, AprsIsClient& aprs, Logger& log) {
    if (cfg.station_beacon_interval_sec <= 0) return;

    constexpr int kRetryWhileDisconnectedSec = 10;

    while (!g_shutdown.load()) {
        std::string line = AprsFormat::buildStationBeacon(cfg);
        if (aprs.sendLine(line)) {
            log.debug("Station beacon sent: " + line);

            for (int waited = 0; waited < cfg.station_beacon_interval_sec && !g_shutdown.load(); ++waited) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        } else {
 
            log.debug("Station beacon dropped (APRS-IS disconnected), retrying shortly");

            for (int waited = 0; waited < kRetryWhileDisconnectedSec && !g_shutdown.load(); ++waited) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string config_file = "aprs.ini";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "--config" || a == "-c") && i + 1 < argc) {
            config_file = argv[++i];
        } else if (a == "--help" || a == "-h") {
            std::fprintf(stderr, "Usage: %s [--config aprs.ini]\n", argv[0]);
            return 0;
        }
    }

    AprsConfig cfg;
    try {
        cfg = AprsConfig::load(config_file);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Config error: %s\n", e.what());
        return 1;
    }

    Logger log(cfg.verbose);
    log.info("wsrx-aprs-gw starting, forwarding UDP telemetry to APRS-IS as " + cfg.callsign);

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    std::signal(SIGPIPE, SIG_IGN);

    AprsIsClient aprs(cfg, log);
    aprs.start();

    AltitudeRateLimiter rate_limiter;

    std::thread beacon_thread(stationBeaconLoop, std::cref(cfg), std::ref(aprs), std::ref(log));

    int udp_fd = openUdpListenSocket(cfg, log);
    if (udp_fd < 0) {
        g_shutdown = true;
        beacon_thread.join();
        aprs.stop();
        return 1;
    }

    char buf[4096];
    while (!g_shutdown.load()) {
        sockaddr_storage src{};
        socklen_t src_len = sizeof(src);
        ssize_t n = ::recvfrom(udp_fd, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&src), &src_len);
        if (n < 0) {
            continue;  // recv timeout or interrupted, loop checks g_shutdown
        }
        buf[n] = '\0';

        JsonObject frame;
        if (!JsonObject::parse(buf, frame)) {
            log.warn("UDP: received non-JSON or malformed datagram, ignored");
            continue;
        }

        std::string serial = frame.getString("serial");
        std::string type = frame.getString("type");
        double altitude_m = frame.getDouble("altitude", 0.0);

        if (!rate_limiter.allow(serial, altitude_m)) {
            log.debug("Rate-limited " + type + " " + serial + " (alt=" + std::to_string(altitude_m) + "m)");
            continue;
        }

        std::string line = AprsFormat::buildObjectReport(cfg, frame);
        bool ok = aprs.sendLine(line);

        if (ok) {
            log.info("Forwarded " + type + " " + serial + " to APRS-IS");
            log.debug(line);
        } else {
            log.warn("Dropped " + type + " " + serial + " (APRS-IS disconnected)");
        }
    }

    ::close(udp_fd);
    beacon_thread.join();
    aprs.stop();
    log.info("wsrx-aprs-gw stopped");
    return 0;
}
