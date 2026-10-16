#include "logger.h"

#include <chrono>
#include <ctime>
#include <iostream>

Logger::Logger(bool verbose) : verbose_(verbose) {}

void Logger::info(const std::string& msg) { write("INFO", msg); }
void Logger::warn(const std::string& msg) { write("WARN", msg); }
void Logger::debug(const std::string& msg) { if (verbose_) write("DEBUG", msg); }

void Logger::write(const std::string& level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char ts[32] = {0};
    std::strftime(ts, sizeof(ts), "(%H:%M:%S)", std::localtime(&now));

    std::cout << ts << " [" << level << "] " << msg << std::endl;
}
