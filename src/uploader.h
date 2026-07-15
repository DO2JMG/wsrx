#pragma once

#include "config.h"
#include "telemetryframe.h"
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

class Logger;

class Uploader {
public:
    Uploader(const Config& cfg, Logger& log);

    bool sendTelemetry(const TelemetryFrame& frame);
    bool maybeSendReceiverPosition();

private:
    bool allowedByRateLimit(const std::string& serial);
    bool hasGpsFix(const TelemetryFrame& frame) const;
    bool validTypeSerial(const TelemetryFrame& frame) const;
    std::string buildTelemetryPostData(const TelemetryFrame& frame) const;
    bool postWithCurl(const std::string& url, const std::string& data);

    const Config& cfg_;
    Logger& log_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_upload_;
    std::chrono::steady_clock::time_point last_position_upload_;
    std::mutex mutex_;
};
