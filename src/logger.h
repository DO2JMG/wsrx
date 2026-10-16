#pragma once

#include <mutex>
#include <string>

class Logger {
public:
    explicit Logger(bool verbose);
    void info(const std::string& msg);
    void warn(const std::string& msg);
    void debug(const std::string& msg);

private:
    void write(const std::string& level, const std::string& msg);

    bool verbose_ = false;
    std::mutex mutex_;
};
