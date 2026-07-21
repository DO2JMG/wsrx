#include "udpout.h"
#include "logger.h"
#include "telemetryjson.h"

#include <cstring>
#include <netdb.h>
#include <unistd.h>

namespace {
constexpr const char* APP_VERSION = "0.1.02";
}

UdpSender::UdpSender(const Config& cfg, Logger& log) : cfg_(cfg), log_(log) {}

UdpSender::~UdpSender() {
    if (sock_ >= 0) {
        ::close(sock_);
        sock_ = -1;
    }
}

bool UdpSender::resolveDestination() {
    if (destination_resolved_) return destination_valid_;
    destination_resolved_ = true;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    addrinfo* result = nullptr;
    const std::string port_str = std::to_string(cfg_.udp_port);
    int rc = ::getaddrinfo(cfg_.udp_host.c_str(), port_str.c_str(), &hints, &result);
    if (rc != 0 || result == nullptr) {
        log_.warn("UDP telemetry: could not resolve " + cfg_.udp_host + ":" + port_str + " (" + gai_strerror(rc) + ")");
        return false;
    }

    std::memcpy(&dest_addr_, result->ai_addr, result->ai_addrlen);
    dest_len_ = result->ai_addrlen;

    sock_ = ::socket(result->ai_family, SOCK_DGRAM, IPPROTO_UDP);
    ::freeaddrinfo(result);

    if (sock_ < 0) {
        log_.warn("UDP telemetry: could not create socket for " + cfg_.udp_host + ":" + port_str);
        return false;
    }

    destination_valid_ = true;
    log_.info("UDP telemetry output enabled -> " + cfg_.udp_host + ":" + port_str);
    return true;
}

bool UdpSender::sendDatagram(const std::string& data) {
    if (!resolveDestination()) return false;

    ssize_t sent = ::sendto(sock_, data.data(), data.size(), 0,
                             reinterpret_cast<const sockaddr*>(&dest_addr_), dest_len_);
    if (sent < 0 || static_cast<size_t>(sent) != data.size()) {
        log_.warn("UDP telemetry: send failed to " + cfg_.udp_host + ":" + std::to_string(cfg_.udp_port));
        return false;
    }
    return true;
}

bool UdpSender::sendTelemetry(const TelemetryFrame& frame) {
    if (!cfg_.udp_enabled) return false;

    // Same acceptance rules as the HTTP uploader, so the serial number
    // in the JSON is validated/normalized exactly like for the uploader.
    if (!TelemetryJson::hasGpsFix(frame)) {
        return false;
    }
    if (!TelemetryJson::validTypeSerial(frame)) {
        return false;
    }

    const std::string data = TelemetryJson::buildTelemetryJson(frame, cfg_.callsign, APP_VERSION);

    if (cfg_.dry_run) {
        log_.info("DRY-RUN UDP telemetry JSON: " + data);
        return true;
    }

    if (cfg_.verbose) {
        log_.debug("UDP telemetry JSON to " + cfg_.udp_host + ":" + std::to_string(cfg_.udp_port) + ": " + data);
    }

    return sendDatagram(data);
}
