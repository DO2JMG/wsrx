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

    std::optional<std::string> readLine(int timeout_ms);

    bool atEof() const;

private:
    bool isRunningUnlocked() const;
    std::optional<std::string> popLineUnlocked();

    mutable std::mutex mutex_;
    pid_t pid_ = -1;
    int stdout_fd_ = -1;
    bool eof_ = false;
    std::string buffer_;
    size_t buffer_pos_ = 0;
};

