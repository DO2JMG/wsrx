#pragma once

#include <chrono>
#include <cmath>
#include <mutex>
#include <vector>

class ScanBackoff {
public:
    struct Settings {
        int min_sec = 60;
        int max_sec = 300;
        double rise_db = 6.0;
        double match_hz = 5000.0;
    };

    void configure(const Settings& settings);
    bool enabled() const;

    bool holdBack(double frequency_hz, double snr_db, int* remaining_sec = nullptr);

    int recordMiss(double frequency_hz, double snr_db);

    void recordHit(double frequency_hz);

private:
    using Clock = std::chrono::steady_clock;

    struct Entry {
        double frequency_hz = 0.0;
        double snr_db = NAN;
        int misses = 0;
        Clock::time_point hold_until;
        Clock::time_point last_touch;
    };

    Entry* findLocked(double frequency_hz);
    void pruneLocked(Clock::time_point now);
    int holdOffSecLocked(int misses) const;

    Settings settings_;
    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
};
