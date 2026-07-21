#pragma once

#include <chrono>
#include <map>
#include <mutex>
#include <string>

class AltitudeRateLimiter {
public:
    static constexpr double kAltitudeThresholdM = 2000.0;
    static constexpr int kHighIntervalSec = 30;
    static constexpr int kLowIntervalSec = 2;

    bool allow(const std::string& serial, double altitude_m);

private:
    std::mutex mutex_;
    std::map<std::string, std::chrono::steady_clock::time_point> last_sent_;
};
