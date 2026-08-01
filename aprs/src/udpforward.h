#pragma once

#include "config.h"
#include "logger.h"

#include <mutex>
#include <string>

#include <netinet/in.h>
#include <sys/socket.h>

// Optionally forwards every generated APRS frame (station beacon and object
// reports) as an AXUDP datagram, in addition to sending it to APRS-IS.
//
// AXUDP is the raw-AX.25-over-UDP convention used internally by the
// dxlAPRS toolchain (udpbox, udpgate4, udpflex, aprsmap's "RF port"), and
// by other AXUDP-speaking software (soundmodem, direwolf -X, svxlink, ...).
// Each datagram carries one binary AX.25 UI frame (address field, control
// byte, PID byte, info field, CRC-16 FCS) - see ax25frame.h for the
// encoder. This is NOT a plain-text line; the payload is the exact bytes
// that would be sent over the air, minus HDLC flags/bit-stuffing.
//
// Purely fire-and-forget: since UDP is connectionless there is no
// reconnect logic, and forwarding failures are just logged.
class UdpForwarder {
public:
    UdpForwarder(const AprsConfig& cfg, Logger& log);
    ~UdpForwarder();

    UdpForwarder(const UdpForwarder&) = delete;
    UdpForwarder& operator=(const UdpForwarder&) = delete;

    // Resolves the configured target and opens the socket. No-op (returns
    // true) if udp-out is disabled in the config. Returns false if enabled
    // but the target could not be resolved/opened; forwarding then simply
    // stays off for this run.
    bool start();
    void stop();

    // Encodes and sends one APRS line (TNC2 form "SRC>DST:info") as an
    // AXUDP frame to the configured udp-out target. No-op (returns true)
    // if forwarding is disabled or start() didn't succeed.
    bool send(const std::string& line);

private:
    const AprsConfig& cfg_;
    Logger& log_;

    mutable std::mutex mutex_;
    int sock_ = -1;
    sockaddr_storage addr_{};
    socklen_t addr_len_ = 0;
};
