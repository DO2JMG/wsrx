#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <sys/types.h>

class DecoderProcess {
public:
    DecoderProcess() = default;
    ~DecoderProcess();

    DecoderProcess(const DecoderProcess&) = delete;
    DecoderProcess& operator=(const DecoderProcess&) = delete;

    bool start(const std::string& command);
    void stop();
    bool isRunning() const;
    std::optional<std::string> readLine();

private:
    bool isRunningUnlocked() const;

    mutable std::mutex mutex_;
    pid_t pid_ = -1;
    int stdout_fd_ = -1;
    std::string buffer_;
};
