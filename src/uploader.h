#pragma once

#include "config.h"
#include "telemetryframe.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Logger;

class Uploader {
public:
    Uploader(const Config& cfg, Logger& log);
    ~Uploader();

    Uploader(const Uploader&) = delete;
    Uploader& operator=(const Uploader&) = delete;

    void sendTelemetry(const TelemetryFrame& frame);
    void maybeSendReceiverPosition();

    void stop();

private:
    using Clock = std::chrono::steady_clock;

    struct SondeState {
        TelemetryFrame latest;
        Clock::time_point latest_received{};
        Clock::time_point last_upload{};
        bool ever_uploaded = false;
        bool has_unsent = false;  
        bool queued = false;      
        bool in_flight = false;   
    };

    struct Request {
        std::string url;
        std::string body;  
    };

    void workerLoop();
    void queueIfDueLocked(const std::string& serial, SondeState& st, Clock::time_point now);
    void pruneLocked(Clock::time_point now);
    static int minUploadIntervalMs(const TelemetryFrame& frame);
    void notifyEncryptedRs41SgmLocked(const TelemetryFrame& frame);

    const Config& cfg_;
    Logger& log_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::unordered_map<std::string, SondeState> sondes_;
    std::deque<std::string> ready_;     
    std::deque<Request> requests_;    
    std::unordered_set<std::string> encrypt_notified_serials_;
    Clock::time_point last_position_upload_;
    Clock::time_point last_prune_;
    bool stopping_ = false;
    std::atomic<bool> abort_transfers_{false};
    std::vector<std::thread> workers_;
};
