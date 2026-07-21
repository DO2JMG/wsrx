#pragma once

#include <mutex>
#include <string>

class Logger {
public:
    explicit Logger(bool verbose) : verbose_(verbose) {}

    void info(const std::string& msg) const { print("INFO", msg); }
    void warn(const std::string& msg) const { print("WARN", msg); }
    void debug(const std::string& msg) const {
        if (verbose_) print("DEBUG", msg);
    }

private:
    void print(const char* level, const std::string& msg) const;

    bool verbose_;
    mutable std::mutex mutex_;
};
