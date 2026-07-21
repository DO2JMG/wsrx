#include "logger.h"

#include <cstdio>
#include <ctime>

void Logger::print(const char* level, const std::string& msg) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char ts[16];
    std::strftime(ts, sizeof(ts), "%H:%M:%S", &tm);
    std::fprintf(stderr, "(%s) [%s] %s\n", ts, level, msg.c_str());
}
