#include "logger.h"

#include <chrono>
#include <ctime>
#include <iostream>

Logger::Logger(const std::string& file, bool verbose) : verbose_(verbose) {
    if (!file.empty()) {
        file_.open(file, std::ios::app);
    }
}

void Logger::info(const std::string& msg) { write("INFO", msg); }
void Logger::warn(const std::string& msg) { write("WARN", msg); }
void Logger::error(const std::string& msg) { write("ERROR", msg); }
void Logger::debug(const std::string& msg) { if (verbose_) write("DEBUG", msg); }

void Logger::write(const std::string& level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char ts[32] = {0};
    std::strftime(ts, sizeof(ts), "(%H:%M:%S)", std::localtime(&now));

    std::string line = std::string(ts) + " [" + level + "] " + msg;
    std::cout << line << std::endl;
    if (file_.is_open()) {
        file_ << line << std::endl;
    }
}
