#include "scanbackoff.h"

#include <algorithm>

namespace {
constexpr auto kForgetAfter = std::chrono::minutes(30);
constexpr int kMaxCountedMisses = 30;
}

void ScanBackoff::configure(const Settings& settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = settings;
    if (settings_.min_sec < 0) settings_.min_sec = 0;
    if (settings_.max_sec < settings_.min_sec) settings_.max_sec = settings_.min_sec;
    if (!(settings_.rise_db >= 0.0)) settings_.rise_db = 0.0;
    if (!(settings_.match_hz > 0.0)) settings_.match_hz = 5000.0;
    entries_.clear();
}

bool ScanBackoff::enabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_.min_sec > 0;
}

ScanBackoff::Entry* ScanBackoff::findLocked(double frequency_hz) {
    Entry* best = nullptr;
    double best_dist = settings_.match_hz;
    for (auto& e : entries_) {
        const double dist = std::fabs(e.frequency_hz - frequency_hz);
        if (dist <= best_dist) {
            best = &e;
            best_dist = dist;
        }
    }
    return best;
}

void ScanBackoff::pruneLocked(Clock::time_point now) {
    const Clock::duration keep = std::max<Clock::duration>(kForgetAfter, std::chrono::seconds(2LL * settings_.max_sec));
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& e) {
        return now >= e.hold_until && now - e.last_touch > keep;
    }), entries_.end());
}

int ScanBackoff::holdOffSecLocked(int misses) const {
    long long sec = settings_.min_sec;
    for (int i = 1; i < misses && sec < settings_.max_sec; ++i) sec *= 2;
    return static_cast<int>(std::min<long long>(sec, settings_.max_sec));
}

bool ScanBackoff::holdBack(double frequency_hz, double snr_db, int* remaining_sec) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (settings_.min_sec <= 0) return false;

    const auto now = Clock::now();
    pruneLocked(now);
    Entry* e = findLocked(frequency_hz);
    if (e == nullptr) return false;
    e->last_touch = now;

    if (now >= e->hold_until) return false;
    if (std::isfinite(snr_db)) {
        if (!std::isfinite(e->snr_db)) {
            e->snr_db = snr_db;
        } else if (snr_db >= e->snr_db + settings_.rise_db) {
            return false;
        }
    }

    if (remaining_sec != nullptr) {
        const auto left = std::chrono::duration_cast<std::chrono::seconds>(e->hold_until - now).count();
        *remaining_sec = static_cast<int>(left) + 1;
    }
    return true;
}

int ScanBackoff::recordMiss(double frequency_hz, double snr_db) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (settings_.min_sec <= 0) return 0;

    const auto now = Clock::now();
    pruneLocked(now);
    Entry* e = findLocked(frequency_hz);
    if (e == nullptr) {
        entries_.push_back(Entry{});
        e = &entries_.back();
    }
    e->frequency_hz = frequency_hz;
    if (std::isfinite(snr_db)) e->snr_db = snr_db;
    e->misses = std::min(e->misses + 1, kMaxCountedMisses);
    e->last_touch = now;

    const int hold = holdOffSecLocked(e->misses);
    e->hold_until = now + std::chrono::seconds(hold);
    return hold;
}

void ScanBackoff::recordHit(double frequency_hz) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& e) {
        return std::fabs(e.frequency_hz - frequency_hz) <= settings_.match_hz;
    }), entries_.end());
}
