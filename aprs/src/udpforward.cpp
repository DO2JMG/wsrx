#include "udpforward.h"

#include "ax25frame.h"

#include <cstring>
#include <netdb.h>
#include <unistd.h>

UdpForwarder::UdpForwarder(const AprsConfig& cfg, Logger& log) : cfg_(cfg), log_(log) {}

UdpForwarder::~UdpForwarder() { stop(); }

bool UdpForwarder::start() {
    if (!cfg_.udp_out_enabled) return true;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    addrinfo* result = nullptr;
    const std::string port_str = std::to_string(cfg_.udp_out_port);
    int rc = ::getaddrinfo(cfg_.udp_out_host.c_str(), port_str.c_str(), &hints, &result);
    if (rc != 0 || result == nullptr) {
        log_.warn("UDP-OUT: could not resolve " + cfg_.udp_out_host + ":" + port_str + ", forwarding disabled");
        return false;
    }

    int fd = -1;
    sockaddr_storage addr{};
    socklen_t addr_len = 0;
    for (addrinfo* p = result; p != nullptr; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        std::memcpy(&addr, p->ai_addr, p->ai_addrlen);
        addr_len = p->ai_addrlen;
        break;
    }
    ::freeaddrinfo(result);

    if (fd < 0) {
        log_.warn("UDP-OUT: could not create socket for " + cfg_.udp_out_host + ":" + port_str);
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    sock_ = fd;
    addr_ = addr;
    addr_len_ = addr_len;

    log_.info("UDP-OUT: forwarding APRS frames to " + cfg_.udp_out_host + ":" + port_str +
               " (dxlAPRS udpgate4/aprsmap compatible)");
    return true;
}

void UdpForwarder::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sock_ >= 0) {
        ::close(sock_);
        sock_ = -1;
    }
}

bool UdpForwarder::send(const std::string& line) {
    if (!cfg_.udp_out_enabled) return true;

    std::vector<uint8_t> frame = Ax25::encodeAxUdpFrame(line);
    if (frame.empty()) {
        log_.warn("UDP-OUT: could not encode AXUDP frame for line: " + line);
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (sock_ < 0) return false;  // start() failed or was never enabled successfully

    ssize_t sent = ::sendto(sock_, frame.data(), frame.size(), 0, reinterpret_cast<sockaddr*>(&addr_), addr_len_);
    if (sent < 0 || static_cast<size_t>(sent) != frame.size()) {
        log_.warn("UDP-OUT: send failed");
        return false;
    }
    return true;
}
