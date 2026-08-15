#pragma once

#include "config.h"
#include "logger.h"

#include <mutex>
#include <string>

#include <netinet/in.h>
#include <sys/socket.h>

class UdpForwarder {
public:
    UdpForwarder(const AprsConfig& cfg, Logger& log);
    ~UdpForwarder();

    UdpForwarder(const UdpForwarder&) = delete;
    UdpForwarder& operator=(const UdpForwarder&) = delete;

    bool start();
    void stop();
    bool send(const std::string& line);

private:
    const AprsConfig& cfg_;
    Logger& log_;

    mutable std::mutex mutex_;
    int sock_ = -1;
    sockaddr_storage addr_{};
    socklen_t addr_len_ = 0;
};
