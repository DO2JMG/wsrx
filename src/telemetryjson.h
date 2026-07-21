#pragma once

#include "telemetryframe.h"
#include <string>

// Shared between Uploader (HTTP) and UdpSender (UDP) so both output
// exactly the same JSON representation of a telemetry frame, including
// the same serial-number normalization / validity rules.
namespace TelemetryJson {

// Same GPS-fix check the uploader uses to reject frames without a fix.
bool hasGpsFix(const TelemetryFrame& frame);

// Same per-decoder serial-number sanity check the uploader uses
// (RS41/DFM/M10/M20/IMET/MEISEI/C50/S1 patterns).
bool validTypeSerial(const TelemetryFrame& frame);

// Builds the identical JSON payload the HTTP uploader sends to
// api.wettersonde.net, with the same field names, ordering, number
// formatting and (already normalized) serial number.
std::string buildTelemetryJson(const TelemetryFrame& frame, const std::string& callsign,
                                const std::string& app_version);

}  // namespace TelemetryJson
