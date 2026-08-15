#pragma once

#include "telemetryframe.h"
#include <string>

namespace TelemetryJson {

struct StationLocation {
    double lat;
    double lon;
    double alt_m;
};

bool hasGpsFix(const TelemetryFrame& frame);
bool validTypeSerial(const TelemetryFrame& frame);

std::string buildTelemetryJson(const TelemetryFrame& frame, const std::string& callsign,
                                const std::string& app_version,
                                const StationLocation* station = nullptr);

}  // namespace TelemetryJson
