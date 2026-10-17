#pragma once

#include "config.h"
#include "telemetryframe.h"
#include <mutex>
#include <string>

#include <netinet/in.h>
#include <sys/socket.h>

class Logger;

class UdpSender {
public:
    UdpSender(const Config& cfg, Logger& log);
    ~UdpSender();

    UdpSender(const UdpSender&) = delete;
    UdpSender& operator=(const UdpSender&) = delete;

    bool sendTelemetry(const TelemetryFrame& frame);
    void setStationLocation(const StationLocation& station);

private:
    bool sendDatagram(const std::string& data);
    bool resolveDestination();

    const Config& cfg_;
    Logger& log_;
    std::mutex mutex_;
    StationLocation station_;
    int sock_ = -1;
    bool destination_resolved_ = false;
    bool destination_valid_ = false;
    sockaddr_storage dest_addr_{};
    socklen_t dest_len_ = 0;
};
