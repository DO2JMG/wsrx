#pragma once

#include "config.h"
#include "telemetryframe.h"
#include <string>

#include <netinet/in.h>
#include <sys/socket.h>

class Logger;

// Sends the telemetry frame as a JSON UDP datagram to a configured
// host/port, e.g. for local display tools. Uses exactly the same JSON
// layout and (already normalized/validated) serial number as the
// Uploader's HTTP upload, see telemetryjson.h.
class UdpSender {
public:
    UdpSender(const Config& cfg, Logger& log);
    ~UdpSender();

    UdpSender(const UdpSender&) = delete;
    UdpSender& operator=(const UdpSender&) = delete;

    bool sendTelemetry(const TelemetryFrame& frame);

private:
    bool sendDatagram(const std::string& data);
    bool resolveDestination();

    const Config& cfg_;
    Logger& log_;
    int sock_ = -1;
    bool destination_resolved_ = false;
    bool destination_valid_ = false;
    sockaddr_storage dest_addr_{};
    socklen_t dest_len_ = 0;
};
