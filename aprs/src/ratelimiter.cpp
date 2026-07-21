#include "ratelimiter.h"

bool AltitudeRateLimiter::allow(const std::string& serial, double altitude_m) {
    const int interval_sec = (altitude_m > kAltitudeThresholdM) ? kHighIntervalSec : kLowIntervalSec;

    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    auto it = last_sent_.find(serial);
    if (it != last_sent_.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
        if (elapsed < interval_sec) return false;
    }
    last_sent_[serial] = now;
    return true;
}
