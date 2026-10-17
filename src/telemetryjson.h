#pragma once

#include "config.h"
#include "telemetryframe.h"
#include <string>

namespace TelemetryJson {

using StationLocation = ::StationLocation;

bool hasGpsFix(const TelemetryFrame& frame);
bool validTypeSerial(const TelemetryFrame& frame);

std::string buildTelemetryJson(const TelemetryFrame& frame, const std::string& callsign,
                                const std::string& app_version,
                                const StationLocation* station = nullptr);

}
