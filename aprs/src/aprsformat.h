#pragma once

#include "config.h"
#include "jsonlite.h"
#include <string>

namespace AprsFormat {

std::string formatLatitude(double lat);
std::string formatLongitude(double lon);

std::string buildObjectReport(const AprsConfig& cfg, const JsonObject& frame);
std::string buildStationBeacon(const AprsConfig& cfg);

}  // namespace AprsFormat
