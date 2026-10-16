#pragma once

#include "config.h"
#include "jsonlite.h"
#include <string>

namespace AprsFormat {

std::string buildObjectReport(const AprsConfig& cfg, const JsonObject& frame);
std::string buildStationBeacon(const AprsConfig& cfg);

}
