#pragma once

#include "config.h"
#include "logger.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class AprsIsClient {
public:
    AprsIsClient(const AprsConfig& cfg, Logger& log);
    ~AprsIsClient();

    AprsIsClient(const AprsIsClient&) = delete;
    AprsIsClient& operator=(const AprsIsClient&) = delete;

    void start();
    void stop();
    bool sendLine(const std::string& line);
    bool isConnected() const { return connected_.load(); }

private:
    void connectionLoop();
    bool connectOnce();
    void disconnectLocked();

    const AprsConfig& cfg_;
    Logger& log_;

    std::thread thread_;
    std::atomic<bool> shutdown_{false};
    std::atomic<bool> connected_{false};

    mutable std::mutex sock_mutex_;
    int sock_ = -1;
};
