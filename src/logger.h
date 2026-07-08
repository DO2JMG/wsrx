#pragma once

#include <fstream>
#include <mutex>
#include <string>

class Logger {
public:
    Logger(const std::string& file, bool verbose);
    void info(const std::string& msg);
    void warn(const std::string& msg);
    void error(const std::string& msg);
    void debug(const std::string& msg);

private:
    void write(const std::string& level, const std::string& msg);

    bool verbose_ = false;
    std::ofstream file_;
    std::mutex mutex_;
};
