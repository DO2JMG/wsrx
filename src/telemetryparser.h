#pragma once

#include "telemetryframe.h"
#include <optional>
#include <string>

class TelemetryParser {
public:
    std::optional<TelemetryFrame> parseLine(const std::string& line, double frequency_mhz, const std::string& receiver);

private:
    static std::optional<double> extractNumber(const std::string& text, const std::string& key);
    static std::optional<std::string> extractString(const std::string& text, const std::string& key);

    std::string current_serial_;
    std::string current_type_ = "RS41";
    int current_frame_ = -1;
};
